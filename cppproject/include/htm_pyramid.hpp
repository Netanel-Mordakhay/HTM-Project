#pragma once

// HTM Pyramid - hierarchical structure
// Matches Python's ModelPyramid

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <htm/types/Sdr.hpp>
#include "data_streamer.hpp"
#include "htm_module.hpp"
#include "config.hpp"

namespace htm_swat {

using namespace htm;

class HTMPyramid {
public:
    // Constructor matching Python ModelPyramid
    HTMPyramid(const std::vector<std::map<std::string, double>>& data,
               const std::map<std::string, std::map<std::string, std::string>>& features_config,
               const std::map<std::string, std::map<std::string, std::string>>& model_config,
               const std::map<std::string, std::vector<std::string>>& feature_plan,
               const std::map<std::string, std::vector<std::string>>& connections,
               const std::map<int, std::vector<std::string>>& layer_dict,
               UInt seed,
               const std::string& feature_merge_mode = "u",
               const std::string& htm_merge_mode = "u",
               bool anomaly_score = true,
               const std::vector<int>& max_pool = {1, 1, 1, 2},
               int learn_period = 5000);
    
    ~HTMPyramid();
    
    // Build the pyramid structure
    void build();
    
    // Run the pyramid on all data
    void run();
    
    // Get anomaly scores from head module
    std::vector<float> getScores() const { return scores_; }
    
    // Get scores as map (for compatibility)
    std::map<std::string, std::vector<float>> getScoresMap() const;
    
private:
    // Build pyramid structure
    void buildPyramid();
    
    // Run a single layer
    std::map<std::string, SDR> runLayer(const std::map<std::string, SDR>& inputs, int layer_idx);
    
    // Merge layer results for next layer
    std::map<std::string, SDR> mergeLayerResults(
        const std::map<std::string, SDR>& unmerged_results,
        const std::vector<std::string>& next_layer_nodes);
    
    // Get input dimensions for a module
    std::vector<UInt> getInputDims(const std::string& node_name, int layer_idx);
    
    // Data and configs
    std::vector<std::map<std::string, double>> data_;
    std::unique_ptr<DataStreamer> data_streamer_;
    std::map<std::string, std::map<std::string, std::string>> features_config_;
    std::map<std::string, std::map<std::string, std::string>> model_config_;
    
    // Network structure
    std::map<std::string, std::vector<std::string>> feature_plan_;
    std::map<std::string, std::vector<std::string>> connections_;
    std::map<int, std::vector<std::string>> layer_dict_;
    
    // All the HTM modules organized by layer
    std::map<int, std::map<std::string, HTMModule*>> modules_by_layer_;
    std::map<std::string, std::unique_ptr<HTMModule>> modules_;  // Flat map for easy access
    
    // Settings
    UInt seed_;
    int learn_period_;
    std::string feature_merge_mode_;
    std::string htm_merge_mode_;
    bool calc_anomaly_;
    std::vector<int> max_pool_;
    
    // Results
    std::vector<float> scores_;
    std::string head_node_;  // L3_1
};

} // namespace htm_swat

