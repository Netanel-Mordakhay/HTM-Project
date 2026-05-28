# HTM-SWAT System Architecture

**Project:** Hierarchical Temporal Memory anomaly detection on the SWAT (Secure Water Treatment) dataset  
**Target deployment:** Raspberry Pi 4B (ARM Cortex-A72, 2 GB RAM)  
**Language:** C++17 · **Build:** CMake + Docker  

---

## 1. High-Level Pipeline

```
┌─────────────────────────────────────────────────────────────────────┐
│                          SWAT Dataset                               │
│          946,719 rows × 41 features  (42 MB parquet)               │
│          Labels: 894,611 normal  /  52,108 attack                   │
└─────────────────────────────┬───────────────────────────────────────┘
                              │  rows 446k–946k, stride 5
                              ▼  → 100,000 rows processed
┌─────────────────────────────────────────────────────────────────────┐
│                       Data Streamer                                 │
│   Reads one row at a time (RowStreamer / batch vector)              │
│   Selects the 25 sensor columns required by the feature plan        │
└─────────────────────────────┬───────────────────────────────────────┘
                              │ raw sensor values (doubles)
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    Feature Encoding Layer                           │
│   One RDSE per sensor  (RandomDistributedScalarEncoder)             │
│   Output per sensor:  2304-bit SDR,  ~80 active bits (3.5%)        │
│   Sensors grouped into 16 groups → union-merged into L0 inputs     │
└─────────────────────────────┬───────────────────────────────────────┘
                              │ 16 × 2304-bit merged SDRs
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                       HTM Pyramid  (26 modules)                     │
│                                                                     │
│   L0 (16 nodes) ──► L1 (6 nodes) ──► L2 (3 nodes) ──► L3 (head)   │
│                                                                     │
│   Each module = SpatialPooler + TemporalMemory                      │
│   SP output: 1664-column SDR                                        │
│   Anomaly score = 1 − (predictive ∩ active) / active               │
└─────────────────────────────┬───────────────────────────────────────┘
                              │ anomaly score per row (float 0–1)
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                   Post-Processing & Evaluation                      │
│   Grid search over 101 thresholds (min→max score)                  │
│   Metrics: F1, precision, recall, accuracy                         │
│   Learn period (10,000 rows) excluded from evaluation              │
└─────────────────────────────┬───────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         Output Files                                │
│   anomaly_scores_<ts>.csv                                           │
│   metrics/cpp_metrics_<ts>.txt                                      │
│   experiments_<branch>_<ts>/                                        │
│     ├── model_performance_metrics.json                              │
│     ├── model_efficiency_metrics.json                               │
│     └── roc_thresholds.json                                         │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 2. HTM Pyramid — Detailed Wiring

Each node is an independent HTM module (SP + TM). Inputs from lower nodes are **union-merged** into a single SDR before being passed upward.

```
                          ┌─────────┐
                          │  L3_1   │  ← HEAD (anomaly score output)
                          └────┬────┘
             ┌─────────────────┼─────────────────┐
         ┌───┴───┐         ┌───┴───┐         ┌───┴───┐
         │ L2_1  │         │ L2_2  │         │ L2_3  │
         └───┬───┘         └───┬───┘         └───┬───┘
         ┌───┴───┐         ┌───┴───┐         ┌───┴───┐
       L1_1   L1_2       L1_3   L1_4       L1_5   L1_6
        ↑       ↑          ↑       ↑          ↑       ↑
      L0_1   L0_3        L0_6   L0_9       L0_12  L0_15
      L0_2  L0_4,5      L0_7,8 L0_10,11  L0_13,14 L0_16
```

### Layer connections

| Node | Inputs | Sensors covered |
|------|--------|----------------|
| **L0_1** | mv101, fit101, lit101 | Inlet valve + flow + tank level |
| **L0_2** | lit101, fit201, p101 | Tank level + RO feed flow + pump |
| **L0_3** | ait201, p201 | NaCl concentration + pump |
| **L0_4** | ait202, p203, ait402 | NaOCl + pump + UV |
| **L0_5** | ait203, p205, ait402 | HCl + pump + UV |
| **L0_6** | lit301, fit201, p101 | UF tank + feed flow + pump |
| **L0_7** | dpit301, p302, fit301 | Differential pressure + pump + UF flow |
| **L0_8** | lit301, fit301, p302 | UF tank + UF flow + pump |
| **L0_9** | lit401, fit301, p302 | RO tank + UF flow + pump |
| **L0_10** | fit401, p402, uv401 | RO flow + pump + UV dosing |
| **L0_11** | ait401, ait402, p403 | Conductivity + UV + pump |
| **L0_12** | fit501, pit501, p501 | Backwash flow + pressure + pump |
| **L0_13** | fit502, pit502, ait504 | Permeate flow + pressure + turbidity |
| **L0_14** | ait501, ait502, ait503 | pH + ORP + conductivity |
| **L0_15** | fit503, pit503, fit504 | Reject flow + pressure + feed flow |
| **L0_16** | fit601, p602, dpit301 | Cleaned water flow + pump + dif. pressure |
| **L1_1** | L0_1, L0_2 | Inlet / tank zone |
| **L1_2** | L0_3, L0_4, L0_5 | Chemical dosing zone |
| **L1_3** | L0_6, L0_7, L0_8 | UF feed zone |
| **L1_4** | L0_9, L0_10, L0_11 | RO + dosing zone |
| **L1_5** | L0_12, L0_13, L0_14 | Backwash + permeate zone |
| **L1_6** | L0_15, L0_16 | Reject + clean water zone |
| **L2_1** | L1_1, L1_2 | Inlet + chemical abstraction |
| **L2_2** | L1_3, L1_4 | UF + RO abstraction |
| **L2_3** | L1_5, L1_6 | Backwash + clean abstraction |
| **L3_1** | L2_1, L2_2, L2_3 | **Global plant state (head)** |

---

## 3. HTM Module (per node)

```
  Input SDR (2304 bits)
        │
        ▼
  ┌─────────────┐
  │SpatialPooler│  columnDimensions=1664, localAreaDensity=0.019
  │             │  potentialRadius=1.0, boostStrength=1.2
  └──────┬──────┘
         │  active columns (~32 bits)
         ▼
  ┌─────────────┐
  │TemporalMemory│  cellsPerColumn=16 → 26,624 cells total per module
  │              │  activationThreshold=[26,30,28,32] per layer
  │              │  maxSegmentsPerCell=[128,64,160,112]
  └──────┬───────┘
         │
         ├─► output SDR (active cells → pooled to column-level)
         │
         └─► anomaly score = 1 − |predictive ∩ active| / |active|
```

**Learning:** enabled for first `learn_period=10,000` rows, disabled after. Anomaly scores from the learning period are excluded from metric evaluation.

---

## 4. Data & Feature Encoding

### Dataset
| Property | Value |
|----------|-------|
| Source | SWAT (Secure Water Treatment) |
| Total rows | 946,719 |
| Columns | 43 (41 sensors + timestamp + label) |
| File format | Parquet (42 MB) with CSV fallback |
| Normal rows | 894,611 (94.5%) |
| Attack rows | 52,108 (5.5%) |
| Run window | rows 446,000–946,000, stride 5 → **100,000 rows** |

### Feature types
| Type | Count | Encoding |
|------|-------|---------|
| Float sensors | 26 | RDSE with per-sensor resolution |
| Categorical (valves/pumps) | 14 | RDSE in category mode |
| Timestamp | 1 | RDSE (resolution 1.0) |

### SDR parameters
| Parameter | Value |
|-----------|-------|
| Encoder output size | 2304 bits |
| Active bits per encoder | ~80 (3.5% sparsity) |
| Merge mode (within group) | Union |
| Merge mode (between layers) | Union |

---

## 5. Source Code Structure

```
cppproject/
├── src/
│   ├── main.cpp              Entry point: config → data → pyramid → metrics → save
│   ├── htm_pyramid.cpp       Builds 26-module pyramid; run loop; layer orchestration
│   ├── htm_module.cpp        SP + TM forward pass; anomaly scoring; learn toggle
│   ├── data_streamer.cpp     Row-by-row RDSE encoding; SDR group merging
│   ├── config.cpp            YAML → feature config and model config maps
│   ├── utils.cpp             SDR ops; CSV/Parquet I/O; grid search; metrics
│   └── experiment_utils.cpp  ExperimentMonitor: CPU/RAM sampling; JSON writers
│
├── include/
│   ├── htm_pyramid.hpp
│   ├── htm_module.hpp
│   ├── data_streamer.hpp
│   ├── config.hpp
│   ├── utils.hpp
│   └── experiment_utils.hpp
│
├── config/
│   ├── data/config_swat.yaml      41 feature definitions (type, resolution, range)
│   └── model/config_model_default.yaml  SP/TM hyperparameters per layer
│
├── data/
│   └── swat_dataset.parquet       946,719 × 43 (mounted as Docker volume)
│
├── results/                       Output directory (mounted as Docker volume)
│   ├── anomaly_scores_<ts>.csv
│   ├── metrics/cpp_metrics_<ts>.txt
│   └── experiments_<branch>_<ts>/
│       ├── model_performance_metrics.json
│       ├── model_efficiency_metrics.json
│       └── roc_thresholds.json
│
├── Dockerfile
├── docker-compose.yml
└── CMakeLists.txt
```

---

## 6. Experiment Tracking

Every run produces a timestamped experiment folder tagged with the git branch name.

```
ExperimentMonitor (singleton)
  │
  ├─ Background thread (every 2s)
  │    reads /proc/self/stat  → CPU %
  │    reads /proc/self/status → VmRSS (RAM MB)
  │
  ├─ Timing events (named checkpoints)
  │    run_start → configs_loaded → data_load_complete
  │    → pyramid_built → pyramid_run_complete → saving_results
  │
  ├─ Per-row latency
  │    chrono::steady_clock wrap in run loop → min/avg/peak ms
  │
  └─ On exit → writes to results/experiments_<branch>_<timestamp>/
       model_efficiency_metrics.json   (CPU, RAM, timing, latency)
       model_performance_metrics.json  (F1, precision, recall, threshold)
       roc_thresholds.json             (101-point threshold sweep)
```

---

## 7. Build & Deployment

```
Developer Machine
      │
      ├─ Local build:  cmake .. && make -j$(nproc)
      │                ./build/htm_swat
      │
      └─ Docker build: docker compose build
                       docker compose up
                            │
                   ubuntu:22.04 base image
                   + boost, yaml-cpp, Arrow/Parquet
                   + CMake 3.28 (Kitware APT)
                   + htm.core (built from source, ~15 min, CACHED)
                   + project source compiled
                            │
                   Container mounts:
                     ./data/    → /workspace/data    (read-only)
                     ./results/ → /workspace/results (write)
                     ./config/  → /workspace/config  (read-only)
                            │
                   Target: Raspberry Pi 4B
                     ARM Cortex-A72, 4 cores, 2 GB RAM
                     Goal: < 350 MB RAM, < 65% CPU
                     Current: ~1,329 MB RAM, ~7% CPU (host-normalised)
```

---

## 8. Performance Baseline (April 2026)

| Metric | Single-thread (`draft_abed`) | Multi-thread (`batch-load`) | Pi 4B target |
|--------|------------------------------|----------------------------|-------------|
| Runtime | 680s | 430s | — |
| Avg CPU | 105.5% (1 core) | 61.2% | < 65% (1 core) |
| Peak RAM | 5,039 MB | 1,329 MB | < 350 MB |
| Best F1 | 0.663 *(synthetic labels)* | 0.448 *(real labels)* | — |

> **RAM is the primary bottleneck.** 1.3 GB is driven by 26 HTM modules each holding a full SpatialPooler + TemporalMemory with 26,624 cells. The Pi target of 350 MB requires an approximately 4× reduction.
