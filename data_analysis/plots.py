import pandas as pd
import numpy as np
from pathlib import Path
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import seaborn as sns  # added import
import matplotlib as mpl
from matplotlib.patches import Rectangle




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

#Change depending on graph
#full_df    = full_df.query("migration_frequency != 1")
full_df    = full_df.query("topology != 1 and msg_limit != 1")

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
is_one = False
is_two = False
is_three = False
is_four = False
is_five = False
is_six = True

if is_one:
    ################################
    ### Topology effects paired with msg limit subplot
    ################################

    #calculate jitter
    lat_col   = "latency"          # or "latency_ms", whichever is in your frame
    time_col  = "second_offset"
    dev_col   = "device_mac_id"
    exp_col   = "experiment_id"
    top_col   = "topology"
    msg_col   = "msg_limit"

    # 1 .  Keep rows with valid latency + timestamp
    lat_rows = full_df.loc[
        full_df[lat_col].notna() & full_df[time_col].notna(),
        [exp_col, dev_col, time_col, lat_col, top_col, msg_col]
    ]

    # 2 .  Build latency-diff series for each (experiment, device)
    def device_jitter_std(df):
        """
        df: rows for a single (experiment, device) stream, sorted by time.
        Returns population stdev of latency_diff.
        """
        lat = df[lat_col].to_numpy(dtype=float)
        if lat.size < 2:
            return np.nan                             # undefined with <2 msgs
        latency_diff = np.diff(lat)
        # population standard deviation  (ddof=0)
        return np.std(latency_diff, ddof=0)

    device_jit = (
        lat_rows.sort_values([exp_col, dev_col, time_col])
                .groupby([exp_col, dev_col, top_col, msg_col])
                .apply(device_jitter_std)
                .rename("device_jitter")
                .reset_index()
    )

    #get experiment level mean jitter and mean latency
    exp_stats = (
        device_jit.merge(
            lat_rows.groupby([exp_col, dev_col, top_col, msg_col])[lat_col].mean().rename("mean_latency").reset_index(), 
            on=[exp_col, dev_col, top_col, msg_col]
        )
        .groupby([exp_col, top_col, msg_col])
        .agg(mean_jitter=("device_jitter", "mean"),
            mean_latency=("mean_latency", "mean")
            )
        ).reset_index()

    #calculate mean throughput by exp_col, top_col, msg_col from full_df
    exp_throughput = (
        full_df.groupby([exp_col, top_col, msg_col])["throughput"]
        .mean()
        .rename("mean_throughput")
        .reset_index()
    )


    fig, axes = plt.subplots(1, 3, figsize=(12.8, 3.6), gridspec_kw={'width_ratios': [2, 1, 1]})
    #axes 1 and 2 to share y axis

    sns.scatterplot(
        data=exp_stats,
        x="mean_latency",
        y="mean_jitter",
        hue="topology",
        hue_order=topology_order,
        ax=axes[0],
        s=12,
        alpha=0.6,
        edgecolor=None,
        palette=topology_palette,
        legend="full",
    )

    #second plot boxplot 
    sns.boxplot(
        data=exp_throughput,
        x="topology",
        y="mean_throughput",
        ax=axes[1],
        palette=topology_palette,
        width=0.4,  # make boxplot narrower
        fliersize=3, 
        showmeans=True,
        meanprops={"marker": "o", "markerfacecolor": "none", "markeredgecolor": "gray", "markersize": 3}  
    )

    #third plot boxplot 
    sns.boxplot(
        data=exp_throughput,
        x="msg_limit",
        y="mean_throughput",
        ax=axes[2],
        palette="Pastel1",
        width=0.4,  # make boxplot narrower
        fliersize=3, 
        showmeans=True,
        meanprops={"marker": "o", "markerfacecolor": "none", "markeredgecolor": "gray", "markersize": 3}  
    )

    axes[0].set_title("(a)")
    axes[0].set_xlabel("Mean Latency (ms)")
    axes[0].set_ylabel("Mean Jitter (ms)")
    axes[0].spines['top'].set_visible(False)
    axes[0].spines['right'].set_visible(False)
    leg = axes[0].get_legend()
    if leg is not None:
        leg.set_title(leg.get_title().get_text().title())
        for lh in leg.legend_handles:
            try:
                lh.set_alpha(1)
            except AttributeError:
                pass
    rect_x = 0   # x-coordinate of lower left corner
    rect_y = 0    # y-coordinate of lower left corner
    rect_width = 45  # width of rectangle
    rect_height = 25  # height of rectangle 
    rect = Rectangle(
        (rect_x, rect_y),
        rect_width,
        rect_height,
        linewidth=0.6,
        edgecolor="grey",
        facecolor='none',
        linestyle='dashed'
    )
    axes[0].add_patch(rect)



    axes[1].set_title("(b)")
    axes[1].set_xlabel("Topology")
    axes[1].set_ylabel("Mean Throughput (Kbps)")
    axes[1].spines['top'].set_visible(False)
    axes[1].spines['right'].set_visible(False)

    axes[2].sharey(axes[1])
    axes[2].set_title("(c)")
    axes[2].set_xlabel("Message Limit")
    axes[2].set_ylabel("")
    axes[2].spines['top'].set_visible(False)
    axes[2].spines['right'].set_visible(False)


    plt.tight_layout(rect=[0, 0.05, 1, 1])
    plt.savefig(ROOT / '../report/topology_impact.pdf', bbox_inches='tight')

elif is_two:
    ################################
    ### Performance Effects
    ################################

    per_col   = "fitness_score"          
    time_col  = "second_offset"
    dev_col   = "device_mac_id"
    exp_col   = "experiment_id"
    top_col   = "topology"
    num_col   = "num_robots"

    Q3 = full_df[per_col].quantile(0.75)
    Q1 = full_df[per_col].quantile(0.25)
    IQR = Q3 - Q1
    upper_bound = Q3 + 1.5 * IQR
    
    filtered_df = full_df[full_df[per_col] <= upper_bound]

    per_rows = filtered_df[(filtered_df[num_col] != 2)].loc[
        filtered_df[per_col].notna() & filtered_df[time_col].notna(),
        [exp_col, dev_col, time_col, per_col, top_col, num_col]
    ]

    #Calculate the adaptation rate, defined as 
    #"swarm adapted when the number of robots with the new best fitness value reach 70% and remains there for at least 10 seconds"
    def calculate_adaptation_rate(
        df,
        bin_seconds=2,
        threshold=0.70,
        sustain_seconds=6,
        tol=1e-3
    ):
        """
        Compute adaptation rate over time per (experiment_id, topology).

        Definitions implemented:
        - Time is bucketed into `bin_seconds` bins to absorb ±1s jitter.
        - 'Bucket best' = lowest fitness_score in that time bucket (lower is better).
        - 'Global best-so-far' = cumulative minimum of bucket best over time.
        - Adaptation rate at time t = fraction of devices whose bucket-level fitness equals
            the current global best-so-far at t.
        - Swarm considered 'adapted' when adaptation rate >= `threshold` and remains
            at/above that for at least `sustain_seconds`.

        Returns
        -------
        rates_df : DataFrame
            One row per (experiment_id, topology, time_bin). Columns:
            - time_bin: left edge of the bin in seconds (int)
            - n_devices: total unique devices in the group
            - bucket_best: min fitness in the bin
            - global_best_so_far: expanding min of bucket_best
            - at_global_best: number of devices at the global best this bin
            - rate: at_global_best / n_devices
            - cum_rate: expanding max of `rate` (monotone, tends to 1.0)
            - adapted_now: boolean, rate >= threshold
        summary_df : DataFrame
            One row per (experiment_id, topology) with:
            - adapted: bool
            - adapted_from_bin: first bin (left edge, seconds) where sustained adaptation starts,
                                or NaN if never adapted
            - adapted_from_time_s: same as above (alias)
        """
        if df.empty:
            return (
                pd.DataFrame(columns=[exp_col, top_col, "time_bin", "n_devices", "bucket_best",
                                    "global_best_so_far", "at_global_best", "rate", "cum_rate", "adapted_now"]),
                pd.DataFrame(columns=[exp_col, top_col, "adapted", "adapted_from_bin", "adapted_from_time_s"])
            )

        work = df.copy()

        # 1) Bucket time (left edge integer seconds, e.g., 0,2,4,...)
        work["time_bin"] = (work[time_col] // bin_seconds) * bin_seconds

        # 2) Within each (group, time_bin, device), take the *min* fitness in that bin
        grp_keys = [exp_col, top_col, "time_bin", dev_col]
        dev_bin = (
            work
            .groupby(grp_keys, as_index=False)[per_col]
            .min()
            .rename(columns={per_col: "dev_bin_fitness"})
        )

        # === Handle missing device rows in the FIRST bucket ===
        dev_first = (
            work.groupby([exp_col, dev_col], as_index=False)[per_col]
            .max()
            .rename(columns={per_col: "device_first_fitness"})
        )

        first_bins = (
            work.groupby([exp_col, top_col], as_index=False)["time_bin"]
                .min()
                .rename(columns={"time_bin": "first_time_bin"})
        )
        dev_bin = dev_bin.merge(first_bins, on=[exp_col, top_col], how="left")
        dev_bin = dev_bin.merge(dev_first, on=[exp_col, dev_col], how="left")

        mask = (dev_bin["time_bin"] == dev_bin["first_time_bin"]) & dev_bin["dev_bin_fitness"].isna()
        dev_bin.loc[mask, "dev_bin_fitness"] = dev_bin.loc[mask, "device_first_fitness"]
        dev_bin = dev_bin.drop(columns=["first_time_bin", "device_first_fitness"])

        dev_bin = dev_bin.sort_values([exp_col, top_col, dev_col, "time_bin"])
        dev_bin["dev_bin_fitness"] = (
            dev_bin
            .groupby([exp_col, top_col, dev_col])["dev_bin_fitness"]
            .ffill()   # propagate previous value forward
        )

        # 3) Bucket best (overall) per (group, time_bin)
        grp_tb = [exp_col, top_col, "time_bin"]
        bucket_best = (
            dev_bin
            .groupby(grp_tb, as_index=False)["dev_bin_fitness"]
            .min()
            .rename(columns={"dev_bin_fitness": "bucket_best"})
        )

        # 4) Global best-so-far (cumulative min over time within each group)
        bucket_best = (
            bucket_best
            .sort_values(grp_tb)
            .assign(global_best_so_far=lambda d:
                    d.groupby([exp_col, top_col])["bucket_best"].cummin())
        )

        # 5) Join global best back to per-device rows for each bin
        dev_bin = dev_bin.merge(bucket_best, on=grp_tb, how="left")

        # 6) Count devices that are exactly at the current global best this bin
        #    (Exact equality assumed; if needed, add a tolerance)
        dev_bin["is_at_global_best"] = (dev_bin["dev_bin_fitness"] - dev_bin["global_best_so_far"]).abs() <= tol

        # 7) Per (group, time_bin): compute rate
        totals = (
            dev_bin
            .groupby([exp_col, top_col], as_index=False)[dev_col]
            .nunique()
            .rename(columns={dev_col: "n_devices"})
        )

        at_best = (
            dev_bin
            .groupby(grp_tb, as_index=False)["is_at_global_best"]
            .sum()
            .rename(columns={"is_at_global_best": "at_global_best"})
        )

        rates = (
            bucket_best
            .merge(at_best, on=grp_tb, how="left")
            .merge(totals, on=[exp_col, top_col], how="left")
            .fillna({"at_global_best": 0})
        )

        rates["rate"] = rates["at_global_best"] / rates["n_devices"].where(rates["n_devices"] > 0, np.nan)
        #rates.loc[(rates["at_global_best"] <= 1) & (rates["time_bin"] < 8), "rate"] = 0.0
        # An optional monotone version (useful for plotting "cumulative adoption")
        rates = rates.sort_values(grp_tb)
        rates["cum_rate"] = rates.groupby([exp_col, top_col])["rate"].cummax()

        # 8) Detect sustained adaptation: rate >= threshold for >= sustain_seconds
        bins_needed = int(np.ceil(sustain_seconds / bin_seconds))
        rates["adapted_now"] = rates["rate"] >= threshold

        def _first_sustained_start(g):
            # rolling window over boolean -> meet all True for window
            # Convert to int and require sum == bins_needed
            if g.empty:
                return np.nan
            
            ser = g["adapted_now"].astype(int)
            # Use rolling with window 'bins_needed' and min==1 to indicate all ones
            ok = ser.rolling(window=bins_needed, min_periods=bins_needed).min() == 1
            if not ok.any():
                return np.nan
            idx = ok.idxmax()  # first True index
            # Start of the sustained window = index - (bins_needed - 1)
            start_pos = g.index.get_loc(idx) - (bins_needed - 1)
            start_idx = g.index[start_pos]

            return g.loc[start_idx, "time_bin"]

        starts = (
            rates
            .set_index(grp_tb)
            .groupby(level=[0,1], sort=False, group_keys=False)
            .apply(lambda gg: _first_sustained_start(gg.reset_index()))
            .reset_index(name="adapted_from_bin")
        )

        summary = rates[[exp_col, top_col]].drop_duplicates().merge(starts, on=[exp_col, top_col], how="left")
        summary["adapted"] = ~summary["adapted_from_bin"].isna()
        summary["adapted_from_time_s"] = summary["adapted_from_bin"]

        return rates, summary
    
    df_rate = full_df[(full_df[num_col] != 2)]

    rates, summary = calculate_adaptation_rate(
        df_rate,
        bin_seconds=2,
        threshold=0.80,
        sustain_seconds=4,
        tol=1e-3
    )

    agg_rates = (
    rates.groupby([top_col, "time_bin"], as_index=False)
         .agg(mean_rate=("cum_rate", "mean"),
              std_rate=("cum_rate", "std"))
    )

    #I want a violin plot with num_col as x axis desc, fitness_score as y-axis and each violin split into two halves by topology
    fig, axes = plt.subplots(1, 2, figsize=(12.8, 3.6), gridspec_kw={'width_ratios': [2, 1]})

    sns.violinplot(
        data=per_rows,
        x=num_col,
        y=per_col,
        hue=top_col,
        hue_order=topology_order,
        split=True,
        ax=axes[0],
        gap=.05,
        palette=topology_palette,
        inner="quartile",
        linewidth=0.5,
        legend=False

    )

    sns.lineplot(
        data=agg_rates,
        x="time_bin",
        y="mean_rate",
        hue=top_col,
        hue_order=topology_order,
        ax=axes[1],
        palette=topology_palette,
        linewidth=2
    )

    axes[0].set_title("(a)")
    axes[0].set_xlabel("Number of Robots")
    axes[0].set_ylabel("Fitness Value")
    axes[0].spines['top'].set_visible(False)
    axes[0].spines['right'].set_visible(False)

    axes[1].set_title("(b)")
    axes[1].set_xlabel("Time (s)")
    axes[1].set_ylabel("Adaptation Rate")
    axes[1].spines['top'].set_visible(False)
    axes[1].spines['right'].set_visible(False)
    axes[1].set_xlim(left=0)
    axes[1].set_ylim(bottom=0, top=1)
    for name, group in agg_rates.groupby(top_col):
        axes[1].fill_between(
            group["time_bin"],
            group["mean_rate"] - group["std_rate"],
            group["mean_rate"] + group["std_rate"],
            alpha=0.2,
            color= topology_palette[name]
        )
    legend = axes[1].get_legend()
    if legend is not None:
        legend.set_title("Topology")

    plt.tight_layout(rect=[0, 0.05, 1, 1])
    plt.savefig(ROOT / '../report/performance_impact.pdf', bbox_inches='tight')

elif is_three:

    #calculate error rates by experiment, topology and num_robots
    flag_col   = "msg_rcv_flag"  
    num_col    = "num_robots" 
    top_col    = "topology"         
    exp_col    = "experiment_id"
    dev_col    = "device_mac_id"


    msg_rows = full_df[(full_df[num_col] != 2)].loc[
    (full_df[flag_col].notna()),
    [exp_col, dev_col, flag_col, num_col, top_col, "second_offset"]
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

    # 3 .  Combine and calculate the error rate
    err_tbl = (
        totals.to_frame()
        .join(fails, how="left")
        .fillna({"n_failed": 0})
        .assign(dev_error_rate=lambda d: d.n_failed / d.n_messages.replace({0: pd.NA}))
        .reset_index()
    )

    err_tbl = err_tbl.merge(
        msg_rows[[exp_col, dev_col, "second_offset", top_col, num_col]].drop_duplicates(),
        on=[exp_col, dev_col, "second_offset"],
        how="left"
    )

    # 4 .  Aggregate to per-experiment, topology, num_robots
    err_rates = (
        err_tbl
        .groupby([exp_col, top_col, num_col], as_index=False)
        .agg(error_rate=("dev_error_rate", "mean"),
             n_messages=("n_messages", "sum"),
             n_failed=("n_failed", "sum"))
        .reset_index()
    )

    fig, axes = plt.subplots(1, 2, figsize=(6.4, 3.6), gridspec_kw={'width_ratios': [1, 1]})

    #print(sns.color_palette("Pastel1").as_hex())

    sns.boxplot(
        data=err_rates,
        x="num_robots",
        y="error_rate",
        ax=axes[0],
        width=0.4,  # make boxplot narrower
        fliersize=3, 
        showmeans=True,
        meanprops={"marker": "o", "markerfacecolor": "none", "markeredgecolor": "gray", "markersize": 3},
        color = sns.color_palette("Pastel1")[0]
    )

    sns.boxplot(
        data=err_rates,
        x="topology",
        y="error_rate",
        ax=axes[1],
        palette=topology_palette,
        width=0.3,  # make boxplot narrower
        fliersize=3, 
        showmeans=True,
        meanprops={"marker": "o", "markerfacecolor": "none", "markeredgecolor": "gray", "markersize": 3},
    )    

    axes[0].sharey(axes[1])
    axes[0].set_title("(a)")
    axes[0].set_xlabel("Number of Robots")
    axes[0].set_ylabel("Mean Error Rate")
    axes[0].spines['top'].set_visible(False)
    axes[0].spines['right'].set_visible(False)

    axes[1].set_title("(b)")
    axes[1].set_xlabel("Topology")
    axes[1].set_ylabel("")
    axes[1].spines['top'].set_visible(False)
    axes[1].spines['right'].set_visible(False)

    plt.tight_layout(rect=[0, 0.05, 1, 1])
    plt.savefig(ROOT / '../report/reliability_impact.pdf', bbox_inches='tight')

elif is_four:

    #take rows that have rssi and group them by experiment and robot_speed
    speed_col = "robot_speed"
    num_col = "num_robots"
    exp_col = "experiment_id"
    top_col = "topology"

    rssi_rows = full_df[full_df[num_col] != 2]

    n_devices = (
    rssi_rows.groupby("experiment_id")["device_mac_id"]
           .nunique()                          
    )

    rssi_rows["n_devices"] = rssi_rows["experiment_id"].map(n_devices)

    rssi_cols = ["rssi_2004", "rssi_DA8C", "rssi_78C0", "rssi_669C", "rssi_9288", "rssi_A984", "rssi_DE9C", "rssi_DF8C", "rssi_B19C", "rssi_A184",
              "rssi_228C", "rssi_CBAC", "rssi_AC44"]

    row_mean = rssi_rows[rssi_cols].mean(axis=1, skipna=True)

    rssi_rows["avg_rssi_row"] = row_mean / (rssi_rows["n_devices"] - 1)

    rssi_groups = rssi_rows.groupby([exp_col, speed_col]).agg(
        mean_rssi=("avg_rssi_row", "mean"),
        std_rssi=("avg_rssi_row", "std"),
        n_samples=("avg_rssi_row", "count")
    ).reset_index()

    rssi_top = rssi_rows.groupby([exp_col, top_col]).agg(
        mean_rssi=("avg_rssi_row", "mean"),
        std_rssi=("avg_rssi_row", "std"),
        n_samples=("avg_rssi_row", "count")
    ).reset_index()

    fig, axes = plt.subplots(1, 2, figsize=(6.4, 3.6), gridspec_kw={'width_ratios': [1, 1]})

    sns.boxplot(
        data=rssi_groups,
        x=speed_col,
        y="mean_rssi",
        ax=axes[0],
        palette="Paired",
        fliersize=3, 
        showmeans=True,
        meanprops={"marker": "o", "markerfacecolor": "none", "markeredgecolor": "gray", "markersize": 3},
        width=0.4
    )

    sns.boxplot(
        data=rssi_top,
        x=top_col,
        y="mean_rssi",
        ax=axes[1],
        hue=top_col,
        hue_order=topology_order,
        palette=topology_palette,
        fliersize=3, 
        showmeans=True,
        meanprops={"marker": "o", "markerfacecolor": "none", "markeredgecolor": "gray", "markersize": 3},
        width=0.4
    )


    axes[0].sharey(axes[1])
    axes[0].set_title("(a)")
    axes[0].set_xlabel("Locomotion")
    axes[0].set_ylabel("Mean RSSI (dBm)")
    axes[0].spines['top'].set_visible(False)
    axes[0].spines['right'].set_visible(False)

    axes[1].set_title("(b)")
    axes[1].set_xlabel("Topology")
    axes[1].set_ylabel("")
    axes[1].spines['top'].set_visible(False)
    axes[1].spines['right'].set_visible(False)

    plt.tight_layout(rect=[0, 0.05, 1, 1])
    plt.savefig(ROOT / '../report/speed_impact.pdf', bbox_inches='tight')

elif is_five:

    #calculate jitter
    lat_col   = "latency"          # or "latency_ms", whichever is in your frame
    time_col  = "second_offset"
    dev_col   = "device_mac_id"
    exp_col   = "experiment_id"
    fre_col   = "migration_frequency"

    # 1 .  Keep rows with valid latency + timestamp
    lat_rows = full_df.loc[
        full_df[lat_col].notna() & full_df[time_col].notna(),
        [exp_col, dev_col, time_col, lat_col, fre_col]
    ]

    # 2 .  Build latency-diff series for each (experiment, device)
    def device_jitter_std(df):
        """
        df: rows for a single (experiment, device) stream, sorted by time.
        Returns population stdev of latency_diff.
        """
        lat = df[lat_col].to_numpy(dtype=float)
        if lat.size < 2:
            return np.nan                             # undefined with <2 msgs
        latency_diff = np.diff(lat)
        # population standard deviation  (ddof=0)
        return np.std(latency_diff, ddof=0)
    
    device_jit = (
        lat_rows.sort_values([exp_col, dev_col, time_col])
                .groupby([exp_col, dev_col, fre_col])
                .apply(device_jitter_std)
                .rename("device_jitter")
                .reset_index()
    )

    #get experiment level mean jitter and mean latency
    exp_stats = (
        device_jit.merge(
            lat_rows.groupby([exp_col, dev_col, fre_col])[lat_col].mean().rename("mean_latency").reset_index(), 
            on=[exp_col, dev_col, fre_col]
        )
        .groupby([exp_col, fre_col])
        .agg(mean_jitter=("device_jitter", "mean"),
            mean_latency=("mean_latency", "mean")
            )
        ).reset_index()
    
    #calculate mean throughput by exp_col, fre_col from full_df
    exp_throughput = (
        full_df.groupby([exp_col, fre_col])["throughput"]
        .mean()
        .rename("mean_throughput")
        .reset_index()
    )

    #calculate mean_latency by second_offset, migration_frequency, exclude the last second offset
    mean_latency = (
        full_df[full_df["second_offset"] <= 60]
        .groupby(["second_offset", fre_col])[lat_col]
        .mean()
        .rename("mean_latency")
        .reset_index()
    )

    fig, axes = plt.subplots(1, 3, figsize=(12.8, 3.6), gridspec_kw={'width_ratios': [2, 1, 1]})

    sns.scatterplot(
        data=exp_stats,
        x="mean_latency",
        y="mean_jitter",
        hue=fre_col,
        hue_order=frequency_order,
        ax=axes[0],
        s=12,
        alpha=0.8,
        edgecolor=None,
        palette=frequency_palette,
        legend=False
    )

    #second plot lineplot
    sns.lineplot(
        data=mean_latency,
        x="second_offset",
        y="mean_latency",
        hue=fre_col,
        hue_order=frequency_order,
        ax=axes[1],
        palette=frequency_palette,
        linewidth=2,
        legend=False
    )

    #third plot boxplot
    sns.boxplot(
        data=exp_throughput,
        x="migration_frequency",
        y="mean_throughput",
        ax=axes[2],
        palette=frequency_palette,
        width=0.4,
        fliersize=3,
        showmeans=True,
        meanprops={"marker": "o", "markerfacecolor": "none", "markeredgecolor": "gray", "markersize": 3}
    )

    handles = [
    plt.Line2D([0], [0], marker='s', color=color, label=label, linestyle='None')
    for label, color in zip(frequency_order, pastel3_alt[:len(frequency_order)])
    ]
    fig.legend(
        handles=handles,
        labels=frequency_order,
        loc='lower center',
        bbox_to_anchor=(0.5, -0.1),
        ncol=len(frequency_order),
        title="Transmission Frequency",
        frameon=False,
    )

    axes[0].set_title("(a)")
    axes[0].set_xlabel("Mean Latency (ms)")
    axes[0].set_ylabel("Mean Jitter (ms)")
    axes[0].spines['top'].set_visible(False)
    axes[0].spines['right'].set_visible(False)

    rect_x = 0   # x-coordinate of lower left corner
    rect_y = 0    # y-coordinate of lower left corner
    rect_width = 65  # width of rectangle
    rect_height = 45  # height of rectangle
    rect = Rectangle(
        (rect_x, rect_y),
        rect_width,
        rect_height,
        linewidth=0.6,
        edgecolor="grey",
        facecolor='none',
        linestyle='dashed'
    )
    axes[0].add_patch(rect)

    axes[1].set_title("(b)")
    axes[1].set_xlabel("Time (s)")
    axes[1].set_ylabel("Mean Latency (ms)")
    axes[1].spines['top'].set_visible(False)
    axes[1].spines['right'].set_visible(False)

    axes[2].set_title("(c)")
    axes[2].set_xlabel("Transmission Frequency")
    axes[2].set_ylabel("Mean Throughput (Kbps)")
    axes[2].spines['top'].set_visible(False)
    axes[2].spines['right'].set_visible(False)


    plt.tight_layout(rect=[0, 0.05, 1, 1])
    plt.savefig(ROOT / '../report/frequency_impact.pdf', bbox_inches='tight')

elif is_six:
    ################################
    ### Performance Effects
    ################################

    per_col   = "fitness_score"          
    time_col  = "second_offset"
    dev_col   = "device_mac_id"
    exp_col   = "experiment_id"
    top_col   = "migration_frequency"
    num_col   = "num_robots"

    Q3 = full_df[per_col].quantile(0.75)
    Q1 = full_df[per_col].quantile(0.25)
    IQR = Q3 - Q1
    upper_bound = Q3 + 1.5 * IQR
    
    filtered_df = full_df[full_df[per_col] <= upper_bound]

    per_rows = filtered_df[(filtered_df[num_col] != 2)].loc[
        filtered_df[per_col].notna() & filtered_df[time_col].notna(),
        [exp_col, dev_col, time_col, per_col, top_col, num_col]
    ]

    #Calculate the adaptation rate, defined as 
    #"swarm adapted when the number of robots with the new best fitness value reach 70% and remains there for at least 10 seconds"
    def calculate_adaptation_rate(
        df,
        bin_seconds=2,
        threshold=0.70,
        sustain_seconds=6,
        tol=1e-3
    ):
        """
        Compute adaptation rate over time per (experiment_id, topology).

        Definitions implemented:
        - Time is bucketed into `bin_seconds` bins to absorb ±1s jitter.
        - 'Bucket best' = lowest fitness_score in that time bucket (lower is better).
        - 'Global best-so-far' = cumulative minimum of bucket best over time.
        - Adaptation rate at time t = fraction of devices whose bucket-level fitness equals
            the current global best-so-far at t.
        - Swarm considered 'adapted' when adaptation rate >= `threshold` and remains
            at/above that for at least `sustain_seconds`.

        Returns
        -------
        rates_df : DataFrame
            One row per (experiment_id, topology, time_bin). Columns:
            - time_bin: left edge of the bin in seconds (int)
            - n_devices: total unique devices in the group
            - bucket_best: min fitness in the bin
            - global_best_so_far: expanding min of bucket_best
            - at_global_best: number of devices at the global best this bin
            - rate: at_global_best / n_devices
            - cum_rate: expanding max of `rate` (monotone, tends to 1.0)
            - adapted_now: boolean, rate >= threshold
        summary_df : DataFrame
            One row per (experiment_id, topology) with:
            - adapted: bool
            - adapted_from_bin: first bin (left edge, seconds) where sustained adaptation starts,
                                or NaN if never adapted
            - adapted_from_time_s: same as above (alias)
        """
        if df.empty:
            return (
                pd.DataFrame(columns=[exp_col, top_col, "time_bin", "n_devices", "bucket_best",
                                    "global_best_so_far", "at_global_best", "rate", "cum_rate", "adapted_now"]),
                pd.DataFrame(columns=[exp_col, top_col, "adapted", "adapted_from_bin", "adapted_from_time_s"])
            )

        work = df.copy()

        # 1) Bucket time (left edge integer seconds, e.g., 0,2,4,...)
        work["time_bin"] = (work[time_col] // bin_seconds) * bin_seconds

        # 2) Within each (group, time_bin, device), take the *min* fitness in that bin
        grp_keys = [exp_col, top_col, "time_bin", dev_col]
        dev_bin = (
            work
            .groupby(grp_keys, as_index=False)[per_col]
            .min()
            .rename(columns={per_col: "dev_bin_fitness"})
        )

        # === Handle missing device rows in the FIRST bucket ===
        dev_first = (
            work.groupby([exp_col, dev_col], as_index=False)[per_col]
            .max()
            .rename(columns={per_col: "device_first_fitness"})
        )

        first_bins = (
            work.groupby([exp_col, top_col], as_index=False)["time_bin"]
                .min()
                .rename(columns={"time_bin": "first_time_bin"})
        )
        dev_bin = dev_bin.merge(first_bins, on=[exp_col, top_col], how="left")
        dev_bin = dev_bin.merge(dev_first, on=[exp_col, dev_col], how="left")

        mask = (dev_bin["time_bin"] == dev_bin["first_time_bin"]) & dev_bin["dev_bin_fitness"].isna()
        dev_bin.loc[mask, "dev_bin_fitness"] = dev_bin.loc[mask, "device_first_fitness"]
        dev_bin = dev_bin.drop(columns=["first_time_bin", "device_first_fitness"])

        dev_bin = dev_bin.sort_values([exp_col, top_col, dev_col, "time_bin"])
        dev_bin["dev_bin_fitness"] = (
            dev_bin
            .groupby([exp_col, top_col, dev_col])["dev_bin_fitness"]
            .ffill()   # propagate previous value forward
        )

        # 3) Bucket best (overall) per (group, time_bin)
        grp_tb = [exp_col, top_col, "time_bin"]
        bucket_best = (
            dev_bin
            .groupby(grp_tb, as_index=False)["dev_bin_fitness"]
            .min()
            .rename(columns={"dev_bin_fitness": "bucket_best"})
        )

        # 4) Global best-so-far (cumulative min over time within each group)
        bucket_best = (
            bucket_best
            .sort_values(grp_tb)
            .assign(global_best_so_far=lambda d:
                    d.groupby([exp_col, top_col])["bucket_best"].cummin())
        )

        # 5) Join global best back to per-device rows for each bin
        dev_bin = dev_bin.merge(bucket_best, on=grp_tb, how="left")

        # 6) Count devices that are exactly at the current global best this bin
        #    (Exact equality assumed; if needed, add a tolerance)
        dev_bin["is_at_global_best"] = (dev_bin["dev_bin_fitness"] - dev_bin["global_best_so_far"]).abs() <= tol

        # 7) Per (group, time_bin): compute rate
        totals = (
            dev_bin
            .groupby([exp_col, top_col], as_index=False)[dev_col]
            .nunique()
            .rename(columns={dev_col: "n_devices"})
        )

        at_best = (
            dev_bin
            .groupby(grp_tb, as_index=False)["is_at_global_best"]
            .sum()
            .rename(columns={"is_at_global_best": "at_global_best"})
        )

        rates = (
            bucket_best
            .merge(at_best, on=grp_tb, how="left")
            .merge(totals, on=[exp_col, top_col], how="left")
            .fillna({"at_global_best": 0})
        )

        rates["rate"] = rates["at_global_best"] / rates["n_devices"].where(rates["n_devices"] > 0, np.nan)
        #rates.loc[(rates["at_global_best"] <= 1) & (rates["time_bin"] < 8), "rate"] = 0.0
        # An optional monotone version (useful for plotting "cumulative adoption")
        rates = rates.sort_values(grp_tb)
        rates["cum_rate"] = rates.groupby([exp_col, top_col])["rate"].cummax()

        # 8) Detect sustained adaptation: rate >= threshold for >= sustain_seconds
        bins_needed = int(np.ceil(sustain_seconds / bin_seconds))
        rates["adapted_now"] = rates["rate"] >= threshold

        def _first_sustained_start(g):
            # rolling window over boolean -> meet all True for window
            # Convert to int and require sum == bins_needed
            if g.empty:
                return np.nan
            
            ser = g["adapted_now"].astype(int)
            # Use rolling with window 'bins_needed' and min==1 to indicate all ones
            ok = ser.rolling(window=bins_needed, min_periods=bins_needed).min() == 1
            if not ok.any():
                return np.nan
            idx = ok.idxmax()  # first True index
            # Start of the sustained window = index - (bins_needed - 1)
            start_pos = g.index.get_loc(idx) - (bins_needed - 1)
            start_idx = g.index[start_pos]

            return g.loc[start_idx, "time_bin"]

        starts = (
            rates
            .set_index(grp_tb)
            .groupby(level=[0,1], sort=False, group_keys=False)
            .apply(lambda gg: _first_sustained_start(gg.reset_index()))
            .reset_index(name="adapted_from_bin")
        )

        summary = rates[[exp_col, top_col]].drop_duplicates().merge(starts, on=[exp_col, top_col], how="left")
        summary["adapted"] = ~summary["adapted_from_bin"].isna()
        summary["adapted_from_time_s"] = summary["adapted_from_bin"]

        return rates, summary
    
    df_rate = full_df[(full_df[num_col] != 2)]

    rates, summary = calculate_adaptation_rate(
        df_rate,
        bin_seconds=2,
        threshold=0.80,
        sustain_seconds=4,
        tol=1e-3
    )

    agg_rates = (
    rates.groupby([top_col, "time_bin"], as_index=False)
         .agg(mean_rate=("cum_rate", "mean"),
              std_rate=("cum_rate", "std"))
    )

    #I want a violin plot with num_col as x axis desc, fitness_score as y-axis and each violin split into two halves by topology
    fig, axes = plt.subplots(1, 2, figsize=(12.8, 3.6), gridspec_kw={'width_ratios': [2, 1]})

    sns.violinplot(
        data=per_rows,
        x=num_col,
        y=per_col,
        hue=top_col,
        hue_order=frequency_order,
        split=True,
        ax=axes[0],
        gap=.05,
        palette=frequency_palette,
        inner="quartile",
        linewidth=0.5,
        legend=False

    )

    sns.lineplot(
        data=agg_rates,
        x="time_bin",
        y="mean_rate",
        hue=top_col,
        hue_order=frequency_order,
        ax=axes[1],
        palette=frequency_palette,
        linewidth=2,
        legend=False
    )

    handles = [
    plt.Line2D([0], [0], marker='s', color=color, label=label, linestyle='None')
    for label, color in zip(frequency_order, pastel3_alt[:len(frequency_order)])
    ]
    fig.legend(
        handles=handles,
        labels=frequency_order,
        loc='lower center',
        bbox_to_anchor=(0.5, -0.1),
        ncol=len(frequency_order),
        title="Transmission Frequency",
        frameon=False,
    )

    axes[0].set_title("(a)")
    axes[0].set_xlabel("Number of Robots")
    axes[0].set_ylabel("Fitness Value")
    axes[0].spines['top'].set_visible(False)
    axes[0].spines['right'].set_visible(False)

    axes[1].set_title("(b)")
    axes[1].set_xlabel("Time (s)")
    axes[1].set_ylabel("Adaptation Rate")
    axes[1].spines['top'].set_visible(False)
    axes[1].spines['right'].set_visible(False)
    axes[1].set_xlim(left=0)
    axes[1].set_ylim(bottom=0, top=1)
    for name, group in agg_rates.groupby(top_col):
        axes[1].fill_between(
            group["time_bin"],
            group["mean_rate"] - group["std_rate"],
            group["mean_rate"] + group["std_rate"],
            alpha=0.2,
            color= frequency_palette[name]
        )

    plt.tight_layout(rect=[0, 0.05, 1, 1])
    plt.savefig(ROOT / '../report/f_performance_impact.pdf', bbox_inches='tight')


print("done")