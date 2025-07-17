import os
import glob
import json
import pandas as pd
import matplotlib as mpl
import matplotlib.pyplot as plt
import seaborn as sns
import numpy as np

import matplotlib as mpl

# Use a serif font (like Times New Roman or Computer Modern)
mpl.rcParams.update({
    "font.family": "serif",
    "font.serif": ["Times New Roman", "Times", "DejaVu Serif", "Computer Modern Roman"],
    "axes.labelsize": 11,
    "axes.titlesize": 12,
    "xtick.labelsize": 8,
    "ytick.labelsize": 8,
    "legend.fontsize": 8,
    "axes.edgecolor": "black",
    "axes.linewidth": 1,
    "grid.color": "#cccccc",
    "grid.linestyle": "--",
    "grid.linewidth": 0.5,
    "axes.grid": True,
    "figure.dpi": 150,
})


def parse_log_message(log_message):
    """
    Parses the log_message string into fitness_score and gene_1 to gene_5.
    Returns a dict with these fields.
    """
    parts = log_message.strip('|').split('|')
    if len(parts) == 6:
        return {
            'fitness_score': float(parts[0]),
            'gene_1': float(parts[1]),
            'gene_2': float(parts[2]),
            'gene_3': float(parts[3]),
            'gene_4': float(parts[4]),
            'gene_5': float(parts[5]),
        }
    elif len(parts) >= 11:
        return {
            'fitness_score': float(parts[0]),
            'gene_1': float(parts[1]),
            'gene_2': float(parts[2]),
            'gene_3': float(parts[3]),
            'gene_4': float(parts[4]),
            'gene_5': float(parts[5]),
            'gene_6': float(parts[6]),
            'gene_7': float(parts[7]),
            'gene_8': float(parts[8]),
            'gene_9': float(parts[9]),
            'gene_10': float(parts[10]),
        }
    return {}

def parse_log_type(record):
    """
    Parses the log_message field in logs based on log_level and tag.
    Returns a dict of parsed fields.
    """
    log_level = record.get('log_level')
    tag = record.get('tag')
    log_type = record.get('log_type')

    parts = log_type.strip('|').split('|')
    parsed = {}

    # Case 1: CPU Utilisation
    if log_level == 'U':
        if len(parts) >= 2:
            parsed['cpu_util_core0'] = int(parts[0])
            parsed['cpu_util_core1'] = int(parts[1])
        else:
            parsed['cpu_util_core0'] = None
            parsed['cpu_util_core1'] = None

    # Case 2: Throughput
    elif log_level == 'T':
        if len(parts) >= 2:
            parsed['kbps_in'] = float(parts[0])
            parsed['kbps_out'] = float(parts[1])
        else:
            parsed['kbps_in'] = None
            parsed['kbps_out'] = None

    # Case 3: rssi matrix (mapping robot IDs)
    elif log_level == 'C':
        # Example mapping, update as needed
        robot_id_map = ['DA8C', '78C0', '2004', '669C', '9288', 'A984', 'DE9C', 'DF8C', 'B19C', 'A184',
                        '228C', 'CBAC', 'AC44', 'IIII', 'JJJJ', 'KKKK', 'LLLL', 'MMMM', 'NNNN', 'OOOO']
        for idx, robot_id in enumerate(robot_id_map):
            try:
                parsed[f'rssi_{robot_id}'] = int(parts[idx])
            except (IndexError, ValueError):
                parsed[f'rssi_{robot_id}'] = None

    # Case 4: Message received flag and latency
    elif tag == 'M' and log_level == 'I':
        if len(parts) >= 3:
            parsed['msg_rcv_flag'] = parts[0]
            parsed['latency'] = int(parts[1])
            parsed['rcv_robot_id'] = parts[2]
        else:
            parsed['msg_rcv_flag'] = None
            parsed['latency'] = None
            parsed['rcv_robot_id'] = None

    # Default: just take log_type as is
    else:
        parsed['log_type_value'] = log_type

    return parsed

def ingest_data(root_folder):
    messages_data = []
    logs_data = []
    metadata_data = []

    print("Current working directory:", os.getcwd())

    for device_mac_id in os.listdir(root_folder):
        device_path = os.path.join(root_folder, device_mac_id)
        if not os.path.isdir(device_path):
            continue

        for data_type in ['messages', 'logs', 'metadata']:
            data_path = os.path.join(device_path, data_type)
            if not os.path.isdir(data_path):
                continue

            for json_file in glob.glob(os.path.join(data_path, '*.json')):
                experiment_id = os.path.basename(json_file).split('_')[0]
                with open(json_file, 'r') as f:
                    try:
                        lines = [line.strip() for line in f if line.strip()]
                        if len(lines) == 1:
                            try:
                                data = json.loads(lines[0])
                                # If it's a dict, wrap in list for uniformity
                                if isinstance(data, dict):
                                    data = [data]
                            except Exception:
                                data = [json.loads(line) for line in lines]
                        else:
                            data = [json.loads(line) for line in lines]
                    except Exception as e:
                        print(f"Error loading {json_file}: {e}") 
                        data = []

                # Flatten: each record gets its own row
                for entry in data:
                    record = {
                        'device_mac_id': device_mac_id,
                        'experiment_id': experiment_id,
                        **entry  # Unpack the JSON fields into columns
                    }
                    if data_type == 'messages' and 'log_message' in record:
                        record.update(parse_log_message(record['log_message']))
                    if data_type == 'logs' and 'log_type' in record:
                        record.update(parse_log_type(record))

                    if data_type == 'messages':
                        messages_data.append(record)
                    elif data_type == 'logs':
                        logs_data.append(record)
                    elif data_type == 'metadata':
                        metadata_data.append(record)

    messages_df = pd.DataFrame(messages_data)
    logs_df = pd.DataFrame(logs_data)
    metadata_df = pd.DataFrame(metadata_data)
    return messages_df, logs_df, metadata_df

# Example usage:
messages_df, logs_df, metadata_df = ingest_data('data_analysis/temp/data')

##################### EXPERIMENT SCOPE ########################
in_scope = metadata_df[
    (metadata_df['num_robots'] == 13) &
    (metadata_df['robot_speed'] == 0.0) &
    (metadata_df['topology'] == 0) &
    (metadata_df['msg_limit'] == 0) &
    (metadata_df['max_genes'] == 10) 
]
in_scope_experiment_ids = in_scope['experiment_id'].unique()

# Filter logs_df and messages_df to only those experiments
logs_df = logs_df[logs_df['experiment_id'].isin(in_scope_experiment_ids)]
messages_df = messages_df[messages_df['experiment_id'].isin(in_scope_experiment_ids)]


##################### DATA COMPLETNESS ########################

# 1. Total number of unique experiments
unique_experiments = logs_df['experiment_id'].nunique()
print(f"Total unique experiments: {unique_experiments}")
# 2. Check for missing data from devices per experiment
# Get all unique device_mac_ids
all_devices = logs_df['device_mac_id'].unique()
print(f"Devices found: {all_devices}")
# Create a pivot table to see which device has logs for which experiment
completness_pivot = logs_df.pivot_table(index='experiment_id', columns='device_mac_id', values='log_id', aggfunc='count', fill_value=0)
# Find experiments where any device is missing (i.e., count is 0)
missing_data = completness_pivot.apply(lambda row: any(row == 0), axis=1)
experiments_with_missing = completness_pivot[missing_data]

print(f"Number of experiments with missing device data: {experiments_with_missing.shape[0]}")

for experiment_id, row in experiments_with_missing.iterrows():
    missing_devices = row[row == 0].index.tolist()
    print(f"Experiment {experiment_id} is missing data from devices: {missing_devices}")

##################### ANALYSIS & PLOTS ########################

# Only keep experiments with all devices present
complete_experiments = completness_pivot[~completness_pivot.apply(lambda row: any(row == 0), axis=1)].index.tolist()
logs_complete = logs_df[logs_df['experiment_id'].isin(complete_experiments)]
logs_complete['log_datetime'] = pd.to_datetime(logs_complete['log_datetime'], unit='s')

# Define a consistent color palette for devices
device_ids = sorted(logs_complete['device_mac_id'].unique())
palette = sns.color_palette("tab10", n_colors=len(device_ids))
device_color_map = dict(zip(device_ids, palette))

###LATENCY###

latency_logs = logs_complete[logs_complete['latency'].notnull()]

latency_logs['second'] = latency_logs['log_datetime'].dt.floor('s')
latency_per_sec = (
    latency_logs.groupby(['experiment_id', 'device_mac_id', 'second'])['latency']
    .mean()
    .reset_index()
)

# Create a full DataFrame with all seconds for each experiment and device
full_rows = []
for (exp_id, dev_id), group in latency_per_sec.groupby(['experiment_id', 'device_mac_id']):
    start = group['second'].min()
    end = group['second'].max()
    all_seconds = pd.date_range(start, end, freq='s')
    for sec in all_seconds:
        full_rows.append({'experiment_id': exp_id, 'device_mac_id': dev_id, 'second': sec})

full_df = pd.DataFrame(full_rows)
latency_full = pd.merge(full_df, latency_per_sec, on=['experiment_id', 'device_mac_id', 'second'], how='left')
latency_full['exp_start'] = latency_full.groupby('experiment_id')['second'].transform('min')
latency_full['second_offset'] = (latency_full['second'] - latency_full['exp_start']).dt.total_seconds().astype(int)

# Compute mean and std by second_offset and device
agg = latency_full.groupby(['second_offset', 'device_mac_id'])['latency'].agg(['mean', 'std']).reset_index()
# Compute overall network mean by second_offset
network_mean = latency_full.groupby('second_offset')['latency'].mean().reset_index()

###Error Rate###

# Filter for complete experiments and not-null msg_rcv_flag
msg_logs = logs_complete[logs_complete['msg_rcv_flag'].notnull()]
# Count successful messages per device per experiment
success_counts = (
    msg_logs[msg_logs['msg_rcv_flag'] == "O"]
    .groupby(['experiment_id', 'device_mac_id'])
    .size()
    .reset_index(name='success_count')
)

# Prepare error rate data
msg_logs['second'] = msg_logs['log_datetime'].dt.floor('s')
msg_logs['exp_start'] = msg_logs.groupby('experiment_id')['second'].transform('min')
msg_logs['second_offset'] = (msg_logs['second'] - msg_logs['exp_start']).dt.total_seconds().astype(int)

error_rate = (
    msg_logs.groupby('second_offset')['msg_rcv_flag']
    .apply(lambda x: (x != "O").mean())
    .reset_index(name='error_rate')
)
error_rate['error_rate'] = (error_rate['error_rate'] * 100).round().astype(int)

###Throughput and RSSI###
# Filter for throughput records
throughput_logs = logs_complete[logs_complete['kbps_in'].notnull() | logs_complete['kbps_out'].notnull()].copy()
# Calculate second_offset
throughput_logs['second'] = throughput_logs['log_datetime'].dt.floor('s')
throughput_logs['exp_start'] = throughput_logs.groupby('experiment_id')['second'].transform('min')
throughput_logs['second_offset'] = (throughput_logs['second'] - throughput_logs['exp_start']).dt.total_seconds().astype(int)
throughput_logs['second_bucket'] = (throughput_logs['second_offset'] // 2) * 2

# Throughput: group by second_bucket
throughput_agg = throughput_logs.groupby(['experiment_id', 'second_bucket'])[['kbps_in', 'kbps_out']].mean().reset_index()
throughput_time = throughput_agg.groupby('second_bucket')[['kbps_in', 'kbps_out']].agg(['mean', 'std']).reset_index()
throughput_time.columns = ['second_bucket', 'kbps_in_mean', 'kbps_out_mean', 'kbps_in_std', 'kbps_out_std']

# Global averages
mean_kbps_in = throughput_time['kbps_in_mean'].mean()
mean_kbps_out = throughput_time['kbps_out_mean'].mean()


# Find all columns that start with 'rssi_'
rssi_cols = [col for col in logs_complete.columns if col.startswith('rssi_')]
# Melt to long format for plotting
rssi_long = logs_complete.melt(
    id_vars=['device_mac_id', 'log_datetime', 'experiment_id'],
    value_vars=rssi_cols,
    var_name='rssi_target',
    value_name='rssi'
).dropna(subset=['rssi'])

# Calculate second_offset
rssi_long['second'] = pd.to_datetime(rssi_long['log_datetime'], unit='s').dt.floor('s')
rssi_long['exp_start'] = rssi_long.groupby('experiment_id')['second'].transform('min')
rssi_long['second_offset'] = (rssi_long['second'] - rssi_long['exp_start']).dt.total_seconds().astype(int)
rssi_long['second_bucket'] = (rssi_long['second_offset'] // 2) * 2

# Group by device and second_offset, mean across experiments
rssi_agg = rssi_long.groupby(['device_mac_id', 'second_bucket'])['rssi'].agg(['mean', 'std']).reset_index()
mean_rssi = rssi_agg['mean'].mean()

fig, axes = plt.subplots(2, 3, figsize=(20, 10))

# (a) Exchanged Messages (boxplot)
sns.boxplot(
    x='device_mac_id',
    y='success_count',
    data=success_counts,
    showfliers=False,
    ax=axes[0, 0],
    palette=device_color_map
)
axes[0, 0].set_title('(a)', fontweight='bold')
axes[0, 0].set_xlabel('Device ID')
axes[0, 0].set_ylabel('Successful Message Count')
axes[0, 0].spines['top'].set_visible(False)
axes[0, 0].spines['right'].set_visible(False)

# (b) Latency
for dev_id in agg['device_mac_id'].unique():
    dev_data = agg[agg['device_mac_id'] == dev_id]
    axes[0, 1].plot(
        dev_data['second_offset'],
        dev_data['mean'],
        label=f'ID: {dev_id}',
        color=device_color_map.get(dev_id, None)
    )
    axes[0, 1].fill_between(
        dev_data['second_offset'],
        dev_data['mean'] - dev_data['std'],
        dev_data['mean'] + dev_data['std'],
        alpha=0.2,
        color=device_color_map.get(dev_id, None)
    )
overall_mean = latency_full['latency'].mean()
axes[0, 1].axhline(overall_mean, color='k', linestyle='--', label='Mean')
axes[0, 1].set_title('(b)', fontweight='bold')
axes[0, 1].set_xlabel('Time (s)')
axes[0, 1].set_ylabel('Mean Latency (ms)')
axes[0, 1].set_xlim(0, 120)
axes[0, 1].set_xticks(range(0, 121, 20))
axes[0, 1].legend(loc='upper right', frameon=False)
axes[0, 1].spines['top'].set_visible(False)
axes[0, 1].spines['right'].set_visible(False)

# (c) Latency Distribution (kde)
sns.kdeplot(latency_full['latency'].dropna(), fill=True ,ax=axes[0, 2], color='#1f77b4', alpha=0.3)
axes[0, 2].set_title('(c)', fontweight='bold')
axes[0, 2].set_xlabel('Latency (ms)')
axes[0, 2].set_ylabel('Density')
axes[0, 2].spines['top'].set_visible(False)
axes[0, 2].spines['right'].set_visible(False)


# (d) Error Rate Over Time
sns.barplot(
    x='second_offset',
    y='error_rate',
    data=error_rate,
    color='#d62728',
    ax=axes[1, 0]
)
axes[1, 0].set_title('(d)', fontweight='bold')
axes[1, 0].set_xlabel('Time (s)')
axes[1, 0].set_ylabel('Error Rate (%)')
max_sec = error_rate['second_offset'].max()
axes[1, 0].set_xticks(range(0, int(max_sec) + 1, 20))
axes[1, 0].spines['top'].set_visible(False)
axes[1, 0].spines['right'].set_visible(False)

# (e) Throughput
axes[1, 1].plot(throughput_time['second_bucket'], throughput_time['kbps_in_mean'], color='green', label='In')
axes[1, 1].fill_between(
    throughput_time['second_bucket'],
    np.maximum(throughput_time['kbps_in_mean'] - throughput_time['kbps_in_std'], 0),
    throughput_time['kbps_in_mean'] + throughput_time['kbps_in_std'],
    color='green', alpha=0.2
)
axes[1, 1].plot(throughput_time['second_bucket'], throughput_time['kbps_out_mean'], color='blue', label='Out')
axes[1, 1].fill_between(
    throughput_time['second_bucket'],
    np.maximum(throughput_time['kbps_out_mean'] - throughput_time['kbps_out_std'], 0),
    throughput_time['kbps_out_mean'] + throughput_time['kbps_out_std'],
    color='blue', alpha=0.2
)
axes[1, 1].axhline(throughput_time['kbps_in_mean'].mean(), color='green', linestyle='--', label='Mean In')
axes[1, 1].axhline(throughput_time['kbps_out_mean'].mean(), color='blue', linestyle='--', label='Mean Out')
axes[1, 1].set_title('(e)', fontweight='bold')
axes[1, 1].set_xlabel('Time (s)')
axes[1, 1].set_ylabel('Throughput (kbps)')
axes[1, 1].set_xlim(0, 120)
axes[1, 1].set_xticks(range(0, 121, 20))
axes[1, 1].legend(loc='upper right', frameon=False)
axes[1, 1].spines['top'].set_visible(False)
axes[1, 1].spines['right'].set_visible(False)

# (f) RSSI
for dev_id in rssi_agg['device_mac_id'].unique():
    dev_data = rssi_agg[rssi_agg['device_mac_id'] == dev_id]
    axes[1, 2].plot(dev_data['second_bucket'], dev_data['mean'], label=f'ID: {dev_id}', color=device_color_map.get(dev_id, None))
    axes[1, 2].fill_between(
        dev_data['second_bucket'],
        dev_data['mean'] - dev_data['std'],
        dev_data['mean'] + dev_data['std'],
        color=device_color_map.get(dev_id, None),
        alpha=0.2
    )
axes[1, 2].axhline(mean_rssi, color='k', linestyle='--', label='Mean')
axes[1, 2].set_title('(f)', fontweight='bold')
axes[1, 2].set_xlabel('Time (s)')
axes[1, 2].set_ylabel('RSSI (dBm)')
axes[1, 2].set_xlim(0, 120)
axes[1, 2].set_xticks(range(0, 121, 20))
axes[1, 2].legend(loc='upper right', frameon=False)
axes[1, 2].spines['top'].set_visible(False)
axes[1, 2].spines['right'].set_visible(False)

plt.tight_layout(rect=[0, 0.05, 1, 1])
plt.subplots_adjust(hspace=0.3, wspace= 0.2, left=0.05, top =0.93)
#plt.savefig('/home/yallicol/Documents/robotics_dissertation/report/base_comm_stats.pdf', bbox_inches='tight')
plt.show()

############### Calculate and print jitter, bandwidth and QoS #################

# Calculate jitter using second_offset
latency_full_sorted = latency_full.sort_values(['experiment_id', 'device_mac_id', 'second_offset'])
latency_full_sorted['latency_diff'] = latency_full_sorted.groupby(['experiment_id', 'device_mac_id'])['latency'].diff()
jitter_per_group = latency_full_sorted.groupby(['experiment_id', 'device_mac_id'])['latency_diff'].std()
mean_jitter = jitter_per_group.mean()
print(f"Average Jitter (ms): {mean_jitter:.2f}")

# Bandwidth: max throughput observed (in or out)
max_bandwidth = max(
    throughput_logs['kbps_in'].max(skipna=True),
    throughput_logs['kbps_out'].max(skipna=True)
)
print(f"Max Bandwidth (kbps): {max_bandwidth:.2f}")
# QoS calculation
w1 = w2 = w3 = 1/3
# Use mean error rate as a fraction
mean_error_rate = error_rate['error_rate'].mean() / 100.0
print(f"Mean Error Rate (%): {mean_error_rate:.2f}")
max_jitter = jitter_per_group.max()
jitter_norm = mean_jitter / max_jitter if max_jitter > 0 else 0
# Normalize throughput (mean of kbps_in and kbps_out, divided by max bandwidth)
mean_throughput = np.nanmean([throughput_logs['kbps_in'].mean(), throughput_logs['kbps_out'].mean()])
throughput_norm = mean_throughput / max_bandwidth if max_bandwidth > 0 else 0
# QoS formula
qos = w1 * (1 - mean_error_rate) + w2 * (1 - jitter_norm) + w3 * throughput_norm
print(f"QoS (0-1): {qos:.3f}")

####### Fitness & System Metrics ########

# Filter messages for complete experiments
messages_complete = messages_df[messages_df['experiment_id'].isin(complete_experiments)].copy()
messages_complete['log_datetime'] = pd.to_datetime(messages_complete['log_datetime'], unit='s')

# Calculate second_offset for each experiment
messages_complete['second'] = messages_complete['log_datetime'].dt.floor('s')
messages_complete['exp_start'] = messages_complete.groupby('experiment_id')['second'].transform('min')
messages_complete['second_offset'] = (messages_complete['second'] - messages_complete['exp_start']).dt.total_seconds().astype(int)

# (a) Mean fitness score over time by device (with std)
fitness_agg = messages_complete.groupby(['device_mac_id', 'second_offset'])['fitness_score'].agg(['mean', 'std']).reset_index()

# (b) Distribution of final fitness scores (kde)
# Get the last fitness_score for each experiment and device
final_fitness = (
    messages_complete.sort_values(['experiment_id', 'second_offset'])
    .groupby(['experiment_id', 'device_mac_id'])
    .tail(1)
)

# (c) CPU Utilisation Plot Data Preparation
cpu_logs = logs_complete[logs_complete['cpu_util_core0'].notnull() & logs_complete['cpu_util_core1'].notnull()].copy()
cpu_logs['second'] = cpu_logs['log_datetime'].dt.floor('s')
cpu_logs['exp_start'] = cpu_logs.groupby('experiment_id')['second'].transform('min')
cpu_logs['second_offset'] = (cpu_logs['second'] - cpu_logs['exp_start']).dt.total_seconds().astype(int)

cpu_agg = cpu_logs.groupby('second_offset')[['cpu_util_core0', 'cpu_util_core1']].agg(['mean', 'std']).reset_index()
cpu_agg.columns = ['second_offset', 'core0_mean', 'core1_mean', 'core0_std', 'core1_std']


fig, axes = plt.subplots(1, 3, figsize=(12, 3))

# (a) Mean fitness score over time by device
for dev_id in fitness_agg['device_mac_id'].unique():
    dev_data = fitness_agg[fitness_agg['device_mac_id'] == dev_id]
    color = device_color_map.get(dev_id, None)
    axes[0].plot(dev_data['second_offset'], dev_data['mean'], label=f'ID: {dev_id}', color=color, linewidth=1)
    axes[0].fill_between(
        dev_data['second_offset'],
        dev_data['mean'] - dev_data['std'],
        dev_data['mean'] + dev_data['std'],
        color=color,
        alpha=0.15
    )
axes[0].set_title('(a)', fontweight='bold')
axes[0].set_xlabel('Time (s)')
axes[0].set_ylabel('Fitness Score')
axes[0].legend(loc='upper left', frameon=False)
axes[0].spines['top'].set_visible(False)
axes[0].spines['right'].set_visible(False)

# (b) KDE of final fitness scores
sns.kdeplot(final_fitness['fitness_score'], fill=True, ax=axes[1], color='#1f77b4', alpha=0.3)
axes[1].set_title('(b)', fontweight='bold')
axes[1].set_xlabel('Final Fitness Score')
axes[1].set_ylabel('Density')
axes[1].spines['top'].set_visible(False)
axes[1].spines['right'].set_visible(False)

# (c) CPU Utilisation %
core0_color = '#FFC300'  # yellow
core1_color = '#FF5733'  # orange
axes[2].plot(cpu_agg['second_offset'], cpu_agg['core0_mean'], color=core0_color, label='CPU 0', linewidth=1)
# axes[2].fill_between(
#     cpu_agg['second_offset'],
#     np.maximum(cpu_agg['core0_mean'] - cpu_agg['core0_std'], 0),
#     cpu_agg['core0_mean'] + cpu_agg['core0_std'],
#     color=core0_color, alpha=0.15
# )
axes[2].plot(cpu_agg['second_offset'], cpu_agg['core1_mean'], color=core1_color, label='CPU 1', linewidth=1)
# axes[2].fill_between(
#     cpu_agg['second_offset'],
#     np.maximum(cpu_agg['core1_mean'] - cpu_agg['core1_std'], 0),
#     cpu_agg['core1_mean'] + cpu_agg['core1_std'],
#     color=core1_color, alpha=0.15
# )
axes[2].set_title('(c)', fontweight='bold')
axes[2].set_xlabel('Time (s)')
axes[2].set_ylabel('CPU Utilisation (%)')
#axes[2].set_ylim(0, 120)
axes[2].legend(loc='upper right', frameon=False)
axes[2].spines['top'].set_visible(False)
axes[2].spines['right'].set_visible(False)


plt.tight_layout()
plt.subplots_adjust(wspace=0.25, left=0.08, top=0.92)
#plt.savefig('/home/yallicol/Documents/robotics_dissertation/report/base_fitness_stats.pdf', bbox_inches='tight')
plt.show()


print("done")

