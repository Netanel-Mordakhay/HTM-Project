import pandas as pd
import yaml

# Load parquet file
df = pd.read_parquet('/home/abed/final-project/HTM-Project/cppproject/data/swat_dataset.parquet')
parquet_features = set([col for col in df.columns if col not in ['timestamp', 'label']])

# Load config file
with open('/home/abed/final-project/HTM-Project/cppproject/config/data/config_swat.yaml', 'r') as f:
    config = yaml.safe_load(f)

config_features = set([key for key in config['features'].keys() if key != 'timestamp'])

# Compare
missing_in_data = config_features - parquet_features
extra_in_data = parquet_features - config_features

print(f"Features in config but NOT in parquet data: {missing_in_data}")
print(f"Features in parquet data but NOT in config: {extra_in_data}")
print(f"\nConfig features: {len(config_features)}")
print(f"Parquet features: {len(parquet_features)}")