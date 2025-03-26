import pandas as pd
import os
import re

# Function to extract fitness_score (first value from 'log_message')
def extract_fitness_score(log_message):
    if isinstance(log_message, str):
        values = log_message.split('|')
        try:
            return float(values[0])  # Convert first value to float
        except ValueError:
            return None
    return None

# Function to extract experiment_id from filename (first 10-digit sequence)
def extract_experiment_id(filename):
    if isinstance(filename, str):
        match = re.search(r'\d{10}', filename)
        if match:
            return int(match.group(0))  
    return None

# Load the CSV file
def process_athena_output(input_file, log_file, output_file):
    # Read the CSV file
    df = pd.read_csv(input_file)
    
    # Ensure experiment_start is a valid Unix timestamp and is >= 2025
    df = df[pd.to_numeric(df['experiment_start'], errors='coerce').notna()]
    df = df[df['experiment_start'] >= 1735689600]  # Unix timestamp for 2025-01-01
    
    # Keep rows where either 'pop_size' or 'max_genes' is present (not NaN or 0)
    df = df[(df['pop_size'].notna() & (df['pop_size'] > 0)) | 
            (df['max_genes'].notna() & (df['max_genes'] > 0))]
    
    # Read the log file
    df_logs = pd.read_csv(log_file)
    
    # Apply extraction functions
    df_logs['fitness_score'] = df_logs['log_message'].apply(extract_fitness_score)
    df_logs['experiment_id'] = df_logs['filename'].apply(extract_experiment_id)
    
    # Ensure experiment_id columns are of the same type
    df['experiment_id'] = df['experiment_id'].astype(int)
    df_logs['experiment_id'] = df_logs['experiment_id'].astype(int)
    
    # Merge with the original dataset based on experiment_id
    df_merged = df.merge(df_logs[['experiment_id', 'log_id', 'log_datetime', 'fitness_score']], on='experiment_id', how='left')
    df_merged = df_merged[df_merged['experiment_start'].notna()]  
    
    # Convert Unix timestamps to datetime
    df_merged['experiment_start'] = pd.to_datetime(df_merged['experiment_start'], unit='s')
    df_merged['experiment_end'] = pd.to_datetime(df_merged['experiment_end'], unit='s')
    df_merged['log_datetime'] = pd.to_datetime(df_merged['log_datetime'], unit='s')
    
    # Save the processed DataFrame to a new CSV file
    df_merged.to_csv(output_file, index=False)
    print(f"Processed data saved to {output_file}")

# Example usage
script_dir = os.path.dirname(__file__)
input_file = os.path.join(script_dir, "temp/metadata.csv")
log_file = os.path.join(script_dir, "temp/logs.csv")
output_file = os.path.join(script_dir, "temp/results/filtered_output.csv")  # Replace with desired output file path
process_athena_output(input_file, log_file, output_file)
