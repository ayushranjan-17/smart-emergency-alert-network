#!/usr/bin/env bash
# ==============================================================================
# Smart Emergency Alert and Priority Messaging Network (SEAN)
# Congestion-Level Experiment Runner (50-Node Topology)
# Evaluates 5 Offered Load Levels (50%, 80%, 100%, 120%, 140%) x 2 Queueing Modes
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

RUNS_DIR="data/congestion/runs"
MANIFEST_FILE="data/congestion/manifest.csv"
MANIFEST_RUNS_FILE="${RUNS_DIR}/manifest.csv"

mkdir -p "$RUNS_DIR"

LOAD_LEVELS=(50 80 100 120 140)
MODES=("baseline" "priority")

# Default runs count (can be overridden by argument, e.g. 3 for pilot, 10 for full)
NUM_RUNS=10
START_RUN=1

if [ "$1" == "--pilot" ]; then
    echo "================================================================================"
    echo "  RUNNING PILOT EXPERIMENT (3 Runs per condition: Runs 1..3)"
    echo "================================================================================"
    NUM_RUNS=3
    START_RUN=1
    # Initialize / reset manifest for pilot
    cat << 'EOF' > "$MANIFEST_FILE"
RunIndex,Seed,LoadPercent,Mode,Status,ExitCode,SummaryFile,FlowFile,QueueFile,Timestamp
EOF
elif [ "$1" == "--continue-full" ]; then
    echo "================================================================================"
    echo "  CONTINUING FULL EXPERIMENT (Remaining Runs 4..10)"
    echo "================================================================================"
    NUM_RUNS=10
    START_RUN=4
    # Do not overwrite manifest; append remaining runs
elif [ "$1" == "--full" ] || [ -z "$1" ]; then
    echo "================================================================================"
    echo "  RUNNING FULL EXPERIMENT CAMPAIGN (10 Runs per condition: Runs 1..10)"
    echo "================================================================================"
    NUM_RUNS=10
    START_RUN=1
    cat << 'EOF' > "$MANIFEST_FILE"
RunIndex,Seed,LoadPercent,Mode,Status,ExitCode,SummaryFile,FlowFile,QueueFile,Timestamp
EOF
fi

echo ""
echo "[Step 1] Building simulation binary: sean-congestion-50node..."
./ns3 build sean-congestion-50node

echo ""
echo "[Step 2] Executing simulations across 5 Load Levels x 2 Queueing Modes (Runs ${START_RUN}..${NUM_RUNS})..."

TOTAL_SUCCESS=0
TOTAL_FAILED=0

for load in "${LOAD_LEVELS[@]}"; do
    echo "================================================================================"
    echo "  OFFERED LOAD CONDITION: ${load}% of Bottleneck Capacity (${load}0 Kbps App Payload)"
    echo "================================================================================"

    for mode in "${MODES[@]}"; do
        if [ "$mode" == "priority" ]; then
            PRIO_VAL=1
        else
            PRIO_VAL=0
        fi

        for r in $(seq $START_RUN $NUM_RUNS); do
            SUM_FILE="${RUNS_DIR}/results-summary-${mode}-load-${load}-run-${r}.csv"
            FLOW_FILE="${RUNS_DIR}/flow-results-${mode}-load-${load}-run-${r}.csv"
            Q_FILE="${RUNS_DIR}/queue-validation-${mode}-load-${load}-run-${r}.csv"

            rm -f "$SUM_FILE" "$FLOW_FILE" "$Q_FILE"

            echo -n "  [Load ${load}% | ${mode} | Run ${r}/${NUM_RUNS}] Running... "
            set +e
            ./ns3 run "sean-congestion-50node --priority=${PRIO_VAL} --load=${load} --run=${r} --seed=1 --simTime=30.0 --summaryCsv=${SUM_FILE} --flowCsv=${FLOW_FILE} --queueCsv=${Q_FILE}" > /dev/null 2>&1
            RET=$?
            set -e

            TIME_STAMP=$(date +"%Y-%m-%d %H:%M:%S")

            if [ $RET -eq 0 ] && [ -f "$SUM_FILE" ] && [ -f "$FLOW_FILE" ] && [ -f "$Q_FILE" ]; then
                echo "SUCCESS"
                echo "${r},1,${load},${mode},SUCCESS,0,${SUM_FILE},${FLOW_FILE},${Q_FILE},\"${TIME_STAMP}\"" >> "$MANIFEST_FILE"
                TOTAL_SUCCESS=$((TOTAL_SUCCESS + 1))
            else
                echo "FAILED (Exit Code: $RET)"
                echo "${r},1,${load},${mode},FAILED,${RET},${SUM_FILE},${FLOW_FILE},${Q_FILE},\"${TIME_STAMP}\"" >> "$MANIFEST_FILE"
                TOTAL_FAILED=$((TOTAL_FAILED + 1))
                echo "ERROR: Simulation failed at Load ${load}%, Mode ${mode}, Run ${r}! Aborting."
                exit 1
            fi
        done
    done
done

cp -f "$MANIFEST_FILE" "$MANIFEST_RUNS_FILE"

echo ""
echo "================================================================================"
echo "  Batch execution completed: ${TOTAL_SUCCESS} successful runs, ${TOTAL_FAILED} failed runs."
echo "  Manifest updated at: ${MANIFEST_FILE}"
echo "================================================================================"
