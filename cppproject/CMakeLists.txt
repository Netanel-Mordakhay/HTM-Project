cmake_minimum_required(VERSION 3.15)
project(HTMSwatImplementation)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
    message(STATUS "Build type not specified, defaulting to Release")
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

# ============================================================================
# HTM.core Library Configuration
# ============================================================================

# Try multiple methods to find htm.core
# Method 1: Check if HTM_CORE_ROOT environment variable is set
if(DEFINED ENV{HTM_CORE_ROOT})
    set(HTM_CORE_ROOT "$ENV{HTM_CORE_ROOT}" CACHE PATH "HTM.core root directory")
    message(STATUS "Using HTM_CORE_ROOT from environment: ${HTM_CORE_ROOT}")
else()
    # Method 2: Try default location (relative to project)
    set(HTM_CORE_ROOT "${CMAKE_SOURCE_DIR}/../htm.core/build/Release" CACHE PATH "HTM.core root directory")
    message(STATUS "Trying default HTM_CORE_ROOT: ${HTM_CORE_ROOT}")
endif()

# Set include and library directories
set(HTM_CORE_INCLUDE_DIR "${HTM_CORE_ROOT}/include")
set(HTM_CORE_LIB_DIR "${HTM_CORE_ROOT}/lib")

# Verify htm.core installation
if(EXISTS "${HTM_CORE_LIB_DIR}/libhtm_core.a" OR EXISTS "${HTM_CORE_LIB_DIR}/libhtm_core.so")
    message(STATUS "✓ Found HTM.core library at: ${HTM_CORE_LIB_DIR}")
    
    # Find the actual library file
    if(EXISTS "${HTM_CORE_LIB_DIR}/libhtm_core.a")
        set(HTM_CORE_LIBRARY "${HTM_CORE_LIB_DIR}/libhtm_core.a")
        message(STATUS "  Using static library: libhtm_core.a")
    elseif(EXISTS "${HTM_CORE_LIB_DIR}/libhtm_core.so")
        set(HTM_CORE_LIBRARY "${HTM_CORE_LIB_DIR}/libhtm_core.so")
        message(STATUS "  Using shared library: libhtm_core.so")
    endif()
    
    # Verify headers exist
    if(EXISTS "${HTM_CORE_INCLUDE_DIR}/htm/algorithms/SpatialPooler.hpp")
        message(STATUS "✓ Found HTM.core headers at: ${HTM_CORE_INCLUDE_DIR}")
    else()
        message(WARNING "HTM.core headers not found at: ${HTM_CORE_INCLUDE_DIR}")
    endif()
    
else()
    # Try using find_package (if htm.core was installed via Python wheel)
    find_package(htm-core QUIET)
    
    if(htm-core_FOUND)
        message(STATUS "✓ Found HTM.core via find_package")
    else()
        message(FATAL_ERROR 
            "\n"
            "========================================\n"
            "HTM.core library not found!\n"
            "========================================\n"
            "\n"
            "Please install HTM.core first:\n"
            "  ./scripts/install_htm_core.sh\n"
            "\n"
            "Or set HTM_CORE_ROOT environment variable:\n"
            "  export HTM_CORE_ROOT=/path/to/htm.core/build/Release\n"
            "\n"
            "Expected location: ${HTM_CORE_ROOT}\n"
            "Library should be at: ${HTM_CORE_LIB_DIR}/libhtm_core.a\n"
        )
    endif()
endif()

# ============================================================================
# Include Directories
# ============================================================================

include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${HTM_CORE_INCLUDE_DIR}
)

# ============================================================================
# Find Required Packages
# ============================================================================

# Threads (required by htm.core)
find_package(Threads REQUIRED)

# Boost (required by htm.core)
# Try to find Boost - on macOS with Homebrew, it might be in /opt/homebrew
set(BOOST_ROOT "/opt/homebrew" CACHE PATH "Boost root directory")
find_package(Boost QUIET COMPONENTS system filesystem)
if(Boost_FOUND)
    message(STATUS "✓ Found Boost: ${Boost_VERSION}")
else()
    # If not found, try to find it without components (htm.core might link it internally)
    find_package(Boost QUIET)
    if(Boost_FOUND)
        message(STATUS "✓ Found Boost: ${Boost_VERSION} (without components)")
        message(WARNING "Boost components not found, but htm.core may link Boost internally")
    else()
        message(WARNING "Boost not found - htm.core may link it internally")
    endif()
endif()

# yaml-cpp (for YAML config parsing)
find_package(yaml-cpp REQUIRED)
if(yaml-cpp_FOUND)
    message(STATUS "✓ Found yaml-cpp: ${yaml-cpp_VERSION}")
else()
    message(FATAL_ERROR "yaml-cpp not found. Install with: sudo apt-get install libyaml-cpp-dev")
endif()

# ============================================================================
# Source Files
# ============================================================================

# Source files
set(SOURCES
    src/main.cpp
    src/config.cpp
    src/data_streamer.cpp
    src/htm_module.cpp
    src/htm_pyramid.cpp
    src/encoder.cpp
    src/utils.cpp
)

# Header files (for IDE support)
set(HEADERS
    include/htm_module.hpp
    include/htm_pyramid.hpp
    include/encoder.hpp
    include/config.hpp
    include/data_streamer.hpp
    include/utils.hpp
)

# ============================================================================
# Create Executable
# ============================================================================

add_executable(htm_swat ${SOURCES} ${HEADERS})

# ============================================================================
# Link Libraries
# ============================================================================

# Link htm.core
if(htm-core_FOUND)
    # Use CMake target if available (from Python wheel installation)
    target_link_libraries(htm_swat PRIVATE htm-core::htm-core)
else()
    # Link directly to library file
    target_link_libraries(htm_swat PRIVATE ${HTM_CORE_LIBRARY})
endif()

# Link Boost (if found)
if(Boost_FOUND AND TARGET Boost::system AND TARGET Boost::filesystem)
    target_link_libraries(htm_swat PRIVATE 
        Boost::system 
        Boost::filesystem
    )
    message(STATUS "Linking Boost libraries")
else()
    message(STATUS "Skipping explicit Boost linking (htm.core may link it internally)")
endif()

# Link Threads
target_link_libraries(htm_swat PRIVATE Threads::Threads)

# Link yaml-cpp
target_link_libraries(htm_swat PRIVATE yaml-cpp)

# ============================================================================
# Build Configuration Summary
# ============================================================================

message(STATUS "")
message(STATUS "==========================================")
message(STATUS "Build Configuration Summary")
message(STATUS "==========================================")
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "HTM_CORE_ROOT: ${HTM_CORE_ROOT}")
message(STATUS "HTM_CORE_INCLUDE_DIR: ${HTM_CORE_INCLUDE_DIR}")
message(STATUS "HTM_CORE_LIBRARY: ${HTM_CORE_LIBRARY}")
message(STATUS "Boost Version: ${Boost_VERSION}")
message(STATUS "==========================================")
message(STATUS "")

