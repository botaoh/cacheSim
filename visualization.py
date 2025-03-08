import pandas as pd
import matplotlib.pyplot as plt

# Load and parse the test results log file
log_file_path = "test_results.log"

# Read the log file
with open(log_file_path, "r") as file:
    log_data = file.readlines()

# Extract relevant data
data = []
current_trace = None
current_policy = None

for line in log_data:
    line = line.strip()
    
    if line.startswith("Testing with trace:"):
        current_trace = line.split(":")[1].strip()
    elif line.startswith("L2 (C,B,S):"):
        current_policy = line.split("Replace policy: ")[1].split(".")[0]
    elif line.startswith("L1 hit ratio:"):
        l1_hit_ratio = float(line.split(":")[1].strip())
    elif line.startswith("L1 average access time (AAT):"):
        l1_aat = float(line.split(":")[1].strip())
    elif line.startswith("L2 read hit ratio:"):
        l2_hit_ratio = float(line.split(":")[1].strip())
    elif line.startswith("L2 average access time (AAT):"):
        l2_aat = float(line.split(":")[1].strip())
        # Append to data once we have all required fields
        data.append([current_trace, current_policy, l1_hit_ratio, l1_aat, l2_hit_ratio, l2_aat])

# Convert data to a DataFrame
df = pd.DataFrame(data, columns=["Trace", "Replacement Policy", "L1 Hit Ratio", "L1 AAT", "L2 Hit Ratio", "L2 AAT"])

# Plot Hit Ratio
plt.figure(figsize=(10, 5))
for trace in df["Trace"].unique():
    subset = df[df["Trace"] == trace]
    plt.plot(subset["Replacement Policy"], subset["L1 Hit Ratio"], marker="o", linestyle="-", label=f"{trace} - L1")
    plt.plot(subset["Replacement Policy"], subset["L2 Hit Ratio"], marker="s", linestyle="--", label=f"{trace} - L2")

plt.xlabel("Replacement Policy")
plt.ylabel("Hit Ratio")
plt.title("Effect of Replacement Policy on Hit Ratios")
plt.legend()
plt.grid(True)
plt.xticks(rotation=45)
plt.show()

# Plot AAT
plt.figure(figsize=(10, 5))
for trace in df["Trace"].unique():
    subset = df[df["Trace"] == trace]
    plt.plot(subset["Replacement Policy"], subset["L1 AAT"], marker="o", linestyle="-", label=f"{trace} - L1")
    plt.plot(subset["Replacement Policy"], subset["L2 AAT"], marker="s", linestyle="--", label=f"{trace} - L2")

plt.xlabel("Replacement Policy")
plt.ylabel("Average Access Time (AAT)")
plt.title("Effect of Replacement Policy on AAT")
plt.legend()
plt.grid(True)
plt.xticks(rotation=45)
plt.show()
