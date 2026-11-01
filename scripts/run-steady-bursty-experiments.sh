#!/usr/bin/env bash
# ==============================================================================
# Smart Emergency Alert and Priority Messaging Network (SEAN)
# Controlled Experiment Runner: Steady vs. Bursty Ordinary Traffic
# Evaluates 4 Conditions (Steady/Bursty x FIFO/Priority) x 10 Replications = 40 Runs
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

RUNS_DIR="data/steady_bursty/runs"
MANIFEST_FILE="data/steady_bursty/manifest.csv"
FIGURES_DIR="reports/figures/steady_bursty"

mkdir -p "$RUNS_DIR"
mkdir -p "$FIGURES_DIR"

echo "================================================================================"
echo "  SEAN Steady vs. Bursty Traffic Experiment Runner (ns-3.49)"
echo "  40 Total Runs: [Steady FIFO, Steady Priority, Bursty FIFO, Bursty Priority]"
echo "================================================================================"

echo ""
echo "[Step 1/3] Building target: sean-steady-bursty..."
./ns3 build sean-steady-bursty

# Initialize Manifest CSV
cat << 'EOF' > "$MANIFEST_FILE"
RunIndex,Seed,Scenario,Mode,Status,ExitCode,SummaryFile,FlowFile,QueueFile,Timestamp
EOF

NUM_RUNS=10
TOTAL_SUCCESS=0
TOTAL_FAILED=0
START_TIME=$(date +%s)

echo ""
echo "[Step 2/3] Executing 40 Simulation Runs (10 Paired Replications per condition)..."

SCENARIOS=("steady" "bursty")
MODES=("baseline" "priority")

for r in $(seq 1 $NUM_RUNS); do
    echo "--------------------------------------------------------------------------------"
    echo "  Replication $r / $NUM_RUNS (Seed=$r, Run=$r)"
    echo "--------------------------------------------------------------------------------"

    for scen in "${SCENARIOS[@]}"; do
        burst_val=0
        if [ "$scen" == "bursty" ]; then
            burst_val=1
        fi

        for mode in "${MODES[@]}"; do
            prio_val=0
            if [ "$mode" == "priority" ]; then
                prio_val=1
            fi

            summary_file="${RUNS_DIR}/results-summary-${scen}-${mode}-run-${r}.csv"
            flow_file="${RUNS_DIR}/flow-results-${scen}-${mode}-run-${r}.csv"
            queue_file="${RUNS_DIR}/queue-validation-${scen}-${mode}-run-${r}.csv"

            printf "  [Run %2d/10] %-7s %-8s ... " "$r" "$scen" "$mode"

            if ./ns3 run "sean-steady-bursty --bursty=${burst_val} --priority=${prio_val} --run=${r} --seed=${r} --summaryCsv=${summary_file} --flowCsv=${flow_file} --queueCsv=${queue_file}" > /dev/null 2>&1; then
                echo "SUCCESS"
                TOTAL_SUCCESS=$((TOTAL_SUCCESS + 1))
                echo "${r},${r},${scen},${mode},SUCCESS,0,${summary_file},${flow_file},${queue_file},\"$(date '+%Y-%m-%d %H:%M:%S')\"" >> "$MANIFEST_FILE"
            else
                echo "FAILED"
                TOTAL_FAILED=$((TOTAL_FAILED + 1))
                echo "${r},${r},${scen},${mode},FAILED,1,${summary_file},${flow_file},${queue_file},\"$(date '+%Y-%m-%d %H:%M:%S')\"" >> "$MANIFEST_FILE"
            fi
        done
    done
done

END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))

echo ""
echo "================================================================================"
echo "  Simulation batch complete: $TOTAL_SUCCESS successful runs, $TOTAL_FAILED failed runs."
echo "  Total execution time: ${ELAPSED} seconds."
echo "  Manifest recorded at: $MANIFEST_FILE"
echo "================================================================================"

echo ""
echo "[Step 3/3] Executing Statistical Analysis & Figure Generation..."
python3 reports/analyze-steady-bursty.py

