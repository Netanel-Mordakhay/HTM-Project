#pragma once

#include <string>
#include <map>

// Configuration loading
// Loads YAML files like the Python version (htm_source/utils/fs.py)

namespace htm_swat {

// Config struct with default values (legacy, kept for compatibility)
struct Config {
    // General settings
    int seed = 69;
    int learn_period = 100;
    std::string htm_merge_mode = "u";
    
    // Encoder settings
    int encoder_size = 600;
    double encoder_sparsity = 0.0166;
    
    // Spatial Pooler settings
    int column_dimensions = 2048;
    double potential_radius = 1.0;
    double local_area_density = 0.02;
    
    // Temporal Memory settings
    int cells_per_column = 4;
    int activation_threshold = 13;
};

// Load data config YAML (features definitions)
// Equivalent to Python: load_config(config_path_data) -> data_cfg['features']
// Returns: map<feature_name, map<key, value>>
std::map<std::string, std::map<std::string, std::string>> loadDataConfig(const std::string& config_path);

// Load model config YAML (encoder/SP/TM parameters)
// Equivalent to Python: load_config(config_path_model) -> run_cfg
// Returns: map<section, map<key, value>> (e.g., config["encoders"]["n"])
std::map<std::string, std::map<std::string, std::string>> loadModelConfig(const std::string& config_path);

// Legacy function (kept for compatibility)
Config loadConfig(const std::string& config_path);

} // namespace htm_swat

