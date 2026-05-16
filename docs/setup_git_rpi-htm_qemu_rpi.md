
### 09/05/2026
# Setup HTM-Project (C++ & Python), COUTA & TFMAE on QEMU RPi

### Note: Enlarge Disk Space beforehand
Shut down the RPi:
`sudo poweroff`

Expand Disk space to 20 GB:
```bash
qemu-img resize 2025-12-04-raspios-trixie-arm64-lite.img 20G
```
**Then boot the VM, SSH in:**
Default Login Credentials
- **Username:** `pi`
- **Password:** `raspberry`

```bash
sudo qemu-system-aarch64   -machine virt   -cpu cortex-a72   -smp 4   -m 2G   -kernel vmlinuz-arm64   -append "root=/dev/vda2 rw console=ttyAMA0 rootwait"   -drive file=2025-12-04-raspios-trixie-arm64-lite.img,format=raw,if=none,id=hd0   -device virtio-blk-pci,drive=hd0   -netdev user,id=mynet,hostfwd=tcp::2223-:22   -device virtio-net-pci,netdev=mynet   -nographic
```

**To Exit:** Press `Ctrl + A`, then immediately press `X`.

**SSH Access:** Open a new terminal and run:

```bash
ssh -p 2223 pi@localhost
```


 **Expand the partition + filesystem:**
 ```bash
sudo raspi-config --expand-rootfs
```
Or manually with parted + resize2fs:

```bash
sudo parted /dev/vda resizepart 2 100%
sudo resize2fs /dev/vda2
df -h /
```
**Reboot again:**
```bash
sudo poweroff
```


## Steps After RPi QEMU is ready and logged in: 

### Step 1 - Install git:
`sudo apt update && sudo apt install -y git`

### Step 2 - Install Docker

```bash
curl -fsSL https://get.docker.com | sudo sh
sudo usermod -aG docker pi
newgrp docker
Then install Docker Compose plugin:


sudo apt install -y docker-compose-plugin
After that, use the modern syntax (no hyphen):


docker compose up --build
Or if the project specifically requires the old docker-compose binary:


sudo apt install -y docker-compose
```

### If Docker doesnt work, build natively:

Docker won't work on this QEMU kernel. Build natively instead — the Dockerfile translates directly to these apt commands on the Pi:

### Step 1: Install system dependencies

```bash
sudo apt install -y build-essential git python3 python3-pip python3-venv python3-dev \
    libboost-all-dev libyaml-cpp-dev pkg-config wget curl cmake
```
### Step 2: Install Arrow/Parquet

Traditional Arrow installation wont work:
```bash
sudo apt install -y libarrow-dev libparquet-dev
```
Install Apache Arrow repo for Debian trixie

```bash
sudo apt install -y libarrow-dev libparquet-dev

curl -fsSL https://apache.jfrog.io/artifactory/arrow/debian/apache-arrow-apt-source-latest-trixie.deb \
    -o /tmp/arrow.deb
sudo apt install -y /tmp/arrow.deb
sudo apt update
sudo apt install -y libarrow-dev libparquet-dev
```

### Step 3: Build htm.core

```bash
cd ~/HTM-Project/cppproject
chmod +x scripts/install_htm_core.sh
./scripts/install_htm_core.sh ~/htm.core
```

### Step 4: Build the project
```bash
cd ~/HTM-Project/cppproject
mkdir -p build && cd build
cmake .. -DCMAKE_PREFIX_PATH=~/htm.core/build/Release
make -j$(nproc)
```

For slower build, as not to overuse RAM:
- Fix 1 — Add Swap (Quickest, no restart needed)

*Run this once on QEMU device*
```bash
Run this inside your QEMU VM before retrying the build:
bash# Create a 2GB swapfile
sudo fallocate -l 2G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile
free -h   # confirm swap is visible
```
```bash 
cd ~/HTM-Project/cppproject
rm -rf build && mkdir -p build && cd build
cmake .. -DHTM_CORE_ROOT=~/htm.core/build/Release
make -j1

```

### RUN CPP ON QEMU RPi

Since Docker does not work on QEMU, we use the local build and run.
```bash
cd ~/HTM-Project/cppproject
./build/htm_swat
```

   - Press `Ctrl + A`, then `X` to exit QEMU
