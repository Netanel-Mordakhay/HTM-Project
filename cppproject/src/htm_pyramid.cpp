#include "htm_pyramid.hpp"
#include "utils.hpp"
#include "config.hpp"
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <iterator>
#include <future>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <htm/encoders/RandomDistributedScalarEncoder.hpp>

namespace htm_swat {

using namespace htm;

// ---------------------------------------------------------------------------
// ThreadPool
//
// Creates N worker threads once at construction. Tasks are submitted via
// submit() and return a std::future for the result. Threads block on an
// internal queue when idle and wake immediately when work is available.
//
// This replaces std::async(std::launch::async, ...) in runLayer(). The
// std::async approach creates and destroys one OS thread per layer call
// (~12 create/join cycles per row). On QEMU ARM emulation each cycle costs
// ~1-5 ms, totalling 1,200-6,000 s of overhead across 100K rows.
// With a pool the threads are created once and each submission costs only a
// queue push + condition_variable notify (~1 µs).
// ---------------------------------------------------------------------------
class ThreadPool {
public:
    explicit ThreadPool(std::size_t num_threads) : stop_(false) {
        for (std::size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        cv_.wait(lock, [this]{ return stop_ || !tasks_.empty(); });
                        if (stop_ && tasks_.empty()) return;
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) w.join();
    }

    // Submit a callable with no arguments; returns std::future<ReturnType>.
    template<typename F>
    auto submit(F&& f) -> std::future<std::invoke_result_t<F>> {
        using R = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
        auto fut  = task->get_future();
        {
            std::unique_lock<std::mutex> lock(mutex_);
            tasks_.emplace([task]{ (*task)(); });
        }
        cv_.notify_one();
        return fut;
    }

    std::size_t size() const { return workers_.size(); }

private:
    std::vector<std::thread>           workers_;
    std::queue<std::function<void()>>  tasks_;
    std::mutex                         mutex_;
    std::condition_variable            cv_;
    bool                               stop_;
};

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
    
    // Create thread pool — one thread per available vCPU, capped at 4.
    // hardware_concurrency() returns 0 if the value is not computable.
    const std::size_t hw        = std::thread::hardware_concurrency();
    const std::size_t pool_size = (hw > 0) ? std::min(hw, std::size_t(4)) : 4;
    thread_pool_ = std::make_unique<ThreadPool>(pool_size);
    std::cout << "  Thread pool: " << pool_size << " persistent workers" << std::endl;

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

    size_t row_idx = 0;
    while (row_streamer_->hasNext()) {
        auto row = row_streamer_->nextRow();
        labels_.push_back(row_streamer_->lastLabel());
        
        // Encode row using DataStreamer (get merged SDRs for L0)
        auto encoded_row = data_streamer_->encodeRowMerged(row);

        if (row_idx == 0) {
            std::cout << "  Debug encoded inputs (row 0):" << std::endl;
            for (const auto& [name, sdr] : encoded_row) {
                std::cout << "    " << name << " size=" << sdr.size << " dims=";
                const auto& dims = sdr.dimensions;
                for (size_t i = 0; i < dims.size(); i++) {
                    std::cout << dims[i];
                    if (i + 1 < dims.size()) std::cout << "x";
                }
                std::cout << std::endl;
            }
        }
        
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
    // Modules in the same layer are independent per timestep, so their
    // forward passes can safely run in parallel. We partition the node
    // list into up to 4 contiguous chunks and submit each chunk as a
    // task to the persistent ThreadPool. The pool threads were created
    // once in build() and are reused here on every call, avoiding the
    // ~1-5 ms OS thread create/join cost that std::async would incur.
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
            thread_pool_->submit([&worker, t, start, end]{
                return worker(t, start, end);
            }));
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
