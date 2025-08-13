import pandas as pd
import numpy as np
from pathlib import Path
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import seaborn as sns  # added import
import matplotlib as mpl
from sklearn.preprocessing import MinMaxScaler




mpl.rcParams.update({
    "font.family": "sans-serif",
    "font.sans-serif": ["Arial", "Helvetica", "DejaVu Sans"],  # fallbacks
    "font.size": 6,
    "axes.labelsize": 7,
    "axes.titlesize": 8,
    "axes.linewidth": 1,   # axis border thickness
    "lines.linewidth": 1,
    "lines.markersize": 5,
    "xtick.labelsize": 6,
    "ytick.labelsize": 6,
    "legend.fontsize": 6,
    "legend.title_fontsize": 7,
    "legend.frameon": False,
    "figure.dpi": 300,
    "axes.grid": True,
    "grid.linewidth": 0.3,
    "grid.alpha": 0.5,
    "axes.grid.which": 'both',
    "axes.edgecolor": "black",
    "savefig.dpi": 300,
    "pdf.fonttype": 42,  # TrueType fonts for pdflatex
    "ps.fonttype": 42,
})
sns.set_theme(style="whitegrid", font_scale=1)

#ingest parquet file
ROOT       = Path(__file__).parent  
data       = ROOT / "temp/pre-processed_data/full_data.parquet"
full_df    = pd.read_parquet(data, engine="pyarrow")


topology_map = {0: "Stochastic", 1: "Comm Aware"}
msg_limit_map = {0: "Unlimited", 1: "Limited"}
locomotion_map = {0: "Static", 1: "Brownian"}
frequency_map = {0: "None", 1: "Modulated"}

full_df["topology"] = full_df["topology"].replace(topology_map)
full_df["msg_limit"] = full_df["msg_limit"].replace(msg_limit_map)
full_df["robot_speed"] = full_df["robot_speed"].replace(locomotion_map)
full_df["migration_frequency"] = full_df["migration_frequency"].astype(int) .replace(frequency_map)

topology_order = ["Stochastic", "Comm Aware"]
topology_palette = dict(zip(topology_order, sns.color_palette("Pastel2", n_colors=len(topology_order))))

pastel3_alt = [
    (0.85, 0.78, 0.95),  # lilac blush
    (0.95, 0.92, 0.80),  # cream yellow
    (0.85, 0.85, 0.85),  # gray
    (0.96, 0.88, 0.82),  # peach beige
    (0.88, 0.80, 0.85),  # dusty mauve
    (0.94, 0.90, 0.84),  # light sand
    (0.90, 0.86, 0.90),  # pale violet gray
    (0.98, 0.94, 0.88)   # ivory
]

sns.set_palette(pastel3_alt)

frequency_order = ["None", "Modulated"]
frequency_palette = dict(zip(frequency_order, pastel3_alt[:len(frequency_order)]))


################################
### Determine which plot to make
################################
is_seven = False
is_eight = True


if is_seven:

    #calculate jitter, error rate, throughput and latency per experiment
    lat_col = "latency"
    time_col = "second_offset"
    dev_col = "device_mac_id"
    exp_col = "experiment_id"
    top_col = "topology"
    num_col = "num_robots"
    msg_col = "msg_limit"
    flag_col = "msg_rcv_flag"
    fre_col = "migration_frequency"
    per_col = "fitness_score"
    loc_col = "robot_speed"

    # Filter rows with valid latency and timestamp for jitter calculation
    lat_rows = full_df.loc[
        full_df[lat_col].notna() & full_df[time_col].notna(),
        [exp_col, dev_col, time_col, lat_col]
    ]

    # Calculate jitter
    def device_jitter_std(df):
        lat = df[lat_col].to_numpy(dtype=float)
        if lat.size < 2:
            return np.nan  # Undefined for fewer than 2 messages
        latency_diff = np.diff(lat)
        return np.std(latency_diff, ddof=0)  # Population standard deviation

    device_jit = (
        lat_rows.sort_values([exp_col, dev_col, time_col])
                .groupby([exp_col, dev_col])
                .apply(device_jitter_std)
                .rename("device_jitter")
                .reset_index()
    )

    #get experiment level mean jitter and mean latency
    exp_stats = (
        device_jit.merge(
            lat_rows.groupby([exp_col, dev_col])[lat_col].mean().rename("mean_latency").reset_index(), 
            on=[exp_col, dev_col]
        )
        .groupby([exp_col])
        .agg(mean_jitter=("device_jitter", "mean"),
            mean_latency=("mean_latency", "mean")
            )
        ).reset_index()
    
    #calculate mean throughput by exp_col, fre_col from full_df
    exp_throughput = (
        full_df.groupby([exp_col])["throughput"]
        .mean()
        .rename("mean_throughput")
        .reset_index()
    )

    #error rates
    msg_rows = full_df[(full_df[num_col] != 2)].loc[
    (full_df[flag_col].notna()),
    [exp_col, dev_col, flag_col, "second_offset"]
    ]

    # 2 .  Compute per-experiment counts
    totals = (
        msg_rows
        .groupby([exp_col, dev_col, "second_offset"])
        .size()
        .rename("n_messages")
    )

    fails = (
        msg_rows.loc[msg_rows[flag_col] != "O"]
        .groupby([exp_col, dev_col, "second_offset"])
        .size()
        .rename("n_failed")
    )

    err_tbl = (
        totals.to_frame()
        .join(fails, how="left")
        .fillna({"n_failed": 0})
        .assign(dev_error_rate=lambda d: d.n_failed / d.n_messages.replace({0: pd.NA}))
        .reset_index()
    )

    err_tbl = err_tbl.merge(
        msg_rows[[exp_col, dev_col, "second_offset"]].drop_duplicates(),
        on=[exp_col, dev_col, "second_offset"],
        how="left"
    )

    err_rates = (
        err_tbl
        .groupby([exp_col], as_index=False)
        .agg(error_rate=("dev_error_rate", "mean"))
        .reset_index()
    )

    grouped_data = (
    full_df.groupby([exp_col, top_col, num_col, msg_col, fre_col, loc_col])
    .size()  # Example aggregation: count the number of rows in each group
    .rename("group_count")  # Rename the aggregated column
    .reset_index()  # Convert the GroupBy object to a DataFrame
)

    # Merge all stats into a single DataFrame
    combined_stats = (
        exp_stats.merge(exp_throughput, on=exp_col, how="left")
        .merge(err_rates, on=exp_col, how="left")
        #.merge(exp_per, on=exp_col, how="left")
        #.merge(adapted_times, on=exp_col, how="left")
        .merge(grouped_data, on=exp_col, how="left")
        .reset_index(drop=True)
    )

    #normalise the continous variables in combined_stats
    continuous_vars = ["mean_jitter", "mean_latency", "mean_throughput", "error_rate"]#, "fitness_score", "adapted_time_s"]
    scaler = MinMaxScaler()
    combined_stats[continuous_vars] = scaler.fit_transform(combined_stats[continuous_vars])

    # calculate QoS columns
    combined_stats["QoS_c"] = 0.5*(1 - combined_stats["mean_latency"]) + 0.25*(1 - combined_stats["mean_jitter"]) + 0.15*(1-combined_stats["error_rate"])  + 0.1*combined_stats["mean_throughput"]
    combined_stats["QoS_s"] = 0.15*(1 - combined_stats["mean_latency"]) + 0.15*(1 - combined_stats["mean_jitter"]) + 0.50*(1-combined_stats["error_rate"])  + 0.2*combined_stats["mean_throughput"]

    #Plot subplot 1,3
    fig, axes = plt.subplots(1, 4, figsize=(12.8, 3.6), gridspec_kw={'width_ratios': [1, 1, 1, 1]}, sharey=True)
    #axes 1 and 2 to share y axis


    sns.kdeplot(
        data=combined_stats[combined_stats["num_robots"] == 13],
        x="QoS_c",
        y="QoS_s",
        hue="num_robots",
        #hue_order=topology_order,
        palette= "PiYG",
        #fill=True,
        levels=8,
        ax=axes[0], 
        alpha=0.5,
        legend=False
    )

    sns.kdeplot(
        data=combined_stats[combined_stats["num_robots"] == 13],
        x="QoS_c",
        y="QoS_s",
        hue="topology",
        hue_order=topology_order,
        palette=topology_palette,
        #fill=True,
        levels=6,
        ax=axes[1], 
        alpha=1,
        legend=False
    )

    sns.kdeplot(
        data=combined_stats[combined_stats["num_robots"] == 13],
        x="QoS_c",
        y="QoS_s",
        hue= "migration_frequency",
        hue_order=frequency_order,
        palette=frequency_palette,
        #fill=True,
        levels=6,
        ax=axes[2], 
        alpha=1,
        legend=False
    )

    sns.kdeplot(
        data=combined_stats[combined_stats["num_robots"] == 13],
        x="QoS_c",
        y="QoS_s",
        hue= "msg_limit",
        #hue_order=frequency_order,
        palette='Set1',
        #fill=True,
        levels=6,
        ax=axes[3], 
        alpha=0.3,
        legend=False
    )

    # Create unified legends
    handles_topology = [
        plt.Line2D([0], [0], marker='s', color=color, label=label, linestyle='None')
        for label, color in topology_palette.items()
    ]
    handles_frequency = [
        plt.Line2D([0], [0], marker='s', color=color, label=label, linestyle='None')
        for label, color in frequency_palette.items()
    ]
    handles_msg_limit = [
        plt.Line2D([0], [0], marker='s', color=color, label=label, linestyle='None')
        for label, color in zip(["Unlimited", "Limited"], [sns.color_palette("Set1")[0], sns.color_palette("Set1")[1]])
    ]

    fig.legend(
        handles=handles_topology,
        labels=topology_order,
        loc='lower center',
        bbox_to_anchor=(0.25, -0.1),
        ncol=len(topology_order),
        title="Topology",
        frameon=False
    )

    fig.legend(
        handles=handles_frequency,
        labels=frequency_order,
        loc='lower center',
        bbox_to_anchor=(0.5, -0.1),
        ncol=len(frequency_order),
        title="Transmission Frequency",
        frameon=False
    )

    fig.legend(
        handles=handles_msg_limit,
        labels=["Unlimited", "Limited"],
        loc='lower center',
        bbox_to_anchor=(0.75, -0.1),
        ncol=2,
        title="Message Limit",
        frameon=False
    )

    axes[0].set_title("(a)")
    axes[0].set_xlabel(r"QoS$_c$")
    axes[0].set_ylabel(r"QoS$_s$")
    axes[0].spines['top'].set_visible(False)
    axes[0].spines['right'].set_visible(False)
    axes[0].set_xlim(0, 1)

    axes[1].set_title("(b)")
    axes[1].set_xlabel(r"QoS$_c$")
    axes[1].spines['top'].set_visible(False)
    axes[1].spines['right'].set_visible(False)
    axes[1].set_xlim(0, 1)

    axes[2].set_title("(c)")
    axes[2].set_xlabel(r"QoS$_c$")
    axes[2].spines['top'].set_visible(False)
    axes[2].spines['right'].set_visible(False)
    axes[2].set_xlim(0, 1)

    axes[3].set_title("(d)")
    axes[3].set_xlabel(r"QoS$_c$")
    axes[3].spines['top'].set_visible(False)
    axes[3].spines['right'].set_visible(False)
    axes[3].set_xlim(0, 1)


    plt.tight_layout(rect=[0, 0.05, 1, 1])
    plt.savefig(ROOT / '../report/qos_impact.pdf', bbox_inches='tight')

if is_eight:

    

    plt.tight_layout(rect=[0, 0.05, 1, 1])
    plt.savefig(ROOT / '../report/convergence_impact.pdf', bbox_inches='tight')

print("done")