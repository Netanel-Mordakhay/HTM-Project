# config.cpp - Comprehensive Analysis

## Overview

**Purpose**: Configuration file parsing and loading for the HTM SWAT system

**Location**: `/home/abed/final-project/HTM-Project/cppproject/src/config.cpp`

**Language**: C++17

**Dependencies**:
- yaml-cpp library (YAML parsing)
- Standard library (maps, strings, exceptions)

**Key Responsibility**: Bridge between YAML configuration files and C++ runtime by parsing and providing configuration data to the rest of the pipeline.

---

## File Structure

The file contains **157 lines of code** organized into three main functions:

1. `loadDataConfig()` - Lines 11-56 (46 lines)
2. `loadModelConfig()` - Lines 58-157 (100 lines)
3. `loadConfig()` - Lines 159-161 (3 lines, deprecated)

---

## Function Analysis

### 1. loadDataConfig()

**Function Signature**:
```cpp
map<string, map<string, string>> loadDataConfig(const char* data_config_file)
```

**Purpose**: Parse feature definitions from the data configuration YAML file

**Input Parameters**:
- `data_config_file` (const char*): Path to data configuration YAML file (typically `config_model_default.yaml`)

**Return Value**:
- `map<string, map<string, string>>`: Nested map structure organized as:
  - **Outer key**: Feature name (e.g., "fit101", "lit101", "p101")
  - **Inner key**: Feature property (e.g., "type", "weight", "resolution", "timeOfDay")
  - **Value**: Configuration value as string

**Implementation Details**:

1. **YAML File Loading**:
   - Loads YAML file using `YAML::LoadFile()`
   - Extracts the `features:` key section from root
   - Iterates through each feature definition

2. **Feature Processing**:
   - For each feature in the YAML:
     - Creates a nested map entry `config[feature_name]`
     - Extracts all scalar properties:
       - `type` (string): Encoder type (e.g., "Scalar")
       - `weight` (double): Feature weight in ensemble
       - `resolution` (double): Encoder resolution parameter
     - Extracts array properties:
       - `timeOfDay` (sequence): Array of time-of-day values for seasonal patterns
       - Converts YAML arrays to comma-separated strings for C++ storage

3. **Data Type Handling**:
   - **Scalars**: Directly converted to strings and stored
     ```
     yaml_config["fit101"]["weight"] → "1.0" (stored as string)
     yaml_config["fit101"]["resolution"] → "0.5"
     ```
   - **Sequences (Arrays)**: Converted from YAML array format to comma-separated string
     ```
     yaml_config["fit101"]["timeOfDay"] = [1, 2, 3, 4, 5]
     →
     config["fit101"]["timeOfDay"] = "1,2,3,4,5"
     ```

4. **Error Handling**:
   - Wraps entire function in try-catch block
   - Catches `YAML::Exception` for YAML parsing errors
   - Catches `std::exception` as fallback for other C++ exceptions
   - Prints error message to stderr and returns empty config on failure

5. **Logging**:
   - Upon successful parsing, prints:
     ```
     ✓ Parsed X features from config_model_default.yaml
     ```

**Example Output** (for SWAT dataset with 42 features):
```
config["fit101"]["type"] = "Scalar"
config["fit101"]["weight"] = "1.0"
config["fit101"]["resolution"] = "0.5"
config["fit101"]["timeOfDay"] = "1,2,3,4,5,6,7,8,9,..."

config["lit101"]["type"] = "Scalar"
config["lit101"]["weight"] = "1.0"
config["lit101"]["resolution"] = "0.5"
... (40 more features)
```

**Design Pattern**: All values stored as **strings** for maximum flexibility. Type conversion happens downstream in the encoder initialization where actual integer/double values are needed.

---

### 2. loadModelConfig()

**Function Signature**:
```cpp
map<string, map<string, string>> loadModelConfig(const char* model_config_file)
```

**Purpose**: Parse model parameters from the model configuration YAML file

**Input Parameters**:
- `model_config_file` (const char*): Path to model configuration YAML file (typically `config_swat.yaml`)

**Return Value**:
- `map<string, map<string, string>>`: Nested map organized as:
  - **Outer key**: Configuration section name ("general", "encoders", "sp", "tm")
  - **Inner key**: Parameter name (e.g., "feature_merge_mode", "boostStrength")
  - **Value**: Parameter value as string

**Implementation Details**:

1. **Three-Section Parsing**:
   The function extracts three main sections from the YAML:

   **A. GENERAL Settings** (General model parameters)
   ```
   config["general"]["feature_merge_mode"] = "u"  (union)
   config["general"]["htm_merge_mode"] = "u"      (union)
   config["general"]["use_predictive"] = "no"
   config["general"]["learn_period"] = "10"
   config["general"]["anomaly_period"] = "100"
   config["general"]["thresholds"] = "0.1,0.95,0.01"
   ```

   **B. ENCODERS Settings** (Encoder parameters)
   ```
   config["encoders"]["bits"] = "2304"
   config["encoders"]["sparsity"] = "0.035"
   config["encoders"]["w"] = "80"
   config["encoders"]["resolution"] = "0.5"
   ```

   **C. MODELS Settings** (Spatial Pooler and Temporal Memory)
   ```
   // Spatial Pooler (SP) parameters:
   config["sp"]["num_columns"] = "1664"
   config["sp"]["cells_per_column"] = "16"
   config["sp"]["local_area_density"] = "0.019"
   config["sp"]["boost_strength"] = "1.2,1.2,1.2,1.2"  (per layer)
   config["sp"]["duty_cycle_period"] = "1000"
   config["sp"]["max_boost"] = "10.0"
   
   // Temporal Memory (TM) parameters:
   config["tm"]["cells_per_column"] = "16"
   config["tm"]["activation_threshold"] = "13"
   config["tm"]["learning_radius"] = "24"
   config["tm"]["initial_permanence"] = "0.21"
   config["tm"]["connected_permanence"] = "0.5"
   config["tm"]["min_threshold"] = "10"
   config["tm"]["permanence_increment"] = "0.1"
   config["tm"]["permanence_decrement"] = "0.1"
   config["tm"]["predicted_segment_decrement"] = "0.001"
   config["tm"]["max_segments_per_cell"] = "255"
   config["tm"]["max_synapses_per_segment"] = "255"
   ```

2. **Section-by-Section Loading**:
   ```cpp
   // For each section (general, encoders, models):
   const YAML::Node& section = root[section_name];
   
   // Iterate through all parameters in section
   for (const auto& param : section) {
       config[section_name][param.first.as<string>()] = param.second.as<string>();
   }
   ```

3. **Array Handling**:
   - When a parameter is an array in YAML (e.g., `[1.2, 1.2, 1.2, 1.2]`):
     - Converted to comma-separated string: `"1.2,1.2,1.2,1.2"`
     - Each layer in pyramid gets its own value from the array
     - Example: `config["sp"]["boostStrength"] = "1.2,1.2,1.2,1.2"` (4 values for 4 layers)

4. **String Storage Pattern**:
   - All values (scalars, arrays, booleans) stored as strings for flexibility
   - Type conversion happens downstream when values are used:
     - `stoi()` for integers (num_columns, cells_per_column)
     - `stof()` for floats (sparsity, boost_strength)
     - Direct string usage for categorical values (merge modes)

5. **Error Handling**:
   - Same two-level try-catch as loadDataConfig()
   - Catches YAML::Exception and std::exception
   - Returns empty config on failure

6. **Logging**:
   - Upon successful parsing, prints:
     ```
     ✓ Parsed model configuration from config_swat.yaml
     ```

**Data Structure Created**:
```
config = {
  "general" → {
    "feature_merge_mode": "u",
    "htm_merge_mode": "u",
    "use_predictive": "no",
    ...
  },
  "encoders" → {
    "bits": "2304",
    "sparsity": "0.035",
    ...
  },
  "sp" → {
    "num_columns": "1664",
    "boost_strength": "1.2,1.2,1.2,1.2",
    ...
  },
  "tm" → {
    "cells_per_column": "16",
    "activation_threshold": "13",
    ...
  }
}
```

---

### 3. loadConfig() [DEPRECATED]

**Function Signature**:
```cpp
map<string, string> loadConfig(const char* config_file)
```

**Status**: **Deprecated, marked for removal**

**Current Usage**: Unused in the pipeline

**Note**: This legacy function is present but not called in the main pipeline. Modern code uses `loadDataConfig()` and `loadModelConfig()` instead.

---

## Key Design Patterns

### 1. Nested Map Organization
```
map<string, map<string, string>>
├── Feature/Section Name (outer key)
    ├── Parameter Name (inner key)
        └── Value (string)
```

**Advantages**:
- Hierarchical organization matching YAML structure
- O(log n) lookup performance for both levels
- Natural representation of configuration hierarchy

### 2. String-Based Storage
- **All values stored as strings**, regardless of actual type
- **Why**: Maximum flexibility, deferred type conversion
- **Type Conversion**: Happens downstream where values are used via `stoi()`, `stof()`, etc.

```cpp
// Stored in config
config["sp"]["num_columns"] = "1664"

// Used later with type conversion
int num_cols = stoi(config["sp"]["num_columns"]);
```

### 3. Array-to-String Conversion
- YAML arrays converted to comma-separated format
- Simplifies C++ storage and transmission
- Values reconstructed into arrays when needed

```yaml
# YAML file
boost_strength: [1.2, 1.2, 1.2, 1.2]

# Stored in C++
config["sp"]["boost_strength"] = "1.2,1.2,1.2,1.2"

# Reconstructed when used
vector<float> boosts = parseCommaSeparated<float>(config["sp"]["boost_strength"]);
```

---

## Error Handling Mechanism

### Two-Level Try-Catch Strategy

```cpp
try {
    // YAML parsing operations
    YAML::Node root = YAML::LoadFile(file);
    // ... feature extraction ...
} 
catch (YAML::Exception& e) {
    // Specific handling for YAML parsing errors
    cerr << "YAML Error: " << e.what() << endl;
    return config;  // Return empty config
}
catch (std::exception& e) {
    // Fallback for other C++ exceptions
    cerr << "Error: " << e.what() << endl;
    return config;  // Return empty config
}
```

**Characteristics**:
- Specific exception handling for YAML errors
- Generic fallback for other C++ exceptions
- Returns empty config rather than throwing (graceful degradation)
- Prints meaningful error messages to stderr
- Allows program to continue (though with missing configuration)

**Error Scenarios Handled**:
1. File not found or not readable
2. Invalid YAML syntax
3. Missing required keys in YAML
4. Type conversion errors
5. Memory allocation failures

---

## Data Structures Created

### For Data Configuration

**Input**: YAML file with 42 feature definitions
```yaml
features:
  fit101:
    type: Scalar
    weight: 1.0
    resolution: 0.5
    timeOfDay: [1, 2, 3, ...]
  lit101:
    type: Scalar
    weight: 1.0
    resolution: 0.5
    # ... 40 more features
```

**Output**:
```cpp
map<string, map<string, string>> data_config = {
  "fit101" → {"type": "Scalar", "weight": "1.0", "resolution": "0.5", "timeOfDay": "1,2,3,..."},
  "lit101" → {"type": "Scalar", "weight": "1.0", "resolution": "0.5", "timeOfDay": "..."},
  ... (40 more features)
}
```

**Size**: ~42 features × 4-5 properties each = ~210 string key-value pairs

### For Model Configuration

**Input**: YAML file with 4 sections (general, encoders, sp, tm)

**Output**:
```cpp
map<string, map<string, string>> model_config = {
  "general" → {
    "feature_merge_mode": "u",
    "htm_merge_mode": "u",
    "use_predictive": "no",
    "learn_period": "10",
    "anomaly_period": "100",
    "thresholds": "0.1,0.95,0.01"
  },
  "encoders" → {
    "bits": "2304",
    "sparsity": "0.035",
    "w": "80",
    "resolution": "0.5"
  },
  "sp" → {
    "num_columns": "1664",
    "cells_per_column": "16",
    "local_area_density": "0.019",
    "boost_strength": "1.2,1.2,1.2,1.2",
    ... (8 more parameters)
  },
  "tm" → {
    "cells_per_column": "16",
    "activation_threshold": "13",
    ... (9 more parameters)
  }
}
```

**Size**: ~4 sections × 8-15 parameters each = ~45 key-value pairs

---

## Integration with Pipeline

### Initialization Sequence (main.cpp)

1. **Configuration Loading** (early in main)
   ```cpp
   auto data_config = loadDataConfig("config_model_default.yaml");
   auto model_config = loadModelConfig("config_swat.yaml");
   ```

2. **Encoder Initialization** (uses data_config)
   - Each of 16 L0 encoders reads feature parameters from data_config
   - Sets weight, resolution, and encoding parameters

3. **HTM Module Initialization** (uses model_config)
   - Spatial Pooler initialized with `config["sp"]` parameters
   - Temporal Memory initialized with `config["tm"]` parameters
   - Pyramid layers use `config["general"]` settings

4. **Pipeline Parameters** (from general settings)
   - Learn period: How many iterations before learning starts
   - Anomaly period: Window size for anomaly calculation
   - Thresholds: Range to search for optimal threshold
   - Merge modes: How to combine multi-layer predictions

### Data Flow

```
config_model_default.yaml
        ↓
   loadDataConfig()
        ↓
  data_config map
        ↓
   encoder initialization
        ↓
   individual feature encoders

config_swat.yaml
        ↓
   loadModelConfig()
        ↓
  model_config map
        ↓
   SP/TM initialization
        ↓
   HTM modules in pyramid
```

---

## Key Parameters Explained

### General Settings

| Parameter | Type | Default | Purpose |
|-----------|------|---------|---------|
| `feature_merge_mode` | string | "u" | Union (u) or concat (c) L0 SDRs |
| `htm_merge_mode` | string | "u" | Union (u) or concat (c) layer predictions |
| `use_predictive` | bool | no | Use predicted cells for anomaly |
| `learn_period` | int | 10 | Skip learning for first N timesteps |
| `anomaly_period` | int | 100 | Window size for anomaly calculation |
| `thresholds` | array | [0.1, 0.95, 0.01] | [min, max, step] for threshold search |

### Encoder Parameters

| Parameter | Type | Default | Purpose |
|-----------|------|---------|---------|
| `bits` | int | 2304 | SDR width (total bits) |
| `sparsity` | float | 0.035 | Fraction of bits to set (3.5%) |
| `w` | int | 80 | Number of active bits per value |
| `resolution` | float | 0.5 | Minimum change to trigger bit change |

### Spatial Pooler Parameters

| Parameter | Type | Default | Purpose |
|-----------|------|---------|---------|
| `num_columns` | int | 1664 | N active columns per layer |
| `cells_per_column` | int | 16 | Multiplied with columns for total cells |
| `local_area_density` | float | 0.019 | % of columns that can be active |
| `boost_strength` | float | 1.2 | How much to boost underactive columns |
| `duty_cycle_period` | int | 1000 | Window for calculating column activity |
| `max_boost` | float | 10.0 | Maximum boost factor |

### Temporal Memory Parameters

| Parameter | Type | Default | Purpose |
|-----------|------|---------|---------|
| `cells_per_column` | int | 16 | Cells per SP column for temporal context |
| `activation_threshold` | int | 13 | Min connected synapses to activate cell |
| `learning_radius` | int | 24 | Distance in cell space for learning |
| `initial_permanence` | float | 0.21 | Starting synaptic weight |
| `connected_permanence` | float | 0.5 | Permanence threshold for connection |
| `permanence_increment` | float | 0.1 | Amount to increment on learning |
| `permanence_decrement` | float | 0.1 | Amount to decrement on learning |

---

## Summary

**config.cpp Role**: Configuration bridge translating YAML definitions into C++ data structures

**Key Functions**:
- `loadDataConfig()`: 42 features → feature properties map
- `loadModelConfig()`: Model parameters → 4-section configuration map

**Design Strengths**:
- Hierarchical organization matching YAML structure
- Flexible string-based storage with deferred type conversion
- Robust error handling with graceful degradation
- Clean separation of data vs. model configuration

**Integration Point**: Core initialization function called early in main.cpp to provide all downstream components with required parameters

**Current Status**: Fully functional and actively used in the production pipeline
