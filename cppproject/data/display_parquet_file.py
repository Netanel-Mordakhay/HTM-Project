import pandas as pd

# Load parquet file
df = pd.read_parquet('/home/abed/final-project/HTM-Project/cppproject/data/swat_dataset.parquet')

# Display basic info
print("Shape:", df.shape)
print("\nFirst 5 rows:")
print(df.head())
print("\nColumn names:")
print(df.columns.tolist())
print("\nData types:")
print(df.dtypes)
print("\nMemory usage:")
print(df.memory_usage(deep=True))