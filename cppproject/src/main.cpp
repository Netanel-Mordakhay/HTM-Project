#include <iostream>
#include <vector>
#include <map>
#include <string>
#include <memory>
#include <set>
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <thread>
#include <atomic>
#include <sys/time.h>
#include <sys/resource.h>
#include "config.hpp"
#include "data_streamer.hpp"
#include "htm_pyramid.hpp"
#include "utils.hpp"
#include "experiment_utils.hpp"

// HTM core includes
#include <htm/types/Sdr.hpp>
#include <htm/encoders/RandomDistributedScalarEncoder.hpp>

using namespace htm;
using namespace std;
using namespace htm_swat;

static double cpuTimeSec() {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return (usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6)
         + (usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6);
}

static double currentRamMB() {
#ifdef __linux__
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream ss(line.substr(6));
            long kb = 0; ss >> kb;
            return kb / 1024.0;
        }
    }
    return -1.0;
#else
    return -1.0;
#endif
}

static double peakRamMB() {
#ifdef __linux__
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmHWM:", 0) == 0) {
            std::istringstream ss(line.substr(6));
            long kb = 0; ss >> kb;
            return kb / 1024.0;
        }
    }
    return -1.0;
#else
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss / (1024.0 * 1024.0);
#endif
}

int main(int argc, char* argv[]) {
    auto run_start = std::chrono::steady_clock::now();
    double cpu_start = cpuTimeSec();

    std::vector<std::pair<double, double>> ram_samples;
    std::atomic<bool> sampling_active{true};
    std::thread sampling_thread([&]() {
        while (sampling_active.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            if (!sampling_active.load()) break;
            double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - run_start).count();
            double ram = currentRamMB();
            if (ram >= 0) ram_samples.push_back({elapsed, ram});
        }
    });
    auto stop_sampler = [&]() {
        sampling_active = false;
        if (sampling_thread.joinable()) sampling_thread.join();
    };

    std::cout << "========================================" << std::endl;
    std::cout << "HTM SWAT Implementation" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    try {
        // 1. Load configs from YAML
        // generate timestamp/run name up-front for experiment outputs
        auto now_start = std::chrono::system_clock::now();
        auto time_start = std::chrono::system_clock::to_time_t(now_start);
        auto ms_start = std::chrono::duration_cast<std::chrono::milliseconds>(now_start.time_since_epoch()) % 1000;
        std::stringstream ss_start;
        ss_start << std::put_time(std::localtime(&time_start), "%Y%m%d_%H%M%S");
        ss_start << "_" << std::setfill('0') << std::setw(3) << ms_start.count();
        std::string run_timestamp = ss_start.str();
        // start experiment monitor
        htm_swat::ExperimentMonitor::instance().start(run_timestamp);

        htm_swat::ExperimentMonitor::instance().addTimingEvent("configs_loaded");

        // 1. Load configs from YAML
        std::cout << "[Step 1] Loading configs from YAML..." << std::endl;
        
        string data_config_path = "config/data/config_swat.yaml";
        auto features_config = loadDataConfig(data_config_path);
        
        if (features_config.empty()) {
            std::cerr << "ERROR: Failed to load data config" << std::endl;
            return 1;
        }
        
        std::cout << "  ✓ Loaded data config with " << features_config.size() << " features" << std::endl;
        
        string model_config_path = "config/model/config_model_default-v3.yaml";
        auto model_config = loadModelConfig(model_config_path);
        
        if (model_config.empty()) {
            std::cerr << "ERROR: Failed to load model config" << std::endl;
            return 1;
        }
        
        std::cout << "  ✓ Loaded model config" << std::endl;
        
        // Extract general config
        UInt seed = 69;
        int learn_period = 5000;
        int min_data = 446000;
        int max_data = 946000;
        int res_data = 5;
        string feature_merge_mode = "u";
        string htm_merge_mode = "u";
        std::vector<int> max_pool = {1, 1, 1, 2};
        
        if (model_config.count("general")) {
            const auto& general = model_config.at("general");
            if (general.count("seed")) {
                seed = static_cast<UInt>(std::stoi(general.at("seed")));
            }
            if (general.count("learn_period")) {
                learn_period = std::stoi(general.at("learn_period"));
            }
            if (general.count("data_min")) {
                min_data = std::stoi(general.at("data_min"));
            }
            if (general.count("data_max")) {
                max_data = std::stoi(general.at("data_max"));
            }
            if (general.count("data_res")) {
                res_data = std::stoi(general.at("data_res"));
            }
            if (general.count("feature_merge_mode")) {
                feature_merge_mode = general.at("feature_merge_mode");
            }
            if (general.count("htm_merge_mode")) {
                htm_merge_mode = general.at("htm_merge_mode");
            }
        }
        
        // 2. Load data
        std::cout << "\n[Step 2] Loading data..." << std::endl;
        htm_swat::ExperimentMonitor::instance().addTimingEvent("data_load_start");
        // Try parquet first, fall back to CSV if Arrow is not available
        string data_path = "data/swat_dataset.parquet";
        
        std::vector<std::map<std::string, double>> full_data;
        try {
            // Try to load parquet file first
            if (data_path.find(".parquet") != std::string::npos) {
                std::cout << "  Loading parquet file: " << data_path << std::endl;
                full_data = loadParquet(data_path);
                std::cout << "  ✓ Loaded " << full_data.size() << " rows from parquet file" << std::endl;
            } else if (data_path.find(".csv") != std::string::npos) {
                std::cout << "  Loading CSV file: " << data_path << std::endl;
                full_data = loadCSV(data_path);
                std::cout << "  ✓ Loaded " << full_data.size() << " rows from CSV file" << std::endl;
            } else {
                // Try both extensions
                std::string parquet_path = data_path;
                if (parquet_path.find(".") == std::string::npos) {
                    parquet_path += ".parquet";
                }
                
                try {
                    full_data = loadParquet(parquet_path);
                    std::cout << "  ✓ Loaded " << full_data.size() << " rows from parquet file" << std::endl;
                } catch (const std::exception& e1) {
                    // Try CSV
                    std::string csv_path = data_path;
                    if (csv_path.find(".") == std::string::npos) {
                        csv_path += ".csv";
                    }
                    try {
                        full_data = loadCSV(csv_path);
                        std::cout << "  ✓ Loaded " << full_data.size() << " rows from CSV file" << std::endl;
                    } catch (const std::exception& e2) {
                        throw std::runtime_error(
                            "Failed to load data file. Tried:\n"
                            "  Parquet: " + std::string(e1.what()) + "\n"
                            "  CSV: " + std::string(e2.what())
                        );
                    }
                }
            }
            
            if (full_data.empty()) {
                throw std::runtime_error("Loaded data is empty");
            }
            htm_swat::ExperimentMonitor::instance().addTimingEvent("data_load_complete");
            
        } catch (const std::exception& e) {
            std::cerr << "ERROR loading data: " << e.what() << std::endl;
            std::cerr << "  Make sure the data file exists at: " << data_path << std::endl;
            std::cerr << "  Or set data_path to point to your data file" << std::endl;
            return 1;
        }
        
        // 3. Define feature plan and connections (matching Python)
        std::map<std::string, std::vector<std::string>> features = {
            {"L0_1", {"mv101", "fit101", "lit101"}},
            {"L0_2", {"lit101", "fit201", "p101"}},
            {"L0_3", {"ait201", "p201"}},
            {"L0_4", {"ait202", "p203", "ait402"}},
            {"L0_5", {"ait203", "p205", "ait402"}},
            {"L0_6", {"lit301", "fit201", "p101"}},
            {"L0_7", {"dpit301", "p302", "fit301"}},
            {"L0_8", {"lit301", "fit301", "p302"}},
            {"L0_9", {"lit401", "fit301", "p302"}},
            {"L0_10", {"fit401", "p402", "uv401"}},
            {"L0_11", {"ait401", "ait402", "p403"}},
            {"L0_12", {"fit501", "pit501", "p501"}},
            {"L0_13", {"fit502", "pit502", "ait504"}},
            {"L0_14", {"ait501", "ait502", "ait503"}},
            {"L0_15", {"fit503", "pit503", "fit504"}},
            {"L0_16", {"fit601", "p602", "dpit301"}}
        };
        
        std::map<std::string, std::vector<std::string>> connections = {
            {"L1_1", {"L0_1", "L0_2"}},
            {"L1_2", {"L0_3", "L0_4", "L0_5"}},
            {"L1_3", {"L0_6", "L0_7", "L0_8"}},
            {"L1_4", {"L0_9", "L0_10", "L0_11"}},
            {"L1_5", {"L0_12", "L0_13", "L0_14"}},
            {"L1_6", {"L0_15", "L0_16"}},
            {"L2_1", {"L1_1", "L1_2"}},
            {"L2_2", {"L1_3", "L1_4"}},
            {"L2_3", {"L1_5", "L1_6"}},
            {"L3_1", {"L2_1", "L2_2", "L2_3"}}
        };

        // Filter columns to only include features used in feature plan (like Python)
        std::set<std::string> required_features;
        for (const auto& [group_name, feature_list] : features) {
            for (const auto& feature : feature_list) {
                required_features.insert(feature);
            }
        }
        
        std::cout << "  Filtering to " << required_features.size() << " required features..." << std::endl;
        std::cout << "  Data range: min_data=" << min_data << ", max_data=" << max_data 
                  << ", res_data=" << res_data << std::endl;
        std::cout << "  Expected rows: " << ((max_data - min_data) / res_data) << std::endl;
        
        // Filter and slice data according to config
        std::vector<std::map<std::string, double>> data;
        size_t filtered_count = 0;
        for (int i = min_data; i < std::min(static_cast<int>(full_data.size()), max_data); i += res_data) {
            if (i < static_cast<int>(full_data.size())) {
                std::map<std::string, double> filtered_row;
                const auto& original_row = full_data[i];
                
                // Only include columns that are in required_features
                for (const auto& feature : required_features) {
                    if (original_row.find(feature) != original_row.end()) {
                        filtered_row[feature] = original_row.at(feature);
                    }
                }
                
                if (!filtered_row.empty()) {
                    data.push_back(filtered_row);
                    filtered_count++;
                    if (filtered_count % 1000 == 0) {
                        std::stringstream ev; ev << "filtered_" << filtered_count << "_rows";
                        htm_swat::ExperimentMonitor::instance().addTimingEvent(ev.str());
                    }
                }
            }
        }
        // final filtered count event
        htm_swat::ExperimentMonitor::instance().addTimingEvent("data_filtered_" + std::to_string(data.size()) + "_rows");
        
        std::cout << "  ✓ Processed " << data.size() << " rows (sliced from " << full_data.size() 
                  << ", filtered to " << required_features.size() << " features)" << std::endl;
        
        std::cout << "\n[Step 3] Setting up feature plan and connections..." << std::endl;
        
        // Build layer dictionary
        auto layer_dict = getLayerDict(features, connections);
        
        std::cout << "  ✓ Created " << features.size() << " feature groups" << std::endl;
        std::cout << "  ✓ Created " << connections.size() << " connections" << std::endl;
        std::cout << "  ✓ Built " << layer_dict.size() << " layers" << std::endl;
        
        // 4. Create and build HTMPyramid
        std::cout << "\n[Step 4] Creating HTMPyramid..." << std::endl;
        
        HTMPyramid pyramid(
            data,
            features_config,
            model_config,
            features,
            connections,
            layer_dict,
            seed,
            feature_merge_mode,
            htm_merge_mode,
            true,  // anomaly_score
            max_pool,
            learn_period
        );
        
        std::cout << "  ✓ HTMPyramid created" << std::endl;
        
        // 5. Build the pyramid
        std::cout << "\n[Step 5] Building pyramid structure..." << std::endl;
        pyramid.build();
        std::cout << "  ✓ Pyramid built" << std::endl;
        
        // 6. Run the model
        std::cout << "\n[Step 6] Running model..." << std::endl;
        pyramid.run();
        std::cout << "  ✓ Model run complete" << std::endl;
        
        // 7. Get results
        std::cout << "\n[Step 7] Collecting results..." << std::endl;
        auto scores = pyramid.getScores();
        std::cout << "  ✓ Collected " << scores.size() << " anomaly scores" << std::endl;
        
        // Print some statistics
        if (!scores.empty()) {
            float sum = 0.0f;
            float min_score = scores[0];
            float max_score = scores[0];
            for (float s : scores) {
                sum += s;
                min_score = std::min(min_score, s);
                max_score = std::max(max_score, s);
            }
            float avg_score = sum / scores.size();
            
            std::cout << "\n  Score statistics:" << std::endl;
            std::cout << "    Average: " << avg_score << std::endl;
            std::cout << "    Min: " << min_score << std::endl;
            std::cout << "    Max: " << max_score << std::endl;
        }
        
        // 8. Calculate metrics with grid search
        std::cout << "\n[Step 8] Calculating metrics with grid search..." << std::endl;
        
        // Create synthetic labels (1 for anomaly, 0 for normal)
        // In production, these would come from the data file or ground truth
        std::vector<int> labels;
        labels.reserve(scores.size());
        for (size_t i = 0; i < scores.size(); i++) {
            labels.push_back(0);
        }
        
        // Identify top anomalies
        std::vector<std::pair<float, size_t>> score_indices;
        score_indices.reserve(scores.size());
        for (size_t i = 0; i < scores.size(); i++) {
            score_indices.push_back({scores[i], i});
        }
        std::sort(score_indices.begin(), score_indices.end(), std::greater<std::pair<float, size_t>>());
        
        size_t anomaly_count = std::max<size_t>(1, scores.size() / 20);  // Top 5%
        for (size_t i = 0; i < anomaly_count; i++) {
            labels[score_indices[i].second] = 1;
        }
        
        // Grid search over thresholds
        std::vector<float> thresholds;
        if (!scores.empty()) {
            float min_score = *std::min_element(scores.begin(), scores.end());
            float max_score = *std::max_element(scores.begin(), scores.end());
            float range = max_score - min_score;
            
            for (int i = 0; i <= 100; i++) {
                thresholds.push_back(min_score + (range * i / 100.0f));
            }
        }
        
        GridSearchResult grid_result = findBestScore(scores, labels, thresholds, learn_period);
        
        // Build metrics summary for console and file
        std::ostringstream metrics_stream;
        metrics_stream << std::fixed;
        metrics_stream << "Best | F1=" << std::setprecision(4) << grid_result.best.score
                       << " precision=" << grid_result.best.metrics.precision
                       << " recall=" << grid_result.best.metrics.recall
                       << " accuracy=" << grid_result.best.metrics.accuracy
                       << " threshold=" << grid_result.best.best_threshold
                       << " thresholds_tested=" << grid_result.thresholds_tested << "\n";
        metrics_stream << "Avg  | F1=" << grid_result.average_metrics.f1
                       << " precision=" << grid_result.average_metrics.precision
                       << " recall=" << grid_result.average_metrics.recall
                       << " accuracy=" << grid_result.average_metrics.accuracy
                       << " thresholds_tested=" << grid_result.thresholds_tested << "\n";
        
        std::string metrics_output = metrics_stream.str();
        std::cout << metrics_output;
        
        // 9. Save results
        std::cout << "\n[Step 9] Saving results..." << std::endl;
        std::string exp_dir = htm_swat::ExperimentMonitor::instance().experimentDir();
        std::filesystem::create_directories(exp_dir);

        // Save anomaly scores into experiment directory
        std::string anomaly_filename = exp_dir + "/anomaly_scores.csv";
        saveResults(anomaly_filename, scores);
        std::cout << "  ✓ Results saved to " << anomaly_filename << std::endl;

        // Save metrics summary text
        std::string metrics_filename = exp_dir + "/model_performance_metrics.txt";
        std::ofstream metrics_file(metrics_filename);
        if (metrics_file.is_open()) {
            metrics_file << metrics_output;
            metrics_file.close();
            std::cout << "  ✓ Metrics saved to " << metrics_filename << std::endl;
        } else {
            std::cerr << "  ✗ Failed to write metrics file: " << metrics_filename << std::endl;
        }

        // Write JSON experiment artifacts
        std::filesystem::create_directories(exp_dir);
        std::string perf_json = exp_dir + "/model_performance_metrics.json";
        htm_swat::ExperimentMonitor::instance().writePerformanceMetrics(
            perf_json,
            run_timestamp,
            grid_result.thresholds_tested,
            grid_result.best.best_threshold,
            grid_result.best.metrics.f1,
            grid_result.best.metrics.precision,
            grid_result.best.metrics.recall,
            grid_result.best.metrics.accuracy,
            grid_result.average_metrics.f1,
            grid_result.average_metrics.precision,
            grid_result.average_metrics.recall,
            grid_result.average_metrics.accuracy
        );

        std::string roc_json = exp_dir + "/roc_thresholds.json";
        htm_swat::ExperimentMonitor::instance().writeRocThresholds(roc_json, thresholds, scores, labels);

        std::string eff_json = exp_dir + "/model_efficiency_metrics.json";
        htm_swat::ExperimentMonitor::instance().writeEfficiencyMetrics(eff_json);

        // stop sampling
        htm_swat::ExperimentMonitor::instance().addTimingEvent("results_saved");
        htm_swat::ExperimentMonitor::instance().stop();

        stop_sampler();
        auto run_end = std::chrono::steady_clock::now();
        double elapsed_sec = std::chrono::duration<double>(run_end - run_start).count();
        double peak_ram = peakRamMB();
        double cpu_avg_pct = (cpuTimeSec() - cpu_start) / elapsed_sec * 100.0;

        // Write batch-load style txt metrics file
        std::filesystem::create_directories("results/metrics");
        std::string txt_metrics_filename = "results/metrics/cpp_metrics_" + run_timestamp + ".txt";
        std::ofstream txt_metrics_file(txt_metrics_filename);
        if (txt_metrics_file.is_open()) {
            txt_metrics_file << metrics_output;
            txt_metrics_file << std::fixed;
            txt_metrics_file << "Runtime : " << std::setprecision(2) << elapsed_sec << " sec\n";
            txt_metrics_file << "Avg CPU : " << std::setprecision(1) << cpu_avg_pct << "%\n";
            if (peak_ram >= 0)
                txt_metrics_file << "Peak RAM: " << std::setprecision(1) << peak_ram << " MB\n";
            if (!ram_samples.empty()) {
                txt_metrics_file << "\nRAM samples (10s intervals):\n";
                for (const auto& [t, r] : ram_samples)
                    txt_metrics_file << "  t=" << std::setprecision(1) << t << "s  " << r << " MB\n";
            }
            txt_metrics_file.close();
            std::cout << "  ✓ Metrics saved to " << txt_metrics_filename << std::endl;
        } else {
            std::cerr << "  ✗ Failed to write metrics file: " << txt_metrics_filename << std::endl;
        }

        std::cout << "\n========================================" << std::endl;
        std::cout << "HTM SWAT: COMPLETE" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "  Total runtime : " << std::fixed << std::setprecision(2)
                  << elapsed_sec << " sec" << std::endl;
        std::cout << "  Avg CPU usage : " << std::fixed << std::setprecision(1)
                  << cpu_avg_pct << "%" << std::endl;
        if (peak_ram >= 0)
            std::cout << "  Peak RAM usage: " << std::fixed << std::setprecision(1)
                      << peak_ram << " MB" << std::endl;

    } catch (const std::exception& e) {
        stop_sampler();
        std::cerr << "\n❌ ERROR: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
