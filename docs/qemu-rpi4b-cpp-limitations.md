# Why `experiments/draft_abed` (Single-Threaded C++) Cannot Run on QEMU Raspberry Pi 4B

## Summary

The single-threaded C++ branch (`experiments/draft_abed`) cannot complete a full run on a QEMU Raspberry Pi 4B emulation. The program is killed by the OS OOM (Out-Of-Memory) killer during the data loading step, before any HTM processing begins.

---

## What Happens

```
[Step 1] Loading configs from YAML...
  ✓ Loaded data config with 43 features
  ✓ Loaded model config

[Step 2] Loading data...
  Loading parquet file: data/swat_dataset.parquet
  Found 43 columns, 946719 rows
Killed
```

The process is terminated by the Linux kernel's OOM killer immediately after printing the row count.

---

## Root Cause: `ParquetRowStreamer` Loads the Entire Dataset Into RAM

The data loading is handled by `ParquetRowStreamer` in [src/utils.cpp](../cppproject/src/utils.cpp). Despite the name "streamer", the constructor calls Apache Arrow's `ReadTable()` which loads the **entire parquet file** into a single in-memory Arrow Table before any row-by-row processing begins:

```cpp
// utils.cpp — ParquetRowStreamer constructor
auto reader = *parquet::arrow::OpenFile(infile, arrow::default_memory_pool());
if (!reader->ReadTable(&table_).ok())          // ← full 946K-row table loaded here
    throw std::runtime_error("Failed to read parquet table");
```

### Memory cost

| Factor | Value |
|--------|-------|
| Dataset rows | 946,719 |
| Columns | 43 |
| Arrow in-memory size (float64) | ~330 MB |
| Parquet compressed file size | 42 MB |

The 42 MB parquet file decompresses into ~330 MB of columnar Arrow data in RAM. On a QEMU RPi 4B with typically ≤1 GB of emulated RAM, this alone consumes roughly one-third of total available memory — before the OS, the HTM pyramid (26 modules), or any intermediate buffers are accounted for.

---

## Why Docker Also Doesn't Work

Docker requires the kernel to support `cgroup` namespacing features that the QEMU RPi 4B emulation does not expose correctly. Attempting to run the container produces:

```
unable to get image 'cppproject-htm_swat': Cannot connect to the Docker daemon
at unix:///var/run/docker.sock. Is the docker daemon running?
```

This means neither the recommended Docker path nor the native binary path works on QEMU.

---

## Why the CSV Fallback Is Not Triggered

`main.cpp` does include a CSV fallback:

```cpp
// main.cpp — Step 3
try {
    streamer = makeStreamer(data_path, ...);          // tries .parquet
} catch (...) {
    streamer = makeStreamer(csv_path, ...);           // fallback .csv
}
```

However, the parquet file opens and reads successfully — the process is not killed by an exception. The OOM kill happens silently at the OS level, mid-`ReadTable`, so no C++ exception is thrown and the fallback is never reached.

---

## The CSV Streamer Is Truly Row-by-Row

`CSVRowStreamer` (also in [src/utils.cpp](../cppproject/src/utils.cpp)) holds no full-file buffer — it keeps an open `std::ifstream` and reads one line per `nextRow()` call. Its memory footprint is constant regardless of dataset size.

The parquet format offers no equivalent path in the current code: `ParquetRowStreamer` must call `ReadTable()` upfront or the Arrow row-group API must be used instead.

---

## Workaround (No Code Change)

Convert the parquet file to CSV **on the host machine** (not on the Pi), then copy it over. The program will fail to open the `.parquet` path... 

Wait — as noted above, the file opens fine, so the fallback is not triggered by a missing file. Instead, **rename or remove the parquet file** on the Pi so the open fails and the code falls through to CSV:

**On the host machine:**
```bash
cd /home/abed/final-project/HTM-Project/cppproject/data

# Install dependencies if needed
pip install pandas pyarrow

# Convert (needs ~330 MB RAM — run on host, not Pi)
python3 -c "import pandas as pd; pd.read_parquet('swat_dataset.parquet').to_csv('swat_dataset.csv', index=False)"
```

**Transfer to Pi** (via `scp`, shared folder, etc.):
```bash
scp data/swat_dataset.csv pi@<qemu-ip>:~/HTM-Project/cppproject/data/
```

**On the Pi — disable the parquet file so the fallback triggers:**
```bash
mv ~/HTM-Project/cppproject/data/swat_dataset.parquet \
   ~/HTM-Project/cppproject/data/swat_dataset.parquet.bak
```

**Run:**
```bash
cd ~/HTM-Project/cppproject
mkdir -p results
./build/htm_swat
```

> **Note:** The CSV will be ~400–500 MB on disk (uncompressed text). Ensure the Pi image has sufficient storage before copying.

---

## Proper Fix (Requires Code Change)

Replace `ReadTable()` in `ParquetRowStreamer` with row-group level reads using `reader->ReadRowGroup(group_idx, column_indices, &table)`. This loads one row group at a time (~1–10 MB) and discards it before loading the next, keeping peak memory well within QEMU limits regardless of total dataset size.

---

## Environment

| Property | Value |
|----------|-------|
| Branch | `experiments/draft_abed` |
| Implementation | Single-threaded C++ (`cppproject/`) |
| Target | QEMU Raspberry Pi 4B emulation |
| Host OS | Raspberry Pi OS (Debian-based, ARM) |
| Dataset | SWaT (`swat_dataset.parquet`, 946,719 rows × 43 columns) |
| Crash point | `ParquetRowStreamer` constructor, `ReadTable()` call |
| Symptom | Process killed with no error message (`Killed`) |
