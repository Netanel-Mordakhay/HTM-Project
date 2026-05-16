# HTM SWAT C++ Implementation

C++ version of the HTM algorithm from the Python `quickstart-swat.py` script.

## Branch: `batch-load-qemu-threadpool`

**Goal:** Reduce runtime from ~4640 s to ≤400 s on QEMU Raspberry Pi 4B by replacing
per-layer `std::async` thread creation with a persistent thread pool.

### Problem (batch-load baseline)

The previous branch (`batch-load`) parallelised HTM modules within each layer using
`std::async(std::launch::async, ...)`. The C++ standard requires `std::launch::async`
to start a **new OS thread on every call** — threads are never reused. The call pattern
per data row was:

| Layer | Modules | Threads created |
|-------|---------|----------------|
| L0    | 16      | 4              |
| L1    | 6       | 4              |
| L2    | 3       | 3              |
| L3    | 1       | 1              |
| **Per row** | | **12** |

Over 100,000 rows this produced **1.2 million OS thread create/join cycles**. On QEMU
ARM emulation each cycle costs ~1–5 ms (vs ~10 µs on real hardware), accounting for the
majority of the 4640 s runtime. Average CPU was only 162% out of a theoretical 400% —
the remaining capacity was consumed by thread management overhead, not computation.

### Fix: persistent `ThreadPool`

A `ThreadPool` class is defined in `src/htm_pyramid.cpp` and owned by `HTMPyramid`.
It creates N worker threads **once** during `build()` and keeps them alive for the
entire `run()` call. Each `runLayer()` invocation submits tasks to the pool's work
queue instead of spawning new threads. The per-submission cost drops from ~1–5 ms to
~1 µs (queue push + condition variable signal).

**Files changed:**

- `include/htm_pyramid.hpp` — forward declaration of `ThreadPool`; added
  `std::unique_ptr<ThreadPool> thread_pool_` member.
- `src/htm_pyramid.cpp` — `ThreadPool` class definition; initialisation in `build()`;
  `runLayer()` now submits to the pool instead of calling `std::async`.

**Expected improvement on QEMU:**

```
4640 s  (batch-load, std::async — 1.2M thread create/join)
÷ 3–4   (thread pool eliminates churn)
≈ 1160–1550 s  on QEMU
```

Reaching ≤400 s additionally requires real RPi 4B hardware (÷4 QEMU emulation
overhead). On real hardware with the pool: ~290–390 s.

---

## What's Done So Far

- Project structure is set up
- HTM.core library is installed and linked
- Build system (CMake) works
- Full HTM pyramid runs end-to-end (SP + TM, 4 layers, 26 modules)
- Anomaly scores and F1 metrics are produced
- Data loaded via Apache Arrow batch read (`ReadTable`)
- Layer-level parallelism via persistent thread pool (this branch)

## What's Next

- Validate runtime improvement on QEMU with the thread pool
- Row-group streaming for parquet (replace `ReadTable` to cut peak RAM from ~1333 MB)

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
