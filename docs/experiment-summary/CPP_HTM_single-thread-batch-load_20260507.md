# CPP HTM Single-Threaded Batch-Load — 07/05/2026

**Run ID:** `single-thread-batch-load_20260507_162506_772`
**Branch:** `batch-load` (single-threaded)

---

## Resource Usage

| Metric | Value |
|---|---|
| Avg CPU | 39.0% |
| Peak RAM | 1290.7 MB |
| Avg RAM | 1285.6 MB *(computed over 94 samples at 10s intervals)* |
| Total Time | 950.05 sec *(~15 min 50 sec)* |

---

## Anomaly Detection Results

**Dataset:** 100,000 samples

| Metric | Best Threshold (0.97) | Avg Across Thresholds |
|---|---|---|
| F1 Score | 0.4478 | 0.2541 |
| Precision | 0.4592 | 0.1588 |
| Recall | 0.4370 | 0.8567 |
| Accuracy | 0.7877 | 0.3324 |

---

## RAM Profile

RAM rises gradually from ~1259 MB at t=10s to ~1290 MB by t=310s, then locks completely flat for the remainder of the run — a clean streaming steady-state with no growth after the initial batch load.

---

## Comparison Against Previous Batch-Load Run (20260428)

| Metric | Single-Thread (this run) | Multi-Thread (20260428) | Delta |
|---|---|---|---|
| F1 (best) | 0.4478 | 0.4478 | = |
| Avg CPU | 39.0% | 61.2% | −22.2 pp |
| Peak RAM | 1290.7 MB | 1328.4 MB | −37.7 MB |
| Avg RAM | 1285.6 MB | 1324.1 MB | −38.5 MB |
| Runtime | 950.05 s | 430.01 s | +520 s (+2.21×) |

---

## Notes

- **Detection quality is identical** to the multi-threaded batch-load run (F1 0.4478, same threshold) — confirming threading has no effect on model behaviour, only throughput.
- **Runtime is 2.21× slower** than the multi-threaded batch-load (950s vs 430s), directly attributable to the removal of parallelism across stage-group sub-models.
- **CPU usage dropped from 61.2% to 39.0%** — consistent with a single-core execution profile; the remaining CPU is OS/IO overhead.
- **RAM is ~38 MB lower** on average than the previous batch-load run, likely due to the absence of thread stack and synchronisation overhead.
- **RAM profile is extremely stable** after the initial ramp (~310s to reach steady state), making this variant highly predictable for memory-constrained deployments.
- Compared to the naive preload variants (~5.0 GB RAM), this run uses **~3.9× less memory**, making it the most viable candidate for Raspberry Pi deployment.
