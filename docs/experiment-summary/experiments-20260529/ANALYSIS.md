# C++ HTM Experiment Analysis — 2026-05-29/30

**Config:** v5 (`data_min=486800`, `data_max=946719`, `data_res=1`, `learn_period=10000`)  
**Dataset:** SWaT — 459,919 rows processed, 52,108 attack rows (11.09% of evaluated window)

---

## 1. Results Per Run

### experiments/draft_abed — Single-Thread C++ (v5)

| Metric | Value |
|--------|-------|
| Best F1 | 0.1635 |
| Precision | 0.0891 |
| Recall | **1.0000** |
| Accuracy | 0.4886 |
| Threshold | 0.97 |
| Runtime | 2070 s |
| Avg CPU | 108.5% |
| Peak RAM | **7034 MB** |

> Recall=1.0 at threshold=0.97 means the model flags almost everything as an anomaly — a degenerate result. The model never produced low scores, possibly due to single-threaded batch loading causing inconsistent SP/TM initialization across the 26 modules.

---

### experiments/draft_abed — Single-Thread C++ (v3)

| Metric | Value |
|--------|-------|
| Best F1 | 0.1400 |
| Precision | 0.0753 |
| Recall | **1.0000** |
| Accuracy | 0.3859 |
| Threshold | 0.97 |
| Runtime | 2110 s |
| Avg CPU | 108.9% |
| Peak RAM | **7259 MB** |

> Same degenerate recall=1.0 issue as v5. Older config (v3) performs slightly worse.

---

### multithreaded — Multithreaded C++ (v5, in-memory)

| Metric | Value |
|--------|-------|
| Best F1 | 0.2683 |
| Precision | 0.1612 |
| Recall | 0.7985 |
| Accuracy | 0.4848 |
| Threshold | 0.97 |
| Runtime | 1610 s |
| Avg CPU | 68.8% |
| Peak RAM | **7031 MB** |

> Multithreading resolves the degenerate recall issue (recall drops from 1.0 to 0.8, precision improves from 0.089 to 0.161). CPU drops 37% vs single-thread. RAM unchanged — the full dataset is still loaded into memory before processing. Row-by-row streaming was not yet added on this branch.

---

### batch-load — Multithreaded C++ with Streaming (v5)

| Metric | Value |
|--------|-------|
| Best F1 | 0.2683 |
| Precision | 0.1612 |
| Recall | 0.7985 |
| Accuracy | 0.4848 |
| Threshold | 0.97 |
| Runtime | 1580 s |
| Avg CPU | 68.5% |
| Peak RAM | **1310 MB** |

> Identical F1 to multithreaded (same algorithm, same config). The critical improvement: **switching from batch loading to row-by-row streaming reduced RAM from 7031 MB → 1310 MB (5.4× reduction)**. Runtime also slightly faster.

---

### sliding-window-2 — SW Window=2 (v5)

| Metric | Value |
|--------|-------|
| Best F1 | 0.2994 |
| Precision | **0.2373** |
| Recall | 0.4057 |
| Accuracy | **0.7632** |
| Threshold | 0.97 |
| Runtime | 980 s |
| Avg CPU | 70.3% |
| Peak RAM | 1323 MB |

---

### sliding-window-4 — SW Window=4 (v5)

| Metric | Value |
|--------|-------|
| Best F1 | 0.2652 |
| Precision | 0.1638 |
| Recall | 0.6948 |
| Accuracy | 0.5419 |
| Threshold | 0.78 |
| Runtime | 650 s |
| Avg CPU | 70.7% |
| Peak RAM | 1357 MB |

---

### sliding-window-8 — SW Window=8 (v5) ⭐ Best Novelty Result

| Metric | Value |
|--------|-------|
| Best F1 | **0.3269** |
| Precision | 0.2155 |
| Recall | 0.6766 |
| Accuracy | 0.6626 |
| Threshold | 0.78 |
| Runtime | **440 s** |
| Avg CPU | 73.8% |
| Peak RAM | **1310 MB** |

> Best F1 across all runs. Merging 8 rows per window provides the optimal temporal context — wide enough to capture meaningful change, not so wide that the SDR becomes too dense.

---

### sliding-window-16 — SW Window=16 (v5)

| Metric | Value |
|--------|-------|
| Best F1 | 0.2888 |
| Precision | 0.1742 |
| Recall | **0.8425** |
| Accuracy | 0.5080 |
| Threshold | 0.52 |
| Runtime | 340 s |
| Avg CPU | **75.2%** |
| Peak RAM | 1313 MB |

> F1 drops vs SW-8 — SDR union of 16 rows becomes too dense, causing precision collapse. Highest recall of all runs.

---

## 2. Cross-Run Comparison Table

| Branch | F1 | Precision | Recall | Accuracy | Runtime (s) | CPU % | Peak RAM (MB) |
|--------|----|-----------|--------|----------|-------------|-------|---------------|
| Single-thread v5 | 0.1635 | 0.0891 | 1.0000 | 0.4886 | 2070 | 108.5% | 7034 |
| Single-thread v3 | 0.1400 | 0.0753 | 1.0000 | 0.3859 | 2110 | 108.9% | 7259 |
| Multithreaded v5 (in-memory) | 0.2683 | 0.1612 | 0.7985 | 0.4848 | 1610 | 68.8% | 7031 |
| Batch-load v5 | 0.2683 | 0.1612 | 0.7985 | 0.4848 | 1580 | 68.5% | 1310 |
| SW-2 v5 | 0.2994 | **0.2373** | 0.4057 | **0.7632** | 980 | 70.3% | 1323 |
| SW-4 v5 | 0.2652 | 0.1638 | 0.6948 | 0.5419 | 650 | 70.7% | 1357 |
| **SW-8 v5** ⭐ | **0.3269** | 0.2155 | 0.6766 | 0.6626 | **440** | 73.8% | **1310** |
| SW-16 v5 | 0.2888 | 0.1742 | 0.8425 | 0.5080 | 340 | 75.2% | 1313 |

---

## 3. Key Observations

### F1 Progression
```
Single-thread  0.1635
Multithreaded  0.2683  (+64%)  — multithreading fixes degenerate scoring
Batch-load     0.2683          — same F1, 5.4× less RAM
SW-2           0.2994  (+12%)  — sliding window novelty improves F1
SW-4           0.2652          — too short a temporal gap per window
SW-8           0.3269  (+22%)  — BEST: optimal window size ⭐
SW-16          0.2888          — SDR too dense, precision collapses
```

### RAM Progression
```
Single-thread   7034 MB  (full dataset loaded into memory)
Multithreaded   7031 MB  (still in-memory — no RAM improvement)
Batch-load      1310 MB  (row-by-row streaming — 5.4× reduction) ✅
Sliding-window  1310–1357 MB  (all streaming)
Pi 4B target     350 MB  (still 3.7× over target ❌)
```

### Runtime Progression
```
Single-thread  2070 s
Multithreaded  1610 s  (1.3× faster)
Batch-load     1580 s  (similar to multithreaded)
SW-2            980 s  (1.6× faster than batch-load)
SW-4            650 s  (2.4× faster)
SW-8            440 s  (3.6× faster) ⭐
SW-16           340 s  (4.6× faster)
```

### CPU Progression
```
Single-thread  108.5%  (1 full core saturated)
Multithreaded   68.8%  (37% reduction via parallelism) ✅
Batch-load      68.5%
SW-2–SW-16   70–75%   (slightly higher due to per-window encoding cost)
Pi 4B target  < 65%   (marginally over for all streaming runs ❌)
```

---

## 4. Sliding Window — Window Size Tradeoff

| Window Size | HTM Runs | F1 | Precision | Recall | Runtime |
|-------------|----------|----|-----------|--------|---------|
| 2 | 229,960 | 0.2994 | 0.2373 | 0.4057 | 980 s |
| 4 | 114,980 | 0.2652 | 0.1638 | 0.6948 | 650 s |
| **8** | **57,490** | **0.3269** | **0.2155** | **0.6766** | **440 s** |
| 16 | 28,745 | 0.2888 | 0.1742 | 0.8425 | 340 s |

**Conclusion:** SW-8 is the sweet spot. Larger windows reduce HTM runs (faster) but merge too many rows into one SDR — the signal becomes too blurry past window=8 and precision collapses. SW-8 achieves **best F1 (+22% over batch-load)** at **3.6× faster runtime** with **identical RAM**.

---

## 5. Summary

| Goal | Best Run | Value |
|------|----------|-------|
| Best F1 | SW-8 | **0.3269** |
| Best precision | SW-2 | 0.2373 |
| Best recall | SW-16 | 0.8425 |
| Best accuracy | SW-2 | 0.7632 |
| Fastest runtime | SW-16 | 340 s |
| Lowest RAM | Batch-load / SW-8 | 1310 MB |
| Lowest CPU | Batch-load | 68.5% |
| Best overall | **SW-8** | F1=0.3269, 440s, 1310MB, 73.8% CPU |
