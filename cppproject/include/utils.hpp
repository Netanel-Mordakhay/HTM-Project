#pragma once

// Helper functions for SDRs, file I/O, etc.

#include <vector>
#include <string>

// Forward declare
namespace htm {
    class SDR;
}

namespace htm_swat {

// SDR helper functions

// TODO: Merge SDRs with union (Python uses mode='u')
// htm::SDR mergeSDRsUnion(const std::vector<htm::SDR>& sdrs);

// TODO: Calculate anomaly score
// Formula: 1 - (intersection_size / active_size)
// float calcAnomalyScore(const htm::SDR& active, const htm::SDR& predictive);

// File I/O

// TODO: Read CSV file
// std::vector<std::map<std::string, float>> loadCSV(const std::string& filename);

// TODO: Write results to CSV
// void saveResults(const std::string& filename, const std::vector<float>& scores);

} // namespace htm_swat

