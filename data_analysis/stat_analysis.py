import pandas as pd
import numpy as np
from datetime import datetime, timedelta
from pathlib import Path
from sklearn.metrics.pairwise import cosine_similarity
import statsmodels.formula.api as smf
import statsmodels.api as sm
from statsmodels.stats.multitest import multipletests
import seaborn as sns
import matplotlib.pyplot as plt

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
#logs_meta = logs_meta.query("migration_frequency != 1")

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
# 5. Build SECOND_OFFSET
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

# create msg_limit_flag
full_df = full_df.sort_values(["device_mac_id", "experiment_id", "log_id"])
is_tu = (full_df["status"] == "T") & (full_df["log_type_value"] == "U")
tu_logs = full_df[is_tu][["device_mac_id", "experiment_id", "log_id"]].copy()
tu_logs = tu_logs.rename(columns={"log_id": "tu_log_id"})
tu_logs["next_tu_log_id"] = (
    tu_logs.groupby(["device_mac_id", "experiment_id"])["tu_log_id"].shift(-1)
)

full_df = pd.merge_asof(
    full_df.sort_values("log_id"),
    tu_logs.sort_values("tu_log_id"),
    left_on="log_id",
    right_on="tu_log_id",
    by=["device_mac_id", "experiment_id"],
    direction="backward",
    allow_exact_matches=True
)

full_df["in_tu_interval"] = (
    (full_df["log_id"] > full_df["tu_log_id"]) &
    (full_df["log_id"] < full_df["next_tu_log_id"])
)

received_mask = full_df["msg_rcv_flag"].notna() & full_df["in_tu_interval"]
received_counts = (
    full_df[received_mask]
    .groupby(["device_mac_id", "experiment_id", "tu_log_id"])
    .size()
    .rename("any_received_between")
    .reset_index()
)

tu_logs = tu_logs.merge(received_counts, how="left", on=["device_mac_id", "experiment_id", "tu_log_id"])
tu_logs["any_received_between"] = tu_logs["any_received_between"].fillna(0)

tu_logs["msg_limit_flag"] = np.where(
    tu_logs["next_tu_log_id"].notna() & (tu_logs["any_received_between"] == 0),
    1, 0
)

full_df = full_df.merge(
    tu_logs[["device_mac_id", "experiment_id", "tu_log_id", "msg_limit_flag"]],
    left_on=["device_mac_id", "experiment_id", "log_id"],
    right_on=["device_mac_id", "experiment_id", "tu_log_id"],
    how="left"
)

full_df["msg_limit_flag"] = full_df["msg_limit_flag"].astype("Int64")
full_df["migration_frequency"] = full_df["migration_frequency"].astype("Int64")
full_df["throughput"] = full_df["kbps_in"].fillna(0) + full_df["kbps_out"].fillna(0)

#save full_df to a parquet file
full_df.to_parquet(ROOT / "temp/pre-processed_data/full_data.parquet", index=False)

###############################################################################
# 6. Sanity checks
###############################################################################
print(full_df.columns)

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
    "throughput"         : ["mean", "std"],
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
# Successful Migration Ratio
###############################################################################

# Calculate success and reject masks
success_mask = full_df["log_type_value"] == "A"
reject_mask  = full_df["log_type"] == "R"

# Count per experiment
success_counts = full_df[success_mask].groupby("experiment_id").size().rename("n_success")
reject_counts  = full_df[reject_mask].groupby("experiment_id").size().rename("n_reject")


# 3 . Combine and compute the ratio  (guard against divide-by-zero)
success_tbl = (
    pd.concat([success_counts, reject_counts], axis=1)
      .fillna(0)
      .assign(success_ratio=lambda df: df.n_success / (df.n_success + df.n_reject))
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
# Genetic Spread (Identify local deviations)
###############################################################################

full_df["bucket_2s"] = (full_df["second_offset"] // 2).astype(int)

min_fitness_per_bucket = (
    full_df.groupby(["experiment_id", "bucket_2s"])["fitness_score"]
           .min()
           .rename("min_fitness")
           .reset_index()
)

full_df = full_df.merge(min_fitness_per_bucket, on=["experiment_id", "bucket_2s"], how="left")
full_df["is_min_fitness"] = full_df["fitness_score"] == full_df["min_fitness"]

min_fitness_counts = (
    full_df[full_df["is_min_fitness"]]
      .groupby(["experiment_id", "bucket_2s"])
      .size()
      .rename("n_robots_min_fitness")
      .reset_index()
)

spread_exp = (
    min_fitness_counts.groupby("experiment_id")["n_robots_min_fitness"]
      .agg(["mean", "std"])
      .rename(columns={"mean": "fitness_spread_mean", "std": "fitness_spread_std"})
)

###############################################################################
# Message transmission rate accross network
###############################################################################

sent_msgs = full_df[full_df["msg_limit_flag"] == 0]

device_msg_counts = (
    sent_msgs.groupby(["experiment_id", "device_mac_id"])
             .size()
             .rename("message_count")
             .reset_index()
)

#60 experiment duration
device_msg_counts["device_msg_rate"] = device_msg_counts["message_count"] / 60

msg_rate_exp = (
    device_msg_counts.groupby("experiment_id")["device_msg_rate"]
                     .mean()
                     .rename("msg_rate")
)

###############################################################################
# Variance of exchanged-message counts across sender-receiver pairs
###############################################################################

sender_col   = "rcv_robot_id"    # sender MAC/ID
receiver_col = "device_mac_id"     # receiver (already in logs)
exp_col      = "experiment_id"

msg_pairs = (
    full_df
      .loc[
          (full_df[sender_col].notna()),
          [exp_col, sender_col, receiver_col]
      ]
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

# 1 .  Consider only rows that represent a message (msg_count not null) and not skipped
msg_rows = full_df.loc[
    (full_df[flag_col].notna()),
    [exp_col, flag_col]
]

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
exp_tbl = exp_tbl.join([rssi_exp, gene_var_exp, spread_exp, msg_rate_exp, pair_var_exp, jitter_exp], how="left")
exp_tbl = exp_tbl.join(success_tbl[["success_ratio"]], how="left")
exp_tbl = exp_tbl.join(err_tbl[["error_rate"]], how="left")

print(exp_tbl.columns)

# Save the final table to .csv
exp_tbl.to_csv(ROOT / "experiment_statistics.csv")

###############################################################################
# STATISTICS    
###############################################################################

factor_cols = ["topology", "migration_frequency"]

for col in factor_cols:
    print(f"{col}: {exp_tbl[col].unique()}")

# any *numeric* column that is NOT one of the factors and not the key
response_cols = [
    c for c in exp_tbl.select_dtypes(include=[np.number]).columns
    if c not in factor_cols and c not in ["experiment_id", "max_genes", "num_robots", "msg_limit", "robot_speed"]
]


###############################################################################
# 2 .  Helper: run Type-II ANOVA on one response and return a tidy frame
###############################################################################
def anova_for(response):
    factors = [col for col in factor_cols if exp_tbl[col].nunique() > 1]
    if not factors:
        return pd.DataFrame()
    formula = f"{response} ~ " + " * ".join([f"C({f})" for f in factors])
    try:
        model   = smf.ols(formula, data=exp_tbl).fit()
        aov     = sm.stats.anova_lm(model, typ=2)      
        aov = aov.reset_index().rename(columns={
            "index": "effect",
            "F":      "F_value",
            "PR(>F)": "p_value",
            "sum_sq": "sum_sq",
            "df": "df"
        })
        aov.insert(0, "response", response)
        return aov[["response","effect","df","sum_sq","F_value","p_value"]]
    except Exception as e:
        print(f"ANOVA failed for {response}: {e}")
        return pd.DataFrame()

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

#TODO: this needs updating everytime
keepers = ["C(topology)", "C(msg_limit)", "C(topology):C(msg_limit)", "Residual"]
res = results[results["effect"].isin(keepers)].copy()

def compute_es(df_one_resp):
    # split rows
    if "Residual" not in df_one_resp["effect"].values:
        return pd.DataFrame()
    resid = df_one_resp[df_one_resp["effect"]=="Residual"].iloc[0]
    ss_err, df_err = resid["sum_sq"], resid["df"]
    ms_err = ss_err / df_err
    ss_total = df_one_resp["sum_sq"].sum()

    # effect rows only
    eff = df_one_resp[df_one_resp["effect"]!="Residual"].copy()
    eff["partial_eta_sq"] = eff["sum_sq"] / (eff["sum_sq"] + ss_err)
    eff["omega_sq"] = (eff["sum_sq"] - eff["df"]*ms_err) / (ss_total + ms_err)
    eff["omega_sq"] = eff["omega_sq"].clip(lower=0)  # floor at 0 for readability
    eff["df_error"] = df_err
    eff["ms_error"] = ms_err
    eff["ss_error"] = ss_err
    eff["ss_total"] = ss_total
    return eff

effsizes = (
    res.groupby("response", group_keys=False)
       .apply(compute_es)
       .reset_index(drop=True)
)

mask = ~(
    effsizes['response'].str.contains('cpu_', case=False, na=False) |
    effsizes['response'].str.contains('msg_', case=False, na=False) |
    effsizes['response'].str.contains('_std', case=False, na=False) 
    )

plot_df = effsizes.loc[mask & (effsizes['p_value'] < 0.05) & (effsizes['partial_eta_sq'] > 0.0125)].copy()

plot_df["effect_pretty"] = plot_df["effect"].replace({
    "C(topology)":"Topology",
    "C(msg_limit)":"Msg limit",
    "C(topology):C(msg_limit)":"Interaction"
})

plot_df = plot_df[plot_df["effect_pretty"] != "Interaction"]

# order responses by how strong 'Topology' is
order_resp = (
    plot_df[plot_df["effect_pretty"]=="Topology"]
    .sort_values("partial_eta_sq", ascending=False)["response"].tolist()
)

# one big faceted forest (partial η²)
g = sns.FacetGrid(
    plot_df, row="response", hue="effect_pretty", palette='Pastel2', sharex=True, sharey=False,
    row_order=order_resp, height=1.2, aspect=5
)

def draw_forest(data, color, **k):
    y = data["effect_pretty"]
    x = data["partial_eta_sq"]
    # draw CIs if present
    if {"ci_low","ci_high"}.issubset(data.columns):
        plt.hlines(y, data["ci_low"], data["ci_high"], color="#888", lw=2, alpha=0.7)
    plt.scatter(x, y, color=color, s=120, edgecolor="white", linewidth=1.5, zorder=3)

g.map_dataframe(draw_forest)
g.set(xlim=(0, 0.25), xlabel="", ylabel="")
for i, (ax, resp) in enumerate(zip(g.axes.flat, order_resp)):
    ax.set_title(resp, loc="left", fontsize=11, fontweight="bold")
    ax.grid(axis="x", alpha=0.5)
    if i == len(order_resp) - 1:
        ax.set_xlabel("Partial $\eta^2$")  # Only bottom facet gets label
    else:
        ax.set_xlabel("")   

plt.tight_layout()
plt.show()


print("done")
