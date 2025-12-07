#!/bin/bash
# HTM.core Installation Script
# This script automates the installation of HTM.core library
# It follows the official htm.core installation process

set -e  # Exit on any error

echo "=== HTM.core Installation Script ==="
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_step() {
    echo -e "${BLUE}[STEP]${NC} $1"
}

# Check if running as root (not recommended)
if [[ $EUID -eq 0 ]]; then
   print_warning "Running as root. This is not recommended but will continue..."
fi

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Check prerequisites
print_step "Checking prerequisites..."

MISSING_DEPS=()

# Check for git
if ! command_exists git; then
    MISSING_DEPS+=("git")
    print_error "git is not installed"
else
    print_status "✓ git found: $(git --version | cut -d' ' -f3)"
fi

# Check for python3
if ! command_exists python3; then
    MISSING_DEPS+=("python3")
    print_error "python3 is not installed"
else
    print_status "✓ python3 found: $(python3 --version | cut -d' ' -f2)"
fi

# Check for cmake
if ! command_exists cmake; then
    MISSING_DEPS+=("cmake")
    print_warning "cmake is not installed (may be needed for direct builds)"
else
    print_status "✓ cmake found: $(cmake --version | head -n1 | cut -d' ' -f3)"
fi

# If missing dependencies, provide instructions
if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
    print_error "Missing required dependencies: ${MISSING_DEPS[*]}"
    echo ""
    echo "Please install missing dependencies:"
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        echo "  sudo apt-get update"
        echo "  sudo apt-get install -y ${MISSING_DEPS[*]} build-essential python3-venv python3-dev libboost-all-dev"
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        echo "  brew install ${MISSING_DEPS[*]}"
    else
        echo "  Install: ${MISSING_DEPS[*]}"
    fi
    exit 1
fi

# Determine installation directory
# Default: ../htm.core (relative to project root)
# Can be overridden with first argument
INSTALL_DIR="${1:-../htm.core}"

# Get absolute path
if [[ "$INSTALL_DIR" != /* ]]; then
    # Relative path - make it relative to script location
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
    INSTALL_DIR="$PROJECT_ROOT/$INSTALL_DIR"
fi

print_step "Installing HTM.core to: $INSTALL_DIR"
echo ""

# Check if HTM.core already exists and is built
if [ -d "$INSTALL_DIR" ]; then
    if [ -f "$INSTALL_DIR/build/Release/lib/libhtm_core.a" ] && \
       [ -f "$INSTALL_DIR/build/Release/include/htm/algorithms/SpatialPooler.hpp" ]; then
        print_status "HTM.core already built at: $INSTALL_DIR"
        echo ""
        echo "Library: $INSTALL_DIR/build/Release/lib/libhtm_core.a"
        echo "Include: $INSTALL_DIR/build/Release/include"
        echo ""
        echo "To rebuild, remove the directory first:"
        echo "  rm -rf $INSTALL_DIR"
        echo ""
        echo "Or set HTM_CORE_ROOT:"
        echo "  export HTM_CORE_ROOT=$INSTALL_DIR/build/Release"
        exit 0
    else
        print_warning "HTM.core directory exists but not fully built"
        echo "Will attempt to build..."
    fi
fi

# Clone HTM.core if it doesn't exist
if [ ! -d "$INSTALL_DIR" ]; then
    print_step "Cloning HTM.core repository..."
    print_status "This may take a few minutes..."
    git clone https://github.com/htm-community/htm.core.git "$INSTALL_DIR" || {
        print_error "Failed to clone htm.core repository"
        exit 1
    }
    print_status "✓ Repository cloned"
else
    print_status "Using existing repository at: $INSTALL_DIR"
fi

cd "$INSTALL_DIR"

# Check if htm_install.py exists
if [ ! -f "htm_install.py" ]; then
    print_error "htm_install.py not found in $INSTALL_DIR"
    print_error "This doesn't look like a valid htm.core repository"
    exit 1
fi

# Create virtual environment
print_step "Creating Python virtual environment..."
if [ -d ".venv" ]; then
    print_status "Virtual environment already exists, using it"
else
    python3 -m venv .venv || {
        print_error "Failed to create virtual environment"
        exit 1
    }
    print_status "✓ Virtual environment created"
fi

# Activate virtual environment
print_step "Activating virtual environment..."
source .venv/bin/activate || {
    print_error "Failed to activate virtual environment"
    exit 1
}
print_status "✓ Virtual environment activated"

# Upgrade pip (recommended)
print_step "Upgrading pip..."
python -m pip install --upgrade pip --quiet || {
    print_warning "Failed to upgrade pip, continuing anyway..."
}

# Install HTM.core
print_step "Building HTM.core (this may take 10-15 minutes)..."
print_status "This will compile the C++ library..."
echo ""

# Try to install htm.core
if python htm_install.py; then
    print_status "✓ HTM.core installation completed successfully!"
else
    print_error "HTM.core installation failed"
    echo ""
    print_warning "Trying to diagnose the issue..."
    
    # Check if it's a Boost library issue
    if python -c "import sys; sys.exit(0 if 'boost' in str(sys.exc_info()).lower() else 1)" 2>/dev/null; then
        print_warning "This might be a Boost library issue"
        echo "On Ubuntu/Debian, try: sudo apt-get install -y libboost-all-dev"
    fi
    
    echo ""
    print_error "Please check the error messages above"
    deactivate
    exit 1
fi

# Verify installation
print_step "Verifying installation..."

VERIFICATION_FAILED=0

# Check for library file
if [ -f "build/Release/lib/libhtm_core.a" ]; then
    print_status "✓ Library file found: build/Release/lib/libhtm_core.a"
    LIB_SIZE=$(du -h build/Release/lib/libhtm_core.a | cut -f1)
    print_status "  Library size: $LIB_SIZE"
else
    print_error "✗ Library file not found: build/Release/lib/libhtm_core.a"
    VERIFICATION_FAILED=1
fi

# Check for header files
if [ -f "build/Release/include/htm/algorithms/SpatialPooler.hpp" ]; then
    print_status "✓ Header files found: build/Release/include/htm/"
else
    print_error "✗ Header files not found: build/Release/include/htm/"
    VERIFICATION_FAILED=1
fi

# Check for TemporalMemory header
if [ -f "build/Release/include/htm/algorithms/TemporalMemory.hpp" ]; then
    print_status "✓ TemporalMemory header found"
else
    print_warning "⚠ TemporalMemory header not found (may not be critical)"
fi

# Check for SDR header
if [ -f "build/Release/include/htm/types/Sdr.hpp" ]; then
    print_status "✓ SDR header found"
else
    print_warning "⚠ SDR header not found (may not be critical)"
fi

if [ $VERIFICATION_FAILED -eq 1 ]; then
    print_error "Installation verification failed"
    deactivate
    exit 1
fi

# Deactivate virtual environment
deactivate

# Print success message
echo ""
print_status "=========================================="
print_status "HTM.core Installation Complete!"
print_status "=========================================="
echo ""
echo "Installation location: $(pwd)"
echo ""
echo "Library path: $(pwd)/build/Release/lib/libhtm_core.a"
echo "Include path: $(pwd)/build/Release/include"
echo ""
echo "To use HTM.core in your project, set these environment variables:"
echo ""
echo "  export HTM_CORE_ROOT=$(pwd)/build/Release"
echo "  export CMAKE_PREFIX_PATH=\$HTM_CORE_ROOT:\$CMAKE_PREFIX_PATH"
echo ""
echo "Or in your CMakeLists.txt, use:"
echo "  set(HTM_CORE_ROOT \"$(pwd)/build/Release\")"
echo ""
print_status "You can now build your C++ project!"
echo ""

