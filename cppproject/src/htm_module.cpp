#include "htm_module.hpp"
#include "utils.hpp"
#include <stdexcept>
#include <iostream>
#include <algorithm>

namespace htm_swat {

using namespace htm;

HTMModule::HTMModule(const std::vector<UInt>& input_dims,
                     const std::map<std::string, std::map<std::string, std::string>>& sp_cfg,
                     const std::map<std::string, std::map<std::string, std::string>>& tm_cfg,
                     UInt seed,
                     int learn_period,
                     bool calc_anomaly,
                     int max_pool)
    : learning_(true),
      iteration_(0),
      learn_period_(learn_period),
      calc_anomaly_(calc_anomaly),
      input_dims_(input_dims),
      sp_initialized_(false),
      max_pool_(max_pool),
      last_anomaly_score_(0.0f),
      sp_cfg_(sp_cfg),
      seed_(seed) {
    
    // Extract SP config
    if (sp_cfg.find("sp") == sp_cfg.end()) {
        throw std::runtime_error("SP config not found");
    }
    const auto& sp_params = sp_cfg.at("sp");
    
    // Extract column dimensions
    if (sp_params.find("columnDimensions") == sp_params.end()) {
        throw std::runtime_error("columnDimensions not found in SP config");
    }
    UInt column_dim = static_cast<UInt>(std::stoi(sp_params.at("columnDimensions")));
    column_dims_ = {column_dim};
    
    // Extract TM config
    if (tm_cfg.find("tm") == tm_cfg.end()) {
        throw std::runtime_error("TM config not found");
    }
    const auto& tm_params = tm_cfg.at("tm");
    
    // Extract cells per column
    UInt cells_per_column = 4;  // Default
    if (tm_params.find("cellsPerColumn") != tm_params.end()) {
        cells_per_column = static_cast<UInt>(std::stoi(tm_params.at("cellsPerColumn")));
    }
    
    // Output dims = column_dims * cells_per_column
    output_dims_ = {column_dim, cells_per_column};
    
    // SP will be initialized lazily via initSP()
    // TM will be created now
    
    // Create TemporalMemory
    UInt activation_threshold = 13;  // Default
    if (tm_params.find("activationThreshold") != tm_params.end()) {
        // Handle array - take first value
        std::string act_thresh_str = tm_params.at("activationThreshold");
        // Remove brackets if present
        act_thresh_str.erase(std::remove(act_thresh_str.begin(), act_thresh_str.end(), '['), act_thresh_str.end());
        act_thresh_str.erase(std::remove(act_thresh_str.begin(), act_thresh_str.end(), ']'), act_thresh_str.end());
        // Get first number
        size_t comma_pos = act_thresh_str.find(',');
        if (comma_pos != std::string::npos) {
            act_thresh_str = act_thresh_str.substr(0, comma_pos);
        }
        activation_threshold = static_cast<UInt>(std::stoi(act_thresh_str));
    }
    
    Real initial_perm = 0.21;
    if (tm_params.find("initialPerm") != tm_params.end()) {
        initial_perm = std::stod(tm_params.at("initialPerm"));
    }
    
    Real permanence_connected = 0.6;
    if (tm_params.find("permanenceConnected") != tm_params.end()) {
        permanence_connected = std::stod(tm_params.at("permanenceConnected"));
    }
    
    UInt min_threshold = 16;
    if (tm_params.find("minThreshold") != tm_params.end()) {
        min_threshold = static_cast<UInt>(std::stoi(tm_params.at("minThreshold")));
    }
    
    UInt max_new_synapse_count = 32;
    if (tm_params.find("newSynapseCount") != tm_params.end()) {
        std::string new_syn_str = tm_params.at("newSynapseCount");
        new_syn_str.erase(std::remove(new_syn_str.begin(), new_syn_str.end(), '['), new_syn_str.end());
        new_syn_str.erase(std::remove(new_syn_str.begin(), new_syn_str.end(), ']'), new_syn_str.end());
        size_t comma_pos = new_syn_str.find(',');
        if (comma_pos != std::string::npos) {
            new_syn_str = new_syn_str.substr(0, comma_pos);
        }
        max_new_synapse_count = static_cast<UInt>(std::stoi(new_syn_str));
    }
    
    Real permanence_inc = 0.1;
    if (tm_params.find("permanenceInc") != tm_params.end()) {
        std::string perm_inc_str = tm_params.at("permanenceInc");
        perm_inc_str.erase(std::remove(perm_inc_str.begin(), perm_inc_str.end(), '['), perm_inc_str.end());
        perm_inc_str.erase(std::remove(perm_inc_str.begin(), perm_inc_str.end(), ']'), perm_inc_str.end());
        size_t comma_pos = perm_inc_str.find(',');
        if (comma_pos != std::string::npos) {
            perm_inc_str = perm_inc_str.substr(0, comma_pos);
        }
        permanence_inc = std::stod(perm_inc_str);
    }
    
    Real permanence_dec = 0.1;
    if (tm_params.find("permanenceDec") != tm_params.end()) {
        std::string perm_dec_str = tm_params.at("permanenceDec");
        perm_dec_str.erase(std::remove(perm_dec_str.begin(), perm_dec_str.end(), '['), perm_dec_str.end());
        perm_dec_str.erase(std::remove(perm_dec_str.begin(), perm_dec_str.end(), ']'), perm_dec_str.end());
        size_t comma_pos = perm_dec_str.find(',');
        if (comma_pos != std::string::npos) {
            perm_dec_str = perm_dec_str.substr(0, comma_pos);
        }
        permanence_dec = std::stod(perm_dec_str);
    }
    
    Real predicted_segment_decrement = 0.0;
    if (tm_params.find("predictedSegmentDecrement") != tm_params.end()) {
        std::string pred_dec_str = tm_params.at("predictedSegmentDecrement");
        pred_dec_str.erase(std::remove(pred_dec_str.begin(), pred_dec_str.end(), '['), pred_dec_str.end());
        pred_dec_str.erase(std::remove(pred_dec_str.begin(), pred_dec_str.end(), ']'), pred_dec_str.end());
        size_t comma_pos = pred_dec_str.find(',');
        if (comma_pos != std::string::npos) {
            pred_dec_str = pred_dec_str.substr(0, comma_pos);
        }
        predicted_segment_decrement = std::stod(pred_dec_str);
    }
    
    UInt max_segments_per_cell = 128;
    if (tm_params.find("maxSegmentsPerCell") != tm_params.end()) {
        std::string max_seg_str = tm_params.at("maxSegmentsPerCell");
        max_seg_str.erase(std::remove(max_seg_str.begin(), max_seg_str.end(), '['), max_seg_str.end());
        max_seg_str.erase(std::remove(max_seg_str.begin(), max_seg_str.end(), ']'), max_seg_str.end());
        size_t comma_pos = max_seg_str.find(',');
        if (comma_pos != std::string::npos) {
            max_seg_str = max_seg_str.substr(0, comma_pos);
        }
        max_segments_per_cell = static_cast<UInt>(std::stoi(max_seg_str));
    }
    
    UInt max_synapses_per_segment = 255;
    if (tm_params.find("maxSynapsesPerSegment") != tm_params.end()) {
        std::string max_syn_str = tm_params.at("maxSynapsesPerSegment");
        max_syn_str.erase(std::remove(max_syn_str.begin(), max_syn_str.end(), '['), max_syn_str.end());
        max_syn_str.erase(std::remove(max_syn_str.begin(), max_syn_str.end(), ']'), max_syn_str.end());
        size_t comma_pos = max_syn_str.find(',');
        if (comma_pos != std::string::npos) {
            max_syn_str = max_syn_str.substr(0, comma_pos);
        }
        max_synapses_per_segment = static_cast<UInt>(std::stoi(max_syn_str));
    }
    
    // Create TM - using HTM core C++ API
    // Note: Check HTM core documentation for exact constructor signature
    // This is a placeholder - adjust based on actual HTM core C++ API
    try {
        tm_ = std::make_unique<TemporalMemory>(
            column_dims_,
            cells_per_column,
            activation_threshold,
            initial_perm,
            permanence_connected,
            min_threshold,
            max_new_synapse_count,
            permanence_inc,
            permanence_dec,
            predicted_segment_decrement,
            max_segments_per_cell,
            max_synapses_per_segment,
            seed,
            false  // checkInputs
        );
    } catch (const std::exception& e) {
        std::cerr << "Error creating TemporalMemory: " << e.what() << std::endl;
        throw;
    }
}

HTMModule::~HTMModule() {
    // Smart pointers handle cleanup
}

void HTMModule::initSP() {
    if (sp_initialized_) {
        return;
    }
    
    const auto& sp_params = sp_cfg_.at("sp");
    
    // Extract SP parameters
    Real potential_pct = 0.2;
    if (sp_params.find("potentialPct") != sp_params.end()) {
        potential_pct = std::stod(sp_params.at("potentialPct"));
    }
    
    Real potential_radius = 1.0;
    if (sp_params.find("potentialRadius") != sp_params.end()) {
        potential_radius = std::stod(sp_params.at("potentialRadius"));
    }
    
    Real local_area_density = 0.02;
    if (sp_params.find("localAreaDensity") != sp_params.end()) {
        local_area_density = std::stod(sp_params.at("localAreaDensity"));
    }
    
    bool global_inhibition = true;
    if (sp_params.find("globalInhibition") != sp_params.end()) {
        std::string global_inh = sp_params.at("globalInhibition");
        global_inhibition = (global_inh == "yes" || global_inh == "true" || global_inh == "1");
    }
    
    UInt stimulus_threshold = 0;
    if (sp_params.find("stimulusThreshold") != sp_params.end()) {
        std::string stim_thresh_str = sp_params.at("stimulusThreshold");
        stim_thresh_str.erase(std::remove(stim_thresh_str.begin(), stim_thresh_str.end(), '['), stim_thresh_str.end());
        stim_thresh_str.erase(std::remove(stim_thresh_str.begin(), stim_thresh_str.end(), ']'), stim_thresh_str.end());
        size_t comma_pos = stim_thresh_str.find(',');
        if (comma_pos != std::string::npos) {
            stim_thresh_str = stim_thresh_str.substr(0, comma_pos);
        }
        stimulus_threshold = static_cast<UInt>(std::stoi(stim_thresh_str));
    }
    
    Real syn_perm_connected = 0.5;
    if (sp_params.find("synPermConnected") != sp_params.end()) {
        syn_perm_connected = std::stod(sp_params.at("synPermConnected"));
    }
    
    Real syn_perm_active_inc = 0.01;
    if (sp_params.find("synPermActiveInc") != sp_params.end()) {
        std::string syn_inc_str = sp_params.at("synPermActiveInc");
        syn_inc_str.erase(std::remove(syn_inc_str.begin(), syn_inc_str.end(), '['), syn_inc_str.end());
        syn_inc_str.erase(std::remove(syn_inc_str.begin(), syn_inc_str.end(), ']'), syn_inc_str.end());
        size_t comma_pos = syn_inc_str.find(',');
        if (comma_pos != std::string::npos) {
            syn_inc_str = syn_inc_str.substr(0, comma_pos);
        }
        syn_perm_active_inc = std::stod(syn_inc_str);
    }
    
    Real syn_perm_inactive_dec = 0.01;
    if (sp_params.find("synPermInactiveDec") != sp_params.end()) {
        std::string syn_dec_str = sp_params.at("synPermInactiveDec");
        syn_dec_str.erase(std::remove(syn_dec_str.begin(), syn_dec_str.end(), '['), syn_dec_str.end());
        syn_dec_str.erase(std::remove(syn_dec_str.begin(), syn_dec_str.end(), ']'), syn_dec_str.end());
        size_t comma_pos = syn_dec_str.find(',');
        if (comma_pos != std::string::npos) {
            syn_dec_str = syn_dec_str.substr(0, comma_pos);
        }
        syn_perm_inactive_dec = std::stod(syn_dec_str);
    }
    
    bool wrap_around = true;
    if (sp_params.find("wrapAround") != sp_params.end()) {
        std::string wrap = sp_params.at("wrapAround");
        wrap_around = (wrap == "yes" || wrap == "true" || wrap == "1");
    }
    
    // Create SP - using HTM core C++ API (positional parameters)
    // Note: HTM core C++ API uses positional parameters, not named
    // Adjust constructor call based on actual HTM core C++ API
    try {
        sp_ = std::make_unique<SpatialPooler>(
            input_dims_,
            column_dims_,
            potential_radius,
            potential_pct,
            global_inhibition,
            local_area_density,
            stimulus_threshold,
            syn_perm_active_inc,
            syn_perm_inactive_dec,
            syn_perm_connected,
            0.21,  // synPermInitial
            0.001, // minPctOverlapDutyCycle
            1000,  // dutyCyclePeriod
            0.0,   // boostStrength
            seed_,
            0,     // spVerbosity
            wrap_around
        );
    } catch (const std::exception& e) {
        std::cerr << "Error creating SpatialPooler: " << e.what() << std::endl;
        throw;
    }
    
    sp_initialized_ = true;
}

SDR HTMModule::forward(const SDR& input) {
    // Update learning state
    learning_ = (iteration_ <= learn_period_);
    
    // Initialize SP if needed
    if (!sp_initialized_) {
        initSP();
    }
    
    // SPATIAL POOLER
    SDR active_columns(column_dims_);
    if (sp_) {
        sp_->compute(input, learning_, active_columns);
    } else {
        // No SP, pass through
        active_columns = input;
    }
    
    last_active_columns_ = active_columns;
    
    // TEMPORAL MEMORY
    // Activate dendrites (get predictions)
    tm_->activateDendrites(learning_);
    SDR predictive_cells = tm_->getPredictiveCells();
    SDR predictive_columns = tm_->cellsToColumns(predictive_cells);
    
    last_predictive_columns_ = predictive_columns;
    
    // Calculate anomaly score
    if (calc_anomaly_) {
        last_anomaly_score_ = calcAnomalyScore(active_columns, predictive_columns);
    }
    
    // Activate cells
    tm_->activateCells(active_columns, learning_);
    SDR active_cells = tm_->getActiveCells();
    
    // Apply max pooling if needed
    if (max_pool_ > 1) {
        // Simple max pooling: keep every max_pool-th bit
        auto sparse = active_cells.getSparse();
        std::vector<UInt> pooled_bits;
        for (size_t i = 0; i < sparse.size(); i += max_pool_) {
            pooled_bits.push_back(sparse[i]);
        }
        SDR pooled(output_dims_);
        pooled.setSparse(pooled_bits);
        active_cells = pooled;
    }
    
    incrementIteration();
    return active_cells;
}

bool HTMModule::shouldLearn() const {
    return learning_ && (iteration_ <= learn_period_);
}

void HTMModule::incrementIteration() {
    iteration_++;
}

} // namespace htm_swat

