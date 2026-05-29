#pragma once

#include <string>
#include <map>
#include <memory>
#include <vector>
#include <htm/types/Sdr.hpp>
#include <htm/encoders/RandomDistributedScalarEncoder.hpp>

// Data encoding functionality
// Equivalent to Python: htm_source/data/data_streamer.py

namespace htm_swat {

using namespace htm;

/**
 * DataStreamer - Encodes data rows into SDRs using HTM core encoders
 * 
 * Equivalent to Python: htm_source/data/data_streamer.py::DataStreamer
 * 
 * Usage:
 *   DataStreamer streamer(encoders);
 *   SDR encoded = streamer.encodeFeature("fit101", 2.5);
 *   // Or encode a full row:
 *   map<string, SDR> encoded_row = streamer.encodeRow(row_data);
 */
class DataStreamer {
public:
    // Constructor - takes a map of feature encoders
    DataStreamer(const std::map<std::string, std::shared_ptr<RandomDistributedScalarEncoder>>& encoders);
    
    // Constructor with merge plan and mode
    DataStreamer(const std::map<std::string, std::shared_ptr<RandomDistributedScalarEncoder>>& encoders,
                 const std::map<std::string, std::vector<std::string>>& merge_plan,
                 const std::string& feature_merge_mode = "u");
    
    // Encode a single feature value
    // Equivalent to Python: Feature.encode(data)
    SDR encodeFeature(const std::string& feature_name, double value) const;
    
    // Encode a single feature value (categorical - uses integer)
    SDR encodeFeature(const std::string& feature_name, UInt value) const;

    SDR encodePair(const std::map<std::string, double>& row_t5,
               const std::map<std::string, double>& row_t10) const;

    SDR encodeWindow(const std::vector<std::map<std::string, double>>& rows) const;
    //
    // Encode a full row of data (map of feature_name -> value)
    // Returns individual feature SDRs
    std::map<std::string, SDR> encodeRow(const std::map<std::string, double>& row_data) const;
    
    // Encode a full row with merging according to merge_plan
    // Returns merged SDRs keyed by group names (e.g., "L0_1", "L0_2")
    // Equivalent to Python: DataStreamer.get_encoding(data_row)
    std::map<std::string, SDR> encodeRowMerged(const std::map<std::string, double>& row_data) const;
    
    // Check if encoder exists for a feature
    bool hasEncoder(const std::string& feature_name) const;
    
    // Get the number of encoders
    size_t size() const;
    
    // Get encoding dimensions for a feature group
    std::vector<UInt> getEncodingDims(const std::string& group_name) const;

private:
    std::map<std::string, std::shared_ptr<RandomDistributedScalarEncoder>> encoders_;
    UInt encoder_size_;  // Size of each encoder output
    
    // Feature merging
    std::map<std::string, std::vector<std::string>> merge_plan_;  // Group name -> feature list
    std::string feature_merge_mode_;  // "u" for union, "c" for concat
    bool has_merge_plan_;
};

} // namespace htm_swat

