import pandas as pd
import numpy as np
from datetime import datetime, timedelta
from pathlib import Path
from sklearn.metrics.pairwise import cosine_similarity

ROOT = Path(__file__).parent        # directory where this .py lives
logs_path     = ROOT / "three_device_results/logs.parquet"
metadata_path = ROOT / "three_device_results/metadata.parquet"
messages_path = ROOT / "three_device_results/messages.parquet"

###############################################################################
# 1. Load the raw parquet data  (adapt the paths if needed)
###############################################################################
logs      = pd.read_parquet(logs_path)
metadata  = pd.read_parquet(metadata_path)
messages  = pd.read_parquet(messages_path)

###############################################################################
# 2. Join:  logs  ←→  metadata   on  [device_mac_id, experiment_id]
###############################################################################
logs_meta = logs.merge(
    metadata,
    on=["device_mac_id", "experiment_id"],
    how="left",
    validate="many_to_one"           # each (device, experiment) has one meta row
)

###############################################################################
# 3. Filter out experiments with  max_gene == 5   (in the metadata)
###############################################################################
logs_meta = logs_meta.query("max_genes != 5")

###############################################################################
# 4. Join:  logs_meta  ←→  messages   on  [device_mac_id, experiment_id, log_id]
###############################################################################
full_df = logs_meta.merge(
    messages,
    on=["device_mac_id", "experiment_id", "log_id"],
    how="left",
    validate="many_to_one"           # each log row has ≤1 matching message row
)

###############################################################################
# 5. Build SECOND_OFFSET  — faithful to your DAX formula
###############################################################################
EPOCH = datetime(1970, 1, 1)

def second_offset(row) -> int:
    """
    Re-create the DAX logic in pure Python:

      • experiment_id → YYMMDDHHMI  (10-digit string)
      • Local time   = that timestamp   (no seconds component)
      • UTC time     = local time  – 1 hour   (assumes UTC+1 summer/winter free)
      • experimentUnix = seconds since 1970-01-01 00:00:00 UTC
      • second_offset = row.log_datetime  –  experimentUnix
    """
    # -- parse YY MM DD HH MI from the padded experiment_id
    exp_str = f"{int(row.experiment_id):010d}"
    yy, mm, dd = int(exp_str[:2]),  int(exp_str[2:4]), int(exp_str[4:6])
    hh, mi     = int(exp_str[6:8]), int(exp_str[8:10])

    full_year   = 2000 + yy                     # adjust if <2000 data exists
    local_time  = datetime(full_year, mm, dd, hh, mi, 0)
    utc_time    = local_time - timedelta(hours=1)  # local = UTC+1  →  subtract 1 h
    experiment_unix = int((utc_time - EPOCH).total_seconds())

    return int(row.log_datetime_x) - experiment_unix   # both in seconds

# apply vectorised where practical; fallback to .apply for clarity
full_df["second_offset"] = full_df.apply(second_offset, axis=1, raw=False)

###############################################################################
# 6. Optional: sanity checks
###############################################################################
print(full_df.columns)
print(full_df.second_offset.describe())

###############################################################################
# 7. Statictics
###############################################################################

# “Design” columns that uniquely describe an experiment
exp_keys = [
    "experiment_id",       # already unique
    "max_genes",
    "num_robots",
    "topology",            # or whatever your column is called
    "robot_speed",
    "msg_limit",
]

# Spine of the experiment table
exp_tbl = (
    full_df[exp_keys]
    .drop_duplicates()      # one row per experiment_id
    .set_index("experiment_id")
)

agg_plan = {
    "fitness_score"   : ["mean", "std", "min", "max"],
    "log_id"          : ["count"], 
    "cpu_util_core0"  : ["mean", "std"],
    "cpu_util_core1"  : ["mean", "std"],
    "latency"      : ["mean", "std"],
    "kbps_in"         : ["mean", "std"],
    "kbps_out"        : ["mean", "std"],
}

agg_df = (
    full_df
      .groupby("experiment_id")
      .agg(agg_plan)
)

# tidy up the multi-level column names:  fitness_score_mean, fitness_score_std, …
agg_df.columns = [
    f"{col}_{stat}" if stat != "" else col
    for col, stat in agg_df.columns
]

# Append to the experiment-level table
exp_tbl = exp_tbl.join(agg_df, how="left")

###Custom Columns

n_devices = (
    full_df.groupby("experiment_id")["device_mac_id"]
           .nunique()                          # → Series indexed by experiment_id
)

full_df["n_devices"] = full_df["experiment_id"].map(n_devices)

# FINAL fitness = value at the largest log_id per experiment
final_fit = (
    full_df
      .sort_values(["experiment_id", "log_id"])
      .groupby("experiment_id")
      .tail(1)                      # keep last row of each group
      .set_index("experiment_id")["fitness_score"]
      .rename("fitness_final")
)

# IMPROVEMENT per second  (first – last) / total_time
impr_per_sec = (
    full_df
      .groupby("experiment_id")
      .apply(lambda g: (g.fitness_score.iloc[0] - g.fitness_score.iloc[-1]) /
                       max(g.second_offset))
      .rename("fitness_delta_per_sec")
)

###############################################################################
# RSSI
###############################################################################

rssi_cols = ["rssi_2004", "rssi_DA8C", "rssi_78C0"]

full_df["avg_rssi_row"] = (
    full_df[rssi_cols].mean(axis=1, skipna=True) / (full_df["n_devices"] - 1)  # average per row, excluding self
)

rssi_exp = (
    full_df.groupby("experiment_id")["avg_rssi_row"]
           .agg(rssi_mean="mean", rssi_std="std")
)

###############################################################################
# Successful Migration Rate
#   ▸ Numerator  = # rows with  log_type_value == 'A'
#   ▸ Denominator = total # messages for that experiment
###############################################################################

# 1 . Numerator:  count of success-logs per experiment
num_success = (
    full_df.loc[full_df["log_type_value"] == "A"]
            .groupby("experiment_id")
            .size()
            .rename("n_success")
)

# 2 . Denominator:  total messages per experiment
#     (every merged row represents ≤1 message, so we just count rows)
n_messages = (
    messages.groupby("experiment_id")
           .size()
           .rename("n_messages")
)

# 3 . Combine and compute the rate  (guard against divide-by-zero)
success_tbl = (
    pd.concat([num_success, n_messages], axis=1)
      .fillna({"n_success": 0})               # no ‘A’ logs → zero successes
      .assign(success_migration_rate =
              lambda df: df.n_success / df.n_messages.replace({0: pd.NA}))
)

###############################################################################
# Genetic Variance
###############################################################################

gene_cols = [f"gene_{i}" for i in range(1, 11)]   

msg_long = (
    messages[["experiment_id"] + gene_cols]          # keep only what we need
      .melt(id_vars="experiment_id",
            value_vars=gene_cols,
            var_name="gene_idx",
            value_name="gene_value")
      .dropna(subset=["gene_value"])                 # drop rows where gene is NaN
)

gene_var_exp = (
    msg_long.groupby("experiment_id")["gene_value"]
            .var(ddof=1)                             # sample variance
            .rename("gene_variance")
)

###############################################################################
# Cosine-Similarity between genomes  (gene_1 … gene_10)  per experiment
###############################################################################

def mean_cosine(g):
    """
    g : DataFrame containing *only* the gene columns for one experiment.
    Returns the mean pair-wise cosine similarity (excluding diagonal).
    """
    X = g[gene_cols].to_numpy(dtype=float)
    if X.shape[0] < 2:          # only 1 genome – similarity undefined → NaN
        return np.nan
    C = cosine_similarity(X)    # square (n × n) matrix
    # take upper triangle without diagonal
    iu = np.triu_indices_from(C, k=1)
    return C[iu].mean()

cos_sim_exp = (
    messages[messages["gene_10"].notna()]  # filter out rows with no genes
      .groupby("experiment_id")
      .apply(lambda df: mean_cosine(df[gene_cols]))
      .rename("avg_cosine_similarity")
)

###############################################################################
# Variance of exchanged-message counts across sender-receiver pairs
###############################################################################

sender_col   = "rcv_robot_id"    # sender MAC/ID
receiver_col = "device_mac_id"     # receiver (already in logs)
exp_col      = "experiment_id"

msg_pairs = (
    full_df
      .loc[full_df[sender_col].notna(), [exp_col, sender_col, receiver_col]]
)

pair_counts = (
    msg_pairs
      .groupby([exp_col, sender_col, receiver_col])
      .size()
      .rename("pair_msg_count")
)

pair_var_exp = (
    pair_counts
      .groupby(exp_col)
      .var(ddof=1)                       # sample variance
      .rename("msg_variance")
)

###############################################################################
# Error-Rate per experiment
#   failed = rows where msg_recieved_flag != 'O'
#   total  = rows whose msg_count column is NOT NULL
###############################################################################
flag_col   = "msg_rcv_flag"             # sender’s ACK or status flag
count_col  = "fitness_score"           # any non-null → message exists
exp_col    = "experiment_id"

# 1 .  Consider only rows that represent a message (msg_count not null)
msg_rows = full_df.loc[full_df[count_col].notna(), [exp_col, flag_col]]

# 2 .  Compute per-experiment counts
totals  = msg_rows.groupby(exp_col).size().rename("n_messages")
fails   = (
    msg_rows.loc[msg_rows[flag_col] != "O"]
             .groupby(exp_col)
             .size()
             .rename("n_failed")
)

# 3 .  Combine and calculate the error rate
err_tbl = (
    pd.concat([totals, fails], axis=1)
      .fillna({"n_failed": 0})                 # no failures → 0
      .assign(error_rate=lambda d: d.n_failed / d.n_messages.replace({0: pd.NA}))
)

###############################################################################
# Jitter per experiment  (mean absolute delta of latency over time)
###############################################################################

lat_col   = "latency"
time_col  = "second_offset"
recv_col  = "device_mac_id" 
exp_col   = "experiment_id"

def stream_jitter(df):
    """
    df: DataFrame for one (experiment, device) stream, sorted by time
    → mean absolute delta of successive latency values, or NaN (<2 rows)
    """
    lat = df[lat_col].to_numpy(dtype=float)
    if lat.size < 2:
        return np.nan
    return np.mean(np.abs(np.diff(lat)))


# 1 .  Keep rows with valid latency & timestamp
lat_rows = full_df.loc[
    full_df[lat_col].notna() & full_df[time_col].notna(),
    [exp_col, recv_col, time_col, lat_col]
]

# 2 .  Sort and compute jitter per (experiment, device) stream
stream_jit = (
    lat_rows.sort_values([exp_col, recv_col, time_col])
            .groupby([exp_col, recv_col])
            .apply(stream_jitter)                 # → Series indexed by (exp, dev)
            .rename("device_jitter")
            .reset_index()
)

# 3 .  Aggregate device-jitters to one figure per experiment
#     (simple mean; switch to .median(), .mean(weight=…) if preferred)
jitter_exp = (
    stream_jit.groupby(exp_col)["device_jitter"]
             .mean()                              # average across devices
             .rename("jitter_ms")
)


#MERGE
exp_tbl = exp_tbl.join([rssi_exp, final_fit, impr_per_sec, gene_var_exp, cos_sim_exp, pair_var_exp, err_tbl, jitter_exp], how="left")
exp_tbl = exp_tbl.join(success_tbl[["success_migration_rate"]], how="left")

print(exp_tbl.columns)
print(exp_tbl.describe())

print("done")
