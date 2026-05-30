# Configuration Files

This directory contains configuration files that match the Python implementation in `quickstart-swat.py`.

## Structure

```
config/
├── data/
│   └── config--swat.yaml      # Feature definitions for SWAT dataset
└── model/
    └── config--model_default.yaml  # HTM model parameters
```

## Files

### config/data/config--swat.yaml

Defines all features used in the SWAT dataset:

- **Feature types**: `float`, `cat` (categorical), `timestamp`
- **Resolutions**: For float features (e.g., 0.1, 1.0, 0.01)
- **Weights**: Feature importance weights

**Key features:**

- `fit101`, `fit201`, `fit301`, etc. - Flow indicators (float)
- `lit101`, `lit301`, `lit401` - Level indicators (float)
- `ait201`, `ait202`, etc. - Analog input tags (float)
- `mv101`, `p101`, `p201`, etc. - Motor valves and pumps (categorical)
- `timestamp` - Time-based encoding

### config/model/config--model_default.yaml

Defines HTM model parameters matching Python's default configuration:

**General settings:**

- `seed: 69` - Random seed for reproducibility
- `learn_period: 1000` - Number of iterations for learning
- `htm_merge_mode: u` - Union mode for merging SDRs
- `data_min: 540_000` - Starting row index
- `data_max: 700_000` - Ending row index
- `data_res: 5` - Row sampling resolution

**Encoder settings:**

- `n: 600` - Encoder size (bits)
- `w: 0.0166` - Sparsity (active bits ratio)

**Spatial Pooler (SP) settings:**

- `columnDimensions: [2048]` - Number of columns
- `potentialRadius: 1` - Potential connection radius
- `localAreaDensity: 0.02` - Local area density
- `globalInhibition: yes` - Use global inhibition
- `stimulusThreshold: 10` - Minimum overlap threshold
- `synPermConnected: 0.2` - Connected permanence threshold
- `synPermActiveInc: 0.003` - Active permanence increment
- `synPermInactiveDec: 0.0005` - Inactive permanence decrement

**Temporal Memory (TM) settings:**

- `cellsPerColumn: 4` - Cells per column
- `minThreshold: 10` - Minimum segment activation threshold
- `activationThreshold: 13` - Segment activation threshold
- `initialPerm: 0.21` - Initial permanence value
- `permanenceConnected: 0.5` - Connected permanence threshold
- `permanenceInc: 0.2` - Permanence increment
- `permanenceDec: 0.02` - Permanence decrement
- `predictedSegmentDecrement: 0.008` - Predicted segment decrement
- `maxSegmentsPerCell: 32` - Maximum segments per cell
- `maxSynapsesPerSegment: 128` - Maximum synapses per segment
- `newSynapseCount: 20` - New synapse count

## Usage

These configuration files will be used by the C++ implementation to:

1. **Match Python behavior**: Ensure same parameters are used
2. **Feature encoding**: Define how each feature is encoded
3. **Model configuration**: Set HTM algorithm parameters

## Notes

- These files are copied from the Python project to ensure consistency
- Any changes to these files should be reflected in both implementations
- The C++ code will need to parse these YAML files (or hardcode the values)
