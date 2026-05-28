# Sliding-Window Branch — Bug Analysis & Fixes

**Branch:** `sliding-window`  
**Date:** 2026-05-25  
**Symptom:** F1=0.0000 for all 101 thresholds despite other branches achieving F1≈0.45

---

## Bug 1 (Primary): `learn_period` Consumes Entire Score Set

### Root Cause

The sliding-window novelty processes 100,000 raw rows but only produces **1 score per 10-row window**, yielding **10,000 total scores**. The config value `learn_period: 10000` was inherited unchanged from other branches.

In `utils.cpp`, `calcMetrics` has this guard:

```cpp
size_t start = static_cast<size_t>(learn_period);
if (start >= predictions.size()) return m;  // returns zeroed Metrics{}
```

With `learn_period = 10000` and `predictions.size() = 10000`:

```
10000 >= 10000  →  true  →  returns Metrics{} immediately
```

All metrics (F1, precision, recall, accuracy) are 0. The grid search over 101 thresholds always sees 0.0 and reports best F1 = 0.0000.

### Comparison

| Branch | Total scores | `learn_period` | Scores evaluated |
|--------|-------------|----------------|-----------------|
| `batch-load-abed` | 100,000 | 10,000 | 90,000 ✅ |
| `sliding-window` (broken) | 10,000 | 10,000 | **0** ❌ |
| `sliding-window` (fixed) | 10,000 | **1,000** | 9,000 ✅ |

### Fix

In `config/model/config_model_default.yaml`, change:

```yaml
learn_period: 10000
```
to:
```yaml
learn_period: 1000
```

This preserves the same **10% warm-up proportion** as other branches while leaving 9,000 scores for evaluation.

---

## Bug 2 (Secondary): Label Only Captured at t10, Misses Attacks at t5

### Root Cause

The window pair-encodes two timesteps: row at index 4 (t5) and row at index 9 (t10). The label was only collected from t10:

```cpp
// Before (broken)
labels_.push_back(row_streamer_->lastLabel());  // only t10 label
```

If an attack started or ended at t5 and was over by t10, the window would be mislabeled as **normal**, causing label-score misalignment and degraded F1.

### Fix

Track the label at t5 separately and mark the window as an attack if **either** timestep was an attack:

```cpp
// At t5 (row_idx % 10 == 4):
saved_label_t5 = row_streamer_->lastLabel();

// At t10 (row_idx % 10 == 9):
int window_label = (saved_label_t5 || row_streamer_->lastLabel()) ? 1 : 0;
labels_.push_back(window_label);
```

Applied in `src/htm_pyramid.cpp`.

---

## Summary of Changes

| File | Change |
|------|--------|
| `config/model/config_model_default.yaml` | `learn_period: 10000` → `learn_period: 1000` |
| `src/htm_pyramid.cpp` | Window label = attack if t5 OR t10 was attack |
