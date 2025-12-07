# Dockerfile for HTM SWAT C++ Implementation
# This containerizes the build and runtime environment

FROM ubuntu:22.04

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install system dependencies required for building htm.core and our project
RUN apt-get update && apt-get install -y \
    build-essential \
    git \
    python3 \
    python3-pip \
    python3-venv \
    python3-dev \
    libboost-all-dev \
    pkg-config \
    wget \
    ca-certificates \
    gnupg \
    lsb-release \
    && rm -rf /var/lib/apt/lists/*

# Install CMake 3.28+ from Kitware APT repository (handles architecture automatically)
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | tee /etc/apt/trusted.gpg.d/kitware.gpg >/dev/null && \
    echo "deb https://apt.kitware.com/ubuntu/ $(lsb_release -cs) main" | tee /etc/apt/sources.list.d/kitware.list >/dev/null && \
    apt-get update && \
    apt-get install -y cmake && \
    rm -rf /var/lib/apt/lists/* && \
    cmake --version

# Set working directory
WORKDIR /workspace

# Copy project files (excluding build artifacts)
COPY scripts/ ./scripts/
COPY config/ ./config/
COPY include/ ./include/
COPY src/ ./src/
COPY CMakeLists.txt ./
COPY README.md ./

# Make installation scripts executable and fix line endings (Windows compatibility)
RUN find scripts/ -name "*.sh" -type f -exec chmod +x {} \; && \
    find scripts/ -name "*.sh" -type f -exec sed -i 's/\r$//' {} \;

# Install htm.core library (this takes ~10-15 minutes)
# This builds the C++ library we need
RUN ./scripts/install_htm_core.sh ../htm.core || \
    (echo "HTM.core installation failed. Make sure the script exists and is correct." && exit 1)

# Set environment variables for htm.core
# Note: install script installs to ../htm.core (which becomes /htm.core from /workspace)
ENV HTM_CORE_ROOT=/htm.core/build/Release
ENV CMAKE_PREFIX_PATH=/htm.core/build/Release:${CMAKE_PREFIX_PATH}

# Build the project
RUN mkdir -p build && \
    cd build && \
    cmake .. && \
    make -j$(nproc)

# Create a volume mount point for data
VOLUME ["/workspace/data"]

# Default command - run the executable
CMD ["./build/htm_swat"]

