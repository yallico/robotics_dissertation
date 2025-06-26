import pandas as pd
import os
import matplotlib.pyplot as plt
import seaborn as sns  # added import
sns.set_theme(style="whitegrid")  # set seaborn theme

# Load the data
script_dir = os.path.dirname(__file__)
input_file = os.path.join(script_dir, "temp/results/filtered_output.csv")
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
fig, axes = plt.subplots(1, 2, figsize=(14, 6))

sns.lineplot(data=grouped_df, x='log_id', y='fitness_score', hue='max_genes', marker='o', ax=axes[0])
axes[0].set_xlabel('Log ID (Iteration)')
axes[0].set_ylabel('Mean Fitness Score')
axes[0].set_title('(a)')

sns.boxplot(x='pop_size', y='fitness_score', hue='max_genes', data=df, ax=axes[1])
axes[1].set_xlabel('Population Size')
axes[1].set_ylabel('Fitness Score')
axes[1].set_title('(b)')

# Remove individual legends then add a common legend
handles, labels = axes[1].get_legend_handles_labels()
axes[0].get_legend().remove()
axes[1].get_legend().remove()

fig.legend(handles, labels, loc='lower center', ncol=len(labels), title='Genes')

plt.tight_layout(rect=[0, 0.05, 1, 1])
plt.savefig('/home/yallicol/Documents/robotics_dissertation/report/ga_prelim_analysis.pdf', bbox_inches='tight') 

