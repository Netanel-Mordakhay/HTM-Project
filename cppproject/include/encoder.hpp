#pragma once

// Feature encoder - converts data into SDRs
// Matches the Python encoder implementation
// Handles floats, categories, and timestamps

#include <string>

// Forward declare htm types
namespace htm {
    class SDR;
}

namespace htm_swat {

class FeatureEncoder {
public:
    FeatureEncoder(const std::string& name,
                   const std::string& type,  // "float", "cat", or "timestamp"
                   double resolution = 0.1,
                   int encoder_size = 600,
                   int active_bits = 10);
    
    ~FeatureEncoder();
    
    // TODO: Implement encoding
    // htm::SDR encode(float value) const;  // for float features
    // htm::SDR encode(int value) const;    // for categorical
    // htm::SDR encode(const std::string& value) const;  // for timestamps
    
private:
    std::string feature_name_;
    std::string feature_type_;
    double resolution_;
    int encoder_size_;   // n = 600 (same as Python)
    int active_bits_;    // w * n ≈ 10 (Python uses w=0.0166)
    
    // TODO: Add htm.core encoder here
    // std::unique_ptr<htm::ScalarEncoder> encoder_;
};

} // namespace htm_swat

