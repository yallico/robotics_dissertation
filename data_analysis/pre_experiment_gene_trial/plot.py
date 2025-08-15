import pandas as pd
import os
import matplotlib.pyplot as plt
import seaborn as sns  # added import
import matplotlib as mpl

# --- IEEE-like params ---
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

# Load the data
script_dir = os.path.dirname(__file__)
input_file = os.path.join(script_dir, "../temp/one_device_results/filtered_output.csv")
df = pd.read_csv(input_file)
df['max_genes'] = df['max_genes'].astype(int)      # cast max_genes as int
df['pop_size'] = df['pop_size'].astype(int)          # cast pop_size as int

#========================
# Lineplot steps vs gene
#========================
# Group by (log_id, max_genes) and compute the mean fitness
grouped_df = df.groupby(['log_id', 'max_genes'])['fitness_score'].mean().reset_index()

#========================
# Combined Subplots: Fitness Score vs. Log ID & Fitness Score vs. Population Size differentiated by Max Genes
#========================
# fig, axes = plt.subplots(1, 2, figsize=(7, 3))

# sns.lineplot(data=grouped_df, x='log_id', y='fitness_score', hue='max_genes', marker='o', ax=axes[0])
# axes[0].set_xlabel('Log ID (Iteration)')
# axes[0].set_ylabel('Mean Fitness Score')
# axes[0].set_title('(a)')

# sns.boxplot(x='pop_size', y='fitness_score', hue='max_genes', data=df, ax=axes[1])
# axes[1].set_xlabel('Population Size')
# axes[1].set_ylabel('Fitness Score')
# axes[1].set_title('(b)')

# # Remove individual legends then add a common legend
# handles, labels = axes[1].get_legend_handles_labels()
# axes[0].get_legend().remove()
# axes[1].get_legend().remove()

# for ax in axes:
#     ax.spines['top'].set_visible(False)
#     ax.spines['right'].set_visible(False)

# fig.legend(handles, labels, loc='lower center', ncol=len(labels), title='Genes')
#plt.tight_layout(rect=[0, 0.05, 1, 1])
#plt.savefig('/home/yallicol/Documents/robotics_dissertation/report/ga_prelim_analysis_v2.pdf', bbox_inches='tight') 

fig, ax = plt.subplots(figsize=(6.4, 3.6))

sns.boxplot(
    x='pop_size',
    y='fitness_score',
    hue='max_genes',
    data=df,
    ax=ax,
    linewidth=1,       
    fliersize=3,       
    palette='Pastel1',
    showmeans=True,
    meanprops={"marker": "o", "markerfacecolor": "none", "markeredgecolor": "gray", "markersize": 3}    
)

ax.set_xlabel('Population Size')
ax.set_ylabel('Mean Fitness Value')
#ax.set_title('(b)')

# IEEE styling
ax.spines['top'].set_visible(False)
ax.spines['right'].set_visible(False)

# Legend outside or below, no frame, small font
handles, labels = ax.get_legend_handles_labels()
ax.legend(
    handles,
    labels,
    title='Genes',
    loc='upper center',
    bbox_to_anchor=(0.5, 1.25),  
    ncol=len(labels),
    frameon=False
)

plt.tight_layout(rect=[0, 0.05, 1, 1])
plt.savefig('/home/yallicol/Documents/robotics_dissertation/report/ga_prelim_analysis_v2.pdf', bbox_inches='tight')

