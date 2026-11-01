#!/usr/bin/env bash
# ==============================================================================
# Smart Emergency Alert and Priority Messaging Network (SEAN)
# Prompt 5: Automated Runner for DSCP Classification Experiments
#
# File: run-dscp-classification-experiments.sh
# Simulator: Network Simulator 3 (ns-3.49)
# ==============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

OUTPUT_DIR="data/dscp_classification"
RUNS_DIR="${OUTPUT_DIR}/runs"
MANIFEST="${OUTPUT_DIR}/manifest.csv"
FIG_DIR="reports/figures/dscp_classification"

mkdir -p "${RUNS_DIR}" "${FIG_DIR}"

echo "================================================================================"
echo "  SEAN Prompt 5: Packet Classification & Alternative DSCP Markings Sweep       "
echo "  Executing 8 experimental conditions x 10 matched seeds (80 total runs)       "
echo "================================================================================"

# Initialize Manifest
echo "run_id,condition,qdisc,priomap,em_tos,telem_tos,ord_tos,seed,run,exit_code,status,timestamp" > "${MANIFEST}"

# Build target executable first
echo "[Build] Compiling sean-dscp-classification..."
./ns3 build sean-dscp-classification

# Conditions Definition
# format: condition|qdisc|priomap|emTos|telemTos|ordTos
CONDITIONS=(
    "fifo_be|fifo|none|0x00|0x00|0x00"
    "fifo_diffserv|fifo|none|0xB8|0x88|0x00"
    "prio_default_tos10|prio|1,2,2,2,1,2,0,0,1,1,1,1,1,1,1,1|0x10|0x00|0x00"
    "prio_default_diffserv|prio|1,2,2,2,1,2,0,0,1,1,1,1,1,1,1,1|0xB8|0x88|0x00"
    "prio_default_cs6|prio|1,2,2,2,1,2,0,0,1,1,1,1,1,1,1,1|0xC0|0x88|0x00"
    "prio_default_unshifted|prio|1,2,2,2,1,2,0,0,1,1,1,1,1,1,1,1|0x2E|0x22|0x00"
    "prio_custom_diffserv|prio|2,2,1,2,0,2,0,0,2,2,2,2,2,2,2,2|0xB8|0x88|0x00"
    "prio_custom_tos10|prio|2,2,1,2,0,2,0,0,2,2,2,2,2,2,2,2|0x10|0x88|0x00"
)

TOTAL_RUNS=$(( ${#CONDITIONS[@]} * 10 ))
RUN_COUNT=0
FAIL_COUNT=0

for r in $(seq 1 10); do
    echo "--------------------------------------------------------------------------------"
    echo "  Replication Seed ${r} / 10 (Seed=${r}, Run=${r})"
    echo "--------------------------------------------------------------------------------"
    for cond in "${CONDITIONS[@]}"; do
        IFS='|' read -r C_NAME C_QDISC C_PRIOMAP C_EMTOS C_TELEMTOS C_ORDTOS <<< "${cond}"
        RUN_COUNT=$((RUN_COUNT + 1))
        
        CMD_ARGS="--condition=${C_NAME} --qdiscType=${C_QDISC} --emTos=${C_EMTOS} --telemTos=${C_TELEMTOS} --ordTos=${C_ORDTOS} --run=${r} --seed=${r} --outDir=${RUNS_DIR}"
        if [ "${C_QDISC}" = "prio" ]; then
            CMD_ARGS="${CMD_ARGS} --priomap=${C_PRIOMAP}"
        fi

        printf "  [%2d/%2d] %-24s (Seed %2d) ... " "${RUN_COUNT}" "${TOTAL_RUNS}" "${C_NAME}" "${r}"
        
        TS=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
        EXIT_CODE=0
        if ./ns3 run "sean-dscp-classification ${CMD_ARGS}" > /dev/null 2>&1; then
            STATUS="SUCCESS"
            echo "SUCCESS"
        else
            EXIT_CODE=$?
            STATUS="FAILED"
            FAIL_COUNT=$((FAIL_COUNT + 1))
            echo "FAILED (code: ${EXIT_CODE})"
        fi

        echo "${RUN_COUNT},${C_NAME},${C_QDISC},${C_PRIOMAP},${C_EMTOS},${C_TELEMTOS},${C_ORDTOS},${r},${r},${EXIT_CODE},${STATUS},${TS}" >> "${MANIFEST}"
    done
done

echo "================================================================================"
echo "  Sweep Complete: ${RUN_COUNT} attempted, $((RUN_COUNT - FAIL_COUNT)) successful, ${FAIL_COUNT} failed"
echo "  Manifest written to: ${MANIFEST}"
echo "================================================================================"

if [ "${FAIL_COUNT}" -gt 0 ]; then
    echo "ERROR: Some simulation runs failed! Halting before analysis."
    exit 1
fi

echo "[Analysis] Executing reports/analyze-dscp-classification.py..."
python3 reports/analyze-dscp-classification.py

