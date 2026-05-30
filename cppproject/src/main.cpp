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
#include <sys/resource.h>
#include "config.hpp"
#include "data_streamer.hpp"
#include "htm_pyramid.hpp"
#include "utils.hpp"

// HTM core includes
#include <htm/types/Sdr.hpp>
#include <htm/encoders/RandomDistributedScalarEncoder.hpp>

using namespace htm;
using namespace std;
using namespace htm_swat;

// Returns cumulative CPU time (user + system) in seconds.
static double cpuTimeSec() {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return (usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6)
         + (usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6);
}

// Returns current resident set size in MB (Linux only; -1 on other platforms).
static double currentRamMB() {
#ifdef __linux__
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream ss(line.substr(6));
            long kb = 0;
            ss >> kb;
            return kb / 1024.0;
        }
    }
    return -1.0;
#else
    return -1.0;
#endif
}

// Returns peak resident set size in MB.
// Uses /proc/self/status on Linux (RPi), getrusage on macOS.
static double peakRamMB() {
#ifdef __linux__
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("VmHWM:", 0) == 0) {  // High-Water-Mark RSS in kB
            std::istringstream ss(line.substr(6));
            long kb = 0;
            ss >> kb;
            return kb / 1024.0;
        }
    }
    return -1.0;
#else
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    // macOS: ru_maxrss is bytes; Linux: kilobytes (handled above)
    return usage.ru_maxrss / (1024.0 * 1024.0);
#endif
}

int main(int argc, char* argv[]) {
    auto run_start = std::chrono::steady_clock::now();
    double cpu_start = cpuTimeSec();

    std::vector<std::pair<double, double>> ram_samples; // (elapsed_sec, ram_mb)
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
        std::cout << "[Step 1] Loading configs from YAML..." << std::endl;
        
        string data_config_path = "config/data/config_swat.yaml";
        auto features_config = loadDataConfig(data_config_path);
        
        if (features_config.empty()) {
            std::cerr << "ERROR: Failed to load data config" << std::endl;
            return 1;
        }
        
        std::cout << "  ✓ Loaded data config with " << features_config.size() << " features" << std::endl;
        
        string model_config_path = "config/model/config_model_default-v5.yaml";
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
        
        // 2. Define feature plan and connections (matching Python)
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

        // Collect only the feature columns needed by the feature plan
        std::set<std::string> required_features;
        for (const auto& [group_name, feature_list] : features) {
            for (const auto& feature : feature_list) {
                required_features.insert(feature);
            }
        }

        // 3. Open data file for streaming (one row at a time — avoids loading full file)
        std::cout << "\n[Step 3] Opening data file for streaming..." << std::endl;
        string data_path = "data/swat_dataset.parquet";
        std::unique_ptr<RowStreamer> streamer;
        try {
            streamer = makeStreamer(data_path, min_data, max_data, res_data, required_features);
        } catch (const std::exception& e1) {
            // Parquet failed — try CSV fallback
            std::string csv_path = data_path.substr(0, data_path.rfind('.')) + ".csv";
            try {
                streamer = makeStreamer(csv_path, min_data, max_data, res_data, required_features);
            } catch (const std::exception& e2) {
                std::cerr << "ERROR opening data: " << e1.what() << "\n"
                          << "  CSV fallback also failed: " << e2.what() << std::endl;
                return 1;
            }
        }
        std::cout << "  ✓ Streamer ready: " << streamer->totalRows() << " rows to process"
                  << " (range " << min_data << "–" << max_data << ", stride " << res_data << ")"
                  << ", " << required_features.size() << " features" << std::endl;

        std::cout << "\n[Step 4] Setting up feature plan and connections..." << std::endl;
        
        // Build layer dictionary
        auto layer_dict = getLayerDict(features, connections);
        
        std::cout << "  ✓ Created " << features.size() << " feature groups" << std::endl;
        std::cout << "  ✓ Created " << connections.size() << " connections" << std::endl;
        std::cout << "  ✓ Built " << layer_dict.size() << " layers" << std::endl;

        // 5. Create and build HTMPyramid
        std::cout << "\n[Step 5] Creating HTMPyramid..." << std::endl;

        HTMPyramid pyramid(
            *streamer,
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

        // 6. Build the pyramid
        std::cout << "\n[Step 6] Building pyramid structure..." << std::endl;
        pyramid.build();
        std::cout << "  ✓ Pyramid built" << std::endl;

        // 7. Run the model
        std::cout << "\n[Step 7] Running model..." << std::endl;
        pyramid.run();
        std::cout << "  ✓ Model run complete" << std::endl;

        // 8. Get results
        std::cout << "\n[Step 8] Collecting results..." << std::endl;
        auto scores = pyramid.getScores();
        auto labels = pyramid.getLabels();
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
        
        // 9. Calculate metrics with grid search (ground truth = dataset label column)
        std::cout << "\n[Step 9] Calculating metrics with grid search..." << std::endl;

        if (labels.size() != scores.size()) {
            std::cerr << "ERROR: label count (" << labels.size()
                      << ") != anomaly score count (" << scores.size()
                      << "); cannot compute metrics." << std::endl;
            return 1;
        }
        int n_positive = 0;
        for (int y : labels) {
            if (y > 0) {
                ++n_positive;
            }
        }
        std::cout << "  Ground-truth labels: " << n_positive << " positive / " << labels.size()
                  << " rows (" << std::fixed << std::setprecision(2)
                  << (100.0 * n_positive / std::max(1, static_cast<int>(labels.size()))) << "%)\n";

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
        
        // 10. Save results
        std::cout << "\n[Step 10] Saving results..." << std::endl;
        
        // Generate timestamped filename
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S");
        ss << "_" << std::setfill('0') << std::setw(3) << ms.count();
        std::string timestamp_str = ss.str();
        
        // Save anomaly scores
        std::string timestamp_filename = "results/anomaly_scores_" + timestamp_str + ".csv";
        saveResults(timestamp_filename, scores);
        std::cout << "  ✓ Results saved to " << timestamp_filename << std::endl;
        
        stop_sampler();
        auto run_end = std::chrono::steady_clock::now();
        double elapsed_sec = std::chrono::duration<double>(run_end - run_start).count();
        double peak_ram = peakRamMB();
        double cpu_avg_pct = (cpuTimeSec() - cpu_start) / elapsed_sec * 100.0;

        // Save metrics summary
        std::filesystem::create_directories("results/metrics");
        std::string metrics_filename = "results/metrics/cpp_metrics_" + timestamp_str + ".txt";
        std::ofstream metrics_file(metrics_filename);
        if (metrics_file.is_open()) {
            metrics_file << metrics_output;
            metrics_file << std::fixed;
            metrics_file << "Runtime : " << std::setprecision(2) << elapsed_sec << " sec\n";
            metrics_file << "Avg CPU : " << std::setprecision(1) << cpu_avg_pct << "%\n";
            if (peak_ram >= 0)
                metrics_file << "Peak RAM: " << std::setprecision(1) << peak_ram << " MB\n";
            if (!ram_samples.empty()) {
                metrics_file << "\nRAM samples (10s intervals):\n";
                for (const auto& [t, r] : ram_samples)
                    metrics_file << "  t=" << std::setprecision(1) << t << "s  " << r << " MB\n";
            }
            metrics_file.close();
            std::cout << "  ✓ Metrics saved to " << metrics_filename << std::endl;
        } else {
            std::cerr << "  ✗ Failed to write metrics file: " << metrics_filename << std::endl;
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
