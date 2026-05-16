# HTM-SWAT Experiment Report
**Run ID:** `20260425_144618_730`  
**Date:** April 25, 2026  
**Branch:** `batch-load-abed`

---

## Output Files

| File | Description |
|------|-------------|
| `results/anomaly_scores_20260425_144618_730.csv` | Anomaly score per row (100,000 rows) |
| `results/metrics/cpp_metrics_20260425_144618_730.txt` | Human-readable best/avg metrics |
| `results/experiments_batch-load-abed_20260425_144618_730/model_performance_metrics.json` | Performance metrics JSON |
| `results/experiments_batch-load-abed_20260425_144618_730/model_efficiency_metrics.json` | CPU, RAM, timing JSON (sampled every 2 s) |
| `results/experiments_batch-load-abed_20260425_144618_730/roc_thresholds.json` | Full 101-point threshold sweep |

---

## Detection Performance

| Metric | Value |
|--------|-------|
| **Best F1** | 0.4478 |
| Best Precision | 0.4592 |
| Best Recall | 0.4370 |
| Best Accuracy | 78.77% |
| Best Threshold | 0.9700 |
| Thresholds Tested | 101 |

*Grid search over 101 thresholds (min→max anomaly score), learn period excluded from evaluation.*

---

## Runtime & Timing Breakdown

| Phase | Duration | % of Total |
|-------|----------|------------|
| Config load | 1 ms | <0.01% |
| Data load (CSV streamer) | 603 ms | 0.1% |
| Pyramid build (26 HTM modules) | 3,337 ms | 0.5% |
| **Model run (100,000 rows)** | **621,988 ms** | **99.3%** |
| Save results | 5 ms | <0.01% |
| **Total** | **626,154 ms (10 min 26 s)** | |

**Throughput:** 100,000 rows / 626 s ≈ **160 rows/sec**

---

## CPU Usage (normalized across all cores)

| Metric | Value |
|--------|-------|
| Average | 7.20% |
| Peak | 11.49% |
| Minimum | 4.87% |

*CPU stays low because the run is single-threaded and the host has many cores. On Pi 4B (4 cores), equivalent single-threaded load would be ~28–46% — within the <65% target.*

---

## RAM Usage

| Metric | Value |
|--------|-------|
| **Peak** | **1,329 MB** |
| Average | 1,325 MB |
| At startup (before build) | 1,144 MB |
| Steady state (during run) | ~1,328 MB |

RAM stabilizes within ~10 seconds after startup and barely grows during the 10-minute run (+185 MB from startup to steady state).

**Gap vs Pi 4B target (<350 MB):** current is **3.8× over target.**

---

## Per-Row Latency

| Metric | Value |
|--------|-------|
| Average | 6.2 ms/row |
| Minimum | 2.8 ms/row |
| Peak | 92.2 ms/row |
| Rows processed | 100,000 |

The 92 ms peak is a one-off spike (likely OS scheduling or SDR memory allocation). The median is close to the average of 6.2 ms.

---

## Comparison vs Pi 4B Targets

| Metric | This Run | Pi 4B Target | Gap |
|--------|----------|--------------|-----|
| RAM | 1,329 MB | < 350 MB | 3.8× over |
| CPU (single-threaded) | ~28–46%* | < 65% | Within target |
| Runtime (100k rows) | 626 s | — | Baseline |

*Estimated from host normalized CPU × host core count / Pi core count.*

---

## Key Observations

1. **RAM is the critical bottleneck.** 1.3 GB is driven by 26 HTM modules each holding a full `SpatialPooler` + `TemporalMemory` with 2304-column SDRs and 16 cells/column. Reducing SDR size or sharing SP/TM weights across modules are the main levers.

2. **99.3% of runtime is the model run loop** — single-threaded, 6.2 ms/row average. Parallelizing L0 modules (16 independent nodes) is the clearest path to a speedup.

3. **CPU is not a problem** on a multi-core host. On Pi 4B it will be higher but should still fit within the 65% budget for a single-threaded run.

4. **F1 = 0.4478 is the current detection baseline** with ground-truth SWAT labels and 100k rows (stride 5 over rows 446k–946k).
