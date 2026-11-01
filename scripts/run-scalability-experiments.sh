#!/usr/bin/env bash
# ==============================================================================
# Smart Emergency Alert and Priority Messaging Network (SEAN)
# 50-Node Scalability 10-Run Automated Experiment Runner
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

RUNS_DIR="data/scalability/runs"
MANIFEST_FILE="data/scalability/manifest.csv"
MANIFEST_RUNS_FILE="${RUNS_DIR}/manifest.csv"

mkdir -p "$RUNS_DIR"

echo "================================================================================"
echo "  SEAN 50-Node Scalability Multi-Run Experiment Runner (ns-3.49)"
echo "  Executing 10 Independent Replications (FIFO vs Strict Priority)"
echo "================================================================================"

echo ""
echo "[Step 1/3] Building target: sean-scalability-50node..."
./ns3 build sean-scalability-50node

echo ""
echo "[Step 2/3] Executing 10 Paired Simulation Replications (Runs 1..10)..."

# Initialize Manifest CSV
cat << 'EOF' > "$MANIFEST_FILE"
RunIndex,Seed,Mode,Status,ExitCode,SummaryFile,FlowFile,QueueFile,Timestamp
EOF

NUM_RUNS=10
TOTAL_SUCCESS=0
TOTAL_FAILED=0

for r in $(seq 1 $NUM_RUNS); do
    echo "--------------------------------------------------------------------------------"
    echo "  Replication $r / $NUM_RUNS (RngSeed=1, RngRun=$r)"
    echo "--------------------------------------------------------------------------------"

    # 1. Baseline FIFO Run
    BASE_SUM="${RUNS_DIR}/results-summary-baseline-run-${r}.csv"
    BASE_FLOW="${RUNS_DIR}/flow-results-baseline-run-${r}.csv"
    BASE_Q="${RUNS_DIR}/queue-validation-baseline-run-${r}.csv"
    rm -f "$BASE_SUM" "$BASE_FLOW" "$BASE_Q"

    echo -n "  [Run $r/10] FIFO Baseline mode... "
    set +e
    ./ns3 run "sean-scalability-50node --priority=0 --run=${r} --seed=1 --simTime=30.0 --summaryCsv=${BASE_SUM} --flowCsv=${BASE_FLOW} --queueCsv=${BASE_Q}" > /dev/null 2>&1
    RET_BASE=$?
    set -e

    TIME_STAMP=$(date +"%Y-%m-%d %H:%M:%S")
    if [ $RET_BASE -eq 0 ] && [ -f "$BASE_SUM" ]; then
        echo "SUCCESS"
        echo "${r},1,baseline,SUCCESS,0,${BASE_SUM},${BASE_FLOW},${BASE_Q},\"${TIME_STAMP}\"" >> "$MANIFEST_FILE"
        TOTAL_SUCCESS=$((TOTAL_SUCCESS + 1))
    else
        echo "FAILED (Exit: $RET_BASE)"
        echo "${r},1,baseline,FAILED,${RET_BASE},${BASE_SUM},${BASE_FLOW},${BASE_Q},\"${TIME_STAMP}\"" >> "$MANIFEST_FILE"
        TOTAL_FAILED=$((TOTAL_FAILED + 1))
        echo "Aborting on unexpected failure in Run ${r} (Baseline)!"
        exit 1
    fi

    # 2. Priority Mode Run
    PRIO_SUM="${RUNS_DIR}/results-summary-priority-run-${r}.csv"
    PRIO_FLOW="${RUNS_DIR}/flow-results-priority-run-${r}.csv"
    PRIO_Q="${RUNS_DIR}/queue-validation-priority-run-${r}.csv"
    rm -f "$PRIO_SUM" "$PRIO_FLOW" "$PRIO_Q"

    echo -n "  [Run $r/10] Priority (PrioQueueDisc) mode... "
    set +e
    ./ns3 run "sean-scalability-50node --priority=1 --run=${r} --seed=1 --simTime=30.0 --summaryCsv=${PRIO_SUM} --flowCsv=${PRIO_FLOW} --queueCsv=${PRIO_Q}" > /dev/null 2>&1
    RET_PRIO=$?
    set -e

    TIME_STAMP=$(date +"%Y-%m-%d %H:%M:%S")
    if [ $RET_PRIO -eq 0 ] && [ -f "$PRIO_SUM" ]; then
        echo "SUCCESS"
        echo "${r},1,priority,SUCCESS,0,${PRIO_SUM},${PRIO_FLOW},${PRIO_Q},\"${TIME_STAMP}\"" >> "$MANIFEST_FILE"
        TOTAL_SUCCESS=$((TOTAL_SUCCESS + 1))
    else
        echo "FAILED (Exit: $RET_PRIO)"
        echo "${r},1,priority,FAILED,${RET_PRIO},${PRIO_SUM},${PRIO_FLOW},${PRIO_Q},\"${TIME_STAMP}\"" >> "$MANIFEST_FILE"
        TOTAL_FAILED=$((TOTAL_FAILED + 1))
        echo "Aborting on unexpected failure in Run ${r} (Priority)!"
        exit 1
    fi
done

# Copy manifest to runs directory as well
cp -f "$MANIFEST_FILE" "$MANIFEST_RUNS_FILE"

echo ""
echo "================================================================================"
echo "  Simulation batch complete: $TOTAL_SUCCESS successful runs, $TOTAL_FAILED failed runs."
echo "  Manifest recorded at: $MANIFEST_FILE"
echo "================================================================================"

echo ""
echo "[Step 3/3] Executing Multi-Run Statistical Analysis (Mean & 95% Confidence Intervals)..."
python3 reports/analyze-scalability.py --multirun

echo ""
echo "================================================================================"
echo "  All 10-run scalability evaluations completed successfully!"
echo "================================================================================"

