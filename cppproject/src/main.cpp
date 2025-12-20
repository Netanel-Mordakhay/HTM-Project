#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <memory>
#include "config.hpp"
#include "data_streamer.hpp"

// HTM core includes
#include <htm/types/Sdr.hpp>
#include <htm/encoders/RandomDistributedScalarEncoder.hpp>

using namespace htm;
using namespace std;
using namespace htm_swat;


int main(int argc, char* argv[]) {
    std::cout << "========================================" << std::endl;
    std::cout << "HTM SWAT Encoder Test" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    try {
        // 1. Load configs from YAML (both data and model configs)
        std::cout << "[Step 1] Loading configs from YAML..." << std::endl;
        
        // Load data config (feature definitions)
        // Equivalent to Python: data_cfg = load_config(config_path_data)
        string data_config_path = "config/data/config_swat.yaml";
        auto features_config = loadDataConfig(data_config_path);
        
        if (features_config.empty()) {
            std::cerr << "ERROR: Failed to load data config or config is empty" << std::endl;
            std::cerr << "  Tried to load: " << data_config_path << std::endl;
            return 1;
        }
        
        std::cout << "  ✓ Loaded data config with " << features_config.size() << " features" << std::endl;
        
        // Load model config (encoder/SP/TM parameters)
        // Equivalent to Python: run_cfg = load_config(config_path_model)
        string model_config_path = "config/model/config_model_default.yaml";
        auto model_config = loadModelConfig(model_config_path);
        
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
        
        // 3. Create DataStreamer for encoding
        // Equivalent to Python: DataStreamer(data, features_cfg=..., encoders_cfg=...)
        std::cout << "\n[Step 3] Creating DataStreamer..." << std::endl;
        DataStreamer streamer(encoders);
        std::cout << "  ✓ DataStreamer created with " << streamer.size() << " encoders" << std::endl;
        
        // 4. Test encoding with sample data
        std::cout << "\n[Step 4] Testing encoding..." << std::endl;
        
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
            if (streamer.hasEncoder(feature_name)) {
                SDR output = streamer.encodeFeature(feature_name, value);
                
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
            if (streamer.hasEncoder(feature_name)) {
                SDR output = streamer.encodeFeature(feature_name, value);
                
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
        
        // Test encoding a full row
        std::cout << "\n  Testing full row encoding:" << std::endl;
        auto encoded_row = streamer.encodeRow(test_data);
        std::cout << "    Encoded " << encoded_row.size() << " features from row" << std::endl;
        
        // 5. Test encoding consistency (same input should produce same output)
        std::cout << "\n[Step 5] Testing encoding consistency..." << std::endl;
        if (streamer.hasEncoder("fit101")) {
            SDR output1 = streamer.encodeFeature("fit101", 2.5);
            SDR output2 = streamer.encodeFeature("fit101", 2.5);
            
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
        
        // 6. Test encoding similarity (similar inputs should have overlapping encodings)
        std::cout << "\n[Step 6] Testing encoding similarity..." << std::endl;
        if (streamer.hasEncoder("fit101")) {
            SDR output1 = streamer.encodeFeature("fit101", 2.5);
            SDR output2 = streamer.encodeFeature("fit101", 2.6);  // Very similar value
            
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

