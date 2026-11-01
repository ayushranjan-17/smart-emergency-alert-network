#!/usr/bin/env bash
# ==============================================================================
# Smart Emergency Alert and Priority Messaging Network (SEAN)
# Queue Discipline Evaluation Experiment Execution Script
#
# Disciplines: FIFO, Priority (PrioQueueDisc), RED (RedQueueDisc), 
#              CoDel (CoDelQueueDisc), Prio-CoDel (Hybrid)
# Offered Loads: 50%, 80%, 100%, 120%, 140%
# Replications: 10 independent runs per condition (Seeds 1..10)
# Total Simulations: 5 disciplines x 5 loads x 10 runs = 250 runs
# ==============================================================================

set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_ROOT"

DATA_DIR="data/queue_disc_comparison"
RUNS_DIR="${DATA_DIR}/runs"
MASTER_CSV="${DATA_DIR}/queue_disc_comparison_master.csv"
MANIFEST_CSV="${DATA_DIR}/manifest.csv"

mkdir -p "$RUNS_DIR"

echo "================================================================================"
echo "  SEAN QUEUE DISCIPLINE EXPERIMENT EXECUTION (FIFO, PRIO, RED, CODEL, PRIO-CODEL)"
echo "================================================================================"
echo "Project Root: $PROJECT_ROOT"
echo "Output Directory: $DATA_DIR"
echo "Timestamp: $(date -u +"%Y-%m-%d %H:%M:%SZ")"
echo ""

# Build target first
echo "[1/4] Building ns-3 simulation target: sean-queue-disc-comparison..."
./ns3 build sean-queue-disc-comparison

DISCIPLINES=("fifo" "prio" "red" "codel" "prio-codel")
LOADS=(50 80 100 120 140)
RUNS=10

TOTAL_RUNS=$((${#DISCIPLINES[@]} * ${#LOADS[@]} * RUNS))
COMPLETED=0
FAILURES=0

echo ""
echo "[2/4] Executing $TOTAL_RUNS simulations across 5 disciplines and 5 load levels..."

START_TIME=$(date +%s)

for qdisc in "${DISCIPLINES[@]}"; do
    for load in "${LOADS[@]}"; do
        for run in $(seq 1 $RUNS); do
            seed=$run
            COMPLETED=$((COMPLETED + 1))
            
            summaryCsv="${RUNS_DIR}/results-summary-${qdisc}-load-${load}-run-${run}.csv"
            flowCsv="${RUNS_DIR}/flow-results-${qdisc}-load-${load}-run-${run}.csv"
            queueCsv="${RUNS_DIR}/queue-validation-${qdisc}-load-${load}-run-${run}.csv"
            
            # Execute simulation
            if ./ns3 run "sean-queue-disc-comparison --qdisc=${qdisc} --load=${load} --run=${run} --seed=${seed} --summaryCsv=${summaryCsv} --flowCsv=${flowCsv} --queueCsv=${queueCsv}" > /dev/null 2>&1; then
                printf "\r  [%3d/%3d] QDisc: %-10s | Load: %3d%% | Run: %2d (Seed: %2d) ... SUCCESS" "$COMPLETED" "$TOTAL_RUNS" "$qdisc" "$load" "$run" "$seed"
            else
                printf "\r  [%3d/%3d] QDisc: %-10s | Load: %3d%% | Run: %2d (Seed: %2d) ... FAILED\n" "$COMPLETED" "$TOTAL_RUNS" "$qdisc" "$load" "$run" "$seed"
                FAILURES=$((FAILURES + 1))
            fi
        done
    done
done

END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))

echo ""
echo ""
echo "Simulation Phase Complete: $COMPLETED attempted, $FAILURES failed in ${ELAPSED}s."

if [ $FAILURES -gt 0 ]; then
    echo "ERROR: Some simulations failed! Exiting without consolidation."
    exit 1
fi

echo ""
echo "[3/4] Consolidating summary datasets into master CSV: $MASTER_CSV..."

echo "QDisc,Run,Seed,LoadPercent,TrafficClass,FlowCount,TxPackets,RxPackets,LostPackets,PDR_Percent,Throughput_Kbps,MeanDelay_ms,MeanJitter_ms,QueueDrops" > "$MASTER_CSV"

for qdisc in "${DISCIPLINES[@]}"; do
    for load in "${LOADS[@]}"; do
        for run in $(seq 1 $RUNS); do
            summaryCsv="${RUNS_DIR}/results-summary-${qdisc}-load-${load}-run-${run}.csv"
            if [ -f "$summaryCsv" ]; then
                tail -n +2 "$summaryCsv" >> "$MASTER_CSV"
            else
                echo "Warning: Missing $summaryCsv"
            fi
        done
    done
done

TOTAL_ROWS=$(wc -l < "$MASTER_CSV")
echo "  Consolidated $TOTAL_ROWS lines (including header) in $MASTER_CSV"

echo ""
echo "[4/4] Writing experiment manifest: $MANIFEST_CSV..."

cat <<EOF > "$MANIFEST_CSV"
Parameter,Value
Experiment,SEAN Queue Discipline Evaluation
Simulator,ns-3.49
Network Topology,50 Nodes (40 Sources 3 Regional Routers 2 Core Gateways 5 Sink Servers)
Bottleneck Link,Node 43 to Node 44 (1 Mbps 10 ms delay 50 packet capacity)
Queue Disciplines Evaluated,FIFO PrioQueueDisc RedQueueDisc CoDelQueueDisc Prio-CoDel
RED Parameters,MinTh=10p MaxTh=30p MaxSize=50p QW=0.002 MeanPktSize=540B
CoDel Parameters,Target=5ms Interval=100ms MaxSize=50p MinBytes=540B
Offered Loads Tested,50% 80% 100% 120% 140%
Traffic Distribution,Emergency Alert: 10 flows (1/7 offered load TOS 0x10); Ordinary Msg: 30 flows (6/7 offered load TOS 0x00)
Simulation Duration,30.0s (Active window 29.0s)
Replications,10 runs per condition (Seeds 1..10)
Total Simulation Runs,$TOTAL_RUNS
Execution Timestamp,$(date -u +"%Y-%m-%d %H:%M:%SZ")
Master CSV Path,$MASTER_CSV
Status,COMPLETED_VERIFIED
EOF

echo "  Manifest written successfully."
echo ""
echo "================================================================================"
echo "  QUEUE DISC EXPERIMENTS FULLY EXECUTED AND CONSOLIDATED SUCCESSFULLY!"
echo "================================================================================"

