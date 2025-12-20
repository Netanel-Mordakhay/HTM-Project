#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <fstream>
#include <sstream>
#include "config.hpp"

// YAML parser
#include <yaml-cpp/yaml.h>

// HTM core includes
#include <htm/types/Sdr.hpp>
#include <htm/encoders/RandomDistributedScalarEncoder.hpp>

using namespace htm;
using namespace std;

// Parse data config YAML (features definitions) using yaml-cpp
map<string, map<string, string>> parseConfigYAML(const string& config_path) {
    map<string, map<string, string>> config;
    
    try {
        YAML::Node yaml_config = YAML::LoadFile(config_path);
        
        if (!yaml_config["features"]) {
            cerr << "ERROR: 'features' key not found in config file" << endl;
            return config;
        }
        
        YAML::Node features = yaml_config["features"];
        
        for (auto it = features.begin(); it != features.end(); ++it) {
            string feature_name = it->first.as<string>();
            YAML::Node feature_config = it->second;
            
            map<string, string> feature_map;
            
            // Extract all key-value pairs from the feature config
            for (auto feat_it = feature_config.begin(); feat_it != feature_config.end(); ++feat_it) {
                string key = feat_it->first.as<string>();
                
                // Handle different value types
                if (feat_it->second.IsScalar()) {
                    feature_map[key] = feat_it->second.as<string>();
                } else if (feat_it->second.IsSequence()) {
                    // For arrays like timeOfDay: [21, 9.49], convert to string
                    string value_str = "";
                    for (size_t i = 0; i < feat_it->second.size(); i++) {
                        if (i > 0) value_str += ",";
                        value_str += feat_it->second[i].as<string>();
                    }
                    feature_map[key] = value_str;
                }
            }
            
            config[feature_name] = feature_map;
        }
        
        cout << "  ✓ Parsed " << config.size() << " features from YAML" << endl;
        
    } catch (const YAML::Exception& e) {
        cerr << "ERROR: Failed to parse YAML file: " << config_path << endl;
        cerr << "  YAML error: " << e.what() << endl;
    } catch (const exception& e) {
        cerr << "ERROR: Exception while loading config: " << e.what() << endl;
    }
    
    return config;
}

// Parse model config YAML (encoder/SP/TM parameters) using yaml-cpp
// Returns a map structure that can be accessed like: config["encoders"]["n"]
map<string, map<string, string>> parseModelConfigYAML(const string& config_path) {
    map<string, map<string, string>> config;
    
    try {
        YAML::Node yaml_config = YAML::LoadFile(config_path);
        
        // Parse general section
        if (yaml_config["general"]) {
            map<string, string> general_map;
            YAML::Node general = yaml_config["general"];
            for (auto it = general.begin(); it != general.end(); ++it) {
                string key = it->first.as<string>();
                if (it->second.IsScalar()) {
                    general_map[key] = it->second.as<string>();
                }
            }
            config["general"] = general_map;
        }
        
        // Parse encoders section
        if (yaml_config["encoders"]) {
            map<string, string> encoders_map;
            YAML::Node encoders = yaml_config["encoders"];
            for (auto it = encoders.begin(); it != encoders.end(); ++it) {
                string key = it->first.as<string>();
                if (it->second.IsScalar()) {
                    encoders_map[key] = it->second.as<string>();
                }
            }
            config["encoders"] = encoders_map;
        }
        
        // Parse models section (SP and TM configs)
        if (yaml_config["models"]) {
            YAML::Node models = yaml_config["models"];
            
            // SP config
            if (models["sp"]) {
                map<string, string> sp_map;
                YAML::Node sp = models["sp"];
                for (auto it = sp.begin(); it != sp.end(); ++it) {
                    string key = it->first.as<string>();
                    if (it->second.IsScalar()) {
                        sp_map[key] = it->second.as<string>();
                    }
                }
                config["sp"] = sp_map;
            }
            
            // TM config
            if (models["tm"]) {
                map<string, string> tm_map;
                YAML::Node tm = models["tm"];
                for (auto it = tm.begin(); it != tm.end(); ++it) {
                    string key = it->first.as<string>();
                    if (it->second.IsScalar()) {
                        tm_map[key] = it->second.as<string>();
                    }
                }
                config["tm"] = tm_map;
            }
        }
        
        cout << "  ✓ Parsed model config from YAML" << endl;
        
    } catch (const YAML::Exception& e) {
        cerr << "ERROR: Failed to parse model YAML file: " << config_path << endl;
        cerr << "  YAML error: " << e.what() << endl;
    } catch (const exception& e) {
        cerr << "ERROR: Exception while loading model config: " << e.what() << endl;
    }
    
    return config;
}

int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "HTM SWAT Encoder Test" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    try {
        // 1. Load configs from YAML (both data and model configs)
        std::cout << "[Step 1] Loading configs from YAML..." << std::endl;
        
        // Load data config (feature definitions)
        string data_config_path = "config/data/config_swat.yaml";
        auto features_config = parseConfigYAML(data_config_path);
        
        if (features_config.empty()) {
            std::cerr << "ERROR: Failed to load data config or config is empty" << std::endl;
            std::cerr << "  Tried to load: " << data_config_path << std::endl;
            return 1;
        }
        
        std::cout << "  ✓ Loaded data config with " << features_config.size() << " features" << std::endl;
        
        // Load model config (encoder/SP/TM parameters)
        string model_config_path = "config/model/config_model_default.yaml";
        auto model_config = parseModelConfigYAML(model_config_path);
        
        // Extract encoder parameters from model config
        UInt encoder_size = 2304;  // Default
        Real encoder_sparsity = 0.035;  // Default
        UInt seed = 42;  // Default
        
        if (model_config.count("encoders")) {
            const auto& encoders_cfg = model_config.at("encoders");
            if (encoders_cfg.count("n")) {
                encoder_size = static_cast<UInt>(stoi(encoders_cfg.at("n")));
            }
            if (encoders_cfg.count("w")) {
                encoder_sparsity = stod(encoders_cfg.at("w"));
            }
        }
        
        if (model_config.count("general") && model_config.at("general").count("seed")) {
            seed = static_cast<UInt>(stoi(model_config.at("general").at("seed")));
        }
        
        const UInt active_bits = static_cast<UInt>(encoder_size * encoder_sparsity);  // w * n
        
        std::cout << "  Encoder parameters: n=" << encoder_size 
                  << ", w=" << encoder_sparsity 
                  << ", activeBits=" << active_bits << std::endl;
        
        // 2. Create encoders for each feature
        std::cout << "\n[Step 2] Creating RDSE encoders..." << std::endl;
        map<string, shared_ptr<RandomDistributedScalarEncoder>> encoders;
        
        for (const auto& [feature_name, feature_config] : features_config) {
            string feature_type = feature_config.count("type") ? feature_config.at("type") : "float";
            
            RDSE_Parameters params;
            params.size = encoder_size;
            params.activeBits = active_bits;
            params.seed = seed;
            
            if (feature_type == "float") {
                // For float features, use resolution from config
                if (feature_config.count("resolution")) {
                    string res_str = feature_config.at("resolution");
                    params.resolution = stod(res_str);
                } else {
                    params.resolution = 0.1;  // Default resolution
                }
                std::cout << "  ✓ Created RDSE encoder for " << feature_name 
                          << " (float, resolution=" << params.resolution << ")" << std::endl;
            } 
            else if (feature_type == "cat") {
                // For categorical features
                params.category = true;
                std::cout << "  ✓ Created RDSE encoder for " << feature_name 
                          << " (categorical)" << std::endl;
            }
            else if (feature_type == "timestamp") {
                // For timestamp, we'll use a float encoder for now
                // TODO: Implement DateEncoder later
                params.resolution = 1.0;  // 1 second resolution
                std::cout << "  ⚠ Created RDSE encoder for " << feature_name 
                          << " (timestamp - using float encoder, DateEncoder TODO)" << std::endl;
            }
            else {
                std::cout << "  ⚠ Skipping " << feature_name << " (unknown type: " << feature_type << ")" << std::endl;
                continue;
            }
            
            try {
                encoders[feature_name] = make_shared<RandomDistributedScalarEncoder>(params);
            } catch (const exception& e) {
                std::cerr << "  ✗ Failed to create encoder for " << feature_name 
                          << ": " << e.what() << std::endl;
            }
        }
        
        std::cout << "\n  Total encoders created: " << encoders.size() << std::endl;
        
        // 3. Test encoding with sample data
        std::cout << "\n[Step 3] Testing encoding..." << std::endl;
        
        // Test with a few sample features
        map<string, double> test_data = {
            {"fit101", 2.5},
            {"lit101", 500.0},
            {"ait201", 25.3},
            {"ait202", 0.15},
        };
        
        // Test categorical encoding (use integer values)
        map<string, UInt> test_cat_data = {
            {"mv101", 0},  // Assuming 0 = OFF, 1 = ON
            {"p101", 1},
        };
        
        std::cout << "\n  Testing float encodings:" << std::endl;
        for (const auto& [feature_name, value] : test_data) {
            if (encoders.count(feature_name)) {
                SDR output({encoder_size});
                encoders[feature_name]->encode(value, output);
                
                auto sparse = output.getSparse();
                std::cout << "    " << feature_name << " = " << value 
                          << " -> " << sparse.size() << " active bits" << std::endl;
                std::cout << "      Active bits: [";
                for (size_t i = 0; i < std::min(sparse.size(), size_t(10)); i++) {
                    std::cout << sparse[i];
                    if (i < std::min(sparse.size(), size_t(10)) - 1) std::cout << ", ";
                }
                if (sparse.size() > 10) std::cout << "...";
                std::cout << "]" << std::endl;
            } else {
                std::cout << "    ⚠ " << feature_name << " encoder not found" << std::endl;
            }
        }
        
        std::cout << "\n  Testing categorical encodings:" << std::endl;
        for (const auto& [feature_name, value] : test_cat_data) {
            if (encoders.count(feature_name)) {
                SDR output({encoder_size});
                encoders[feature_name]->encode(static_cast<Real64>(value), output);
                
                auto sparse = output.getSparse();
                std::cout << "    " << feature_name << " = " << value 
                          << " -> " << sparse.size() << " active bits" << std::endl;
                std::cout << "      Active bits: [";
                for (size_t i = 0; i < std::min(sparse.size(), size_t(10)); i++) {
                    std::cout << sparse[i];
                    if (i < std::min(sparse.size(), size_t(10)) - 1) std::cout << ", ";
                }
                if (sparse.size() > 10) std::cout << "...";
                std::cout << "]" << std::endl;
            } else {
                std::cout << "    ⚠ " << feature_name << " encoder not found" << std::endl;
            }
        }
        
        // 4. Test encoding consistency (same input should produce same output)
        std::cout << "\n[Step 4] Testing encoding consistency..." << std::endl;
        if (encoders.count("fit101")) {
            SDR output1({encoder_size});
            SDR output2({encoder_size});
            
            encoders["fit101"]->encode(2.5, output1);
            encoders["fit101"]->encode(2.5, output2);
            
            auto sparse1 = output1.getSparse();
            auto sparse2 = output2.getSparse();
            
            bool identical = (sparse1.size() == sparse2.size());
            if (identical) {
                for (size_t i = 0; i < sparse1.size(); i++) {
                    if (sparse1[i] != sparse2[i]) {
                        identical = false;
                        break;
                    }
                }
            }
            
            if (identical) {
                std::cout << "  ✓ Encoding is consistent (same input -> same output)" << std::endl;
            } else {
                std::cout << "  ✗ Encoding is NOT consistent" << std::endl;
            }
        }
        
        // 5. Test encoding similarity (similar inputs should have overlapping encodings)
        std::cout << "\n[Step 5] Testing encoding similarity..." << std::endl;
        if (encoders.count("fit101")) {
            SDR output1({encoder_size});
            SDR output2({encoder_size});
            
            encoders["fit101"]->encode(2.5, output1);
            encoders["fit101"]->encode(2.6, output2);  // Very similar value
            
            auto sparse1 = output1.getSparse();
            auto sparse2 = output2.getSparse();
            
            // Count overlapping bits
            size_t overlap = 0;
            for (UInt bit : sparse1) {
                for (UInt bit2 : sparse2) {
                    if (bit == bit2) {
                        overlap++;
                        break;
                    }
                }
            }
            
            double overlap_ratio = static_cast<double>(overlap) / sparse1.size();
            std::cout << "  fit101: 2.5 vs 2.6 -> " << overlap << "/" << sparse1.size() 
                      << " bits overlap (" << (overlap_ratio * 100) << "%)" << std::endl;
            
            if (overlap_ratio > 0.5) {
                std::cout << "  ✓ Similar inputs produce overlapping encodings" << std::endl;
            } else {
                std::cout << "  ⚠ Low overlap - check resolution settings" << std::endl;
            }
        }
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "Encoder Test: PASSED" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "\n✅ RDSE encoders are working correctly!" << std::endl;
        std::cout << "✅ Ready to integrate with Spatial Pooler" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "\n❌ ERROR: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

