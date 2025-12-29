#include "htm_pyramid.hpp"
#include "utils.hpp"
#include "config.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <iterator>
#include <htm/encoders/RandomDistributedScalarEncoder.hpp>

namespace htm_swat {

using namespace htm;

HTMPyramid::HTMPyramid(const std::vector<std::map<std::string, double>>& data,
                       const std::map<std::string, std::map<std::string, std::string>>& features_config,
                       const std::map<std::string, std::map<std::string, std::string>>& model_config,
                       const std::map<std::string, std::vector<std::string>>& feature_plan,
                       const std::map<std::string, std::vector<std::string>>& connections,
                       const std::map<int, std::vector<std::string>>& layer_dict,
                       UInt seed,
                       const std::string& feature_merge_mode,
                       const std::string& htm_merge_mode,
                       bool anomaly_score,
                       const std::vector<int>& max_pool,
                       int learn_period)
    : data_(data),
      features_config_(features_config),
      model_config_(model_config),
      feature_plan_(feature_plan),
      connections_(connections),
      layer_dict_(layer_dict),
      seed_(seed),
      feature_merge_mode_(feature_merge_mode),
      htm_merge_mode_(htm_merge_mode),
      calc_anomaly_(anomaly_score),
      max_pool_(max_pool),
      learn_period_(learn_period) {
    
    // Find head node (L3_1)
    for (const auto& [layer_idx, nodes] : layer_dict_) {
        for (const auto& node : nodes) {
            if (node.find("L3_") == 0) {
                head_node_ = node;
                break;
            }
        }
        if (!head_node_.empty()) break;
    }
    
    if (head_node_.empty()) {
        throw std::runtime_error("Head node (L3_1) not found in layer_dict");
    }
}

HTMPyramid::~HTMPyramid() {
    // Smart pointers handle cleanup
}

void HTMPyramid::build() {
    std::cout << "Building model... " << std::endl;
    
    // First, create encoders and DataStreamer
    // Extract encoder parameters
    UInt encoder_size = 2304;
    Real encoder_sparsity = 0.035;
    
    if (model_config_.count("encoders")) {
        const auto& encoders_cfg = model_config_.at("encoders");
        if (encoders_cfg.count("n")) {
            encoder_size = static_cast<UInt>(std::stoi(encoders_cfg.at("n")));
        }
        if (encoders_cfg.count("w")) {
            encoder_sparsity = std::stod(encoders_cfg.at("w"));
        }
    }
    
    const UInt active_bits = static_cast<UInt>(encoder_size * encoder_sparsity);
    
    // Create encoders for each feature
    std::map<std::string, std::shared_ptr<RandomDistributedScalarEncoder>> encoders;
    for (const auto& [feature_name, feature_config] : features_config_) {
        std::string feature_type = feature_config.count("type") ? feature_config.at("type") : "float";
        
        RDSE_Parameters params;
        params.size = encoder_size;
        params.activeBits = active_bits;
        params.seed = seed_;
        
        if (feature_type == "float") {
            if (feature_config.count("resolution")) {
                params.resolution = std::stod(feature_config.at("resolution"));
            } else {
                params.resolution = 0.1;
            }
        } else if (feature_type == "cat") {
            params.category = true;
        } else if (feature_type == "timestamp") {
            params.resolution = 1.0;  // TODO: DateEncoder
        }
        
        try {
            encoders[feature_name] = std::make_shared<RandomDistributedScalarEncoder>(params);
        } catch (const std::exception& e) {
            std::cerr << "Failed to create encoder for " << feature_name << ": " << e.what() << std::endl;
        }
    }
    
    // Create DataStreamer with merge plan
    data_streamer_ = std::make_unique<DataStreamer>(encoders, feature_plan_, feature_merge_mode_);
    
    // Build modules layer by layer
    buildPyramid();
    
    std::cout << "Done building model" << std::endl;
}

void HTMPyramid::buildPyramid() {
    int model_counter = 0;
    
    // Build modules for each layer
    for (const auto& [layer_idx, nodes] : layer_dict_) {
        for (const auto& node_name : nodes) {
        // Get input dimensions
        std::vector<UInt> input_dims;
        try {
            input_dims = getInputDims(node_name, layer_idx);
        } catch (const std::exception& e) {
            std::cerr << "Error getting input dims for " << node_name << ": " << e.what() << std::endl;
            continue;  // Skip this module
        }
            
            // Get layer-specific configs (simplified - use first value from arrays)
            std::map<std::string, std::map<std::string, std::string>> sp_cfg = model_config_;
            std::map<std::string, std::map<std::string, std::string>> tm_cfg = model_config_;
            
            // Get max_pool for this layer
            int max_pool_val = 1;
            if (layer_idx < static_cast<int>(max_pool_.size())) {
                max_pool_val = max_pool_[layer_idx];
            }
            
            // Create module
            model_counter++;
            UInt module_seed = seed_ * model_counter;
            
            auto module = std::make_unique<HTMModule>(
                input_dims,
                sp_cfg,
                tm_cfg,
                module_seed,
                learn_period_,
                calc_anomaly_,
                max_pool_val
            );
            
            // Initialize SP (lazy init)
            module->initSP();
            
            // Store module
            auto* module_ptr = module.get();
            modules_[node_name] = std::move(module);
            modules_by_layer_[layer_idx][node_name] = module_ptr;
        }
    }
    
    std::cout << "Built " << modules_.size() << " HTM modules across " << layer_dict_.size() << " layers" << std::endl;
}

std::vector<UInt> HTMPyramid::getInputDims(const std::string& node_name, int layer_idx) {
    if (layer_idx == 0) {
        // L0: get from DataStreamer encoding dimensions
        if (data_streamer_) {
            return data_streamer_->getEncodingDims(node_name);
        } else {
            // Fallback: use encoder size
            UInt encoder_size = 2304;
            if (model_config_.count("encoders") && model_config_.at("encoders").count("n")) {
                encoder_size = static_cast<UInt>(std::stoi(model_config_.at("encoders").at("n")));
            }
            // For union mode, size stays same; for concat, multiply by num features
            if (feature_plan_.find(node_name) != feature_plan_.end()) {
                size_t num_features = feature_plan_.at(node_name).size();
                if (feature_merge_mode_ == "c" || feature_merge_mode_ == "concat") {
                    return {static_cast<UInt>(encoder_size * num_features)};
                } else {
                    return {encoder_size};
                }
            }
            return {encoder_size};
        }
    } else {
        // L1+: get from predecessor modules' output dimensions
        // Find predecessors in connections
        std::vector<std::string> predecessors;
        if (connections_.find(node_name) != connections_.end()) {
            predecessors = connections_.at(node_name);
        }
        
        if (predecessors.empty()) {
            throw std::runtime_error("No predecessors found for " + node_name);
        }
        
        if (htm_merge_mode_ == "c" || htm_merge_mode_ == "concat") {
            // Concatenation: sum of all predecessor output dims
            UInt total_size = 0;
            for (const auto& pred : predecessors) {
                if (modules_.find(pred) == modules_.end()) {
                    throw std::runtime_error("Predecessor module not found: " + pred);
                }
                auto output_dims = modules_.at(pred)->getOutputDims();
                UInt size = 1;
                for (UInt dim : output_dims) {
                    size *= dim;
                }
                total_size += size;
            }
            return {total_size};
        } else {
            // Union: use first predecessor's output dims
            if (modules_.find(predecessors[0]) == modules_.end()) {
                throw std::runtime_error("Predecessor module not found: " + predecessors[0]);
            }
            return modules_.at(predecessors[0])->getOutputDims();
        }
    }
}

void HTMPyramid::run() {
    std::cout << "Running model on " << data_.size() << " rows..." << std::endl;
    scores_.clear();
    scores_.reserve(data_.size());
    
    for (size_t row_idx = 0; row_idx < data_.size(); row_idx++) {
        const auto& row = data_[row_idx];
        
        // Encode row using DataStreamer (get merged SDRs for L0)
        auto encoded_row = data_streamer_->encodeRowMerged(row);
        
        // Process through layers
        std::map<std::string, SDR> layer_results = encoded_row;
        
        for (const auto& [layer_idx, nodes] : layer_dict_) {
            if (layer_idx == 0) {
                // L0: already have encoded_row
                continue;
            }
            
            // Run this layer
            layer_results = runLayer(layer_results, layer_idx);
            
            // Merge for next layer (if not last layer)
            if (layer_idx < static_cast<int>(layer_dict_.size()) - 1) {
                // Get next layer nodes
                int next_layer = layer_idx + 1;
                if (layer_dict_.find(next_layer) != layer_dict_.end()) {
                    layer_results = mergeLayerResults(layer_results, layer_dict_.at(next_layer));
                }
            }
        }
        
        // Get final score from head
        if (modules_.find(head_node_) != modules_.end()) {
            float score = modules_.at(head_node_)->getAnomalyScore();
            scores_.push_back(score);
        } else {
            scores_.push_back(0.0f);
        }
        
        if ((row_idx + 1) % 1000 == 0) {
            std::cout << "  Processed " << (row_idx + 1) << " rows..." << std::endl;
        }
    }
    
    std::cout << "Done running model" << std::endl;
}

std::map<std::string, SDR> HTMPyramid::runLayer(const std::map<std::string, SDR>& inputs, int layer_idx) {
    std::map<std::string, SDR> results;
    
    if (modules_by_layer_.find(layer_idx) == modules_by_layer_.end()) {
        return results;
    }
    
    const auto& layer_modules = modules_by_layer_.at(layer_idx);
    
    for (const auto& [node_name, module] : layer_modules) {
        // Get input SDR for this node
        SDR input_sdr;
        
        if (layer_idx == 0) {
            // L0: input comes from encoded_row
            if (inputs.find(node_name) != inputs.end()) {
                input_sdr = inputs.at(node_name);
            } else {
                continue;  // Skip if no input
            }
        } else {
            // L1+: input comes from merged predecessor outputs
            if (inputs.find(node_name) != inputs.end()) {
                input_sdr = inputs.at(node_name);
            } else {
                continue;  // Skip if no input
            }
        }
        
        // Run forward pass
        SDR output = module->forward(input_sdr);
        results[node_name] = output;
    }
    
    return results;
}

std::map<std::string, SDR> HTMPyramid::mergeLayerResults(
    const std::map<std::string, SDR>& unmerged_results,
    const std::vector<std::string>& next_layer_nodes) {
    
    std::map<std::string, SDR> merged_results;
    
    for (const auto& node_name : next_layer_nodes) {
        // Get predecessors from connections
        if (connections_.find(node_name) == connections_.end()) {
            continue;
        }
        
        const auto& predecessors = connections_.at(node_name);
        std::vector<SDR> sdrs_to_merge;
        
        for (const auto& pred : predecessors) {
            if (unmerged_results.find(pred) != unmerged_results.end()) {
                sdrs_to_merge.push_back(unmerged_results.at(pred));
            }
        }
        
        if (!sdrs_to_merge.empty()) {
            if (sdrs_to_merge.size() == 1) {
                merged_results[node_name] = sdrs_to_merge[0];
            } else {
                merged_results[node_name] = mergeSDRs(sdrs_to_merge, htm_merge_mode_);
            }
        }
    }
    
    return merged_results;
}

std::map<std::string, std::vector<float>> HTMPyramid::getScoresMap() const {
    std::map<std::string, std::vector<float>> result;
    result["_head_"] = scores_;
    return result;
}

} // namespace htm_swat
