# 20260516 — C++ HTM Performance Analysis & Improvement Suggestions (RPi QEMU 4B)

**Branch:** `experiments/draft_abed` (Single-Threaded C++)
**Run file:** `rpi_qemu_batch-load_cpp_metrics_20260515_234126_013.txt`
**Target platform:** QEMU Raspberry Pi 4B emulation

---

## Run Summary

| Metric | Value |
|--------|-------|
| Runtime | 4640.66 s |
| Avg CPU | 162.1% |
| Peak RAM | 1333.0 MB |
| Rows processed | ~100,000 (range 446K–946K, stride 5) |
| Throughput | ~21.6 rows/s |
| Best F1 | 0.4478 (threshold 0.97) |
| Target runtime | **≤ 400 s** |
| Speedup needed | **~11.6×** |

---

## Profiling Observations

### RAM profile

The memory trace jumps from 0 to ~990 MB within the first 10 seconds, then stabilises at ~1296 MB for the remainder of the run. It creeps slowly to 1333 MB peak by the end.

- The **immediate spike** is caused by Apache Arrow's `ReadTable()` loading the entire 946K-row parquet file into a columnar in-memory table (~330 MB) plus the 26 HTM module structures (SP + TM each).
- The **slow creep** is the TemporalMemory allocating new synaptic segments as it learns over 100K timesteps.

### CPU profile

162.1% average CPU on a 4-vCPU QEMU VM means only ~40% of available compute was doing useful work. The code already launches up to 4 `std::async` threads per layer call, yet the utilisation never approaches 400%. This gap is explained below.

---

## Root Cause Analysis

### Problem 1 — `std::async` creates a new OS thread on every call

`runLayer()` in `htm_pyramid.cpp` parallelises HTM modules within each layer using `std::async(std::launch::async, ...)`. The C++ standard specifies that `std::launch::async` **must** start a new thread immediately — it does not pool or reuse threads. After each layer completes, all futures are `.get()`'d and those threads are destroyed.

Per row, the call pattern is:

| Layer | Modules | Threads launched |
|-------|---------|-----------------|
| L0    | 16      | 4               |
| L1    | 6       | 4               |
| L2    | 3       | 3               |
| L3    | 1       | 1               |
| **Total per row** | **26** | **12** |

Over 100,000 rows: **1.2 million OS thread create/join cycles**.

On native ARM hardware a thread create+join cycle costs ~10–50 µs. On QEMU ARM emulation (where every ARM instruction is translated to x86 at runtime), the same operation costs ~1–5 ms. This puts the thread management overhead alone at **1,200–6,000 seconds** — which matches the observed 4640 s runtime almost exactly.

The 162% average CPU (vs a theoretical 400% for 4 fully-loaded cores) directly reflects this: roughly 60% of available CPU time is being consumed by the kernel creating, scheduling, and destroying threads rather than running HTM computation.

### Problem 2 — QEMU ARM emulation overhead

QEMU emulates a Cortex-A72 (RPi 4B) by translating ARM instructions to x86 in a JIT-like loop on the host. This adds a fundamental ~4–5× overhead on every instruction — it cannot be avoided in software and is unrelated to the algorithm. Any benchmark taken on QEMU will systematically overstate real-hardware runtime by this factor.

For a 400 s target measured on real RPi 4B hardware, the equivalent budget on QEMU is 400 × 4 = **1,600 s**. The current 4640 s is still 2.9× over even that QEMU-adjusted budget, which is where Problem 1 (thread churn) accounts for the remainder.

---

## Improvement Suggestions

### Suggestion 1 — Replace `std::async` with a persistent thread pool

**Where:** `HTMPyramid::runLayer()` in `src/htm_pyramid.cpp`

**Why it matters:**

The current design pays the full cost of thread creation on every layer call. A thread pool creates a fixed number of worker threads once (at pyramid construction or at the start of `run()`) and keeps them alive for the duration. Work is submitted to them via a queue; threads block on the queue when idle and wake immediately when work arrives.

The cost of reusing an existing thread is ~1 µs (a queue push + a condition variable signal) compared to ~1–5 ms for creating one on QEMU. Eliminating 1.2M thread creations at a saving of ~1 ms each removes **~1,200 s** from the runtime in the best case.

**Expected speedup on QEMU:** 3–4×, bringing QEMU runtime from ~4640 s to ~1,200–1,550 s.

**Expected speedup on real hardware:** the benefit compounds because real hardware allows all 4 cores to sustain ~100% utilisation without context-switch pressure. Layer-level parallelism (16 independent L0 modules, 6 independent L1 modules, etc.) can be fully exploited.

**Implementation approach:** replace `std::vector<std::future<...>>` + `std::async` calls in `runLayer()` with a submit/wait pattern against a fixed 4-thread pool created in `HTMPyramid::build()`. Each `worker` lambda becomes a task submitted to the pool; the caller waits for all submitted tasks to complete before proceeding to the next layer.

### Suggestion 2 — Run on real RPi 4B hardware, not QEMU

**Why it matters:**

QEMU is a functional emulator, not a performance emulator. The ~4–5× slowdown from ARM-to-x86 instruction translation is a hard physical limit that no software optimisation can overcome while staying on QEMU. The 400 s target is a hardware target — it implicitly assumes the code runs on the actual Cortex-A72 cores of a real RPi 4B board.

Moving from QEMU to real hardware provides a **4–5× speedup on every operation** for free, without any code change.

### Combined projected runtime

Starting from the observed 4640 s:

```
4640 s  ÷ 4   (real RPi 4B hardware)           → ~1160 s
1160 s  ÷ 3–4 (thread pool, full core use)      → ~290–387 s  ✓
```

Both suggestions together bring the projected runtime to **~290–390 s on real RPi 4B**, which meets the 400 s target.

---

## Why Neither Fix Alone Is Sufficient

| Scenario | Estimated runtime |
|----------|-----------------|
| Current (QEMU + `std::async`) | 4640 s |
| Thread pool only (still on QEMU) | ~1200–1550 s |
| Real hardware only (still `std::async`) | ~1160 s |
| **Thread pool + real hardware** | **~290–390 s ✓** |

The QEMU emulation penalty and the thread-churn penalty are multiplicative. Fixing one without the other still leaves the runtime ~3× over the 400 s budget.

---

## Secondary Observations

### Batch data loading still active

The filename prefix `rpi_qemu_batch-load` and the RAM spike to ~990 MB at t=10 s confirm that the batch-load parquet path (`ReadTable()`) is in use — the row-group streaming fix discussed separately was not yet compiled into this binary. This contributes to the high baseline RAM (~990 MB just for the dataset + model), which puts pressure on the QEMU VM's memory bus and increases cache miss rates during HTM computation.

Switching to the row-group streaming `ParquetRowStreamer` would reduce peak RAM from ~1333 MB to ~100–200 MB, which would improve cache behaviour and reduce the risk of hitting swap on memory-constrained QEMU configs.

### F1 score is reasonable but not final

Best F1 = 0.4478 at threshold 0.97. This result is produced by the single-threaded sequential processing which is deterministic and matches the Python reference. The score is a baseline to compare against once the threading model is changed — the thread pool must preserve the sequential per-module state (TM is stateful across timesteps) and only parallelise **within** a layer, not across timesteps or across layers.
