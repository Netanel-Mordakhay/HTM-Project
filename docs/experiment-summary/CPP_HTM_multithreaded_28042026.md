# CPP HTM multithreaded - 29/04/2026

**Run ID:** `20260429_071324_532`
**Branch:** `multithreaded`

---

## Resource Usage

| Metric | Value |
|---|---|
| Peak CPU | — *(not recorded by rusage on this platform)* |
| Avg CPU | 61.1% |
| Peak RAM | 5039.1 MB (~4.9 GB) |
| Avg RAM | 5018.9 MB (~4.9 GB) |
| Total Time | 460.01 sec *(~7 min 40 sec)* |

---

## Anomaly Detection Results

**Dataset:** 100,000 samples

| Metric | Best Threshold (0.97) | Avg Across Thresholds |
|---|---|---|
| F1 Score | 0.4478 | 0.2541 |
| Precision | 0.4592 | 0.1588 |
| Recall | 0.4370 | 0.8567 |
| Accuracy | 0.7877 | 0.3324 |

**Anomaly Score Stats**

| Metric | Value |
|---|---|
| Peak Score | 1.0000 |
| Avg Score | 0.6603 |

---

## Notes

- RAM ramps from 4279 MB to ~5038 MB within the first 20 seconds (full dataset load), then stabilises — same batch-load memory pattern as `experiments/draft_abed`.
- Avg CPU of 61.1% is significantly lower than `experiments/draft_abed` (105.5%), suggesting the multithreading implementation is not fully utilising available cores during the HTM inference phase.
- F1 of 0.4478 matches `batch-load` exactly — same model performance, same threshold behaviour.
- Run completed in 460 sec — **32% faster** than `experiments/draft_abed` (680 sec) despite identical F1, likely due to differences in the HTM pyramid execution path.
- RAM footprint (~5.0 GB) is consistent across both batch-loading branches (`multithreaded` and `experiments/draft_abed`), and ~3.8x higher than the streaming `batch-load` branch (1.3 GB).
