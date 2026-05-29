#include "data_streamer.hpp"
#include "utils.hpp"
#include <stdexcept>

namespace htm_swat {

using namespace htm;

DataStreamer::DataStreamer(const std::map<std::string, std::shared_ptr<RandomDistributedScalarEncoder>>& encoders)
    : encoders_(encoders),
      has_merge_plan_(false),
      feature_merge_mode_("u") {
    // Get encoder size from first encoder (all should have same size)
    if (!encoders_.empty()) {
        encoder_size_ = encoders_.begin()->second->parameters.size;
    } else {
        encoder_size_ = 0;
    }
}

DataStreamer::DataStreamer(const std::map<std::string, std::shared_ptr<RandomDistributedScalarEncoder>>& encoders,
                           const std::map<std::string, std::vector<std::string>>& merge_plan,
                           const std::string& feature_merge_mode)
    : encoders_(encoders),
      merge_plan_(merge_plan),
      feature_merge_mode_(feature_merge_mode),
      has_merge_plan_(true) {
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

std::map<std::string, SDR> DataStreamer::encodeRowMerged(const std::map<std::string, double>& row_data) const {
    if (!has_merge_plan_) {
        // Fall back to individual encoding
        return encodeRow(row_data);
    }
    
    // First, encode each feature individually
    std::map<std::string, SDR> temp_encoding;
    for (const auto& [feature_name, value] : row_data) {
        if (hasEncoder(feature_name)) {
            temp_encoding[feature_name] = encodeFeature(feature_name, value);
        }
    }
    
    // Then merge according to merge_plan
    std::map<std::string, SDR> final_encoding;
    
    for (const auto& [group_name, feature_list] : merge_plan_) {
        std::vector<SDR> sdrs_to_merge;
        
        for (const auto& feature_name : feature_list) {
            if (temp_encoding.find(feature_name) != temp_encoding.end()) {
                sdrs_to_merge.push_back(temp_encoding[feature_name]);
            }
        }
        
        if (!sdrs_to_merge.empty()) {
            if (sdrs_to_merge.size() == 1) {
                final_encoding[group_name] = sdrs_to_merge[0];
            } else {
                final_encoding[group_name] = mergeSDRs(sdrs_to_merge, feature_merge_mode_);
            }
        }
    }
    
    return final_encoding;
}

// Combines the 5th and 10th timestep rows into a single SDR for windowed processing.
// Uses feature_merge_mode_ (default "u" = union) to merge, which produces a slightly
// denser SDR (~4% active bits if inputs are ~2%) without changing SDR dimensions.
SDR DataStreamer::encodePair(const std::map<std::string, double>& row_t5,
                             const std::map<std::string, double>& row_t10) const {
    std::vector<SDR> sdrs;

    for (const auto& [feature_name, value] : row_t5) {
        if (hasEncoder(feature_name)) {
            sdrs.push_back(encodeFeature(feature_name, value));
        }    
    }
    for (const auto& [feature_name, value] : row_t10) {
        if (hasEncoder(feature_name)) {
            sdrs.push_back(encodeFeature(feature_name, value));
        }
    }

    if (sdrs.empty()) {
        return SDR({encoder_size_});
    }
    if (sdrs.size() == 1) {
        return sdrs[0];
    }
    return mergeSDRs(sdrs, feature_merge_mode_);
}

SDR DataStreamer::encodeWindow(const std::vector<std::map<std::string, double>>& rows) const {
    std::vector<SDR> sdrs;
    for (const auto& row : rows) {
        for (const auto& [feature_name, value] : row) {
            if (hasEncoder(feature_name)) {
                sdrs.push_back(encodeFeature(feature_name, value));
            }
        }
    }
    if (sdrs.empty()) return SDR({encoder_size_});
    if (sdrs.size() == 1) return sdrs[0];
    return mergeSDRs(sdrs, feature_merge_mode_);
}

std::vector<UInt> DataStreamer::getEncodingDims(const std::string& group_name) const {
    if (!has_merge_plan_ || merge_plan_.find(group_name) == merge_plan_.end()) {
        return {encoder_size_};
    }
    
    const auto& features = merge_plan_.at(group_name);
    if (feature_merge_mode_ == "u" || feature_merge_mode_ == "union") {
        // Union: same size as individual encoder
        return {encoder_size_};
    } else {
        // Concatenation: size = encoder_size * num_features
        return {static_cast<UInt>(encoder_size_ * features.size())};
    }
}

} // namespace htm_swat