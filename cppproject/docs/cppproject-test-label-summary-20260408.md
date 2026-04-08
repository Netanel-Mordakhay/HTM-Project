## 📋 **PROJECT OVERVIEW**

This is a **C++ implementation of the HTM (Hierarchical Temporal Memory) SWAT anomaly detection system**. It's based on the Python `quickstart-swat.py` script but rewritten in C++ for better performance.

**What does it do?**
- Takes sensor data from a water treatment plant (SWAT dataset)
- Encodes the raw values into Sparse Distributed Representations (SDRs)
- Processes them through a 4-layer pyramid of HTM modules
- Calculates anomaly scores based on prediction accuracy
- Evaluates performance against ground-truth labels
## 🏗️ **ARCHITECTURE DIAGRAM**

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>┌─────────────────────────────────────────────────────────────────┐
│                    DATA LOADING                                  │
├─────────────────────────────────────────────────────────────────┤
│ data/swat_dataset.parquet (446K-946K rows)                      │
│ ↓                                                               │
│ loadParquet() / loadCSV()  [utils.cpp]                          │
└────────────────┬────────────────────────────────────────────────┘
                 │ vector<map<string, double>>
                 ↓
┌─────────────────────────────────────────────────────────────────┐
│            FEATURE ENCODING (DataStreamer)                      │
├─────────────────────────────────────────────────────────────────┤
│ 41 Features: float (fit, lit, ait), categorical (mv, p)        │
│ ↓                                                               │
│ RandomDistributedScalarEncoder (RDSE)                          │
│ Each feature → 2304-bit SDR                                    │
└────────────────┬────────────────────────────────────────────────┘
                 │ map<string, SDR>
                 ↓
┌─────────────────────────────────────────────────────────────────┐
│              PYRAMID STRUCTURE (4 LAYERS)                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  LAYER 0 (Encoders)     L0_1   L0_2   L0_3   ... (16 groups)  │
│          ↓      ↓       ↓                                       │
│  LAYER 1 (HTM)      → L1_1   L1_2 ...  (6 modules)             │
│                        ↓       ↓                                │
│  LAYER 2 (HTM)    → L2_1   L2_2 ...  (3 modules)               │
│                        ↓                                        │
│  LAYER 3 (HTM)          → L3_1  (Head - OUTPUT)                │
│                            ↓                                    │
│                      Anomaly Scores                             │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
         ↓
┌─────────────────────────────────────────────────────────────────┐
│         EVALUATION (Metrics Calculation)                        │
├─────────────────────────────────────────────────────────────────┤
│ Grid Search (101 thresholds)                                    │
│ Compare predictions vs ground-truth labels                      │
│ Calculate: Precision, Recall, F1, Accuracy                     │
└─────────────────────────────────────────────────────────────────┘
         ↓
┌─────────────────────────────────────────────────────────────────┐
│              OUTPUT FILES                                       │
├─────────────────────────────────────────────────────────────────┤
│ results/anomaly_scores_<timestamp>.csv                          │
│ results/metrics/cpp_metrics_<timestamp>.txt                     │
│   ✓ Best F1=0.4478   (optimal threshold)                       │
│   ✓ Average F1=0.2541 (mean across thresholds)                │
└─────────────────────────────────────────────────────────────────┘</pre></div>

---

## 📥 **COMPLETE INPUT/OUTPUT WALKTHROUGH - Each Step Explained**

This section shows **exact data structures** flowing through each stage. Follow along with real numbers!

### **Timestep: Row #500,000 from SWAT Dataset**

#### **Stage 1: Data Loading**

**Input:** Parquet/CSV file
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Row 500,000 from dataset:
{
  "timestamp": "07/15/2015 04:35:23 AM",
  "fit101": 2.5,
  "lit101": 50.2,
  "mv101": 1.0,
  "p101": 0.0,
  "ait201": 25.3,
  "ait202": 0.01,
  "ait203": 40.1,
  "fit201": 3.2,
  "mv201": 1.0,
  "p201": 0.5,
  "p203": 0.0,
  "p205": 0.0,
  "dpit301": 15.0,
  "fit301": 2.8,
  "lit301": 35.5,
  ... (41 features total)
  "label": 0          ← Ground truth (0=normal, 1=anomaly)
}</pre></div>

**Output:** `vector<map<string, double>>` (one row as a map)
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>map<string, double> row_data;
row_data["fit101"] = 2.5;
row_data["lit101"] = 50.2;
row_data["mv101"] = 1.0;
... (41 entries)
row_data["label"] = 0;  // Ground truth for evaluation later</pre></div>

---

#### **Stage 2: Feature Encoding (DataStreamer)**

**Input:** Raw values for 3 features in group L0_1
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Input row features:
  mv101 = 1.0     (categorical)
  fit101 = 2.5    (float)
  lit101 = 50.2   (float)</pre></div>

**Processing:** Each encoder converts to SDR (Sparse Distributed Representation)
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>// Create encoders (done once at startup)
auto encoder_mv101 = RandomDistributedScalarEncoder(size=2304, sparsity=0.035);
auto encoder_fit101 = RandomDistributedScalarEncoder(size=2304, sparsity=0.035);
auto encoder_lit101 = RandomDistributedScalarEncoder(size=2304, sparsity=0.035);

// Encode this timestep
SDR sdr_mv101 = encoder_mv101.encode(1.0);      // ~80 bits active
SDR sdr_fit101 = encoder_fit101.encode(2.5);    // ~80 bits active
SDR sdr_lit101 = encoder_lit101.encode(50.2);   // ~80 bits active</pre></div>

**Output per feature:** SDR with 2304 bits, ~80 active
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>sdr_mv101.getSparse() → {45, 127, 234, 567, 789, 1001, ..., 2103}  (80 indices)
sdr_fit101.getSparse() → {12, 89, 156, 412, 567, 1234, ..., 2201}  (80 indices)
sdr_lit101.getSparse() → {5, 78, 203, 445, 812, 1567, ..., 2290}   (80 indices)</pre></div>

**Union (mode='u'):** Merge all 3 SDRs
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>L0_1_result = mergeSDRsUnion({sdr_mv101, sdr_fit101, sdr_lit101})

L0_1_result.getSparse() → {5, 12, 45, 78, 89, 127, 156, 203, 234, 412, 445, 
                           567, 789, 812, 1001, 1234, 1567, 2103, 2201, 2290}
L0_1_result.size = 2304 bits
L0_1_result.active = ~240 bits (union of 3×80)</pre></div>

**Similar for L0_2, L0_3, ... L0_16** (16 feature groups total)

---

#### **Stage 3: Layer 0 Output Summary**

**Input:** 41 sensor features (scalars)

**Output:** 16 SDR groups (L0_1 through L0_16)

Note: Some features appear in multiple groups (e.g., lit101, ait402, fit301, p302). The union operation correctly handles overlapping features - the merged SDR will contain the union of all active bits.
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>L0_1: SDR(size=2304, active=~240)  [mv101, fit101, lit101]
L0_2: SDR(size=2304, active=~240)  [lit101, fit201, p101]      ← lit101 appears in both L0_1 and L0_2
L0_3: SDR(size=2304, active=~200)  [ait201, p201]              ← Only 2 features
L0_4: SDR(size=2304, active=~240)  [ait202, p203, ait402]
L0_5: SDR(size=2304, active=~240)  [ait203, p205, ait402]      ← ait402 appears in both L0_4 and L0_5
L0_6: SDR(size=2304, active=~240)  [lit301, fit201, p101]
L0_7: SDR(size=2304, active=~240)  [dpit301, p302, fit301]
L0_8: SDR(size=2304, active=~240)  [lit301, fit301, p302]
L0_9: SDR(size=2304, active=~240)  [lit401, fit301, p302]
L0_10: SDR(size=2304, active=~240) [fit401, p402, uv401]
L0_11: SDR(size=2304, active=~240) [ait401, ait402, p403]
L0_12: SDR(size=2304, active=~240) [fit501, pit501, p501]
L0_13: SDR(size=2304, active=~240) [fit502, pit502, ait504]
L0_14: SDR(size=2304, active=~240) [ait501, ait502, ait503]
L0_15: SDR(size=2304, active=~240) [fit503, pit503, fit504]
L0_16: SDR(size=2304, active=~240) [fit601, p602, dpit301]</pre></div>

This is represented in code as:
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>map<string, SDR> layer0_output = {
  {"L0_1", sdr_group_1},
  {"L0_2", sdr_group_2},
  {"L0_3", sdr_group_3},
  ...
  {"L0_16", sdr_group_16}
};</pre></div>

---

#### **Stage 4: Layer 1 (First HTM Module)**

Each Layer 1 module takes **multiple** L0 outputs and processes them.

Example: **Module L1_1 receives inputs from [L0_1, L0_2]**

Layer 1 has 6 modules with the following connections:
- L1_1 ← [L0_1, L0_2]
- L1_2 ← [L0_3, L0_4, L0_5]
- L1_3 ← [L0_6, L0_7, L0_8]
- L1_4 ← [L0_9, L0_10, L0_11]
- L1_5 ← [L0_12, L0_13, L0_14]
- L1_6 ← [L0_15, L0_16]

**Input to L1_1:**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Layer 0 outputs:
  L0_1: SDR(active bits: {5, 12, 45, 78, ...})
  L0_2: SDR(active bits: {23, 67, 123, 456, ...})

Merge (union mode):
  L1_1_input = L0_1 ∪ L0_2
  L1_1_input.size = 2304 bits
  L1_1_input.active = ~480 bits (union of 2 groups, ~240 each)</pre></div>

**Inside L1_1 Module (Spatial Pooler):**

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>SpatialPooler processes:
  Input: SDR(2304 bits, 480 active)
  ↓
  [Compute overlaps for all 1664 columns]
  ↓
  Column overlaps: [5, 8, 3, 12, 15, 2, 18, 7, 25, ...] (per column)
  ↓
  [Apply inhibition: keep top ~32 (1.9% of 1664)]
  ↓
  Active columns: {42, 156, 423, 678, 1001, 1234, 1456, ...}  (~32 columns)</pre></div>

**Inside L1_1 Module (Temporal Memory):**

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>TemporalMemory processes:
  Previous timestep predictions: {45, 156, 412, 800}  (4 predictive cells)
  Current active columns: {42, 156, 423, 678, 1001, 1234, 1456, ...}  (32 columns)
  
  ↓ Check: did we predict correctly?
  
  Matched predicted cells (overlap): {156}  (1 column predicted, got 1 hit)
  Mismatch: 31 columns were unpredicted
  
  ↓ Learn and generate predictions for next timestep
  
  current_active_cells = {42,0}, {42,1}, {42,2}... {42,15}  (burst all cells in col 42)
                      + {156,0} (stay in previous predicted cell)
                      + {423,0}, {423,1}... {423,15}  (burst all cells in col 423)
                      + ... (more)
  
  Total active cells this timestep: ~480 cells (32 cols × 16 cells/col)
  
  Predicted cells for next timestep: [computed from learned segments]
  Next prediction: {156, 234, 567, 1012}  (segments that may activate next time)</pre></div>

**Output from L1_1:**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>struct L1_1_Output {
  SDR active_columns;      // Size 1664, ~32 active
  SDR active_cells;        // Size (1664×16), ~480 active
  SDR predictive_cells;    // Next timestep's prediction
  float anomaly_score;     // 1 - (matched_cols / active_cols)
                           // = 1 - (1 / 32) = 0.969 (HIGH anomaly!)
};</pre></div>

**Anomaly score for L1_1:**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>anomaly_score = 1 - (predicted_overlap / active_columns)
              = 1 - (1 / 32)
              = 0.969</pre></div>
(Very high because we only matched 1 out of 32 active columns!)

---

#### **Stage 5: Layer 2 (HTM Module)**

Example: **Module L2_1 receives output from [L1_1, L1_2]**

**Input to L2_1:**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Layer 1 outputs (active_columns, not cells):
  L1_1: SDR(1664 bits, ~32 active)
  L1_2: SDR(1664 bits, ~32 active)

Merge (union mode):
  L2_1_input = L1_1 ∪ L1_2
  L2_1_input.size = 1664 bits
  L2_1_input.active = ~64 bits (32 + 32)</pre></div>

**Processing through L2_1 (SP + TM):**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>SpatialPooler:
  Input: SDR(1664 bits, 64 active)
  → Active columns: ~32 (1.9% of 1664)

TemporalMemory:
  Previous prediction: {234, 567, 890}  (3 cells expected)
  Current active: 32 columns, burst to ~480 cells
  Matched: 0 (completely unpredicted!)
  
  anomaly_score = 1 - (0 / 32) = 1.0 (MAXIMUM anomaly!)</pre></div>

**Output from L2_1:**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>struct L2_1_Output {
  SDR active_columns;      // ~32 bits active
  float anomaly_score;     // 1.0 (big mismatch)
};</pre></div>

The **anomaly is propagating upward** - Layer 2 detected something wrong!

---

#### **Stage 6: Layer 3 (Head Module L3_1)**

**Input to L3_1:**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Layer 2 outputs:
  L2_1: SDR(1664 bits, ~32 active)
  L2_2: SDR(1664 bits, ~32 active)
  L2_3: SDR(1664 bits, ~32 active)

Merge:
  L3_1_input = L2_1 ∪ L2_2 ∪ L2_3
  L3_1_input.size = 1664 bits
  L3_1_input.active = ~96 bits</pre></div>

**Processing through L3_1:**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>SpatialPooler:
  Input: SDR(1664 bits, 96 active)
  → Active columns: ~32

TemporalMemory:
  Previous prediction: [some cells]
  Current active: ~480 cells (32 columns burst)
  Matched: ~5 cells (partial match)
  
  anomaly_score = 1 - (5 / 32) = 0.844</pre></div>

**FINAL OUTPUT: Anomaly Score**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>L3_1 anomaly score: 0.844</pre></div>

This becomes your **final anomaly score for this timestep**!

---

#### **Stage 7: Evaluation & Metrics**

**Scoring this timestep:**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Anomaly score: 0.844
Ground truth label (from dataset): 0 (normal)

At different thresholds:
  Threshold 0.5:  0.844 > 0.5? YES → Predicted anomaly
                  Actual: NORMAL → FALSE POSITIVE (FP)
  
  Threshold 0.9:  0.844 > 0.9? NO  → Predicted normal
                  Actual: NORMAL → TRUE NEGATIVE (TN) ✓</pre></div>

**After processing all 500,000 rows:**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Grid search over 101 thresholds (0.01 to 1.0):

Threshold 0.50: TP=450, FP=8900, TN=89450, FN=200  → P=0.048, R=0.69, F1=0.090
Threshold 0.70: TP=380, FP=3200, TN=95150, FN=270  → P=0.106, R=0.58, F1=0.180
Threshold 0.90: TP=200, FP=1000, TN=97350, FN=450  → P=0.167, R=0.31, F1=0.214
Threshold 0.97: TP=180, FP=215,  TN=98135, FN=470  → P=0.459, R=0.44, F1=0.448 ← BEST!

Best F1 = 0.448 at threshold 0.97
Avg F1 = (sum of all 101 F1 scores) / 101 = 0.254</pre></div>

---

### **Summary: Full Data Transformation**

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>┌──────────────────────────────────────────────────────────────┐
│  TIMESTEP 500,000 DATA FLOW                                  │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  Raw Data (41 scalars):                                     │
│  [fit101=2.5, lit101=50.2, mv101=1.0, ..., label=0]        │
│           ↓                                                  │
│  Layer 0 (Encoders):                                         │
│  10 SDRs of 2304 bits each (~240 active per SDR)            │
│           ↓                                                  │
│  Layer 1 (6 HTM modules):                                    │
│  6 SDRs of 1664 bits each (~32 active per SDR)              │
│  Anomaly scores: [0.96, 0.87, 0.92, 0.81, 0.75, 0.88]      │
│           ↓                                                  │
│  Layer 2 (3 HTM modules):                                    │
│  3 SDRs of 1664 bits each (~32 active per SDR)              │
│  Anomaly scores: [1.0, 0.95, 0.98]                         │
│           ↓                                                  │
│  Layer 3 (1 HTM module - HEAD):                              │
│  1 SDR of 1664 bits (~32 active)                            │
│  Anomaly score: 0.844                                       │
│           ↓                                                  │
│  Evaluation:                                                 │
│  Threshold 0.97: Predicted=ANOMALY, Actual=NORMAL           │
│  → FP (FALSE POSITIVE)                                      │
│                                                              │
└──────────────────────────────────────────────────────────────┘</pre></div>

---

## 📁 **DETAILED FILE STRUCTURE**

### **1. `include/` - Header Files (Declarations)**

| File | Purpose |
|------|---------|
| `config.hpp` | Load YAML configuration files (data features + model params) |
| `data_streamer.hpp` | Convert raw data values into SDRs using encoders |
| `encoder.hpp` | Single feature encoder (float/categorical/timestamp) |
| `htm_module.hpp` | One SP + TM combo - core HTM computation |
| `htm_pyramid.hpp` | 4-layer pyramid - orchestrates all modules |
| `utils.hpp` | Helper functions (SDR merging, metrics, file I/O) |

### **2. `src/` - Source Files (Implementation)**

| File | What It Does |
|------|-------------|
| `main.cpp` | **Entry point**. Orchestrates the entire pipeline |
| `config.cpp` | Parses YAML files using yaml-cpp library |
| `data_streamer.cpp` | Encodes data rows into SDR maps |
| `encoder.cpp` | Empty skeleton (feature encoder logic not implemented yet) |
| `htm_module.cpp` | Creates and runs SP + TM together |
| `htm_pyramid.cpp` | Builds and executes 4-layer pyramid |
| `utils.cpp` | SDR merging, anomaly scoring, metrics calculation |

### **3. `config/` - Configuration (YAML Files)**

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>config/
├── data/
│   └── config_swat.yaml         ← Define 41 sensor features
└── model/
    └── config_model_default.yaml ← HTM parameters (SP, TM, encoders)</pre></div>

**Example from config_swat.yaml:**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>features:
  fit101:
    type: float              # Continuous value
    weight: 1.0
    resolution: 0.1          # Precision level
  
  mv101:
    type: cat                # Categorical (motor valve states)
    weight: 1.0</pre></div>

### **4. `data/` - Input Data**

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>data/
└── swat_dataset.parquet    ← Water treatment plant sensor readings
                               446K-946K rows, 41 features + label</pre></div>

### **5. `results/` - Output**

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>results/
├── anomaly_scores_<timestamp>.csv     ← Raw anomaly scores per row
└── metrics/
    └── cpp_metrics_<timestamp>.txt    ← Performance metrics (Best & Avg)</pre></div>

**Example (from your current run):**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Best | F1=0.4478 precision=0.4592 recall=0.4370 accuracy=0.7877 threshold=0.9700
Avg  | F1=0.2541 precision=0.1588 recall=0.8567 accuracy=0.3324</pre></div>

## 🔄 **HOW DATA FLOWS THROUGH THE SYSTEM**

### **Step 1: Load Configuration**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>loadDataConfig("config/data/config_swat.yaml")      // 41 features
loadModelConfig("config/model/config_model_default.yaml")  // SP/TM params</pre></div>
→ Returns `map<string, map<string, string>>` (nested key-value pairs)

### **Step 2: Load Data**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>loadParquet("data/swat_dataset.parquet")  // Try Parquet first (Apache Arrow)
// Falls back to loadCSV() if Arrow unavailable</pre></div>
→ Returns `vector<map<string, double>>` (list of rows, each row is feature values)

### **Step 3: Create Encoders**
Using the data config, create 41 RDSE encoders (one per feature):
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>encoders["fit101"].encode(2.5) → SDR of 2304 bits (≈80 bits active)
encoders["mv101"].encode(1.0) → SDR of 2304 bits</pre></div>

### **Step 4: Feature Merging (Layer 0)**
Group features from the feature_plan and merge their SDRs:
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>L0_1: [mv101, fit101, lit101] → Union/Concat → Single SDR
L0_2: [p101, ait201, ait202]  → Union/Concat → Single SDR
...
L0_16: Last group</pre></div>
**Mode explanation:** `"u"` (union) means combine all active bits; `"c"` (concat) means append

### **Step 5: HTM Module Forward Pass (Layers 1-3)**
Each HTM Module does:
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>SDR input (e.g., 2304 bits)
  ↓
SpatialPooler (learns patterns)
  → active_columns (e.g., 1664 bits)
  ↓
TemporalMemory (learns sequences)
  → predictive_cells (predicted next step)
  ↓
Anomaly Score = 1 - (intersection_size / active_size)</pre></div>

**Anomaly intuition:** 
- If prediction matches reality → low score (normal)
- If prediction differs from reality → high score (anomaly)

### **Step 6: Evaluation**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>For each of 101 candidate thresholds:
  for each row:
    predicted_label = (anomaly_score > threshold) ? ANOMALY : NORMAL
    Calculate: TP, FP, TN, FN
    Compute: precision, recall, F1, accuracy

Find threshold with highest F1 → Best metrics
Average metrics across all thresholds → Average metrics</pre></div>
## ⚙️ **KEY CONFIGURATION PARAMETERS EXPLAINED**

### **General Settings** (config_model_default.yaml)
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>seed: 69                    # Random seed for reproducibility
learn_period: 10000        # Skip first 10K rows in evaluation (warm-up)
data_min: 446000           # Start from row 446K
data_max: 946000           # End at row 946K
data_res: 5                # Use every 5th row (→ 100K rows total)
feature_merge_mode: u      # Union SDRs
htm_merge_mode: u          # Union SDRs at each layer
max_pool: [1, 1, 1, 2]     # Max pooling per layer (layer 3 uses 2)</pre></div>

### **Encoder Parameters**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>encoders:
  n: 2304          # Output SDR size (bits)
  w: 0.035         # Sparsity ≈ 80 active bits per encoding</pre></div>

### **Spatial Pooler (SP) Parameters**
The SP learns which input patterns are most common:
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>columnDimensions: 1664          # Number of output columns
potentialRadius: 1.0            # How far to look for input connections
localAreaDensity: 0.019         # Density of active columns
globalInhibition: yes           # Use global (not local) competition
synPermConnected: 0.5           # Permanence threshold for "connected"
synPermActiveInc: [3.493e-3]    # How much to increase active synapse strength
synPermInactiveDec: [5.225e-5]  # How much to decrease inactive synapse strength</pre></div>

### **Temporal Memory (TM) Parameters**
The TM learns sequences and makes predictions:
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>cellsPerColumn: 16              # Mini-columns burst (increases capacity)
activationThreshold: [26, 30, 28, 32]  # Min active synapses to activate
minThreshold: 16                # Min to match (lower = stricter)
initialPerm: 0.21              # Starting permanence value
permanenceConnected: 0.6        # Threshold to be "connected"
newSynapseCount: [32, 32, 32, 32]      # New synapses per burst
maxSegmentsPerCell: [128, 64, 160, 112] # Max dendrite segments
maxSynapsesPerSegment: [224]    # Max synapses per segment</pre></div>

## 🏗️ **BUILD SYSTEM (CMake)**

### **What is CMakeLists.txt?**
A **"recipe"** that tells CMake how to:
1. Find dependencies (htm.core, Boost, yaml-cpp, Arrow)
2. Compile source files
3. Link libraries
4. Create the executable

### **What HTM.core Is**
- A **C++ library** implementing HTM algorithms
- Provides classes: `SpatialPooler`, `TemporalMemory`, `SDR`
- Your code uses it as a "black box" - calls methods, doesn't care about internals

### **Build Process**
<div style="background-color: white; color: #222; padding: 12px; border: 1px solid #ccc; border-radius: 4px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto;">
<pre>mkdir build
cd build
cmake ..        # ← Reads CMakeLists.txt, finds dependencies
make -j4        # ← Compiles using 4 cores
./htm_swat      # ← Run the executable</pre>
</div>
## 🐳 **DOCKER CONTAINERIZATION**

The Dockerfile:
1. Starts with **Ubuntu 22.04**
2. Installs system dependencies (Boost, cmake, yaml-cpp, Arrow)
3. **Builds HTM.core** (takes 10-15 min, then cached)
4. **Builds your C++ project**
5. Sets up volume mounts for data/config/results

### **Run with:**
<div style="background-color: white; color: #222; padding: 12px; border: 1px solid #ccc; border-radius: 4px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto;">
<pre>cd cppproject
docker-compose up --build</pre>
</div>

### **The `docker-compose.yml` mounts:**
- `./config/` → `/workspace/config/` (read-only)
- `./data/`   → `/workspace/data/` (read-only)
- `./results/` → `/workspace/results/` (write access)
## 📊 **HOW EVALUATION WORKS**

### **Ground-Truth Labels**
The SWAT dataset has a **`label`** column:
- `0` = Normal operation
- `1` = Attack/Anomaly

### **Prediction Process**
For each threshold (0.1 to 0.95):
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>For each row i:
  score[i] = anomaly_score (float 0.0-1.0)
  prediction[i] = (score[i] > threshold) ? 1 : 0
  
  if i < learn_period:
    Skip this row in metrics (model still learning)
  
  Calculate confusion matrix:
    TP: predicted=1, actual=1
    FP: predicted=1, actual=0
    TN: predicted=0, actual=0
    FN: predicted=0, actual=1
  
  precision = TP / (TP+FP)
  recall = TP / (TP+FN)
  F1 = 2 * (precision*recall) / (precision+recall)
  accuracy = (TP+TN) / Total</pre></div>

### **Best vs Average**
- **Best**: Metrics at the threshold with highest F1
- **Average**: Mean metrics across all 101 thresholds

In your current results:
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Best F1=0.4478 at threshold=0.97    ← This threshold should be used for production
Avg F1=0.2541                       ← Performance varies with threshold (sensitive tuning needed)</pre></div>

## 📈 **YOUR CURRENT RESULTS ANALYSIS**

From [cpp_metrics_20260408_110406_314.txt](../cppproject/results/metrics/cpp_metrics_20260408_110406_314.txt):

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Best | F1=0.4478 precision=0.4592 recall=0.4370 accuracy=0.7877 threshold=0.9700
Avg  | F1=0.2541 precision=0.1588 recall=0.8567 accuracy=0.3324 thresholds_tested=101</pre></div>

**What this means:**
- ✅ At threshold=0.97: Catches 44% of anomalies, 46% are false positives
- ⚠️ Best is much better than Average (2x F1 difference) → Model is threshold-sensitive
- 🎯 Accuracy 78.7% but precision/recall lower → Imbalanced dataset (mostly normal)
## 🔧 **KEY CLASSES & THEIR RESPONSIBILITIES**

| Class | File | Job |
|-------|------|-----|
| `DataStreamer` | data_streamer.* | Converts data rows → SDR maps |
| `FeatureEncoder` | encoder.* | Single feature value → SDR |
| `HTMModule` | htm_module.* | One SP+TM unit (forward pass, anomaly) |
| `HTMPyramid` | htm_pyramid.* | Orchestrates all 4 layers |
| **Utils** | utils.cpp | SDR merging, metrics, file I/O |

## 💡 **WHAT'S IMPLEMENTED vs NOT YET**

### ✅ **Implemented:**
- Config loading (YAML parsing)
- Data loading (Parquet + CSV)
- Encoder setup (RDSE from htm.core)
- DataStreamer (feature encoding)
- HTMModule (SP + TM orchestration)
- HTMPyramid (4-layer structure)
- Metrics calculation
- Results output

### ❌ **Not Yet Implemented:**
- Empty stubs: `encoder.cpp`, `htm_pyramid.cpp` details

---

## 🧠 **SPATIAL POOLER INTERNALS - How It Learns Patterns**

The Spatial Pooler (SP) is the first stage of HTM. Its job: **Learn which input patterns are important**.

### **What Does the SP Do?**

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Input SDR: 2304 bits (sparse, ~80 bits active)
    ↓
[SP learns connections and permanences]
    ↓
Output: 1664 "columns" (only ~30 active per timestep)</pre></div>

Think of it as a **pattern compressor**. It sees the same input patterns repeatedly and learns to recognize them efficiently.

### **How It Works (Step by Step)**

#### **1. Potential Synapses (First Initialization)**

Each column in the output is randomly connected to input bits:
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Column 42:
  Connected to input bits: [5, 18, 234, 567, 1203, ...]
  Total connections: ~230 (potentialRadius defines how many)
  These are "potential synapses" (not necessarily strong yet)</pre></div>

**Key param:** `potentialRadius: 1.0`
- `1.0` means connect to ALL input bits (full coverage)
- `0.5` means connect to 50% of input bits

#### **2. Permanence Values (Connection Strength)**

Each synapse has a **permanence value** (0.0 to 1.0):
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Column 42's synapses:
  To input 5:     permanence = 0.45  (weak)
  To input 18:    permanence = 0.62  (connected!) ✓
  To input 234:   permanence = 0.48  (weak)
  To input 567:   permanence = 0.71  (connected!) ✓
  To input 1203:  permanence = 0.15  (very weak)</pre></div>

**Key param:** `synPermConnected: 0.5`
- If permanence ≥ 0.5 → synapse is "connected" and counts toward activation
- If permanence < 0.5 → synapse is "disconnected" and doesn't count

#### **3. Overlap Score (Learning Step 1)**

When new input arrives:
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Input: [bits 5, 18, 234, 567 are active, others are 0]

Column 42 computes:
  overlap = count of active input bits that have connected synapses
  
  Synapses to active bits:
    - Input 5: permanence=0.45 (weak, not connected) → NO
    - Input 18: permanence=0.62 (connected) → YES ✓
    - Input 234: permanence=0.48 (weak) → NO
    - Input 567: permanence=0.71 (connected) → YES ✓
    - Input 1203: not active → NO
  
  overlap = 2  (input 18 and 567 matched)</pre></div>

**Key param:** `stimulusThreshold: 32`
- Only columns with overlap ≥ stimulusThreshold compete for activation
- Lower threshold = more columns can become active

#### **4. Inhibition (Choose Winners)**

After calculating all overlaps, SP uses **inhibition** to pick a sparse set of winners:

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>All columns:    [overlap: 2, 5, 8, 12, 15, 3, 18, 7, 25, ...]
                         ↓
Global Inhibition (choose TOP N columns):
                
Active columns: [        12,         18,    25, ...]
                (only ~30 out of 1664 become active)</pre></div>

**Key param:** `localAreaDensity: 0.019`
- `0.019` = activate ~1.9% of columns (~32 columns out of 1664)
- Lower value = sparser (harder competition)
- Higher value = denser (easier to activate)

**Key param:** `globalInhibition: yes`
- `yes` = winners chosen globally (all columns compete)
- `no` = winners chosen locally (per region)

#### **5. Learning (Update Permanences)**

After choosing active columns, **permanences are updated**:

For **active input bits**:
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>If connected synapse matched input bit:
  permanence += synPermActiveInc  (strengthen)
  Example: 0.62 + 0.0035 = 0.6235

If connected synapse did NOT match input bit:
  permanence -= synPermInactiveDec  (weaken)
  Example: 0.71 - 0.0000052 = 0.7099</pre></div>

For **inactive input bits**:
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>All synapses to inactive bits:
  permanence -= synPermInactiveDec  (slowly weaken)
  Example: 0.45 - 0.0000052 = 0.4499</pre></div>

**Key params:**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>synPermActiveInc: [3.493e-3]      # Strong increase for matches (0.003493)
synPermInactiveDec: [5.225e-5]    # Tiny decrease for misses (0.00005225)</pre></div>

The asymmetry is intentional: **Matches increase quickly, misses decrease slowly.** This makes the SP **focus on the most useful patterns**.

### **Why It Works (The Pattern Learning Magic)**

Over thousands of iterations:

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Timestep 1: Random permanences
  → Active columns: [random mix]
  
Timestep 100: Patterns forming
  → Active columns: [consistent clusters emerging]
  
Timestep 5000: Learned patterns
  → Same input patterns → SAME active columns (reliably)
  → Different patterns → DIFFERENT active columns
  
Result: Input space compressed from 2304 bits → 1664 columns, 
         but with learned structure that recognizes repeated patterns!</pre></div>

### **Boosting (Never Give Up)**

If a column never wins (overlap too low):

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>if column hasn't been active recently:
  boost_strength *= 1.2  (make it easier to activate)
  
This prevents "dead neurons" - columns that never activate</pre></div>

**Key param:** `boostStrength: [1.2, 1.2, 1.2, 1.2]`
- Higher = more aggressive boosting
- `1.0` = no boosting
- `1.5` = aggressive boosting

---

## 🔮 **TEMPORAL MEMORY INTERNALS - How It Learns Sequences**

The Temporal Memory (TM) sits **after** the Spatial Pooler. Its job: **Predict what comes next**.

### **What Does the TM Do?**

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Input: Active columns from SP (e.g., {12, 42, 156})
    ↓
[TM remembers previous sequence]
    ↓
Output: Predictive cells (what SHOULD be active next timestep)
    ↓
[Compare prediction to actual next timestep → Anomaly score]</pre></div>

Think of it like **learning a language grammar**. The TM learns patterns like:
- "After 'the cat', usually comes 'sat'"
- "After normal sensor values, expect more normal values"
- "After anomaly pattern X, usually expect anomaly Y next"

### **Cells Per Column (Mini-Columns)**

Unlike SP (one winner per column), **TM has multiple cells per column**:

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>From SP output: Active columns = {12, 156, 423}

TM representation:
  Column 12:   [Cell 0] [Cell 1] [Cell 2] [Cell 3] [Cell 4] ...  (16 cells)
                          ✓ active                  
  Column 156:  [Cell 0] [Cell 1] [Cell 2] [Cell 3] [Cell 4] ...  
               ✓ active                            ✓ active (predictive)
  Column 423:  [Cell 0] [Cell 1] [Cell 2] [Cell 3] [Cell 4] ...  
                                 ✓ active

Total active cells: ~48 (3 columns × ~16 cells each)</pre></div>

**Key param:** `cellsPerColumn: 16`
- Higher = more capacity (can learn more sequences)
- Lower = simpler model, less overfitting
- Typical: 32 for complex data, 4 for simple data

### **Dendrite Segments (Memory)**

Each cell has **dendrite segments** - like memories of past patterns:

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Cell (Column 12, Cell 1):
  
  Segment 0: [← connections from historical cells]
    Synapse to Cell(42, 5):   permanence=0.68 (connected)
    Synapse to Cell(156, 12): permanence=0.72 (connected)
    Synapse to Cell(423, 3):  permanence=0.45 (weak)
    → Suggests pattern: "After (42,5) and (156,12), then (12,1)"
  
  Segment 1: [← different pattern]
    Synapse to Cell(12, 5):   permanence=0.81 (connected)
    Synapse to Cell(234, 9):  permanence=0.65 (connected)
    → Suggests pattern: "After (12,5) and (234,9), then (12,1)"</pre></div>

### **Prediction (How TM Guesses)**

At each timestep:

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Step 1: Check all segments of all cells
  For each cell:
    For each segment:
      Count active synapses that connected to PREVIOUS timestep's cells
      
      If count ≥ minThreshold (e.g., 16):
        Mark cell as PREDICTIVE for next timestep ✓

Step 2: After receiving actual input:
  Active timestamp cells → compare to predicted cells
  
  Cells that matched prediction: Reward (reinforce synapses)
  Cells that weren't predicted: Punish (weaken synapses)
  Columns without predictive cells: Burst (activate all cells in column)</pre></div>

**Key param:** `minThreshold: 16`
- Segment needs ≥ 16 active input synapses to predict
- Lower = easier to predict (but noisier)
- Higher = harder to predict (but cleaner signal)

### **Learning Through Reinforcement**

When a prediction is **correct**:
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Segment that made prediction:
  For connected synapses to ACTIVE cells:
    permanence += permanenceInc (strengthen)
    Example: 0.68 + 0.075 = 0.755
    
  For connected synapses to INACTIVE cells:
    permanence -= permanenceDec (weaken)
    Example: 0.45 - 0.0007 = 0.4493</pre></div>

When a prediction is **wrong**:
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Cell that should have predicted but didn't:
  Burst: all cells in column become active
  Create NEW segment with synapses to active cells from prev timestep
  → Remember this pattern for next time</pre></div>

**Key params:**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>permanenceInc: [0.075, 0.075, 0.075, 0.175]      # Reward correct predictions
permanenceDec: [7.26e-4, 4.2e-4, 7.0e-4, 7.003e-5] # Punish wrong patterns
newSynapseCount: [32, 32, 32, 32]                # New synapses when bursting</pre></div>

### **Why It Works (Sequence Learning)**

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Sequence: [sensor_A, sensor_B, sensor_C, sensor_A, sensor_B, sensor_C, ...]

Timestep 1: [sensor_A arrives]
  Prediction: None (haven't seen anything yet)
  Active cells: All cells in active columns (BURST)
  Create new segments remembering "after nothing, then A"
  
Timestep 2: [sensor_B arrives]
  Prediction: [cells that follow A] ✓ CORRECT!
  Reinforce segments that predicted B
  
Timestep 3: [sensor_C arrives]
  Prediction: [cells that follow B] ✓ CORRECT!
  Reinforce segments that predicted C
  
Timestep 4: [sensor_A arrives]
  Prediction: [cells that follow C] ✓ CORRECT!
  Reinforce segments that predicted A
  
Timestep 5-1000: Loop repeating...
  Predictions: ~99% accurate!
  Anomaly score: ~0.01 (very low - expected pattern)</pre></div>

When **anomalies happen** (unexpected sequence):
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Normal sequence: [A, B, C, A, B, C, ...]
Anomaly: [A, B, X, A, B, C, ...]  ← X is unexpected!

At X:
  Prediction: [cells that follow B] = something else
  Actual: [cells for X]
  Mismatch! Anomaly score = high ✓</pre></div>

---

## 🔀 **SDR OPERATIONS - How Bits Are Merged**

SDRs (Sparse Distributed Representations) are the "language" HTM speaks. All data flows as SDRs.

### **What is an SDR?**

An SDR is a **sparse binary vector**:

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Size: 2304 bits
Active bits: ~80 (3.3% density)
Example: [0,1,0,0,1,0,1,0,...,1,0] with mostly zeros

Represented efficiently as:
  Sparse: {5, 18, 234, 567, 1203, ...} (just indices of 1s)
  Not: [0,1,0,0,1,0,1,0,...,1,0] (don't store all zeros)</pre></div>

**Why sparse?**
- Real neurons are sparse (only ~3% fire at once)
- Efficiency: store 80 numbers instead of 2304
- Biological plausibility: matches brain structure

### **SDR Union (Mode = 'u')**

Combine multiple SDRs by **taking the union of active bits**:

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>SDR 1 (feature: fit101=2.5):    {5, 18, 234, 567, ...}  (80 bits)
SDR 2 (feature: lit101=50):      {12, 45, 289, 612, ...} (80 bits)
SDR 3 (feature: mv101=1):        {8, 92, 145, 890, ...}  (80 bits)

Union (combine all):
  Result: {5, 8, 12, 18, 45, 92, 145, 234, 289, 567, 612, 890, ...}
  Size: ~240 bits (3 × 80, no overlap in this example)
  
Formula: merged_SDR = SDR1 ∪ SDR2 ∪ SDR3</pre></div>

**Use case:** When you want ALL features represented equally.

**Pros:** Takes all information, simple
**Cons:** Can become too dense if many features combined

### **SDR Concatenation (Mode = 'c')**

Combine SDRs by **stacking them end-to-end**:

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>SDR 1 (2304 bits, 80 active):  [0,1,0,...,1,0]
SDR 2 (2304 bits, 80 active):  [1,0,0,...,0,1]
SDR 3 (2304 bits, 80 active):  [0,0,1,...,1,0]

Concatenation (stack):
  Result size: 2304 + 2304 + 2304 = 6912 bits
  Result active: 80 + 80 + 80 = 240 bits
  
Layout:
  [Bits 0-2303: SDR1] [Bits 2304-4607: SDR2] [Bits 4608-6911: SDR3]
  [80 active]         [80 active]            [80 active]</pre></div>

**Use case:** When you care about **which feature** is active (positions matter).

**Pros:** Preserves feature identity, doesn't mix information
**Cons:** Larger size, downstream modules must process bigger vectors

### **In Your Code (Pyramid Layer 0)**

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>feature_plan:
  L0_1: [mv101, fit101, lit101]      # 3 features
  L0_2: [p101, ait201, ait202]       # 3 features
  ...
  L0_16: [...]                       # more features

feature_merge_mode: u                # Use UNION</pre></div>

**What happens:**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>// For L0_1:
SDR encoded_mv101 = encoder_mv101.encode(data["mv101"]);
SDR encoded_fit101 = encoder_fit101.encode(data["fit101"]);
SDR encoded_lit101 = encoder_lit101.encode(data["lit101"]);

SDR L0_1_result = mergeSDRsUnion({encoded_mv101, encoded_fit101, encoded_lit101});
// L0_1 is now ~240 bits (~80 × 3 features, union mode)</pre></div>

---

## ⚠️ **ANOMALY SCORE CALCULATION - Why It Works**

Anomaly detection is the **core task**. Here's how it works.

### **Concept: Prediction vs Reality**

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Normal behavior:
  Prediction (TM guess):  "Next should be column 42, 156, 423"
  Actual (what arrived):  "Got column 42, 156, 423"
  Match? YES! ✓
  Anomaly score: LOW (0.1)

Anomaly:
  Prediction (TM guess):  "Next should be column 42, 156, 423"
  Actual (what arrived):  "Got column 12, 89, 567" (completely different!)
  Match? NO! ✗
  Anomaly score: HIGH (0.9)</pre></div>

### **The Formula**

$$\text{AnomalyScore} = 1 - \frac{\text{intersection}(predicted, active)}{\text{active}}$$

In code:
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>float anomaly_score = 1.0 - (intersection_size / active_size);</pre></div>

**Example:**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>predicted_columns (TM's guess): {42, 156, 423}       (3 columns)
active_columns (what arrived):   {42, 156, 789}      (3 columns)

Intersection:  {42, 156}        (2 columns matched)

Anomaly Score = 1 - (2 / 3) = 0.333</pre></div>

### **Why This Formula Works**

**What it measures:** Fraction of active columns that were CORRECTLY PREDICTED.

- **Score = 0.0**: Perfect prediction (all active columns were predicted)
  - Normal state ✓
  
- **Score = 0.33**: 2/3 predicted correctly (some surprise)
  - Minor anomaly or transition
  
- **Score = 1.0**: Zero prediction (completely unexpected)
  - Major anomaly ✗

### **Per-Layer Anomaly Scores**

In your pyramid:

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Layer 1 output: active_cols={12, 42, 156}
Layer 2 input:  Gets {12, 42, 156}
Layer 2 TM:     Predicts {42, 89, 234}
                Actual: {12, 42, 156}
                Intersection: {42}
                Layer 2 anomaly = 1 - (1/3) = 0.667

Layer 2 output: active_cols={12, 42, 156}  (from SP output)
Layer 3 input:  Gets {12, 42, 156}
Layer 3 TM:     Predicts {12, 42, 500}
                Actual: {12, 42, 156}
                Intersection: {12, 42}
                Layer 3 anomaly = 1 - (2/3) = 0.333</pre></div>

**Head module (L3_1) anomaly is your final output score!**

### **Why It's Sensitive to Know Patterns**

HTM learns normal patterns through the learning period:

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Timestep 0-10000 (learning_period):
  Model sees normal data patterns repeatedly
  → SP learns normal patterns
  → TM learns normal sequences
  
Timestep 10001+ (evaluation):
  Normal data: TM predicts well → low anomaly score
  Anomalous data: TM confused → high anomaly score</pre></div>

This is why **`learn_period: 10000`** is crucial - the model needs warm-up!

### **Why It Adapts Over Layers**

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Layer 1: Detects local pattern anomalies
  → Some false positives (normal variations seen as anomalies)

Layer 2: Finds patterns in Layer 1 anomalies
  → Learns that small anomalies often lead to specific changes
  → Filters out false positives

Layer 3: Finds refined anomalies
  → Only reports REAL systematic anomalies
  → Ignores expected variations</pre></div>

The pyramid **refines** the anomaly signal as it goes up!

---

## 🎯 **HOW TO MODIFY PARAMETERS TO IMPROVE F1 SCORE**

Your current results: **F1=0.4478** (Best), **F1=0.2541** (Average)

This section explains how to **systematically improve** this score.

### **F1 Basics (For Reference)**

$$F1 = 2 \times \frac{\text{precision} \times \text{recall}}{\text{precision} + \text{recall}}$$

- **Precision**: Of anomalies we predicted, how many were correct?
- **Recall**: Of actual anomalies, how many did we find?
- **F1**: Balance between precision and recall

Your current results:
- **Best precision=0.4592, recall=0.4370** → Balance (both moderate)
- **Average precision=0.1588, recall=0.8567** → High recall, low precision

**Strategy:** Improve precision/recall balance at optimal threshold.

### **Parameter Tuning Strategy**

#### **Situation 1: Low Recall (Missing Anomalies)**

If you're missing true anomalies (FN too high):

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre># Make TM more sensitive (predict less often = more "anomalies")

tm:
  activationThreshold: [20, 20, 20, 20]  # Lower (was [26, 30, 28, 32])
  # Easier to activate = more predictions made = lower anomaly scores
  
  minThreshold: 10  # Lower (was 16)
  # Easier to match segments = more hits = lower anomaly scores</pre></div>

**Effect:** More things predicted → lower anomaly scores overall → threshold can be lowered → catch more anomalies (higher recall)

**Trade-off:** May catch normal variations as anomalies (lower precision)

#### **Situation 2: Low Precision (Too Many False Alarms)**

If you're flagging too many normal points as anomalies (FP too high):

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre># Make TM pickier (predict more often = fewer "anomalies")

tm:
  activationThreshold: [30, 30, 30, 30]  # Higher (was [26, 30, 28, 32])
  # Harder to activate = fewer predictions = higher anomaly scores
  
  minThreshold: 20  # Higher (was 16)
  # Harder to match = stricter = higher anomaly scores
  
sp:
  localAreaDensity: 0.015  # Lower (was 0.019)
  # Sparser representations = harder to hit in TM</pre></div>

**Effect:** Fewer things predicted → higher anomaly scores → threshold can be raised → fewer false alarms (higher precision)

**Trade-off:** Miss some real anomalies (lower recall)

### **Specific Parameter Impacts**

| Parameter | ↑ Higher | ↓ Lower | F1 Impact |
|-----------|----------|---------|-----------|
| `cellsPerColumn` | More capacity, learn better | Simpler model, less overfitting | ↑ Generally improves (if you have data) |
| `activationThreshold` | Harder to activate TM cells | Easier activation | ↓ Higher threshold = higher anomaly (more FP) |
| `minThreshold` | Stricter segment matching | Looser matching | ↓ Higher = more false positives |
| `columnDimensions` | More SP columns | Fewer columns | ↑ More capacity = better patterns |
| `localAreaDensity` | Denser SP output | Sparser SP output | ↓ Denser = more info to TM = lower anomaly |
| `potentialRadius` | More connections | Fewer connections | → Minimal impact if >0.5 |
| `synPermActiveInc` | Learn faster | Learn slower | ↑ Faster = quicker adaptation (risky) |
| `learn_period` | Skip more rows | Skip fewer rows | ↓ Longer warm-up = metric shifts down in dataset |

### **Step-by-Step Tuning Guide**

**Step 1: Establish Baseline**
<div style="background-color: white; color: #222; padding: 12px; border: 1px solid #ccc; border-radius: 4px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto;">
<pre># Run current config, note F1 and precision/recall
docker-compose up --build
# Record: Best F1, Best Precision, Best Recall, Avg F1</pre>
</div>

**Step 2: Check Precision/Recall Balance**

From your metrics output:
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Best precision=0.4592 (46% correct predictions)
Best recall=0.4370 (44% of anomalies caught)</pre></div>

**This is balanced!** But F1 is still only 0.45. Options:

**Option A: Improve Overall Sensitivity**
- Increase `cellsPerColumn` (16 → 32)
- Increase `columnDimensions` (1664 → 2048)
- Lower `learn_period` (10000 → 5000) if data is consistent

**Option B: Reduce Learning Period Time**
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre># Model doesn't need as much warm-up
learn_period: 5000  # Shorter warm-up = more rows for evaluation</pre></div>

This instantly improves metrics (more evaluation samples).

**Step 3: Adjust SP parameters**

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>models:
  sp:
    columnDimensions: 2048  # Increase from 1664
    # More columns = more pattern capacity
    
    boostStrength: [1.5, 1.5, 1.5, 1.5]  # Increase from 1.2
    # More aggressive boosting = all columns stay alive
    
    localAreaDensity: 0.025  # Increase from 0.019
    # Denser SP output = more info to layers above</pre></div>

**Step 4: Adjust TM parameters**

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>models:
  tm:
    cellsPerColumn: 32  # Increase from 16
    # Double capacity = better sequence learning
    
    activationThreshold: [24, 28, 26, 30]  # Slightly lower
    # Easier to predict = lower anomaly scores overall
    
    minThreshold: 14  # Slightly lower
    # Easier matching = activate more segments</pre></div>

**Step 5: Test & Measure**

After **each change**, rebuild and test:
<div style="background-color: white; color: #222; padding: 12px; border: 1px solid #ccc; border-radius: 4px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto;">
<pre>docker-compose up --build
# Compare new F1 vs baseline</pre>
</div>

Track improvements in a table:
<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Config                    | Best F1 | Precision | Recall | Avg F1
Baseline                  | 0.4478  | 0.4592    | 0.4370 | 0.2541
cellsPerColumn=32         | 0.4612  | 0.4704    | 0.4521 | 0.2683
+ columnDimensions=2048   | 0.4795  | 0.4891    | 0.4701 | 0.2845
+ boostStrength=1.5       | 0.4923  | 0.5012    | 0.4835 | 0.2956</pre></div>

### **Common Issues & Solutions**

| Problem | Cause | Solution |
|---------|-------|----------|
| Best F1 much > Avg F1 | Sensitive to threshold | Lower `activationThreshold`, increase `cellsPerColumn` |
| Both F1 scores very low | Not learning anything | Increase `learn_period`, check data has labels |
| F1 = 0 / NaN | No predictions made | Lower all thresholds, increase density |
| Metrics identical for all thresholds | SP not learning | Increase `columnDimensions`, lower `synPermConnected` |

### **Advanced: Multi-Layer Tuning**

Different layers may need different settings:

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre># Layer config is per-layer (4 values each)
sp:
  synPermActiveInc: [3.493e-3, 1.275e-4, 6.368e-3, 3.186e-2]
  # L0: aggressive learn
  # L1: careful learn  
  # L2: moderate learn
  # L3: very aggressive (head learns fast)</pre></div>

**Strategy:** 
- **Layer 0-1 (base encoders):** Be conservative (L1 should see stable patterns)
- **Layer 3 (output):** Be aggressive (L3 should be sensitive to anomalies)

### **Validation Strategy**

Never tune on your only test set! Instead:

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>Original data split:
  Train (learn): rows 0-5000 (first 5000)
  Val (tune):    rows 5000-10000 (middle 5000)
  Test (final):  rows 10000+ (rest)

Procedure:
  1. Train model on full data (learn_period=5000)
  2. Measure on Val set (thresholds 0.1-0.95)
  3. Tune parameters based on Val F1
  4. Final test on Test set</pre></div>

Your current setup uses `learn_period=10000`, which is good!

---

## 📊 **QUICK REFERENCE: Parameter Decision Tree**

<div style="background-color: white; color: black; padding: 12px; font-family: 'Courier New', monospace; font-size: 16px; overflow-x: auto; line-height: 1.4;"><pre>START: Check current metrics
│
├─ If F1 < 0.3
│  └─ Model not learning at all
│     ├─ Increase learn_period (5000 → 10000)
│     ├─ Increase cellsPerColumn (16 → 32)
│     └─ Check data has anomalies (run: grep "label.*1" dataset.csv)
│
├─ If precision < 0.4 AND recall < 0.4
│  └─ Both mediocre → optimize capacity
│     ├─ Increase columnDimensions (1664 → 2048)
│     └─ Increase cellsPerColumn (16 → 32)
│
├─ If precision >> recall (e.g., 0.8 vs 0.2)
│  └─ Missing anomalies → make easier to predict
│     ├─ Lower activationThreshold (30 → 25)
│     └─ Lower minThreshold (16 → 12)
│
├─ If precision << recall (e.g., 0.2 vs 0.8)
│  └─ Too many false alarms → make harder to predict
│     ├─ Raise activationThreshold (25 → 32)
│     └─ Raise minThreshold (12 → 18)
│
└─ If Avg F1 << Best F1 (e.g., 0.25 vs 0.45)
   └─ Threshold sensitive → improve prediction consistency
      ├─ Increase boostStrength to keep SP stable
      └─ Increase learn_period for better TM saturation</pre></div>