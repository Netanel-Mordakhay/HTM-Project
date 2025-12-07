#pragma once

#include <string>

// Configuration loading
// Eventually will load YAML files like the Python version

namespace htm_swat {

// Config struct with default values
// TODO: Add YAML parsing later
struct Config {
    // General settings
    int seed = 69;
    int learn_period = 1000;
    std::string htm_merge_mode = "u";
    
    // Encoder settings
    int encoder_size = 600;
    double encoder_sparsity = 0.0166;
    
    // Spatial Pooler settings
    int column_dimensions = 2048;
    double potential_radius = 1.0;
    double local_area_density = 0.02;
    // More SP settings will go here
    
    // Temporal Memory settings
    int cells_per_column = 4;
    int activation_threshold = 13;
    // More TM settings will go here
};

// TODO: Load config from YAML files
Config loadConfig(const std::string& config_path);

} // namespace htm_swat

