# CPP RPI HTM experiments_draft_abed - 28/04/2026

**Run ID:** `experiments_cpp_naive_20260428_202955_579`
**Branch:** `experiments/draft_abed`

---

## Resource Usage

| Metric | Value |
|---|---|
| Peak CPU | — *(not recorded by rusage on this platform)* |
| Avg CPU | 105.5% |
| Peak RAM | 5039.2 MB (~4.9 GB) |
| Avg RAM | 5022.7 MB (~4.9 GB) |
| Total Time | 680.01 sec *(~11 min 20 sec)* |

---

## Anomaly Detection Results

**Dataset:** 100,000 samples

| Metric | Best Threshold (0.97) | Avg Across Thresholds |
|---|---|---|
| F1 Score | 0.6626 | 0.1716 |
| Precision | 0.4954 | 0.0992 |
| Recall | 1.0000 | 0.9901 |
| Accuracy | 0.9491 | 0.3910 |

**Anomaly Score Stats**

| Metric | Value |
|---|---|
| Peak Score | 1.0000 |
| Avg Score | 0.6603 |

---

## Notes

- RAM ramps from 4324 MB to ~5037 MB in the first 20 seconds (full dataset load), then stabilises — the model holds the entire dataset in memory.
- Avg CPU of 105.5% confirms multi-core utilisation during the run (vs 61.2% on batch-load).
- Perfect recall (1.0000) at threshold 0.97 — every true anomaly is caught, at the cost of ~50% false positive rate (precision 0.4954).
- F1 of 0.6626 is a significant improvement over batch-load (0.4478), driven by the recall gain.
- Run took ~1.58x longer than batch-load (680 sec vs 430 sec) with ~3.8x more RAM (5.0 GB vs 1.3 GB).
