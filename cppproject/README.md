# HTM SWAT C++ Implementation

C++ version of the HTM algorithm from the Python `quickstart-swat.py` script.

## What's Done So Far

I've set up the basic project structure and got HTM.core library working. Right now the code just runs some tests to make sure everything is installed correctly. The actual HTM algorithm still needs to be implemented.

**What works:**

- Project structure is set up
- HTM.core library is installed and linked
- Build system (CMake) works
- Test program runs and verifies the library works

**What's next:**

- Implement the actual HTM algorithm
- Add feature encoding
- Build the pyramid structure
- Process data and calculate anomaly scores

## Quick Start

### 1. Install Dependencies

**Linux (Ubuntu/Debian):**

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake git python3 python3-pip python3-venv python3-dev libboost-all-dev
```

**macOS:**

```bash
brew install cmake git python3 boost
```

**Windows:** Use WSL2 and follow Linux instructions, or install Visual Studio with C++ tools.

### 2. Install HTM.core Library

This takes about 10-15 minutes. The script will clone and build the library for you.

```bash
chmod +x scripts/install_htm_core.sh
./scripts/install_htm_core.sh
```

It installs to `../htm.core/build/Release`. If it's already there, it will skip the installation.

### 3. Build the Project

```bash
mkdir build
cd build
cmake ..
make
```

### 4. Run

```bash
./htm_swat
```

You should see tests pass. If all 5 tests pass, everything is working.

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
│   ├── install_htm_core.sh # Installs HTM.core library (one-time setup)
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
- **scripts/**: Helper scripts. The install script is the important one - it sets up HTM.core.
- **data/**: Where you put your CSV dataset files.
- **build/**: Generated when you compile. Contains the executable.
- **CMakeLists.txt**: Tells the build system how to compile everything and where to find libraries.

## Troubleshooting

**HTM.core not found:**

```bash
export HTM_CORE_ROOT=$(pwd)/../htm.core/build/Release
cd build && cmake .. && make
```

**Boost not found:**

- Linux: `sudo apt-get install libboost-all-dev`
- macOS: `brew install boost`

**CMake errors:**

- Make sure CMake 3.15+ is installed: `cmake --version`
- Make sure you ran the HTM.core installation script first

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

## Docker (Optional)

If you want to use Docker instead:

```bash
docker build -t htm_swat .
docker run --rm -v $(pwd)/data:/workspace/data htm_swat
```

See `DOCKER_README.md` for more details.
