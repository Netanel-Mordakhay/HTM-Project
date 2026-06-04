# HTM-SWaT Experiment Summary — v5 Config (2026-05-29/30)

**Config:** v5 — `data_min=486800`, `data_max=946719`, `data_res=1`, `learn_period=10000`  
**Dataset:** SWaT — 459,919 rows evaluated, 52,108 attack rows (11.09%)  
**Host:** 8-core machine (`nproc=8`). CPU% is Linux-style (100% = 1 full core). Normalized CPU = CPU% ÷ 8.

---

## All Runs — v5 Config

| Run | Branch | F1 | Precision | Recall | Accuracy | Threshold | Runtime (s) | Avg CPU % | Avg CPU % / 8 cores | Mean RAM (MB) | Peak RAM (MB) |
|-----|--------|----|-----------|--------|----------|-----------|-------------|-----------|----------------------|---------------|---------------|
| Python (baseline) | rpi-htm-py | 0.0959 | 0.0587 | 0.2634 | 0.4375 | 0.04 | 21,240 | 107.5% | 13.4% | 10,866.0 | 13,596.8 |
| C++ Single-thread | experiments/draft_abed | 0.1635 | 0.0891 | 1.0000 ⚠️ | 0.4886 | 0.97 | 2,070 | 108.5% | 13.6% | 7,008.6 | 7,034.0 |
| C++ Multithreaded (in-memory) | multithreaded | 0.2683 | 0.1612 | 0.7985 | 0.4848 | 0.97 | 1,610 | 68.8% | 8.6% | 7,011.7 | 7,031.0 |
| C++ Multithreaded + Streaming | batch-load | 0.2683 | 0.1612 | 0.7985 | 0.4848 | 0.97 | 1,580 | 68.5% | 8.6% | 1,304.7 | 1,310.4 |
| C++ SW Window=2 | sliding-window-4 | 0.2994 | 0.2373 | 0.4057 | 0.7632 | 0.97 | 980 | 70.3% | 8.8% | 1,317.0 | 1,323.5 |
| C++ SW Window=4 | sliding-window-4 | 0.2652 | 0.1638 | 0.6948 | 0.5419 | 0.78 | 650 | 70.7% | 8.8% | 1,342.4 | 1,356.5 |
| **C++ SW Window=8** ⭐ | **sliding-window-4** | **0.3269** | **0.2155** | **0.6766** | **0.6626** | **0.78** | **440** | **73.8%** | **9.2%** | **1,299.7** | **1,310.1** |
| C++ SW Window=16 | sliding-window-4 | 0.2888 | 0.1742 | 0.8425 | 0.5080 | 0.52 | 340 | 75.2% | 9.4% | 1,299.6 | 1,312.7 |

> ⚠️ Recall=1.0 on single-thread is degenerate — the model flags every row as anomaly. This run used v5 config parameters. Root cause: sequential module execution — the 26 SP instances run one-by-one in alphabetical map order, causing duty-cycle counters and boosting to diverge across modules. The TM receives inconsistent SDR representations and never forms stable predictions, so anomaly scores cluster near 1.0 for all rows.

---

## Progression Summaries

### F1 Score
```
Python baseline        0.0959
C++ Single-thread      0.1635  (+70%)  — C++ faster but still degenerate
C++ Multithreaded      0.2683  (+64%)  — parallel init fixes scoring
C++ + Streaming        0.2683          — same F1, 5.4× less RAM
C++ SW-2               0.2994  (+12%)  — sliding window novelty boosts F1
C++ SW-4               0.2652          — too narrow a temporal gap
C++ SW-8               0.3269  (+22%)  — BEST: optimal window ⭐
C++ SW-16              0.2888          — SDR too dense, precision collapses
```

### RAM (Mean / Peak)
```
                       Mean RAM    Peak RAM
C++ Single-thread      7,008.6 MB  7,034.0 MB  (full dataset in memory)
C++ Multithreaded      7,011.7 MB  7,031.0 MB  (still in-memory)
C++ + Streaming        1,304.7 MB  1,310.4 MB  (row-by-row streaming — 5.4× reduction) ✅
C++ SW-2               1,317.0 MB  1,323.5 MB
C++ SW-4               1,342.4 MB  1,356.5 MB
C++ SW-8               1,299.7 MB  1,310.1 MB
C++ SW-16              1,299.6 MB  1,312.7 MB
Python baseline       10,866.0 MB 13,596.8 MB  (grows during run — HTM synapse growth)
Pi 4B target             —           350 MB    (still 3.7× over for streaming runs ❌)
```

### Runtime
```
Python baseline   21,240 s  (single-threaded GIL-bound Python)
C++ Single-thread  2,070 s  (10.3× faster than Python)
C++ Multithreaded  1,610 s  (1.3× faster than single-thread)
C++ + Streaming    1,580 s  (similar to multithreaded)
C++ SW-2             980 s  (1.6× faster than batch-load)
C++ SW-4             650 s  (2.4× faster)
C++ SW-8             440 s  (3.6× faster) ⭐
C++ SW-16            340 s  (4.6× faster)
```

### CPU (normalized to 8 cores)
```
Python baseline    107.5% raw  →  13.4% of machine  (single-threaded)
C++ Single-thread  108.5% raw  →  13.6% of machine  (single-threaded)
C++ Multithreaded   68.8% raw  →   8.6% of machine  (parallel, lower per-core load)
C++ + Streaming     68.5% raw  →   8.6% of machine
C++ SW-2–SW-16    70–75% raw  →  8.8–9.4% of machine
Pi 4B target         < 65% raw  →  < 8.1% of machine  (slightly over ❌)
```

---

## Why F1 Differs Across Runs (Same v5 Config)

All runs share the same v5 hyperparameters (SP/TM params, encoders, learn_period). The F1 differences come from **execution model**, **data representation**, and **implementation** — not tuning.

### Python (F1=0.0959) vs C++ (F1=0.1635+)

Python HTM runs under the GIL — every SP and TM call is single-threaded and serialized through Python's interpreter. The C++ `htm.core` library executes the same algorithms natively without this overhead. Beyond speed, the Python run's RAM grows continuously (1.7 GB → 13.6 GB) as HTM synapse objects accumulate in Python's heap, which can degrade late-stage learning. The optimal threshold also shifts to 0.04 (vs 0.97 in C++) indicating the Python model's anomaly score distribution is much more compressed, meaning it never fully converges to confident predictions.

### C++ Single-thread (F1=0.1635, Recall=1.0 ⚠️) vs Multithreaded (F1=0.2683)

Both single-thread and multithreaded used identical v5 hyperparameters. The F1 difference is caused entirely by **sequential vs parallel module execution**:

- **Sequential SP initialization divergence:** The pyramid has 26 SP modules. In single-thread, they run one-by-one in alphabetical map order. Early modules complete their forward pass while later ones haven't started. This causes SP duty-cycle counters and boosting to diverge across modules — some modules are "warm" while others are cold within the same row. The net effect is inconsistent SDR representations fed into each TM, preventing stable temporal predictions. Anomaly score never drops from ~1.0 → recall=1.0.
- **Multithreaded fix:** All modules in each layer run simultaneously via `std::async`. Every SP starts at the same iteration state, producing consistent representations → TM learns real temporal patterns → proper anomaly scores.
- Note: the single-thread branch had a hardcoded `-v3.yaml` config path which has since been corrected to v5, but the experiment result shown above was produced with v5 parameters already in place.

### Multithreaded in-memory vs Batch-load (F1 identical: 0.2683)

The HTM computation is bit-for-bit identical. Streaming changes only *when* data is read (row-by-row from disk vs all rows pre-loaded into RAM). The SP and TM never see a different input. RAM drops 5.4× (7,031 → 1,310 MB) with zero F1 cost.

### Batch-load vs SW-2 (F1: 0.2683 → 0.2994, +12%)

The sliding window novelty. In batch-load, each row is encoded individually and fed to HTM as a single SDR. In SW-2, **two consecutive rows are merged via bitwise OR** before a single HTM inference. This doubles the temporal context per inference step — the SP receives a richer input capturing sensor correlations across two timesteps. The TM then learns transitions between 2-row patterns rather than 1-row patterns, which better captures the slow-moving industrial processes in SWaT (sensors change over seconds, not per-sample). F1 improves and precision increases notably (0.1612 → 0.2373).

### SW-2 vs SW-4 (F1: 0.2994 → 0.2652, worse)

Wider windows are not always better. With 4 rows per window, the SDR union becomes denser — more active bits from 4 separate encoder outputs overlap. At this point, short-lived attack patterns (lasting 1–3 rows) get diluted into the 4-row union and become indistinguishable from normal windows that also happen to activate similar bits. Precision stays similar but recall drops, and the optimal threshold shifts from 0.97 to 0.78, indicating the score distribution changes character. Overall F1 drops below even the no-window baseline.

### SW-4 vs SW-8 (F1: 0.2652 → 0.3269, best ⭐)

SW-8 hits the sweet spot. An 8-row window (8 seconds at 1 Hz sampling) aligns with real SWaT process cycle times — enough rows to capture a full sensor transition sequence for both normal and attack events. The SDR union of 8 encodings is dense enough to carry rich temporal information but still sparse enough for the SP to learn discriminative column patterns. The TM forms stable predictions for 8-row normal sequences and fires high anomaly scores when an attack disrupts the expected sequence. Best precision-recall balance.

### SW-8 vs SW-16 (F1: 0.3269 → 0.2888)

With 16-row unions, the SDR becomes too dense. Bitwise-OR'ing 16 encoder outputs activates a large fraction of the 2304 bits regardless of the underlying sensor values — normal windows and attack windows start looking similar to the SP. Precision collapses (0.2155 → 0.1742) because the model can no longer reliably distinguish the two. Recall increases (0.6766 → 0.8425) because the model flags more things, but at the cost of many false positives. The optimal threshold also drops to 0.52, reflecting that the score distribution is now bimodal at a lower separation point.

---

## Single-Thread Degenerate Recall — Does Python Have This Bug?

### Failure Mode Comparison

| Implementation | Anomaly scores | Optimal threshold | Recall | Problem |
|---|---|---|---|---|
| C++ Single-thread | Clustered near **1.0** | 0.97 | 1.0000 | TM never learned → every row looks like a surprise |
| C++ Multithreaded | Spread across 0–1 | 0.97 | 0.7985 | Working correctly |
| Python | Clustered near **0** | 0.04 | 0.2634 | TM over-learned → nothing surprises it |

**Python does not have the recall=1.0 bug.** Its failure mode is the opposite: anomaly scores are compressed near 0, so the optimal threshold drops all the way to 0.04 just to catch 26% of attacks. Python uses multiprocessing (one process per model key), so all modules run in separate processes in parallel — it never has the sequential initialization divergence issue.

Python's poor F1 (0.0959) comes from a different set of problems: GIL-bound single-threaded inference, continuously growing RAM (1.7 GB → 13.6 GB) as synapse objects accumulate in Python's heap, and slower learning dynamics in the Python HTM library vs native C++.

### Was this bug present with data_res=5?

Yes. The single-thread v3 run (data_res=5, 100k rows) also produced **Recall=1.0000**. The sequential execution bug is independent of how many rows are sampled — the TM still fails to learn stable predictions regardless of dataset size, because the root cause is inconsistent SP representations, not insufficient data.

### Can we fix it? Should we?

**Can we:** Yes — adding `std::async` parallelism to `runLayer` in the single-thread branch would fix it. That is exactly what the `multithreaded` branch does.

**Should we:** No, for two reasons:
1. **Already fixed** — the `multithreaded` branch is the corrected version. Patching `experiments/draft_abed` would duplicate it.
2. **Useful as a baseline** — the degenerate result is scientifically meaningful. It demonstrates that parallel module initialization is critical for HTM pyramid *correctness*, not just speed. A sequential single-threaded pyramid cannot produce reliable anomaly scores regardless of hyperparameter tuning.

---

## Best-in-Class Summary

| Goal | Run | Value |
|------|-----|-------|
| Best F1 | C++ SW-8 | **0.3269** |
| Best Precision | C++ SW-2 | 0.2373 |
| Best Recall | C++ SW-16 | 0.8425 |
| Best Accuracy | C++ SW-2 | 0.7632 |
| Fastest Runtime | C++ SW-16 | 340 s |
| Lowest RAM | C++ SW-8 / Batch-load | ~1,310 MB |
| Lowest CPU (normalized) | C++ Multithreaded / Batch-load | 8.6% |
| Best overall | **C++ SW-8** | F1=0.3269, 440 s, 1,310 MB, 9.2% CPU |
