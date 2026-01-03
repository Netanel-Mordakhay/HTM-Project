# Evaluation

## What is the threshold?
- During evaluation we compare the continuous anomaly score for each timestep against a cutoff value called the **threshold**.
- If `score > threshold`, the point is classified as an anomaly; otherwise it is considered normal.
- We sweep many thresholds (grid search) to find the value that maximizes the chosen metric (currently F1).

## Current evaluation flow (CPP)
1. **Scores**: Run the model to produce anomaly scores for each row.
2. **Synthetic labels (placeholder)**: Top 5% highest scores are marked as anomalies (1), the rest normal (0). Replace this with real ground-truth labels when available.
3. **Grid search**: 101 thresholds are tested evenly between the min and max observed scores.
4. **Metrics per threshold**: For each threshold we compute precision, recall, F1, and accuracy.
5. **Best vs Average**:
   - **Best**: Metrics at the threshold with the highest F1.
   - **Average**: Mean of each metric across all tested thresholds (helps gauge stability/sensitivity).
6. **Outputs**:
   - Console prints two lines: Best and Avg.
   - Files written with matching timestamps:
     - `results/anomaly_scores_<timestamp>.csv` (scores only)
     - `results/metrics/cpp_metrics_<timestamp>.txt` (Best and Avg metrics)

## Metric definitions
- **Precision**: TP / (TP + FP) — how many predicted anomalies were correct.
- **Recall**: TP / (TP + FN) — how many true anomalies were found.
- **F1**: 2 * precision * recall / (precision + recall) — harmonic mean balancing precision and recall.
- **Accuracy**: (TP + TN) / Total — overall correctness; less informative on imbalanced data.

## How to run
```
cd cppproject
./build/htm_swat
```
This produces a timestamped scores CSV and metrics TXT under `results/`.

## Replacing synthetic labels
- If you have ground-truth labels, populate a `labels` vector aligned to the scores and skip the top-5% heuristic.
- Then rerun; threshold search and metrics will reflect real labels.

## Interpreting results
- **Best F1 row**: Shows performance at the optimal threshold; use this threshold for deployment or alerting.
- **Avg row**: Indicates how sensitive the model is to threshold choice; large drops vs Best imply threshold tuning matters a lot.



**Best row:** At threshold ≈0.97, F1≈0.87 with precision=1.0 and recall≈0.77.   
That means when using this cutoff, every predicted anomaly was correct (no false positives), and ~77% of the synthetic anomalies were found.  
**Avg row:** Averaged across all 101 thresholds, F1 is very low (0.16), precision is low (0.11), recall is very high (0.98), and accuracy collapses (~0.27).  
This shows performance is highly sensitive to threshold choice: most thresholds flag almost everything as anomalous (very high recall, very low precision).
