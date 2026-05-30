#include "htm_pyramid.hpp"
#include "utils.hpp"
#include "config.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <iterator>
#include <future>   // for std::async, std::future
#include <vector>
#include <htm/encoders/RandomDistributedScalarEncoder.hpp>

namespace htm_swat {

using namespace htm;

HTMPyramid::HTMPyramid(RowStreamer& streamer,
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
    : row_streamer_(&streamer),
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
    // IMPORTANT: match Python seeding behaviour:
    //   params['seed'] = seed * EncoderFactory.get_encoder_idx()
    // where encoder_idx starts at 1 and increments per encoder.
    std::map<std::string, std::shared_ptr<RandomDistributedScalarEncoder>> encoders;
    int encoder_idx = 1;
    for (const auto& [feature_name, feature_config] : features_config_) {
        std::string feature_type = feature_config.count("type") ? feature_config.at("type") : "float";
        
        RDSE_Parameters params;
        params.size = encoder_size;
        params.activeBits = active_bits;
        // Give each encoder a different seed, like Python's Feature/EncoderFactory logic
        // (seed * encoder_index) to reduce collisions and match Python randomness.
        params.seed = seed_ * encoder_idx;
        
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
            encoder_idx++;
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
            // Union: use first predecessor's output dims (flattened to 1D for SP compatibility)
            if (modules_.find(predecessors[0]) == modules_.end()) {
                throw std::runtime_error("Predecessor module not found: " + predecessors[0]);
            }
            auto output_dims = modules_.at(predecessors[0])->getOutputDims();
            // Flatten to 1D: multiply all dimensions
            UInt size = 1;
            for (UInt dim : output_dims) {
                size *= dim;
            }
            return {size};
        }
    }
}

void HTMPyramid::run() {
    size_t total = row_streamer_->totalRows();
    std::cout << "Running model on " << total << " rows..." << std::endl;
    scores_.clear();
    scores_.reserve(total);
    labels_.clear();
    labels_.reserve(total);
 
    constexpr int WINDOW_SIZE = 16;
    size_t row_idx = 0;
    std::vector<std::map<std::string, double>> window_rows;
    std::vector<int> window_labels;
    window_rows.reserve(WINDOW_SIZE);
    window_labels.reserve(WINDOW_SIZE);

    while (row_streamer_->hasNext()) {
        auto row = row_streamer_->nextRow();
        window_rows.push_back(row);
        window_labels.push_back(row_streamer_->lastLabel());

        // Only process when we have a full window
        if ((int)window_rows.size() < WINDOW_SIZE) {
            row_idx++;
            continue;
        }

        // Push one label per raw row in the window
        for (int lbl : window_labels) labels_.push_back(lbl);

        // Encode all WINDOW_SIZE rows per feature group and merge
        std::map<std::string, SDR> encoded_row;
        for (const auto& [group_name, feature_list] : feature_plan_) {
            std::vector<std::map<std::string, double>> sub_rows;
            for (const auto& wr : window_rows) {
                std::map<std::string, double> sub;
                for (const auto& f : feature_list) {
                    if (wr.count(f)) sub[f] = wr.at(f);
                }
                sub_rows.push_back(sub);
            }
            encoded_row[group_name] = data_streamer_->encodeWindow(sub_rows);
        }

        window_rows.clear();
        window_labels.clear();
        
        // layer_outputs holds outputs of previous layer; start with encoder outputs
        std::map<std::string, SDR> layer_outputs = encoded_row;
 
        // Iterate layers in order (including L0) and run modules sequentially
        for (const auto& [layer_idx, nodes] : layer_dict_) {
            if (layer_idx == 0) {
                // L0 modules consume raw encodings directly
                if (row_idx == 0) {
                    std::cout << "  Debug L0 inputs (row 0):" << std::endl;
                    for (const auto& [name, sdr] : layer_outputs) {
                        std::cout << "    " << name << " size=" << sdr.size << " dims=";
                        const auto& dims = sdr.dimensions;
                        for (size_t i = 0; i < dims.size(); i++) {
                            std::cout << dims[i];
                            if (i + 1 < dims.size()) std::cout << "x";
                        }
                        std::cout << std::endl;
                    }
                }
                layer_outputs = runLayer(layer_outputs, layer_idx);
                if (row_idx == 0) {
                    std::cout << "  Debug L0 outputs (row 0):" << std::endl;
                    for (const auto& [name, sdr] : layer_outputs) {
                        std::cout << "    " << name << " size=" << sdr.size << " dims=";
                        const auto& dims = sdr.dimensions;
                        for (size_t i = 0; i < dims.size(); i++) {
                            std::cout << dims[i];
                            if (i + 1 < dims.size()) std::cout << "x";
                        }
                        std::cout << std::endl;
                    }
                }
            } else {
                // Merge predecessor outputs to build inputs for this layer
                std::map<std::string, SDR> merged_inputs = mergeLayerResults(layer_outputs, nodes);
                if (row_idx == 0) {
                    std::cout << "  Debug merged inputs for layer " << layer_idx << " (row 0):" << std::endl;
                    for (const auto& [name, sdr] : merged_inputs) {
                        std::cout << "    " << name << " size=" << sdr.size << " dims=";
                        const auto& dims = sdr.dimensions;
                        for (size_t i = 0; i < dims.size(); i++) {
                            std::cout << dims[i];
                            if (i + 1 < dims.size()) std::cout << "x";
                        }
                        std::cout << std::endl;
                    }
                }
                // Run this layer using merged inputs
                layer_outputs = runLayer(merged_inputs, layer_idx);
                if (row_idx == 0) {
                    std::cout << "  Debug layer " << layer_idx << " outputs (row 0):" << std::endl;
                    for (const auto& [name, sdr] : layer_outputs) {
                        std::cout << "    " << name << " size=" << sdr.size << " dims=";
                        const auto& dims = sdr.dimensions;
                        for (size_t i = 0; i < dims.size(); i++) {
                            std::cout << dims[i];
                            if (i + 1 < dims.size()) std::cout << "x";
                        }
                        std::cout << std::endl;
                    }
                }
            }
        }
        
        // Replicate score once per raw row in the window
        float score = (modules_.find(head_node_) != modules_.end())
            ? modules_.at(head_node_)->getAnomalyScore()
            : 0.0f;
        for (int i = 0; i < WINDOW_SIZE; i++) scores_.push_back(score);
        
        if ((row_idx + 1) % 1000 == 0) {
            std::cout << "  Processed " << (row_idx + 1) << " rows..." << std::endl;
        }
        row_idx++;
    }
 
    std::cout << "Done running model" << std::endl;
}
 

std::map<std::string, SDR> HTMPyramid::runLayer(const std::map<std::string, SDR>& inputs, int layer_idx) {
    std::map<std::string, SDR> results;
    
    if (modules_by_layer_.find(layer_idx) == modules_by_layer_.end()) {
        return results;
    }
    
    const auto& layer_modules = modules_by_layer_.at(layer_idx);

    // ------------------------------------------------------------------
    // Multithreaded execution of modules within a single layer
    //
    // Python uses multiprocessing (one worker per model key) in
    // ModelPyramid._run_layer. Here we mirror that idea in C++ with
    // threads: modules in the same layer are independent, so we can
    // safely run their forward passes in parallel.
    //
    // For simplicity and control we:
    //   - collect all node names in this layer,
    //   - split them into contiguous chunks,
    //   - run each chunk in a separate worker (up to 4 workers),
    //   - each worker builds a local map<string, SDR>,
    //   - finally we merge all local maps into 'results'.
    //
    // This keeps thread-safety simple (no shared mutation during
    // forward) while giving us parallelism similar to the Python hive.
    // ------------------------------------------------------------------

    // Collect node names for deterministic partitioning
    std::vector<std::string> node_names;
    node_names.reserve(layer_modules.size());
    for (const auto& kv : layer_modules) {
        node_names.push_back(kv.first);
    }

    if (node_names.empty()) {
        return results;
    }

    // Limit the number of worker threads per layer.
    // This can be tuned; 4 is a reasonable default comparable
    // to a small process pool in the Python implementation.
    const std::size_t max_threads = 4;
    const std::size_t num_threads =
        std::min<std::size_t>(max_threads, node_names.size());

    // Worker lambda processes a contiguous subset of node_names
    auto worker = [&](std::size_t thread_id, std::size_t start, std::size_t end)
        -> std::map<std::string, SDR> {
        // std::cout << "  [Thread " << thread_id << "] Started - Layer " << layer_idx 
        //           << ", processing " << (end - start) << " nodes (indices " 
        //           << start << "-" << (end - 1) << ")" << std::endl;
        
        std::map<std::string, SDR> local_results;

        for (std::size_t i = start; i < end; ++i) {
            const std::string& node_name = node_names[i];
            auto it_mod = layer_modules.find(node_name);
            if (it_mod == layer_modules.end()) {
                continue;
            }
            HTMModule* module = it_mod->second;

            // Get input SDR for this node
            SDR input_sdr;
            auto it_in = inputs.find(node_name);
            if (it_in == inputs.end()) {
                // No input for this node in this row; skip (same as before)
                continue;
            }
            input_sdr = it_in->second;

            // Guard against empty SDRs to catch wiring issues early
            if (input_sdr.size == 0) {
                throw std::runtime_error(
                    "Input SDR has size 0 for node " + node_name +
                    " at layer " + std::to_string(layer_idx));
            }

            // Run forward pass; any exception is propagated to the caller
            SDR output;
            try {
                output = module->forward(input_sdr);
            } catch (const std::exception& e) {
                throw std::runtime_error(
                    "Forward failed for node " + node_name + " (layer " +
                    std::to_string(layer_idx) + ") : " + e.what());
            }

            local_results[node_name] = output;
        }

        // std::cout << "  [Thread " << thread_id << "] Ended - Layer " << layer_idx 
        //           << ", processed " << local_results.size() << " nodes" << std::endl;
        
        return local_results;
    };

    // Launch workers with std::async
    std::vector<std::future<std::map<std::string, SDR>>> futures;
    futures.reserve(num_threads);

    const std::size_t total = node_names.size();
    const std::size_t chunk =
        (total + num_threads - 1) / num_threads;  // ceil division

    // std::cout << "  [Layer " << layer_idx << "] Launching " << num_threads 
    //           << " threads for " << total << " nodes" << std::endl;

    for (std::size_t t = 0; t < num_threads; ++t) {
        const std::size_t start = t * chunk;
        if (start >= total) {
            break;
        }
        const std::size_t end = std::min(start + chunk, total);

        // std::cout << "  [Layer " << layer_idx << "] Launching thread " << t 
        //           << " for nodes [" << start << ", " << end << ")" << std::endl;
        
        futures.emplace_back(
            std::async(std::launch::async, worker, t, start, end));
    }

    // Collect results from all workers
    // std::cout << "  [Layer " << layer_idx << "] Collecting results from " 
    //           << futures.size() << " threads..." << std::endl;
    
    for (std::size_t i = 0; i < futures.size(); ++i) {
        auto local = futures[i].get();  // may rethrow exceptions from worker
        results.insert(local.begin(), local.end());
        // std::cout << "  [Layer " << layer_idx << "] Collected results from thread " 
        //           << i << " (" << local.size() << " nodes)" << std::endl;
    }
    
    // std::cout << "  [Layer " << layer_idx << "] All threads completed, total results: " 
    //           << results.size() << std::endl;

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
