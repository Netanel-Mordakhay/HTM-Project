#include "htm_module.hpp"

namespace htm_swat {

HTMModule::HTMModule(const std::vector<unsigned int>& input_dims,
                     const std::vector<unsigned int>& column_dims,
                     int cells_per_column,
                     int seed,
                     int learn_period,
                     bool calc_anomaly)
    : learning_(true),
      iteration_(0),
      learn_period_(learn_period),
      calc_anomaly_(calc_anomaly),
      input_dims_(input_dims),
      output_dims_(column_dims) {
    // TODO: Create SpatialPooler
    // TODO: Create TemporalMemory
    // TODO: Figure out output size
}

HTMModule::~HTMModule() {
    // TODO: Clean up SP and TM
}

bool HTMModule::shouldLearn() const {
    return learning_ && (iteration_ <= learn_period_);
}

void HTMModule::incrementIteration() {
    iteration_++;
}

const std::vector<unsigned int>& HTMModule::getOutputDims() const {
    return output_dims_;
}

} // namespace htm_swat

