#include "experiment_utils.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include <unistd.h>
#include <string>
#include <iomanip>
#include <iostream>
#include <cstdlib>

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
    : sample_interval_ms_(2000), running_(false),
      latency_min_(0), latency_max_(0), latency_sum_(0), latency_count_(0), start_time_ms_(0) {}

ExperimentMonitor::~ExperimentMonitor() { stop(); }

void ExperimentMonitor::start(const std::string& run_name, int sample_interval_ms) {
    std::lock_guard<std::mutex> lk(lock_);
    run_name_ = run_name;
    sample_interval_ms_ = sample_interval_ms;
    resource_samples_.clear();
    timing_events_.clear();
    latency_min_ = 0; latency_max_ = 0; latency_sum_ = 0; latency_count_ = 0;
    start_time_ms_ = nowMs();
    running_ = true;
    timing_events_.push_back({"run_start", 0.0, 0.0});
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
    double delta = timing_events_.empty() ? 0.0 : t - timing_events_.back().elapsed_ms;
    timing_events_.push_back({name, t, delta});
}

void ExperimentMonitor::recordRowLatency(double ms) {
    std::lock_guard<std::mutex> lk(lock_);
    if (latency_count_ == 0) { latency_min_ = ms; latency_max_ = ms; }
    else {
        if (ms < latency_min_) latency_min_ = ms;
        if (ms > latency_max_) latency_max_ = ms;
    }
    latency_sum_ += ms;
    latency_count_ += 1;
}

static std::string getBranchName() {
    // 1. Environment variable set by docker-compose or CI
    const char* env = std::getenv("GIT_BRANCH");
    if (env && env[0] != '\0') return std::string(env);

    // 2. Read .git/HEAD for local runs
    std::ifstream head(".git/HEAD");
    if (head.is_open()) {
        std::string line;
        std::getline(head, line);
        const std::string prefix = "ref: refs/heads/";
        if (line.rfind(prefix, 0) == 0) return line.substr(prefix.size());
    }

    return "unknown";
}

std::string ExperimentMonitor::experimentDir() const {
    return "results/experiments_" + getBranchName() + "_" + run_name_;
}

static unsigned long long readProcUtimeStime() {
    std::ifstream f("/proc/self/stat");
    if (!f.is_open()) return 0;
    std::string s; std::getline(f, s);
    std::istringstream iss(s);
    std::string token;
    unsigned long long utime = 0, stime = 0;
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
            std::string key; long val = 0; std::string unit;
            iss >> key >> val >> unit;
            return val;
        }
    }
    return 0;
}

void ExperimentMonitor::samplerLoop() {
    unsigned long long prev_ticks = readProcUtimeStime();
    double prev_wall = nowMs();
    long clk_tck = sysconf(_SC_CLK_TCK);
    long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);

    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(sample_interval_ms_));
        double t = nowMs();
        unsigned long long ticks = readProcUtimeStime();
        double wall_s = (t - prev_wall) / 1000.0;
        double proc_s = static_cast<double>(ticks - prev_ticks) / static_cast<double>(clk_tck);
        double cpu = (wall_s > 0.0) ? (proc_s / wall_s) * 100.0 : 0.0;
        if (cpu_count > 0) cpu /= static_cast<double>(cpu_count);
        double ram = static_cast<double>(readVmRssKb()) / 1024.0;
        double elapsed = t - start_time_ms_;
        { std::lock_guard<std::mutex> lk(lock_); resource_samples_.push_back({elapsed, cpu, ram}); }
        prev_ticks = ticks; prev_wall = t;
    }
}

void ExperimentMonitor::writeEfficiencyMetrics(const std::string& filename) {
    std::lock_guard<std::mutex> lk(lock_);
    double cpu_min=0, cpu_max=0, cpu_sum=0, ram_min=0, ram_max=0, ram_sum=0;
    if (!resource_samples_.empty()) {
        cpu_min = cpu_max = resource_samples_[0].cpu_percent;
        ram_min = ram_max = resource_samples_[0].ram_mb;
        for (const auto& s : resource_samples_) {
            cpu_sum += s.cpu_percent; ram_sum += s.ram_mb;
            if (s.cpu_percent < cpu_min) cpu_min = s.cpu_percent;
            if (s.cpu_percent > cpu_max) cpu_max = s.cpu_percent;
            if (s.ram_mb < ram_min) ram_min = s.ram_mb;
            if (s.ram_mb > ram_max) ram_max = s.ram_mb;
        }
    }
    size_t n = resource_samples_.size();
    double cpu_avg = n ? cpu_sum / n : 0.0;
    double ram_avg = n ? ram_sum / n : 0.0;
    double total_ms = nowMs() - start_time_ms_;
    double lat_avg = latency_count_ ? latency_sum_ / latency_count_ : 0.0;

    std::ofstream f(filename);
    if (!f.is_open()) return;
    f << std::fixed << std::setprecision(3);
    f << "{\n";
    f << "  \"timestamp\": \"" << run_name_ << "\",\n";
    f << "  \"total_runtime_ms\": " << total_ms << ",\n";
    f << "  \"timing_events\": [\n";
    for (size_t i = 0; i < timing_events_.size(); ++i) {
        const auto& e = timing_events_[i];
        f << "    {\"name\": \"" << e.name << "\", \"elapsed_ms\": " << e.elapsed_ms << ", \"delta_ms\": " << e.delta_ms << "}";
        f << (i+1 < timing_events_.size() ? ",\n" : "\n");
    }
    f << "  ],\n";
    f << "  \"cpu\": {\"min\": " << cpu_min << ", \"avg\": " << cpu_avg << ", \"peak\": " << cpu_max << "},\n";
    f << "  \"ram\": {\"min\": " << ram_min << ", \"avg\": " << ram_avg << ", \"peak\": " << ram_max << "},\n";
    f << "  \"latency\": {\"min_ms\": " << latency_min_ << ", \"avg_ms\": " << lat_avg << ", \"peak_ms\": " << latency_max_ << ", \"count\": " << latency_count_ << "},\n";
    f << "  \"resource_samples\": [\n";
    for (size_t i = 0; i < resource_samples_.size(); ++i) {
        const auto& s = resource_samples_[i];
        f << "    {\"elapsed_ms\": " << s.elapsed_ms << ", \"cpu_percent\": " << s.cpu_percent << ", \"ram_mb\": " << s.ram_mb << "}";
        f << (i+1 < resource_samples_.size() ? ",\n" : "\n");
    }
    f << "  ]\n}\n";
}

void ExperimentMonitor::writeRocThresholds(const std::string& filename, const std::vector<float>& thresholds,
                                           const std::vector<float>& scores, const std::vector<int>& labels) {
    if (thresholds.empty()) return;
    std::ofstream f(filename);
    if (!f.is_open()) return;
    f << std::fixed << std::setprecision(6);
    f << "{\n  \"timestamp\": \"" << run_name_ << "\",\n";
    f << "  \"thresholds_tested\": " << thresholds.size() << ",\n";
    f << "  \"thresholds\": [\n";
    for (size_t i = 0; i < thresholds.size(); ++i) {
        float th = thresholds[i];
        int tp=0, fp=0, tn=0, fn=0;
        for (size_t j = 0; j < scores.size() && j < labels.size(); ++j) {
            bool pred = scores[j] > th; bool actual = labels[j] > 0;
            if (pred && actual) tp++; else if (pred) fp++;
            else if (!pred && !actual) tn++; else fn++;
        }
        double prec = (tp+fp) ? (double)tp/(tp+fp) : 0.0;
        double rec  = (tp+fn) ? (double)tp/(tp+fn) : 0.0;
        double f1   = (prec+rec) ? 2.0*(prec*rec)/(prec+rec) : 0.0;
        double acc  = scores.size() ? (double)(tp+tn)/scores.size() : 0.0;
        double spec = (tn+fp) ? (double)tn/(tn+fp) : 0.0;
        f << "    {\"threshold\": " << th << ", \"sensitivity\": " << rec
          << ", \"specificity\": " << spec << ", \"f1\": " << f1
          << ", \"precision\": " << prec << ", \"recall\": " << rec << ", \"accuracy\": " << acc << "}";
        f << (i+1 < thresholds.size() ? ",\n" : "\n");
    }
    f << "  ]\n}\n";
}

void ExperimentMonitor::writePerformanceMetrics(const std::string& filename,
                                               const std::string& timestamp,
                                               size_t thresholds_tested,
                                               float best_threshold,
                                               float best_f1, float best_precision, float best_recall, float best_accuracy,
                                               float avg_f1, float avg_precision, float avg_recall, float avg_accuracy) {
    std::ofstream f(filename);
    if (!f.is_open()) return;
    f << std::fixed << std::setprecision(6);
    f << "{\n  \"timestamp\": \"" << timestamp << "\",\n";
    f << "  \"thresholds_tested\": " << thresholds_tested << ",\n";
    f << "  \"best\": {\n    \"threshold\": " << best_threshold << ",\n";
    f << "    \"f1\": " << best_f1 << ",\n    \"precision\": " << best_precision << ",\n";
    f << "    \"recall\": " << best_recall << ",\n    \"accuracy\": " << best_accuracy << "\n  },\n";
    f << "  \"average\": {\n    \"f1\": " << avg_f1 << ",\n    \"precision\": " << avg_precision << ",\n";
    f << "    \"recall\": " << avg_recall << ",\n    \"accuracy\": " << avg_accuracy << "\n  }\n}\n";
}

} // namespace htm_swat
