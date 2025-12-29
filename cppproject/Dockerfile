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
    libyaml-cpp-dev \
    pkg-config \
    wget \
    ca-certificates \
    gnupg \
    lsb-release \
    && rm -rf /var/lib/apt/lists/*

# Try to install Apache Arrow/Parquet (optional - for parquet file support)
# Note: Arrow packages may not be available in default Ubuntu repos
# If not available, the code will fall back to CSV loading
RUN apt-get update && \
    (apt-get install -y libarrow-dev libparquet-dev 2>/dev/null || \
     echo "Arrow packages not available - will use CSV fallback") && \
    rm -rf /var/lib/apt/lists/*

# Install CMake 3.28+ from Kitware APT repository (handles architecture automatically)
RUN wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | tee /etc/apt/trusted.gpg.d/kitware.gpg >/dev/null && \
    echo "deb https://apt.kitware.com/ubuntu/ $(lsb_release -cs) main" | tee /etc/apt/sources.list.d/kitware.list >/dev/null && \
    apt-get update && \
    apt-get install -y cmake && \
    rm -rf /var/lib/apt/lists/* && \
    cmake --version

# Set working directory
WORKDIR /workspace

# Copy ONLY the installation script first (needed for htm.core)
# This is a small file that rarely changes, so it won't invalidate cache often
COPY scripts/install_htm_core.sh ./scripts/
RUN chmod +x scripts/install_htm_core.sh && \
    sed -i 's/\r$//' scripts/install_htm_core.sh

# Install htm.core library FIRST (before copying source files)
# This takes ~10-15 minutes, but will be CACHED when source files change
# This way, source code changes won't invalidate this expensive layer
RUN ./scripts/install_htm_core.sh ../htm.core || \
    (echo "HTM.core installation failed. Make sure the script exists and is correct." && exit 1)

# Set environment variables for htm.core
# Note: install script installs to ../htm.core (which becomes /htm.core from /workspace)
ENV HTM_CORE_ROOT=/htm.core/build/Release
ENV CMAKE_PREFIX_PATH=/htm.core/build/Release:${CMAKE_PREFIX_PATH}

# NOW copy project files (this layer will be rebuilt when source changes)
# But the htm.core layer above will be cached and reused!
COPY config/ ./config/
COPY include/ ./include/
COPY src/ ./src/
COPY CMakeLists.txt ./
COPY README.md ./

# Build the project (this will rebuild when source/CMakeLists changes)
# But htm.core is already installed and cached from above
RUN mkdir -p build && \
    cd build && \
    cmake .. && \
    make -j$(nproc)

# Create a volume mount point for data
VOLUME ["/workspace/data"]

# Default command - run the executable
CMD ["./build/htm_swat"]
