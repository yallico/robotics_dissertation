import pandas as pd
import numpy as np
from datetime import datetime, timedelta
from pathlib import Path
from sklearn.metrics.pairwise import cosine_similarity
import statsmodels.formula.api as smf
import statsmodels.api as sm
from statsmodels.stats.multitest import multipletests

ROOT = Path(__file__).parent        # directory where this .py lives
logs_path     = ROOT / "temp/pre-processed_data/logs/data.parquet"
metadata_path = ROOT / "temp/pre-processed_data/metadata/data.parquet"
messages_path = ROOT / "temp/pre-processed_data/messages/data.parquet"

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
    validate="many_to_one"
)

###############################################################################
# 3. Filters
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
# 7. Feature Engineering
###############################################################################

# “Design” columns that uniquely describe an experiment
exp_keys = [
    "experiment_id",       # already unique
    "max_genes",
    "num_robots",
    "topology",            # or whatever your column is called
    "robot_speed",
    "msg_limit",
    "migration_frequency"
]

# Spine of the experiment table
exp_tbl = (
    full_df[exp_keys]
    .drop_duplicates()      # one row per experiment_id
    .set_index("experiment_id")
)

agg_plan = {
    "fitness_score"   : ["mean", "std"],
    "cpu_util_core0"  : ["mean", "std"],
    "cpu_util_core1"  : ["mean", "std"],
    "latency"         : ["mean", "std"],
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

# --- Final fitness score and fitness rate of change (first 30 seconds) ---
fitness_metrics = (
    full_df.loc[full_df["fitness_score"].notna() & full_df["second_offset"].notna(), 
                ["experiment_id", "second_offset", "fitness_score"]]
    .sort_values(["experiment_id", "second_offset"])
    .groupby("experiment_id")
)

# Final fitness score: last fitness_score per experiment
final_fitness = fitness_metrics.tail(1).set_index("experiment_id")["fitness_score"].rename("final_fitness_score")

# Fitness rate of change (slope) for first 30 seconds per experiment
rate_of_change = []
for exp_id, group in fitness_metrics:
    first_30s = group[group["second_offset"] <= 30]
    if len(first_30s) < 2:
        rate_of_change.append((exp_id, np.nan))
    else:
        x = first_30s["second_offset"].values
        y = first_30s["fitness_score"].values
        slope = np.polyfit(x, y, 1)[0]
        rate_of_change.append((exp_id, slope))
fitness_rate_change = pd.Series(dict(rate_of_change), name="fitness_rate_change")

# Join these metrics to exp_tbl
exp_tbl = exp_tbl.join(final_fitness, how="left")
exp_tbl = exp_tbl.join(fitness_rate_change, how="left")

###############################################################################
# RSSI
###############################################################################

n_devices = (
    full_df.groupby("experiment_id")["device_mac_id"]
           .nunique()                          
)

full_df["n_devices"] = full_df["experiment_id"].map(n_devices)

rssi_cols = ["rssi_2004", "rssi_DA8C", "rssi_78C0", "rssi_669C", "rssi_9288", "rssi_A984", "rssi_DE9C", "rssi_DF8C", "rssi_B19C", "rssi_A184",
              "rssi_228C", "rssi_CBAC", "rssi_AC44"]

row_mean = full_df[rssi_cols].mean(axis=1, skipna=True)

full_df["avg_rssi_row"] = row_mean / (full_df["n_devices"] - 1)

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
      .rename("gene_cosine_similarity")
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
exp_col    = "experiment_id"

# 1 .  Consider only rows that represent a message (msg_count not null)
msg_rows = full_df.loc[full_df[flag_col].notna(), [exp_col, flag_col]]

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

lat_col   = "latency"          # or "latency_ms", whichever is in your frame
time_col  = "second_offset"
dev_col   = "device_mac_id"
exp_col   = "experiment_id"

# 1 .  Keep rows with valid latency + timestamp
lat_rows = full_df.loc[
    full_df[lat_col].notna() & full_df[time_col].notna(),
    [exp_col, dev_col, time_col, lat_col]
]

# 2 .  Build latency-diff series for each (experiment, device)
def device_jitter_std(df):
    """
    df: rows for a single (experiment, device) stream, sorted by time.
    Returns population stdev of latency_diff (same as STDEVX.P in DAX).
    """
    lat = df[lat_col].to_numpy(dtype=float)
    if lat.size < 2:
        return np.nan                             # undefined with <2 msgs
    latency_diff = np.diff(lat)
    # population standard deviation  (ddof=0)
    return np.std(latency_diff, ddof=0)

device_jit = (
    lat_rows.sort_values([exp_col, dev_col, time_col])
            .groupby([exp_col, dev_col])
            .apply(device_jitter_std)
            .rename("device_jitter")
            .reset_index()
)

# 3 .  Experiment-level mean of device_jitter
jitter_exp = (
    device_jit.groupby(exp_col)["device_jitter"]
              .mean()                          
              .rename("jitter_ms")            
)

#MERGE
exp_tbl = exp_tbl.join([rssi_exp, gene_var_exp, cos_sim_exp, pair_var_exp, jitter_exp], how="left")
exp_tbl = exp_tbl.join(success_tbl[["success_migration_rate"]], how="left")
exp_tbl = exp_tbl.join(err_tbl[["error_rate"]], how="left")

print(exp_tbl.columns)

# Save the final table to .csv
exp_tbl.to_csv(ROOT / "experiment_statistics.csv")

###############################################################################
# STATISTICS    
###############################################################################

factor_cols = ["topology", "robot_speed", "msg_limit"]

# any *numeric* column that is NOT one of the factors and not the key
response_cols = [
    c for c in exp_tbl.select_dtypes(include=[np.number]).columns
    if c not in factor_cols and c not in ["experiment_id", "max_genes", "num_robots"]
]


###############################################################################
# 2 .  Helper: run Type-II ANOVA on one response and return a tidy frame
###############################################################################
def anova_for(response):
    formula = f"{response} ~ C(topology) * C(robot_speed) * C(msg_limit)"
    model   = smf.ols(formula, data=exp_tbl).fit()
    aov     = sm.stats.anova_lm(model, typ=2)        # DataFrame
    aov = aov.reset_index().rename(columns={
        "index": "effect",         # e.g. 'C(topology):C(robot_speed)'
        "F":      "F_value",
        "PR(>F)": "p_value",
        "sum_sq": "sum_sq"
    })
    aov.insert(0, "response", response)
    return aov[["response", "effect", "F_value", "p_value", "sum_sq"]]

###############################################################################
# 3 .  Loop over every response column and stack the results
###############################################################################
results = pd.concat([anova_for(col) for col in response_cols], ignore_index=True)

###############################################################################
# 5 .  Save or inspect
###############################################################################
results.to_csv(ROOT / "anova_all_metrics.csv", index=False)

sig = results[results["p_value"] < 0.05].sort_values("p_value")
print(sig)

pivot = (
    results.pivot_table(index="response", columns="effect",
                        values="p_value", aggfunc="min")
)

print(pivot)

print("done")
