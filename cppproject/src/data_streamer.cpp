#include "data_streamer.hpp"
#include <stdexcept>

namespace htm_swat {

using namespace htm;

DataStreamer::DataStreamer(const std::map<std::string, std::shared_ptr<RandomDistributedScalarEncoder>>& encoders)
    : encoders_(encoders) {
    // Get encoder size from first encoder (all should have same size)
    if (!encoders_.empty()) {
        encoder_size_ = encoders_.begin()->second->parameters.size;
    } else {
        encoder_size_ = 0;
    }
}

SDR DataStreamer::encodeFeature(const std::string& feature_name, double value) const {
    auto it = encoders_.find(feature_name);
    if (it == encoders_.end()) {
        throw std::runtime_error("Encoder not found for feature: " + feature_name);
    }
    
    SDR output({encoder_size_});
    it->second->encode(value, output);
    return output;
}

SDR DataStreamer::encodeFeature(const std::string& feature_name, UInt value) const {
    // For categorical features, convert UInt to Real64
    return encodeFeature(feature_name, static_cast<double>(value));
}

std::map<std::string, SDR> DataStreamer::encodeRow(const std::map<std::string, double>& row_data) const {
    std::map<std::string, SDR> encoded_row;
    
    for (const auto& [feature_name, value] : row_data) {
        if (hasEncoder(feature_name)) {
            encoded_row[feature_name] = encodeFeature(feature_name, value);
        }
    }
    
    return encoded_row;
}

bool DataStreamer::hasEncoder(const std::string& feature_name) const {
    return encoders_.find(feature_name) != encoders_.end();
}

size_t DataStreamer::size() const {
    return encoders_.size();
}

} // namespace htm_swat

