# Cross-Compilation Guide: HTM-SWaT for ARM64 (RPi 4B QEMU)

**Host:** Ubuntu 24.04 (Noble) x86_64 on WSL2  
**Target:** Raspberry Pi 4B emulated via QEMU (cortex-a72, ARM64, 2 GB RAM)  
**QEMU SSH:** `ssh -p 2223 pi@localhost` | password: `raspberry`  
**Branch built:** `sliding-window-8` (WINDOW_SIZE=8)

---

## Why Cross-Compile?

Building htm.core inside QEMU via Docker took **14+ hours** and stalled (step 8/14, `install_htm_core.sh`). Cross-compiling on the x86 host takes **~5 minutes** and produces a native ARM64 binary that runs without Docker.

---

## Step 1 — Fix ARM64 apt Sources on Host (Ubuntu Noble)

Ubuntu Noble's default `archive.ubuntu.com` does **not** host ARM64 packages. ARM64 packages live on `ports.ubuntu.com`. Without this fix, `apt-get update` returns 404 for all arm64 packages.

**Restrict existing sources to amd64:**
```bash
sudo sed -i '/^Types: deb$/{n; /^Architectures:/! s/^Types: deb$/Types: deb\nArchitectures: amd64/}' \
  /etc/apt/sources.list.d/ubuntu.sources
```

> If the sed is tricky, manually add `Architectures: amd64` on the line after each `Types: deb` line in `/etc/apt/sources.list.d/ubuntu.sources`.

**Add ports.ubuntu.com for ARM64:**
```bash
sudo tee /etc/apt/sources.list.d/ubuntu-ports-arm64.sources << 'EOF'
Types: deb
URIs: http://ports.ubuntu.com/ubuntu-ports/
Suites: noble noble-updates noble-backports
Components: main universe restricted multiverse
Architectures: arm64
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg

Types: deb
URIs: http://ports.ubuntu.com/ubuntu-ports/
Suites: noble-security
Components: main universe restricted multiverse
Architectures: arm64
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
EOF
```

---

## Step 2 — Install Cross-Compiler and ARM64 yaml-cpp

```bash
sudo dpkg --add-architecture arm64
sudo apt-get update
sudo apt-get install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu libyaml-cpp-dev:arm64
```

---

## Step 3 — Create CMake Toolchain File

```bash
cat > /home/abed/final-project/aarch64-toolchain.cmake << 'EOF'
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF
```

---

## Step 4 — Cross-Compile htm.core for ARM64

> **Bug fix:** `BINDING_BUILD=NONE` does NOT skip Python bindings — it falls into the `else` branch of htm.core's CMakeLists.txt which unconditionally calls `add_subdirectory(bindings/py/cpp_src)` and then fails on `find_package(Python)`. The correct value is `CPP_Only` (exact case).

```bash
cd /home/abed/final-project/htm.core
mkdir -p build-aarch64 && cd build-aarch64
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=/home/abed/final-project/aarch64-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DBINDING_BUILD=CPP_Only
make -j$(nproc)
```

Expected output ends with: `[100%] Built target htm_core`  
Library lands at: `build-aarch64/src/libhtm_core.a`

---

## Step 5 — Create ARM64 Install Layout

htm.core's `CPP_Only` build puts the library at `build-aarch64/src/libhtm_core.a`, not the `Release/lib/` layout the project's CMakeLists.txt expects. C++ headers are architecture-independent, so we reuse them from the existing x86 build.

```bash
mkdir -p /home/abed/final-project/htm.core-aarch64/lib
cp /home/abed/final-project/htm.core/build-aarch64/src/libhtm_core.a \
   /home/abed/final-project/htm.core-aarch64/lib/
cp -r /home/abed/final-project/htm.core/build/Release/include \
      /home/abed/final-project/htm.core-aarch64/
```

---

## Step 6 — Cross-Compile HTM-SWaT Project

> **Note:** Build with `-DUSE_ARROW=OFF` — Arrow/Parquet cross-compilation is not set up. The binary falls back to CSV automatically.

```bash
cd /home/abed/final-project/HTM-Project/cppproject
mkdir -p build-aarch64 && cd build-aarch64
cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=/home/abed/final-project/aarch64-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DHTM_CORE_ROOT=/home/abed/final-project/htm.core-aarch64 \
  -DUSE_ARROW=OFF
make -j$(nproc)
```

Expected output ends with: `[100%] Built target htm_swat`

---

## Step 7 — Verify ARM64 Binary

```bash
# You are already inside build-aarch64/, so the binary is ./htm_swat
file htm_swat
# Expected: ELF 64-bit LSB executable, ARM aarch64, version 1 (SYSV), dynamically linked
```

---

## Step 8 — Convert Parquet Data to CSV (on Host)

The binary was built without Arrow, so it reads CSV. The dataset is only available as `.parquet` on the host.

```bash
cd /home/abed/final-project/HTM-Project/cppproject
python3 -c "
import pandas as pd
df = pd.read_parquet('data/swat_dataset.parquet')
df.to_csv('data/swat_dataset.csv', index=False)
print(f'Done: {len(df)} rows, {df.shape[1]} cols')
"
```

Expected: `Done: 946719 rows, 43 cols`

---

## Step 9 — Transfer Binary and Data to QEMU

```bash
# Transfer binary (from build-aarch64/ directory)
scp -P 2223 htm_swat pi@localhost:~/HTM-Project/cppproject/

# Transfer CSV data (from cppproject/ directory)
cd /home/abed/final-project/HTM-Project/cppproject
scp -P 2223 data/swat_dataset.csv pi@localhost:~/HTM-Project/cppproject/data/
```

Password: `raspberry`

---

## Step 10 — Run on QEMU

SSH into QEMU:
```bash
ssh -p 2223 pi@localhost
# password: raspberry
```

Install yaml-cpp runtime:
> **Bug fix:** `libyaml-cpp0.7` does not exist on Raspberry Pi OS — the correct package is `libyaml-cpp0.8`.

```bash
sudo apt install -y libyaml-cpp0.8
```

Run the binary from the cppproject directory (relative config paths require this):
```bash
cd ~/HTM-Project/cppproject
./htm_swat
```

---

## Common Errors and Fixes

| Error | Cause | Fix |
|-------|-------|-----|
| `404 Not Found` for arm64 packages on `archive.ubuntu.com` | Ubuntu Noble arm64 packages are on `ports.ubuntu.com` | Add `ubuntu-ports-arm64.sources` pointing to ports.ubuntu.com (Step 1) |
| `Could NOT find Python` during htm.core cmake | `BINDING_BUILD=NONE` falls through to Python bindings branch | Use `-DBINDING_BUILD=CPP_Only` (exact case) |
| `libhtm_core.a` not found at expected path | `CPP_Only` build puts library in `build-aarch64/src/`, not `Release/lib/` | Create manual install layout (Step 5) |
| `Parquet support not compiled` | Binary built with `-DUSE_ARROW=OFF` | Convert parquet to CSV on host and transfer (Steps 8–9) |
| `Cannot open CSV file: data/swat_dataset.csv` | CSV not on QEMU | Transfer CSV via scp (Step 9) |
| `Unable to locate package libyaml-cpp0.7` | Package version differs on RPi OS | Install `libyaml-cpp0.8` instead |
| `scp: No such file or directory` after double-paste | Command duplicated in terminal paste | First transfer succeeded — ignore second error |

---

## Summary

| Phase | Tool | Time |
|-------|------|------|
| htm.core cross-compile (ARM64) | aarch64-linux-gnu-g++ on x86 host | ~5 min |
| Project cross-compile | aarch64-linux-gnu-g++ on x86 host | ~1 min |
| Data CSV transfer (241 MB) | scp | ~22 sec |
| Binary transfer | scp | <1 sec |
| Run on QEMU | native ARM64 binary | depends on run |

Total setup time: **~10 minutes** vs **14+ hours** with Docker inside QEMU.
