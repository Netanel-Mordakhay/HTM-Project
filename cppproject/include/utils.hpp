#pragma once

// Helper functions for SDRs, file I/O, etc.

#include <vector>
#include <string>
#include <map>
#include <set>
#include <memory>
#include <htm/types/Sdr.hpp>

namespace htm_swat {

using namespace htm;

// SDR helper functions

// Merge SDRs with union (Python uses mode='u')
// All SDRs must have the same size
SDR mergeSDRsUnion(const std::vector<SDR>& sdrs);

// Merge SDRs with concatenation (Python uses mode='c')
// SDRs are concatenated end-to-end
SDR mergeSDRsConcat(const std::vector<SDR>& sdrs);

// Generic merge function (supports 'u' and 'c' modes)
SDR mergeSDRs(const std::vector<SDR>& sdrs, const std::string& mode);

// Calculate anomaly score
// Formula: 1 - (intersection_size / active_size)
float calcAnomalyScore(const SDR& active, const SDR& predictive);

// File I/O

// Read CSV file (simple implementation)
std::vector<std::map<std::string, double>> loadCSV(const std::string& filename);

// Read Parquet file (using Apache Arrow)
std::vector<std::map<std::string, double>> loadParquet(const std::string& filename);

// Write results to CSV
void saveResults(const std::string& filename, const std::vector<float>& scores);

// Metrics calculation
struct Metrics {
    float precision = 0.0f;
    float recall = 0.0f;
    float f1 = 0.0f;
    float accuracy = 0.0f;
};

// Calculate binary classification metrics (per timestep: pred = score > threshold).
// If learn_period > 0, indices [0, learn_period) are skipped (matches Python eval window).
Metrics calcMetrics(const std::vector<float>& predictions,
                   const std::vector<int>& labels,
                   float threshold,
                   int learn_period = 0);

// Find best score using grid search (like Python find_best_score)
struct BestScoreResult {
    float score = 0.0f;
    Metrics metrics;
    float best_threshold = 0.0f;
    std::map<std::string, float> params;
};

struct GridSearchResult {
    BestScoreResult best;
    Metrics average_metrics;
    size_t thresholds_tested = 0;
};

GridSearchResult findBestScore(const std::vector<float>& predictions,
                               const std::vector<int>& labels,
                               const std::vector<float>& thresholds,
                               int learn_period = 5000);

// Graph utilities

// Build layer dictionary from features and connections
// Returns map: layer_index -> vector of node names
std::map<int, std::vector<std::string>> getLayerDict(
    const std::map<std::string, std::vector<std::string>>& features,
    const std::map<std::string, std::vector<std::string>>& connections);

// RowStreamer: streams one filtered row at a time from a data file.
// Avoids loading the entire dataset into memory — only one row is in RAM at a time.
class RowStreamer {
public:
    virtual ~RowStreamer() = default;
    virtual bool hasNext() const = 0;
    // Returns the next filtered row (only required_features columns).
    virtual std::map<std::string, double> nextRow() = 0;
    // Label of the row returned by the last nextRow() call.
    virtual int lastLabel() const = 0;
    // Total number of rows this streamer will produce.
    virtual size_t totalRows() const = 0;
};

// Factory: opens path and returns the appropriate streamer.
// Supports .parquet (requires USE_ARROW) and .csv files.
std::unique_ptr<RowStreamer> makeStreamer(
    const std::string& path,
    int min_row, int max_row, int stride,
    const std::set<std::string>& required_features);

} // namespace htm_swat

