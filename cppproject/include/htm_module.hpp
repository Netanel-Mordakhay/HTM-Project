#pragma once

// HTM Module - one SP + TM combo
// Matches the Python HTMModule class

#include <vector>
#include <string>
#include <memory>
#include <map>
#include <htm/types/Sdr.hpp>
#include <htm/algorithms/SpatialPooler.hpp>
#include <htm/algorithms/TemporalMemory.hpp>

namespace htm_swat {

using namespace htm;

class HTMModule {
public:
    // Constructor with config maps (from YAML)
    HTMModule(const std::vector<UInt>& input_dims,
              const std::map<std::string, std::map<std::string, std::string>>& sp_cfg,
              const std::map<std::string, std::map<std::string, std::string>>& tm_cfg,
              UInt seed,
              int learn_period = 5000,
              bool calc_anomaly = true,
              int max_pool = 1);
    
    ~HTMModule();
    
    // Main forward pass - matches Python forward()
    SDR forward(const SDR& input);
    
    // Get anomaly score for last forward pass
    float getAnomalyScore() const { return last_anomaly_score_; }
    
    // Control learning
    bool shouldLearn() const;
    void incrementIteration();
    
    // Get output dimensions
    const std::vector<UInt>& getOutputDims() const { return output_dims_; }
    
    // Get column dimensions
    const std::vector<UInt>& getColumnDims() const { return column_dims_; }
    
    // Initialize SP (lazy init support)
    void initSP();
    
private:
    std::unique_ptr<SpatialPooler> sp_;
    std::unique_ptr<TemporalMemory> tm_;
    
    // Configs stored for lazy init
    std::map<std::string, std::map<std::string, std::string>> sp_cfg_;
    UInt seed_;
    
    // Learning state
    bool learning_;
    int iteration_;
    int learn_period_;
    bool calc_anomaly_;
    bool sp_initialized_;
    
    // Input/output sizes
    std::vector<UInt> input_dims_;
    std::vector<UInt> column_dims_;
    std::vector<UInt> output_dims_;
    
    // Max pooling
    int max_pool_;
    
    // Anomaly tracking
    float last_anomaly_score_;
    SDR last_active_columns_;
    SDR last_predictive_columns_;
};

} // namespace htm_swat

