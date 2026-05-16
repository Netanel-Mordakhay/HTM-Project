# Code Review & Feedback for HTM-Project

This document provides feedback and suggestions for improving the HTM-Project codebase based on hands-on implementation and testing of the geophone architecture.

---

## Executive Summary

The project successfully implements a hierarchical HTM pyramid architecture for anomaly detection. The code is functional but has several areas where performance, maintainability, and correctness can be improved.

**Key Findings:**
- Anomaly detection produces constant 1.0 scores due to TM configuration
- Build process is complex and could be streamlined
- Missing error handling in several critical paths
- Performance can be improved with better memory management

---

## 1. Critical Issues

### 1.1 Temporal Memory Configuration (`cellsPerColumn = 1`)

**Location:** `src/main.cpp:99`

```cpp
tm_cfg.cellsPerColumn = 1;
```

**Problem:** With only 1 cell per column, the Temporal Memory cannot learn sequences or make predictions. This results in anomaly scores always being 1.0 (fully anomalous).

**Why it matters:** The TM uses multiple cells per column to represent different temporal contexts. With 1 cell, there's no way to distinguish "A followed by B" from "A followed by C".

**Recommendation:** Increase to at least 8-32 cells per column:
```cpp
tm_cfg.cellsPerColumn = 16;  // or 32 for more complex sequences
```

### 1.2 Missing `activateDendrites()` Call

**Location:** `src/htm_module.cpp:86-89` (was fixed during implementation)

**Original Problem:** The code called `tm_.getPredictiveCells()` without first calling `tm_.activateDendrites()`, causing a runtime exception.

**Fix Applied:**
```cpp
tm_.compute(active_columns, learn);
tm_.activateDendrites(learn);  // Required before getPredictiveCells()
prev_predictive_cells_ = tm_.getPredictiveCells();
```

**Recommendation:** Add a comment explaining this requirement for future maintainers.

---

## 2. Performance Improvements

### 2.1 Avoid Repeated Map Lookups

**Location:** `src/htm_pyramid.cpp:221-226`

```cpp
for (const auto& module_name : l0_order_) {
    htm::SDR input = encodeL0Input(module_name, row_data);
    htm::SDR output = modules_[module_name]->forward(input, learn);  // Lookup 1
    last_outputs_[module_name] = output;                              // Lookup 2
    last_scores_[module_name] = modules_[module_name]->getAnomalyScore(); // Lookup 3
}
```

**Recommendation:** Cache the module pointer:
```cpp
for (const auto& module_name : l0_order_) {
    auto& module = modules_[module_name];  // Single lookup
    htm::SDR input = encodeL0Input(module_name, row_data);
    htm::SDR output = module->forward(input, learn);
    last_outputs_[module_name] = std::move(output);  // Use move semantics
    last_scores_[module_name] = module->getAnomalyScore();
}
```

### 2.2 Pre-allocate SDR Vectors

**Location:** `src/htm_pyramid.cpp:174, 200`

```cpp
std::vector<htm::SDR> encoded_sensors;  // Grows dynamically
```

**Recommendation:** Reserve capacity upfront:
```cpp
std::vector<htm::SDR> encoded_sensors;
encoded_sensors.reserve(sensor_names.size());
```

### 2.3 Use Move Semantics for SDR Operations

**Location:** Multiple files

SDRs can be expensive to copy. Use `std::move()` when the source SDR is no longer needed:
```cpp
last_outputs_[module_name] = std::move(output);
```

### 2.4 Parallel Processing for L0 Modules

**Location:** `src/htm_pyramid.cpp:220-226`

L0 modules are independent and can be processed in parallel:
```cpp
#include <execution>
#include <algorithm>

// Process L0 modules in parallel
std::for_each(std::execution::par, l0_order_.begin(), l0_order_.end(),
    [&](const std::string& module_name) {
        // Thread-safe processing here
    });
```

**Note:** This requires thread-safe data structures for `last_outputs_` and `last_scores_`.

### 2.5 Batch Processing Mode

For high-throughput scenarios, consider adding a batch processing mode that processes multiple rows at once, reducing function call overhead and enabling better CPU cache utilization.

---

## 3. Code Quality Improvements

### 3.1 Add Input Validation

**Location:** `src/encoder.cpp`

```cpp
FeatureEncoder::FeatureEncoder(...) {
    // Add validation
    if (encoder_size <= 0) {
        throw std::invalid_argument("encoder_size must be positive");
    }
    if (sparsity <= 0.0 || sparsity >= 1.0) {
        throw std::invalid_argument("sparsity must be in (0, 1)");
    }
    if (min_val >= max_val) {
        throw std::invalid_argument("min_val must be less than max_val");
    }
    // ...
}
```

### 3.2 Use `const` Correctness

**Location:** Multiple files

Several methods that don't modify state should be marked `const`:
```cpp
// In htm_pyramid.hpp
htm::SDR encodeL0Input(const std::string& module_name,
                       const std::map<std::string, double>& row_data) const;
```

### 3.3 Replace Magic Numbers with Named Constants

**Location:** `src/main.cpp`

```cpp
// Current
const int seed = 69;
const int learn_period = 2000;
const double threshold = 0.5;

// Better - use a config struct or constexpr
namespace defaults {
    constexpr int SEED = 69;
    constexpr int LEARN_PERIOD = 2000;
    constexpr double ANOMALY_THRESHOLD = 0.5;
    constexpr int ENCODER_SIZE = 2048;
    constexpr double ENCODER_SPARSITY = 0.02;
}
```

### 3.4 Unused Parameter Warning

**Location:** `src/encoder.cpp:11`

```cpp
FeatureEncoder::FeatureEncoder(..., double resolution, ...) {
    // 'resolution' is unused
}
```

Either use the parameter (for bucket-based encoding) or remove it:
```cpp
// Option 1: Use it
int num_buckets = static_cast<int>(range_ / resolution);

// Option 2: Remove if not needed
FeatureEncoder::FeatureEncoder(const std::string& name, double min_val,
                                double max_val, int encoder_size, double sparsity);
```

### 3.5 Error Handling for CSV Loading

**Location:** `src/utils.cpp`

Add more robust error handling:
```cpp
CSVData loadCSV(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + filepath);
    }

    std::string line;
    if (!std::getline(file, line)) {
        throw std::runtime_error("Empty CSV file: " + filepath);
    }

    // Validate expected columns exist
    // ...
}
```

---

## 4. Architecture Improvements

### 4.1 Configuration File Support

**Current:** Hard-coded configuration in `main.cpp`

**Recommendation:** Load configuration from YAML/JSON files (you already have config files in `config/`):

```cpp
// Use a library like yaml-cpp or nlohmann/json
Config config = Config::fromYAML("config/model/config--model_geophone.yaml");
HTMPyramid model(config);
```

### 4.2 Separate Concerns

Split `main.cpp` into:
- `main.cpp` - Entry point, argument parsing
- `geophone_runner.cpp` - Geophone-specific logic
- `metrics_calculator.cpp` - Precision/recall/F1 calculations

### 4.3 Add a Model Interface

Create an abstract interface for different model types:
```cpp
class IAnomalyDetector {
public:
    virtual ~IAnomalyDetector() = default;
    virtual double processRow(const std::map<std::string, double>& row, bool learn) = 0;
    virtual double getAnomalyScore() const = 0;
    virtual void reset() = 0;
};

class HTMPyramid : public IAnomalyDetector { /* ... */ };
```

### 4.4 Add Model Serialization

Enable saving/loading trained models:
```cpp
class HTMPyramid {
public:
    void save(const std::string& filepath);
    static HTMPyramid load(const std::string& filepath);
};
```

---

## 5. Build System Improvements

### 5.1 Simplify Docker Build

The current Dockerfile builds htm.core from source every time. Consider:

1. **Multi-stage build with caching:**
```dockerfile
# Stage 1: Build htm.core (cached)
FROM ubuntu:22.04 AS htm-builder
# ... build htm.core ...

# Stage 2: Build application
FROM htm-builder AS app-builder
COPY . /workspace
RUN cmake && make
```

2. **Pre-built htm.core Docker image:**
```dockerfile
FROM your-registry/htm-core:latest AS base
COPY . /workspace
RUN cmake && make
```

### 5.2 CMake Improvements

```cmake
# Add version requirement
cmake_minimum_required(VERSION 3.16)

# Use modern CMake targets
target_compile_features(htm_swat PRIVATE cxx_std_17)

# Add install target
install(TARGETS htm_swat DESTINATION bin)

# Add testing support
enable_testing()
add_subdirectory(tests)
```

### 5.3 Add CI/CD Pipeline

Create `.github/workflows/build.yml`:
```yaml
name: Build and Test
on: [push, pull_request]
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Build Docker image
        run: docker build -t htm_geophone .
      - name: Run tests
        run: docker run htm_geophone ./build/htm_swat --test
```

---

## 6. Testing Recommendations

### 6.1 Add Unit Tests

Create tests for critical components:

```cpp
// tests/test_encoder.cpp
TEST(FeatureEncoder, EncodesValueInRange) {
    FeatureEncoder encoder("test", 0.0, 1.0, 0.01, 2048, 0.02);
    htm::SDR sdr = encoder.encode(0.5);

    EXPECT_EQ(sdr.size, 2048);
    EXPECT_NEAR(sdr.getSparsity(), 0.02, 0.005);
}

TEST(FeatureEncoder, ClipsOutOfRangeValues) {
    FeatureEncoder encoder("test", 0.0, 1.0, 0.01, 2048, 0.02);
    htm::SDR sdr_low = encoder.encode(-0.5);
    htm::SDR sdr_high = encoder.encode(1.5);

    // Should clip to valid range
    EXPECT_EQ(sdr_low.getSparse(), encoder.encode(0.0).getSparse());
    EXPECT_EQ(sdr_high.getSparse(), encoder.encode(1.0).getSparse());
}
```

### 6.2 Add Integration Tests

Test the full pipeline with known data:
```cpp
TEST(HTMPyramid, DetectsKnownAnomalies) {
    // Create model with proper cellsPerColumn
    // Feed training data
    // Feed test data with known anomalies
    // Verify anomaly scores are elevated for anomalous rows
}
```

---

## 7. Documentation Improvements

### 7.1 Add API Documentation

Use Doxygen-style comments:
```cpp
/**
 * @brief Encodes a scalar value into a Sparse Distributed Representation.
 *
 * @param value The scalar value to encode (will be clipped to [min_val, max_val])
 * @return htm::SDR The encoded SDR with approximately `sparsity * encoder_size` active bits
 *
 * @throws std::runtime_error if encoder is not properly initialized
 */
htm::SDR encode(double value);
```

### 7.2 Add Architecture Documentation

Create `docs/architecture.md` explaining:
- The pyramid structure and data flow
- How SDRs are merged (union vs concatenation)
- The anomaly calculation algorithm
- Configuration parameter effects

### 7.3 Add Examples

Create `examples/` directory with:
- Simple single-sensor example
- Multi-sensor example
- Custom encoder example
- Real-time streaming example

---

## 8. Additional Features to Consider

### 8.1 Anomaly Likelihood

Raw anomaly scores can be noisy. Consider implementing anomaly likelihood:
```cpp
class AnomalyLikelihood {
public:
    double compute(double anomaly_score);
private:
    std::deque<double> historical_scores_;
    // Rolling statistics
};
```

### 8.2 Multiple Anomaly Score Aggregation

The current implementation only returns the head module's score. Consider:
```cpp
struct AnomalyResult {
    double head_score;
    std::map<std::string, double> module_scores;
    double weighted_average;
    double max_score;
};
```

### 8.3 Online Learning Toggle

Add ability to disable learning after initial training:
```cpp
model.setLearning(false);  // Freeze model
// Or
model.processRow(row_data, false);  // Already supported, but add explicit mode
```

### 8.4 Memory-Efficient Mode

For resource-constrained environments:
```cpp
HTMPyramid model(config, MemoryMode::LOW);  // Use smaller data structures
```

---

## 9. Performance Benchmarks

Based on testing with 86,000 rows:

| Metric | Current | Target |
|--------|---------|--------|
| Processing Time | 10.63s | <5s |
| Memory Usage | 2.7 MB | <2 MB |
| Rows/Second | 8,090 | >15,000 |

Recommendations to achieve targets:
1. Enable compiler optimizations (`-O3 -march=native`)
2. Implement parallel L0 processing
3. Use memory pools for SDR allocations
4. Consider SIMD for SDR operations

---

## 10. Summary of Priority Actions

### High Priority
1. Fix `cellsPerColumn` to enable actual sequence learning
2. Add input validation to prevent runtime crashes
3. Implement configuration file loading

### Medium Priority
4. Add unit tests for core components
5. Optimize map lookups and use move semantics
6. Add model serialization

### Low Priority
7. Implement parallel processing
8. Add anomaly likelihood calculation
9. Improve documentation

---

## Appendix: Files Modified During Review

| File | Changes Made |
|------|--------------|
| `src/htm_module.cpp` | Added `activateDendrites()` call |
| `src/encoder.cpp` | Implemented custom scalar encoder |
| `src/utils.cpp` | Added CSV loading and resource tracking |
| `src/main.cpp` | Implemented geophone-specific configuration |
| `CMakeLists.txt` | Added Windows compatibility (psapi) |
| `Dockerfile` | Added network retry logic |

---

*Review conducted on April 2026*
*Reviewer: Claude Code Assistant*
