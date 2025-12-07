# Installation Scripts

## install_htm_core.sh

Automates the installation of the HTM.core C++ library.

### What it does:

1. **Checks prerequisites**: Verifies git, python3, and cmake are installed
2. **Clones repository**: Downloads htm.core from GitHub (if not already present)
3. **Creates Python environment**: Sets up a virtual environment
4. **Builds library**: Runs htm.core's official installer (compiles C++ code)
5. **Verifies installation**: Checks that library and headers were created
6. **Provides instructions**: Shows how to use the installed library

### Usage:

```bash
# From project root
./scripts/install_htm_core.sh

# Or specify custom installation directory
./scripts/install_htm_core.sh /path/to/htm.core
```

### Default Location:

By default, installs to: `../htm.core/build/Release` (relative to project root)

### What gets installed:

- **Library**: `htm.core/build/Release/lib/libhtm_core.a` (static library)
- **Headers**: `htm.core/build/Release/include/htm/` (C++ header files)
- **CMake config**: (if available) for easy CMake integration

### Time Required:

- First run: ~10-15 minutes (clones repo and compiles)
- Subsequent runs: ~1 second (detects existing installation)

### Troubleshooting:

**Issue**: "git is not installed"
```bash
# Ubuntu/Debian
sudo apt-get install git

# macOS
brew install git
```

**Issue**: "python3 is not installed"
```bash
# Ubuntu/Debian
sudo apt-get install python3 python3-venv python3-dev

# macOS (usually pre-installed)
python3 --version
```

**Issue**: Build fails with Boost errors
```bash
# Ubuntu/Debian
sudo apt-get install libboost-all-dev

# macOS
brew install boost
```

**Issue**: Installation already exists
```bash
# To rebuild, remove the directory first
rm -rf ../htm.core
./scripts/install_htm_core.sh
```

### Environment Variables:

After installation, set these for your build:

```bash
export HTM_CORE_ROOT=/path/to/htm.core/build/Release
export CMAKE_PREFIX_PATH=$HTM_CORE_ROOT:$CMAKE_PREFIX_PATH
```

## test_docker.sh

Tests the Dockerfile build process.

### Usage:

```bash
./scripts/test_docker.sh
```

**Note**: Requires Docker to be installed and running.

