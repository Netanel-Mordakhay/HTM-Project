#include "encoder.hpp"

namespace htm_swat {

FeatureEncoder::FeatureEncoder(const std::string& name,
                                const std::string& type,
                                double resolution,
                                int encoder_size,
                                int active_bits)
    : feature_name_(name),
      feature_type_(type),
      resolution_(resolution),
      encoder_size_(encoder_size),
      active_bits_(active_bits) {
    // TODO: Set up htm.core encoder
    // Float -> ScalarEncoder or RDSE
    // Category -> CategoryEncoder or RDSE
    // Timestamp -> DateEncoder
}

FeatureEncoder::~FeatureEncoder() {
    // TODO: Clean up encoder
}

} // namespace htm_swat

