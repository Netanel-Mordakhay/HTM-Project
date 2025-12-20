#include "config.hpp"
#include <string>
#include <iostream>
#include <yaml-cpp/yaml.h>

namespace htm_swat {

// Load data config YAML (features definitions) using yaml-cpp
// Equivalent to Python: htm_source/utils/fs.py::load_config()
std::map<std::string, std::map<std::string, std::string>> loadDataConfig(const std::string& config_path) {
    std::map<std::string, std::map<std::string, std::string>> config;
    
    try {
        YAML::Node yaml_config = YAML::LoadFile(config_path);
        
        if (!yaml_config["features"]) {
            std::cerr << "ERROR: 'features' key not found in config file" << std::endl;
            return config;
        }
        
        YAML::Node features = yaml_config["features"];
        
        for (auto it = features.begin(); it != features.end(); ++it) {
            std::string feature_name = it->first.as<std::string>();
            YAML::Node feature_config = it->second;
            
            std::map<std::string, std::string> feature_map;
            
            // Extract all key-value pairs from the feature config
            for (auto feat_it = feature_config.begin(); feat_it != feature_config.end(); ++feat_it) {
                std::string key = feat_it->first.as<std::string>();
                
                // Handle different value types
                if (feat_it->second.IsScalar()) {
                    feature_map[key] = feat_it->second.as<std::string>();
                } else if (feat_it->second.IsSequence()) {
                    // For arrays like timeOfDay: [21, 9.49], convert to string
                    std::string value_str = "";
                    for (size_t i = 0; i < feat_it->second.size(); i++) {
                        if (i > 0) value_str += ",";
                        value_str += feat_it->second[i].as<std::string>();
                    }
                    feature_map[key] = value_str;
                }
            }
            
            config[feature_name] = feature_map;
        }
        
        std::cout << "  ✓ Parsed " << config.size() << " features from YAML" << std::endl;
        
    } catch (const YAML::Exception& e) {
        std::cerr << "ERROR: Failed to parse YAML file: " << config_path << std::endl;
        std::cerr << "  YAML error: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Exception while loading config: " << e.what() << std::endl;
    }
    
    return config;
}

// Load model config YAML (encoder/SP/TM parameters) using yaml-cpp
// Equivalent to Python: htm_source/utils/fs.py::load_config()
std::map<std::string, std::map<std::string, std::string>> loadModelConfig(const std::string& config_path) {
    std::map<std::string, std::map<std::string, std::string>> config;
    
    try {
        YAML::Node yaml_config = YAML::LoadFile(config_path);
        
        // Parse general section
        if (yaml_config["general"]) {
            std::map<std::string, std::string> general_map;
            YAML::Node general = yaml_config["general"];
            for (auto it = general.begin(); it != general.end(); ++it) {
                std::string key = it->first.as<std::string>();
                if (it->second.IsScalar()) {
                    general_map[key] = it->second.as<std::string>();
                }
            }
            config["general"] = general_map;
        }
        
        // Parse encoders section
        if (yaml_config["encoders"]) {
            std::map<std::string, std::string> encoders_map;
            YAML::Node encoders = yaml_config["encoders"];
            for (auto it = encoders.begin(); it != encoders.end(); ++it) {
                std::string key = it->first.as<std::string>();
                if (it->second.IsScalar()) {
                    encoders_map[key] = it->second.as<std::string>();
                }
            }
            config["encoders"] = encoders_map;
        }
        
        // Parse models section (SP and TM configs)
        if (yaml_config["models"]) {
            YAML::Node models = yaml_config["models"];
            
            // SP config
            if (models["sp"]) {
                std::map<std::string, std::string> sp_map;
                YAML::Node sp = models["sp"];
                for (auto it = sp.begin(); it != sp.end(); ++it) {
                    std::string key = it->first.as<std::string>();
                    if (it->second.IsScalar()) {
                        sp_map[key] = it->second.as<std::string>();
                    }
                }
                config["sp"] = sp_map;
            }
            
            // TM config
            if (models["tm"]) {
                std::map<std::string, std::string> tm_map;
                YAML::Node tm = models["tm"];
                for (auto it = tm.begin(); it != tm.end(); ++it) {
                    std::string key = it->first.as<std::string>();
                    if (it->second.IsScalar()) {
                        tm_map[key] = it->second.as<std::string>();
                    }
                }
                config["tm"] = tm_map;
            }
        }
        
        std::cout << "  ✓ Parsed model config from YAML" << std::endl;
        
    } catch (const YAML::Exception& e) {
        std::cerr << "ERROR: Failed to parse model YAML file: " << config_path << std::endl;
        std::cerr << "  YAML error: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: Exception while loading model config: " << e.what() << std::endl;
    }
    
    return config;
}

// Legacy function (kept for compatibility)
Config loadConfig(const std::string& config_path) {
    Config config;
    return config;
}

} // namespace htm_swat

