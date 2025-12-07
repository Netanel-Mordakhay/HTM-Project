# Docker Setup Guide

## What is the Dockerfile's Job?

The Dockerfile creates a **containerized environment** that:

1. **Isolates Dependencies**: Packages all system dependencies (CMake, Boost, Python, etc.) in one place
2. **Reproducible Builds**: Ensures the same environment on any machine (your laptop, server, CI/CD)
3. **Automates Setup**: Automatically installs htm.core and builds your project
4. **Clean Environment**: No need to install dependencies on your host machine
5. **Portability**: Works the same way on Linux, macOS, and Windows (via Docker)

### In This Project Specifically:

The Dockerfile:
- Sets up Ubuntu 22.04 base image
- Installs build tools (CMake, GCC, etc.)
- Installs Python (needed to build htm.core)
- Installs Boost libraries (required by htm.core)
- Clones and builds htm.core library
- Builds your C++ project
- Creates a ready-to-run container

## How to Use Docker

### Option 1: Build and Run Manually

```bash
# Build the Docker image (takes ~15-20 minutes first time)
docker build -t htm_swat .

# Run the container
docker run -v $(pwd)/data:/workspace/data htm_swat
```

### Option 2: Using Docker Compose (Recommended)

```bash
# Build and run in one command
docker-compose up --build

# Or run in detached mode
docker-compose up -d --build
```

## Testing the Dockerfile

### Test 1: Build the Image

```bash
cd /Users/nati/Documents/University/D/cppproject
docker build -t htm_swat .
```

**What to check:**
- ✅ No errors during `apt-get install`
- ✅ htm.core installation completes successfully
- ✅ CMake finds htm.core
- ✅ Project compiles without errors
- ✅ Image builds successfully

**Expected output:**
```
Successfully built <image-id>
Successfully tagged htm_swat:latest
```

### Test 2: Verify Image Contents

```bash
# Check what's in the image
docker run --rm htm_swat ls -la /workspace

# Check if htm.core is installed
docker run --rm htm_swat ls -la /workspace/htm.core/build/Release/lib/

# Check if executable exists
docker run --rm htm_swat ls -la /workspace/build/
```

### Test 3: Run the Container

```bash
# Run the container (will fail if executable doesn't exist, which is expected for now)
docker run --rm htm_swat

# Or with data mounted
docker run --rm -v $(pwd)/data:/workspace/data htm_swat
```

## Troubleshooting

### Issue: Build fails at htm.core installation
**Solution**: Check if `scripts/install_htm_core.sh` exists and is executable

### Issue: CMake can't find htm.core
**Solution**: Verify `HTM_CORE_ROOT` environment variable is set correctly

### Issue: Permission denied
**Solution**: Make sure scripts are executable: `chmod +x scripts/*.sh`

### Issue: Out of disk space
**Solution**: Docker images can be large. Clean up: `docker system prune -a`

## When to Use Docker vs Local Build

**Use Docker when:**
- You want a clean, isolated environment
- You're on macOS/Windows and want Linux compatibility
- You want to share the exact environment with others
- You're setting up CI/CD

**Use Local Build when:**
- You're on Linux and want faster iteration
- You want to debug with your IDE
- You prefer native performance

## Next Steps

Once the Dockerfile works:
1. The image will be ready to use
2. You can run your C++ application in the container
3. Data files can be mounted as volumes
4. Results can be saved to mounted volumes

