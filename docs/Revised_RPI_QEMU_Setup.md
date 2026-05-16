# ALL-MLP: Raspberry Pi 4B Performance Emulation

This guide details how to create a high-performance **Digital Twin** of a Raspberry Pi 4B (4-core Cortex-A72, 2GB RAM) using QEMU. By using the virt machine type and VirtIO, we eliminate SD card I/O bottlenecks to ensure benchmarks reflect code efficiency, not emulation lag.

## 1. Prerequisites & Environment Setup

Install the necessary cross-compilation and emulation tools:

```bash
sudo apt update && sudo apt install -y qemu-system-aarch64 qemu-utils gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```

### Download the OS Image

Use Raspberry Pi OS Lite (Trixie/64-bit) to minimize background resource noise.

```bash
wget https://downloads.raspberrypi.com/raspios_lite_arm64/images/raspios_lite_arm64-2025-12-04/2025-12-04-raspios-trixie-arm64-lite.img.xz
unxz 2025-12-04-raspios-trixie-arm64-lite.img.xz
```

## 2. Extracting the "Golden Trio"

To boot the virt machine, we must extract the native kernel from the image.

### Map and Mount Partition 2

```bash
sudo losetup -fP 2025-12-04-raspios-trixie-arm64-lite.img
sudo mount /dev/loop0p2 /tmp/pi_boot
```

### Copy Boot Files

```bash
cp /tmp/pi_boot/boot/vmlinuz-6.12.47+rpt-rpi-v8 .
cp /tmp/pi_boot/boot/initrd.img-6.12.47+rpt-rpi-v8 .
```

### Cleanup

```bash
sudo umount /tmp/pi_boot
sudo losetup -d /dev/loop0
```

## 3. Launching the Emulation

Run the following command to boot the 2GB Cortex-A72 environment:

```bash
qemu-system-aarch64 \
  -machine virt \
  -cpu cortex-a72 \
  -smp 4 \
  -m 2G \
  -kernel vmlinuz-6.12.47+rpt-rpi-v8 \
  -initrd initrd.img-6.12.47+rpt-rpi-v8 \
  -append "root=/dev/vda2 rw console=ttyAMA0 rootwait panic=10" \
  -drive file=2025-12-04-raspios-trixie-arm64-lite.img,format=raw,if=none,id=hd0 \
  -device virtio-blk-pci,drive=hd0 \
  -netdev user,id=mynet,hostfwd=tcp::2223-:22 \
  -device virtio-net-pci,netdev=mynet \
  -nographic
```

**To Exit:** Press `Ctrl + A`, then immediately press `X`.

**SSH Access:** Open a new terminal and run:

```bash
ssh -p 2223 pi@localhost
```

## 4. Benchmarking & Efficiency Analysis

Once logged in, install Docker:

```bash
curl -sSL https://get.docker.com | sh && sudo usermod -aG docker pi
```

### Metrics Collection

- **Memory:** Use `docker stats` for container-specific usage or `free -h` for system-wide overhead.

- **CPU/Power:** Map Guest CPU utilization to the Pi 4B power curve:

$$P_{total} \approx 3.0\text{W} + (\text{CPU\%} \times 0.035\text{W})$$

- **Optimization:** Use `valgrind --tool=massif` for heap profiling and `perf` for identifying CPU hotspots.

## 5. Troubleshooting Login Issues

If the default `pi` / `raspberry` login fails, use the "Backdoor" method:

### Boot to Root Shell

Add `init=/bin/sh` to the `-append` section of the QEMU command.

### Remount Filesystem

```bash
mount -o remount,rw /dev/vda2 /
```

### Reset Credentials

```bash
passwd pi
usermod -aG sudo pi
sync
```

### Exit & Reboot

Use `Ctrl + A`, `X` and restart the standard QEMU command.

## 6. Standard Boot Procedure & Setup

### Final Boot Command

Once your environment is properly configured, use this command to launch the emulated Raspberry Pi 4B:

```bash
cd /home/abed/final-project/rpi-setup

sudo qemu-system-aarch64 \
  -machine virt \
  -cpu cortex-a72 \
  -smp 4 \
  -m 2G \
  -kernel vmlinuz-arm64 \
  -append "root=/dev/vda2 rw console=ttyAMA0 rootwait" \
  -drive file=2025-12-04-raspios-trixie-arm64-lite.img,format=raw,if=none,id=hd0 \
  -device virtio-blk-pci,drive=hd0 \
  -netdev user,id=mynet,hostfwd=tcp::2223-:22 \
  -device virtio-net-pci,netdev=mynet \
  -nographic
```

### Initial Setup: Boot Without User Login

**Important:** On the first boot, the standard command above may not allow immediate login. Use this alternative command to initialize the user credentials:

```bash
cd /home/abed/final-project/rpi-setup

sudo qemu-system-aarch64 \
  -machine virt \
  -cpu cortex-a72 \
  -smp 4 \
  -m 2G \
  -kernel vmlinuz-arm64 \
  -append "root=/dev/vda2 rw console=ttyAMA0 rootwait init=/bin/sh" \
  -drive file=2025-12-04-raspios-trixie-arm64-lite.img,format=raw,if=none,id=hd0 \
  -device virtio-blk-pci,drive=hd0 \
  -netdev user,id=mynet,hostfwd=tcp::2223-:22 \
  -device virtio-net-pci,netdev=mynet \
  -nographic
```

### Default Login Credentials

- **Username:** `pi`
- **Password:** `raspberry`

### Login Troubleshooting Procedure

If the login fails with the default credentials, follow these steps:

1. **Kill the QEMU Terminal**
   - Press `Ctrl + A` (together), then immediately press `X` to exit QEMU

2. **Reboot Without User Authentication**
   - Run the "Boot Without User Login" command above
   - This provides direct root shell access

3. **Remount the Root Filesystem (Read-Write)**
   ```bash
   mount -o remount,rw /dev/vda2 /
   ```

4. **Reset the Pi User Password**
   ```bash
   passwd pi
   ```
   - You will be prompted to enter a new password. For consistency, use: `raspberry`

5. **Ensure Pi User Has Sudo Privileges**
   ```bash
   usermod -aG sudo pi
   ```

6. **Commit Changes to Disk**
   ```bash
   sync
   ```

7. **Exit and Reboot with Standard Command**
   - Press `Ctrl + A`, then `X` to exit QEMU
   - Restart using the "Final Boot Command" (the standard one without `init=/bin/sh`)
   - You should now be able to log in with `pi` / `raspberry`

### Verification Steps After Login

Once successfully logged in, verify the system is working correctly:

```bash
# Check architecture and kernel
uname -a

# Verify available memory
free -h

# Check CPU cores
nproc

# Test network connectivity
ping -c 2 8.8.8.8

# Verify SSH is accessible from host
# (In a separate terminal on your host machine)
ssh -p 2223 pi@localhost
```

## Common Errors

- **Port 2223 Collision:** Change `hostfwd=tcp::2223-:22` to `2224`.

- **Write Lock:** Ensure no loop devices are active with `sudo losetup -D`.
