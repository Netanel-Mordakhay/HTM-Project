#include "experiment_utils.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <string>
#include <iomanip>
#include <iostream>

namespace htm_swat {

static double nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

ExperimentMonitor& ExperimentMonitor::instance() {
    static ExperimentMonitor inst;
    return inst;
}

ExperimentMonitor::ExperimentMonitor()
    : sample_interval_ms_(2000), running_(false), latency_min_(0), latency_max_(0), latency_sum_(0), latency_count_(0), start_time_ms_(0) {
}

ExperimentMonitor::~ExperimentMonitor() {
    stop();
}

void ExperimentMonitor::start(const std::string& run_name, int sample_interval_ms) {
    std::lock_guard<std::mutex> lk(lock_);
    run_name_ = run_name;
    sample_interval_ms_ = sample_interval_ms;
    resource_samples_.clear();
    timing_events_.clear();
    latency_min_ = 0; latency_max_ = 0; latency_sum_ = 0; latency_count_ = 0;
    start_time_ms_ = nowMs();
    running_ = true;

    // initial timing event
    timing_events_.push_back({"run_start", 0.0, 0.0});

    // start sampler thread
    sampler_thread_ = std::thread([this]() { samplerLoop(); });
}

void ExperimentMonitor::stop() {
    if (!running_) return;
    running_ = false;
    if (sampler_thread_.joinable()) sampler_thread_.join();
}

void ExperimentMonitor::addTimingEvent(const std::string& name) {
    std::lock_guard<std::mutex> lk(lock_);
    double t = nowMs() - start_time_ms_;
    double delta = 0.0;
    if (!timing_events_.empty()) {
        delta = t - timing_events_.back().elapsed_ms;
    }
    timing_events_.push_back({name, t, delta});
}

void ExperimentMonitor::recordRowLatency(double ms) {
    std::lock_guard<std::mutex> lk(lock_);
    if (latency_count_ == 0) {
        latency_min_ = ms;
        latency_max_ = ms;
    } else {
        if (ms < latency_min_) latency_min_ = ms;
        if (ms > latency_max_) latency_max_ = ms;
    }
    latency_sum_ += ms;
    latency_count_ += 1;
}

std::string ExperimentMonitor::experimentDir() const {
    std::ostringstream ss;
    ss << "results/experiments_cpp_MultiThread_" << run_name_;
    return ss.str();
}

static unsigned long long readProcUtimeStime() {
    std::ifstream f("/proc/self/stat");
    if (!f.is_open()) return 0;
    std::string s;
    std::getline(f, s);
    f.close();
    // fields: after pid and comm, utime is 14th, stime 15th (1-based)
    std::istringstream iss(s);
    std::string token;
    unsigned long long utime = 0, stime = 0;
    // iterate tokens
    for (int i = 1; iss >> token; ++i) {
        if (i == 14) utime = std::stoull(token);
        if (i == 15) { stime = std::stoull(token); break; }
    }
    return utime + stime;
}

static long readVmRssKb() {
    std::ifstream f("/proc/self/status");
    if (!f.is_open()) return 0;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream iss(line);
            std::string key;
            long val = 0;
            std::string unit;
            iss >> key >> val >> unit;
            return val; // kB
        }
    }
    return 0;
}

void ExperimentMonitor::samplerLoop() {
    // initialize previous values
    unsigned long long prev_proc_ticks = readProcUtimeStime();
    double prev_wall = nowMs();
    long clk_tck = sysconf(_SC_CLK_TCK);
    long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);

    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(sample_interval_ms_));
        double t = nowMs();
        unsigned long long proc_ticks = readProcUtimeStime();
        double wall_delta_s = (t - prev_wall) / 1000.0;
        double proc_delta_ticks = static_cast<double>(proc_ticks - prev_proc_ticks);
        double proc_delta_s = proc_delta_ticks / static_cast<double>(clk_tck);
        double cpu_percent = 0.0;
        if (wall_delta_s > 0.0) cpu_percent = (proc_delta_s / wall_delta_s) * 100.0;
        // Normalize across CPU count to keep value between 0-100
        if (cpu_count > 0) cpu_percent = cpu_percent / static_cast<double>(cpu_count);

        long vmrss_kb = readVmRssKb();
        double ram_mb = static_cast<double>(vmrss_kb) / 1024.0;

        double elapsed = t - start_time_ms_;

        {
            std::lock_guard<std::mutex> lk(lock_);
            resource_samples_.push_back({elapsed, cpu_percent, ram_mb});
        }

        prev_proc_ticks = proc_ticks;
        prev_wall = t;
    }
}

void ExperimentMonitor::writeEfficiencyMetrics(const std::string& filename) {
    std::lock_guard<std::mutex> lk(lock_);
    // compute cpu/ram aggregates
    double cpu_min = 0, cpu_max = 0, cpu_sum = 0;
    double ram_min = 0, ram_max = 0, ram_sum = 0;
    if (!resource_samples_.empty()) {
        cpu_min = cpu_max = resource_samples_[0].cpu_percent;
        ram_min = ram_max = resource_samples_[0].ram_mb;
        for (const auto& s : resource_samples_) {
            cpu_sum += s.cpu_percent;
            ram_sum += s.ram_mb;
            if (s.cpu_percent < cpu_min) cpu_min = s.cpu_percent;
            if (s.cpu_percent > cpu_max) cpu_max = s.cpu_percent;
            if (s.ram_mb < ram_min) ram_min = s.ram_mb;
            if (s.ram_mb > ram_max) ram_max = s.ram_mb;
        }
    }
    double cpu_avg = resource_samples_.empty() ? 0.0 : cpu_sum / static_cast<double>(resource_samples_.size());
    double ram_avg = resource_samples_.empty() ? 0.0 : ram_sum / static_cast<double>(resource_samples_.size());

    // Use wall-clock since monitor start to report total runtime (more accurate even if final event not yet pushed)
    double total_runtime_ms = nowMs() - start_time_ms_;

    // latency summary
    double latency_avg = latency_count_ == 0 ? 0.0 : latency_sum_ / static_cast<double>(latency_count_);

    // build JSON manually
    std::ofstream f(filename);
    if (!f.is_open()) return;
    f << "{\n";
    f << "  \"timestamp\": \"" << run_name_ << "\",\n";
    f << "  \"total_runtime_ms\": " << std::fixed << std::setprecision(3) << total_runtime_ms << ",\n";

    // timing events
    f << "  \"timing_events\": [\n";
    for (size_t i = 0; i < timing_events_.size(); ++i) {
        const auto& e = timing_events_[i];
        f << "    {\"name\": \"" << e.name << "\", \"elapsed_ms\": " << e.elapsed_ms << ", \"delta_ms\": " << e.delta_ms << "}";
        if (i + 1 < timing_events_.size()) f << ",\n";
        else f << "\n";
    }
    f << "  ],\n";

    // cpu/ram aggregates
    f << "  \"cpu\": {\"min\": " << cpu_min << ", \"avg\": " << cpu_avg << ", \"peak\": " << cpu_max << "},\n";
    f << "  \"ram\": {\"min\": " << ram_min << ", \"avg\": " << ram_avg << ", \"peak\": " << ram_max << "},\n";

    // latency
    f << "  \"latency\": {\"min_ms\": " << latency_min_ << ", \"avg_ms\": " << latency_avg << ", \"peak_ms\": " << latency_max_ << ", \"count\": " << latency_count_ << "},\n";

    // resource_samples
    f << "  \"resource_samples\": [\n";
    for (size_t i = 0; i < resource_samples_.size(); ++i) {
        const auto& s = resource_samples_[i];
        f << "    {\"elapsed_ms\": " << s.elapsed_ms << ", \"cpu_percent\": " << s.cpu_percent << ", \"ram_mb\": " << s.ram_mb << "}";
        if (i + 1 < resource_samples_.size()) f << ",\n";
        else f << "\n";
    }
    f << "  ]\n";
    f << "}\n";
    f.close();
}

void ExperimentMonitor::writeRocThresholds(const std::string& filename, const std::vector<float>& thresholds,
                                           const std::vector<float>& scores, const std::vector<int>& labels) {
    if (thresholds.empty()) return;
    std::ofstream f(filename);
    if (!f.is_open()) return;
    f << "{\n";
    f << "  \"timestamp\": \"" << run_name_ << "\",\n";
    f << "  \"thresholds_tested\": " << thresholds.size() << ",\n";
    f << "  \"thresholds\": [\n";
    for (size_t i = 0; i < thresholds.size(); ++i) {
        float th = thresholds[i];
        int tp=0, fp=0, tn=0, fn=0;
        for (size_t j = 0; j < scores.size() && j < labels.size(); ++j) {
            bool pred = scores[j] > th;
            bool actual = labels[j] > 0;
            if (pred && actual) tp++;
            else if (pred && !actual) fp++;
            else if (!pred && !actual) tn++;
            else fn++;
        }
        double sensitivity = (tp + fn) > 0 ? static_cast<double>(tp) / (tp + fn) : 0.0;
        double specificity = (tn + fp) > 0 ? static_cast<double>(tn) / (tn + fp) : 0.0;
        double precision = (tp + fp) > 0 ? static_cast<double>(tp) / (tp + fp) : 0.0;
        double recall = sensitivity;
        double f1 = (precision + recall) > 0 ? 2.0 * (precision * recall) / (precision + recall) : 0.0;
        double accuracy = (scores.size() > 0) ? static_cast<double>(tp + tn) / std::min(scores.size(), labels.size()) : 0.0;

        f << "    {\"threshold\": " << std::fixed << std::setprecision(6) << th
          << ", \"sensitivity\": " << sensitivity
          << ", \"specificity\": " << specificity
          << ", \"f1\": " << f1
          << ", \"precision\": " << precision
          << ", \"recall\": " << recall
          << ", \"accuracy\": " << accuracy << "}";
        if (i + 1 < thresholds.size()) f << ",\n"; else f << "\n";
    }
    f << "  ]\n";
    f << "}\n";
    f.close();
}

void ExperimentMonitor::writePerformanceMetrics(const std::string& filename,
                                               const std::string& timestamp,
                                               size_t thresholds_tested,
                                               float best_threshold,
                                               float best_f1, float best_precision, float best_recall, float best_accuracy,
                                               float avg_f1, float avg_precision, float avg_recall, float avg_accuracy) {
    std::ofstream f(filename);
    if (!f.is_open()) return;
    f << "{\n";
    f << "  \"timestamp\": \"" << timestamp << "\",\n";
    f << "  \"thresholds_tested\": " << thresholds_tested << ",\n";
    f << "  \"best\": {\n";
    f << "    \"threshold\": " << best_threshold << ",\n";
    f << "    \"f1\": " << best_f1 << ",\n";
    f << "    \"precision\": " << best_precision << ",\n";
    f << "    \"recall\": " << best_recall << ",\n";
    f << "    \"accuracy\": " << best_accuracy << "\n";
    f << "  },\n";
    f << "  \"average\": {\n";
    f << "    \"f1\": " << avg_f1 << ",\n";
    f << "    \"precision\": " << avg_precision << ",\n";
    f << "    \"recall\": " << avg_recall << ",\n";
    f << "    \"accuracy\": " << avg_accuracy << "\n";
    f << "  }\n";
    f << "}\n";
    f.close();
}

} // namespace htm_swat
