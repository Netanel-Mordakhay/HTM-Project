# HTM SWAT C++ Implementation

C++ version of the HTM algorithm from the Python `quickstart-swat.py` script.

## What's Done So Far

I've set up the basic project structure and got HTM.core library working. Right now the code just runs some tests to make sure everything is installed correctly. The actual HTM algorithm still needs to be implemented.

**What works:**

- Project structure is set up
- HTM.core library is installed and linked
- Build system (CMake) works
- Test program runs and verifies the library works
- Docker build works (recommended way to run)

**What's next:**

- Implement the actual HTM algorithm
- Add feature encoding
- Build the pyramid structure
- Process data and calculate anomaly scores

## Quick Start (Docker - Recommended)

The easiest way to build and run is using Docker. It handles all dependencies automatically.

### Prerequisites

You only need Docker installed:

- **Mac**: Install [Docker Desktop](https://www.docker.com/products/docker-desktop)
- **Linux**: `sudo apt-get install docker.io` (or similar)
- **Windows**: Install [Docker Desktop](https://www.docker.com/products/docker-desktop)

### Build and Run

```bash
cd cppproject
docker-compose up --build
```

**First time:** This takes ~15-20 minutes (builds HTM.core library inside Docker)  
**After that:** Runs in seconds (uses cached image)

### What Happens

1. Docker builds the image (installs dependencies, builds HTM.core, builds your project)
2. Creates a container and runs the program
3. Shows test results
4. Container exits when done

### Run Again

Just run the same command:

```bash
docker-compose up
```

It will use the cached image, so it's fast.

### Expected Output

You should see:

```
========================================
HTM SWAT C++ Implementation
========================================

Testing HTM.core library...
...
Tests passed: 5/5
✅ HTM.core library is working correctly!
```

## Project Structure

Here's what each file and folder does:

```
cppproject/
├── CMakeLists.txt          # Build configuration - tells CMake how to compile everything
├── README.md               # This file
│
├── include/                # Header files (.hpp) - class declarations
│   ├── config.hpp          # Config loading (not implemented yet)
│   ├── encoder.hpp         # Feature encoder class (not implemented yet)
│   ├── htm_module.hpp      # Single HTM module (SP + TM) class
│   ├── htm_pyramid.hpp     # Pyramid structure class
│   └── utils.hpp           # Helper functions for SDRs, file I/O
│
├── src/                    # Source files (.cpp) - actual implementation
│   ├── main.cpp            # Entry point - currently runs HTM.core tests
│   ├── config.cpp          # Config loading implementation (empty)
│   ├── encoder.cpp          # Encoder implementation (empty)
│   ├── htm_module.cpp      # HTM module implementation (empty)
│   ├── htm_pyramid.cpp     # Pyramid implementation (empty)
│   └── utils.cpp           # Utility functions (empty)
│
├── config/                 # Configuration files (copied from Python version)
│   ├── data/
│   │   └── config--swat.yaml    # Feature definitions for SWAT dataset
│   └── model/
│       └── config--model_default.yaml  # HTM parameters (SP, TM, encoders)
│
├── scripts/                # Helper scripts
│   ├── install_htm_core.sh # Installs HTM.core library (used by Docker)
│   └── test_docker.sh      # Tests Docker build
│
├── data/                   # Put your dataset CSV files here
│
├── build/                  # Build output (created when you run cmake/make)
│   └── htm_swat            # Compiled executable
│
├── Dockerfile              # Docker container setup
├── docker-compose.yml      # Docker Compose config
└── .dockerignore           # Files to ignore in Docker builds
```

**What each part does:**

- **include/**: Header files that declare classes and functions. These are like blueprints.
- **src/**: Implementation files. Right now most are empty placeholders except `main.cpp` which has tests.
- **config/**: YAML files with settings. Same as the Python version uses.
- **scripts/**: Helper scripts. The install script is used by Docker automatically.
- **data/**: Where you put your CSV dataset files.
- **build/**: Generated when you compile. Contains the executable.
- **CMakeLists.txt**: Tells the build system how to compile everything and where to find libraries.

## Local Build (Optional)

If you prefer to build locally instead of using Docker:

### Prerequisites

- C++17 compiler (GCC 8+, Clang 8+, or MSVC 2019+)
- CMake 3.24+ (required for HTM.core)
- Python 3.7+ (for installing htm.core)
- Git
- Boost libraries

### Install HTM.core

```bash
chmod +x scripts/install_htm_core.sh
./scripts/install_htm_core.sh
```

This takes ~10-15 minutes.

### Build

```bash
mkdir build
cd build
cmake ..
make
```


### Run
**Note: Must run the htm_swat docker file from the main "cppproject" directory, so it can resolve the relatie paths for the configs** 

Create Results Directory(make sure you are in the cppproject DIR):  

`mkdir -p results`  

Run the image  
```bash
./build/htm_swat
```
everytime you modify the code, rebuild the image:

`cd /home/abed/final-project/HTM-Project/cppproject && cmake --build build --target htm_swat`
## Experiment Tracking (branch: `batch-load-abed`)

Every run automatically profiles itself and saves structured results. No extra flags needed — the profiling starts at launch and writes files when the run completes.

### New Files Added

| File | Purpose |
|------|---------|
| `include/experiment_utils.hpp` | `ExperimentMonitor` class declaration |
| `src/experiment_utils.cpp` | Implementation: CPU/RAM sampler, latency recorder, JSON writers |

`CMakeLists.txt` was updated to include both files in the build.

### What Gets Measured

**CPU & RAM** — a background thread reads `/proc/self/stat` (CPU ticks) and `/proc/self/status` (VmRSS) every 2 seconds throughout the entire run. Results are stored as a time series and summarized as min/avg/peak.

**Timing events** — named checkpoints record elapsed time and delta from the previous checkpoint:

| Event | What it marks |
|-------|---------------|
| `run_start` | Process launch |
| `configs_loaded` | YAML config parsing done |
| `data_load_complete` | Streamer ready, row count known |
| `pyramid_built` | All 26 HTM modules constructed |
| `pyramid_run_complete` | All rows processed |
| `saving_results` | File I/O begins |

**Per-row latency** — `htm_pyramid.cpp` wraps each row in the run loop with `std::chrono::steady_clock` and calls `ExperimentMonitor::instance().recordRowLatency(ms)`. Min, average, and peak latency are reported.

### Output Files

Each run creates a timestamped experiment folder alongside the existing CSV/TXT outputs:

```
results/
├── anomaly_scores_<timestamp>.csv              # Score per row
├── metrics/
│   └── cpp_metrics_<timestamp>.txt            # Human-readable best/avg metrics
└── experiments_<branch>_<timestamp>/
    ├── model_efficiency_metrics.json           # CPU%, RAM MB, timing events, latency
    ├── model_performance_metrics.json          # F1, precision, recall, accuracy, threshold
    └── roc_thresholds.json                     # Full threshold sweep (101 points)
```

**`model_efficiency_metrics.json` structure:**
```json
{
  "total_runtime_ms": 626154,
  "timing_events": [ { "name": "...", "elapsed_ms": ..., "delta_ms": ... } ],
  "cpu":     { "min": 4.87, "avg": 7.20, "peak": 11.49 },
  "ram":     { "min": 1143.9, "avg": 1324.6, "peak": 1329.1 },
  "latency": { "min_ms": 2.8, "avg_ms": 6.2, "peak_ms": 92.2, "count": 100000 },
  "resource_samples": [ ... ]
}
```

**`model_performance_metrics.json` structure:**
```json
{
  "thresholds_tested": 101,
  "best":    { "threshold": 0.97, "f1": 0.4478, "precision": 0.459, "recall": 0.437, "accuracy": 0.788 },
  "average": { "f1": 0.254, "precision": 0.159, "recall": 0.857, "accuracy": 0.332 }
}
```

### Branch-Aware Folder Naming

The experiment folder name includes the git branch so runs from different branches are never mixed up:

```
results/experiments_batch-load-abed_20260425_144618_730/
```

Branch name is resolved at runtime in this priority order:
1. `GIT_BRANCH` environment variable (set in `docker-compose.yml`)
2. `.git/HEAD` file (works for local builds)
3. Falls back to `"unknown"`

`docker-compose.yml` has `GIT_BRANCH=batch-load-abed` pre-set. Update this value whenever you switch branches for Docker runs.

### Changes to Existing Files

- **`src/main.cpp`** — generates a single timestamp at startup shared across all output filenames; calls `monitor.start()`, adds timing events at each major step, calls `monitor.stop()` and writes all JSON files before exit.
- **`src/htm_pyramid.cpp`** — added `#include "experiment_utils.hpp"` and per-row latency timing in the run loop.
- **`docker-compose.yml`** — added `GIT_BRANCH=batch-load-abed` to the environment section.

## Troubleshooting

**Docker build fails:**

- Make sure Docker Desktop is running
- Check you have enough disk space (~5GB)
- Try: `docker-compose build --no-cache` to rebuild from scratch

**HTM.core not found (local build):**

```bash
export HTM_CORE_ROOT=$(pwd)/../htm.core/build/Release
sudo apt-get install -y libyaml-cpp-dev
cd build
cmake -Dyaml-cpp_DIR=/usr/lib/x86_64-linux-gnu/cmake/yaml-cpp ..
make -j$(nproc)
```
Should get the output `[100%] Built target htm_swat`
**CMake version too old:**

- Docker: Should work automatically (uses CMake 3.28+)
- Local: Install CMake 3.24+ from https://cmake.org/download/

## What the Code Does Now

The `main.cpp` file runs tests to verify HTM.core is working:

- Creates SDR objects
- Creates SpatialPooler and TemporalMemory
- Tests basic operations
- Runs a few learning iterations

If you see "Tests passed: 5/5", you're good to go.

## Next Steps

1. Implement FeatureEncoder (encode data into SDRs)
2. Implement HTMModule (SP + TM algorithm)
3. Implement HTMPyramid (hierarchical structure)
4. Add data loading and processing
5. Calculate anomaly scores

## Configuration

Uses the same config files as the Python version:

- `config/data/config--swat.yaml` - Feature definitions
- `config/model/config--model_default.yaml` - HTM parameters

## Docker Details

See `DOCKER_README.md` for more information about the Docker setup.
