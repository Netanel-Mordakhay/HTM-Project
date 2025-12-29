#pragma once

// Helper functions for SDRs, file I/O, etc.

#include <vector>
#include <string>
#include <map>
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

// Graph utilities

// Build layer dictionary from features and connections
// Returns map: layer_index -> vector of node names
std::map<int, std::vector<std::string>> getLayerDict(
    const std::map<std::string, std::vector<std::string>>& features,
    const std::map<std::string, std::vector<std::string>>& connections);

} // namespace htm_swat

