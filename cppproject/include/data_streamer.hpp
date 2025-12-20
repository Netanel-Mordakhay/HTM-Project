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
    
    // Encode a single feature value
    // Equivalent to Python: Feature.encode(data)
    SDR encodeFeature(const std::string& feature_name, double value) const;
    
    // Encode a single feature value (categorical - uses integer)
    SDR encodeFeature(const std::string& feature_name, UInt value) const;
    
    // Encode a full row of data (map of feature_name -> value)
    // Equivalent to Python: DataStreamer.get_encoding(data_row)
    std::map<std::string, SDR> encodeRow(const std::map<std::string, double>& row_data) const;
    
    // Check if encoder exists for a feature
    bool hasEncoder(const std::string& feature_name) const;
    
    // Get the number of encoders
    size_t size() const;

private:
    std::map<std::string, std::shared_ptr<RandomDistributedScalarEncoder>> encoders_;
    UInt encoder_size_;  // Size of each encoder output
};

} // namespace htm_swat

