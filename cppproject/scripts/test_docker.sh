#!/bin/bash
# Test script for Dockerfile
# This verifies the Dockerfile can build successfully

set -e

echo "=== Testing Dockerfile ==="
echo ""

# Check if Docker is installed
if ! command -v docker &> /dev/null; then
    echo "❌ Docker is not installed. Please install Docker first."
    echo "   Visit: https://docs.docker.com/get-docker/"
    exit 1
fi

echo "✅ Docker is installed"
echo ""

# Check if Docker daemon is running
if ! docker info &> /dev/null; then
    echo "❌ Docker daemon is not running. Please start Docker."
    exit 1
fi

echo "✅ Docker daemon is running"
echo ""

# Check if required files exist
echo "Checking required files..."
REQUIRED_FILES=("Dockerfile" "CMakeLists.txt" "scripts/install_htm_core.sh")

for file in "${REQUIRED_FILES[@]}"; do
    if [ ! -f "$file" ]; then
        echo "⚠️  Warning: $file not found (this is expected if you haven't completed all steps yet)"
    else
        echo "✅ $file exists"
    fi
done

echo ""
echo "=== Docker Build Test ==="
echo "This will take 15-20 minutes on first run..."
echo ""

# Build the Docker image
if docker build -t htm_swat_test . 2>&1 | tee /tmp/docker_build.log; then
    echo ""
    echo "✅ Docker build successful!"
    echo ""
    echo "Testing image contents..."
    
    # Test 1: Check if htm.core is installed
    if docker run --rm htm_swat_test test -f /workspace/htm.core/build/Release/lib/libhtm_core.a; then
        echo "✅ htm.core library found"
    else
        echo "⚠️  htm.core library not found (may not be built yet)"
    fi
    
    # Test 2: Check if build directory exists
    if docker run --rm htm_swat_test test -d /workspace/build; then
        echo "✅ build directory exists"
    else
        echo "⚠️  build directory not found"
    fi
    
    # Test 3: List workspace contents
    echo ""
    echo "Workspace contents:"
    docker run --rm htm_swat_test ls -la /workspace/
    
    echo ""
    echo "=== Test Complete ==="
    echo "Image name: htm_swat_test"
    echo "To run: docker run --rm htm_swat_test"
    
else
    echo ""
    echo "❌ Docker build failed!"
    echo "Check the log above for errors."
    echo "Common issues:"
    echo "  - Missing CMakeLists.txt"
    echo "  - Missing install_htm_core.sh script"
    echo "  - Network issues downloading dependencies"
    exit 1
fi

