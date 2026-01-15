#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>

namespace htm_swat {

struct ResourceSample {
    double elapsed_ms;
    double cpu_percent;
    double ram_mb;
};

struct TimingEvent {
    std::string name;
    double elapsed_ms;
    double delta_ms;
};

class ExperimentMonitor {
public:
    static ExperimentMonitor& instance();

    // start monitoring and create experiment directory with given timestamp
    void start(const std::string& run_name, int sample_interval_ms=2000);
    // stop monitor thread and finalize data
    void stop();

    // timing events
    void addTimingEvent(const std::string& name);

    // per-row latency (milliseconds)
    void recordRowLatency(double ms);

    // write ROC & performance JSONs
    void writeRocThresholds(const std::string& filename, const std::vector<float>& thresholds,
                            const std::vector<float>& scores, const std::vector<int>& labels);

    void writePerformanceMetrics(const std::string& filename,
                                 const std::string& timestamp,
                                 size_t thresholds_tested,
                                 float best_threshold,
                                 float best_f1, float best_precision, float best_recall, float best_accuracy,
                                 float avg_f1, float avg_precision, float avg_recall, float avg_accuracy);

    // write efficiency metrics JSON (samples, timings, latency summary)
    void writeEfficiencyMetrics(const std::string& filename);

    // experiment directory
    std::string experimentDir() const;

private:
    ExperimentMonitor();
    ~ExperimentMonitor();

    // non-copyable
    ExperimentMonitor(const ExperimentMonitor&) = delete;
    ExperimentMonitor& operator=(const ExperimentMonitor&) = delete;

    void samplerLoop();

    std::string run_name_;
    int sample_interval_ms_;
    std::atomic<bool> running_;
    std::mutex lock_;

    std::vector<ResourceSample> resource_samples_;
    std::vector<TimingEvent> timing_events_;

    // latency summary
    double latency_min_;
    double latency_max_;
    double latency_sum_;
    size_t latency_count_;

    // internal timestamps
    double start_time_ms_;

    // sampler thread
    std::thread sampler_thread_;
};

} // namespace htm_swat
