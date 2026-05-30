# CPP RPI HTM 28042026 - branch: batch-load

**Run ID:** `20260428_190327_047`

---

## Resource Usage

| Metric | Value |
|---|---|
| Peak CPU | — *(not recorded)* |
| Avg CPU | 61.2% |
| Peak RAM | 1328.4 MB |
| Avg RAM | 1324.1 MB |
| Total Time | 430.01 sec *(~7 min 10 sec)* |

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

- RAM usage stabilised quickly (~1327 MB) after an initial ramp from 1300 MB, indicating the HTM model reached steady state early in the run.
- High average anomaly score (0.66) with best F1 of only 0.45 suggests significant false positive rate at the optimal threshold — recall-dominated performance at lower thresholds (0.86 avg recall).
