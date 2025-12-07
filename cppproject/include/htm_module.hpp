#pragma once

// HTM Module - one SP + TM combo
// Matches the Python HTMModule class

#include <vector>
#include <string>

// Forward declare htm types
namespace htm {
    class SpatialPooler;
    class TemporalMemory;
    class SDR;
}

namespace htm_swat {

class HTMModule {
public:
    HTMModule(const std::vector<unsigned int>& input_dims,
              const std::vector<unsigned int>& column_dims,
              int cells_per_column,
              int seed,
              int learn_period = 1000,
              bool calc_anomaly = true);
    
    ~HTMModule();
    
    // TODO: Main forward pass - matches Python forward()
    // htm::SDR forward(const htm::SDR& input, bool learn);
    
    // TODO: Calculate anomaly score
    // float getAnomalyScore() const;
    
    // Control learning
    bool shouldLearn() const;
    void incrementIteration();
    
    // Get output dimensions
    const std::vector<unsigned int>& getOutputDims() const;
    
private:
    // TODO: Add SP and TM here
    // std::unique_ptr<htm::SpatialPooler> sp_;
    // std::unique_ptr<htm::TemporalMemory> tm_;
    
    // Learning state
    bool learning_;
    int iteration_;
    int learn_period_;
    bool calc_anomaly_;
    
    // Input/output sizes
    std::vector<unsigned int> input_dims_;
    std::vector<unsigned int> output_dims_;
    
    // TODO: Store SDRs for anomaly calc
    // htm::SDR prev_predictive_cells_;
};

} // namespace htm_swat

