# HTM-SWaT — RPi 4B QEMU Run: SW-8 (2026-06-04)

**Platform:** Raspberry Pi 4B emulated via QEMU (cortex-a72, 4 vCPU, 2 GB RAM)  
**Binary:** Cross-compiled ARM64 on x86 host (`aarch64-linux-gnu-g++`), no Docker  
**Branch:** `sliding-window-8` (WINDOW_SIZE=8)  
**Config:** v5 — `data_min=486800`, `data_max=946719`, `data_res=1`, `learn_period=10000`, `seed=69`  
**Dataset:** SWaT — 459,919 rows evaluated, 52,108 attack rows (11.09%), read from CSV  
**Run timestamp:** 2026-06-04 09:58:14

---

## Results

| Metric | Value |
|--------|-------|
| **F1** | **0.3269** |
| Precision | 0.2155 |
| Recall | 0.6766 |
| Accuracy | 0.6626 |
| Optimal Threshold | 0.78 |
| Thresholds Tested | 101 |

### Avg Across All 101 Thresholds

| Avg F1 | Avg Precision | Avg Recall | Avg Accuracy |
|--------|---------------|------------|--------------|
| 0.2416 | 0.1506 | 0.7832 | 0.4252 |

---

## Resource Usage

| Metric | QEMU RPi 4B | x86 Host (ref) | Notes |
|--------|-------------|----------------|-------|
| Runtime | 3,830.6 s (~63.8 min) | 440 s | 8.7× slower — QEMU ARM emulation overhead |
| Avg CPU % | 142.3% | 73.8% | Linux-style (100% = 1 core) |
| Avg CPU % (normalized) | 35.6% of 4 vCPUs | 9.2% of 8 cores | QEMU emulation is CPU-intensive |
| Mean RAM | **385.0 MB** | 1,299.7 MB | **3.4× lower on QEMU ARM64** |
| Peak RAM | **398.5 MB** | 1,310.1 MB | **3.3× lower on QEMU ARM64** |
| RPi 4B target | < 350 MB | — | 48.5 MB over target ⚠️ |

---

## RAM Profile

RAM grows from ~70 MB at startup to ~393 MB by t=110s, then holds steady at 393.2 MB for the entire run (~110s to ~3810s), with a final bump to 398.4 MB at the end.

```
  0s –  50s   ramp-up:   70 MB → 335 MB  (loading model structures)
 50s – 110s   settling:  335 MB → 393 MB  (SP/TM synapses stabilizing)
110s – 3810s  plateau:   393.2 MB         (stable — no memory growth)
3810s – 3830s finish:   393.2 → 398.5 MB (threshold sweep computation)
```

The plateau at exactly 393.2 MB for 3700+ seconds confirms **no memory leak** — model size is fixed after initialization.

---

## Key Findings

### F1 is bit-identical to x86
The QEMU run produces **F1=0.3269** — exactly matching the x86 sliding-window-8 result. The cross-compiled ARM64 binary is numerically correct.

### RAM is 3.3× lower than x86
Peak RAM dropped from 1,310 MB (x86) to 398.5 MB (QEMU ARM64). Two contributing factors:
1. **No Arrow/Parquet library** — built with `-DUSE_ARROW=OFF`. The Arrow shared libraries add significant memory footprint on x86.
2. **CSV streamer** — reads rows sequentially with no batch buffering, unlike the Arrow Parquet reader.

### RPi 4B RAM target: 48.5 MB over
The target was <350 MB. Peak RAM is 398.5 MB — **13.9% over budget**. This is still a dramatic improvement over the x86 run (1,310 MB → 70% reduction) but the RPi 4B target is not yet met.

### Runtime slowdown is expected
QEMU ARM emulation translates every ARM64 instruction to x86 at runtime — typically 5–10× overhead. The observed 8.7× slowdown is consistent with that range. On real RPi 4B hardware (native ARM64, no emulation), runtime would approach or exceed the x86 figure (~440s).

---

## Comparison: QEMU vs x86 (SW-8, same config)

| | x86 Host | QEMU RPi 4B | Delta |
|--|----------|-------------|-------|
| F1 | 0.3269 | **0.3269** | identical ✅ |
| Precision | 0.2155 | **0.2155** | identical ✅ |
| Recall | 0.6766 | **0.6766** | identical ✅ |
| Accuracy | 0.6626 | **0.6626** | identical ✅ |
| Runtime | 440 s | 3,830 s | 8.7× slower |
| Mean RAM | 1,299.7 MB | **385.0 MB** | 3.4× lower |
| Peak RAM | 1,310.1 MB | **398.5 MB** | 3.3× lower |
| Avg CPU (normalized) | 9.2% / 8 cores | 35.6% / 4 vCPUs | higher per-core on QEMU |
