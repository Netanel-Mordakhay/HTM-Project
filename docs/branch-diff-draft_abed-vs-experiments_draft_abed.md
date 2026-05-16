# Branch Comparison: `draft_abed` vs `experiments/draft_abed`

**Date:** 2026-04-17  
**Current branch:** `experiments/draft_abed`

---

## Summary

`draft_abed` is a strict ancestor of `experiments/draft_abed` — it has no unique commits.  
All changes flow in one direction: `experiments/draft_abed` adds 5 commits on top of `draft_abed`.

---

## Commits unique to `experiments/draft_abed`

| Commit | Message |
|--------|---------|
| `4eb4544` | Added MD file explaining Project-Scope RPI-HTM Integration |
| `c414281` | utilsss |
| `4273feb` | dockerfile nati fix |
| `fbdbac7` | readded cpp |
| `f84a525` | initial experiments Naive CPP |

---

## Code Differences

### 1. `cppproject/src/utils.cpp` — Anomaly score logic fix

- **Before (`draft_abed`):** Returns `1.0f` (anomaly) when there are no active bits.
- **After (`experiments/draft_abed`):** Returns `0.0f` (not anomalous) — aligned with the Python implementation's `calc_anomaly_score` behavior, where no active bits means no evidence of anomaly.

---

### 2. `cppproject/src/htm_pyramid.cpp` — Encoder seeding + per-row timing

- **Encoder seeding:** Each encoder now receives a unique seed (`seed_ * encoder_idx`) instead of all sharing the same seed. Matches Python's `EncoderFactory` logic to reduce SDR collisions.
- **Per-row latency:** Added `std::chrono` timing around each row's processing loop. Results are reported via `ExperimentMonitor::recordRowLatency()`.

---

### 3. `cppproject/src/main.cpp` — Experiment instrumentation

- **Run timestamp** is now generated at startup and reused throughout (previously generated fresh at save time, causing timestamp mismatch between files).
- **`ExperimentMonitor`** integrated at key pipeline stages:
  - `configs_loaded`
  - `data_load_start` / `data_load_complete`
  - `data_filtered_N_rows`
  - `results_saved`
- **Output directory** is now a structured experiment folder (`exp_dir/`) instead of flat `results/` files.
- **New JSON output files** per run:
  - `model_performance_metrics.json`
  - `roc_thresholds.json`
  - `model_efficiency_metrics.json`

---

### 4. `cppproject/CMakeLists.txt` — Build system improvements

- Added `experiment_utils.cpp` and `experiment_utils.hpp` to the build targets.
- Arrow/Parquet linking now gracefully falls back between shared and static targets (`Arrow::arrow_shared` → `Arrow::arrow_static`), fixing compatibility with vcpkg-managed packages.

---

### 5. `cppproject/Dockerfile` — Arrow installation fix

- Replaced the best-effort `apt-get` install (which silently skipped Arrow if unavailable) with a proper install from the **official Apache Arrow APT repository**, ensuring CMake targets are correctly provided.

---

### 6. New files (only in `experiments/draft_abed`)

| Path | Description |
|------|-------------|
| `cppproject/include/experiment_utils.hpp` | `ExperimentMonitor` class declaration |
| `cppproject/src/experiment_utils.cpp` | `ExperimentMonitor` implementation (timing, metrics, JSON output) |
| `cppproject/results/*/anomaly_scores.csv` | Experiment run output CSVs |
| `cppproject/results/*/model_performance_metrics.json` | Per-run performance metrics |
| `cppproject/results/*/model_efficiency_metrics.json` | Per-run efficiency metrics |
| `cppproject/results/*/roc_thresholds.json` | ROC threshold sweep results |
| `docs/` | Project-scope RPI-HTM integration document (MD + PDF) |
| `docs/` | Reference paper PDF |
