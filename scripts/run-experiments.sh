#!/usr/bin/env bash
# ==============================================================================
# Smart Emergency Alert and Priority Messaging Network
# Automated Experiment Runner & Results Generator
# ==============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "================================================================================"
echo "  Smart Emergency Alert and Priority Messaging Network - Test Runner"
echo "================================================================================"

echo ""
echo "[Step 1/4] Building target: smart-emergency-priority..."
./ns3 build smart-emergency-priority

echo ""
echo "[Step 2/4] Running BASELINE Simulation (FIFO Queueing, --priority=0 --runs=10 --anim=1)..."
./ns3 run "smart-emergency-priority --priority=0 --runs=10 --anim=1"

echo ""
echo "[Step 3/4] Running PRIORITY Simulation (PrioQueueDisc, --priority=1 --runs=10 --anim=1)..."
./ns3 run "smart-emergency-priority --priority=1 --runs=10 --anim=1"

echo ""
echo "[Step 4/4] Generating Presentation Charts & Analysis Dashboard (with 95% CI & CDF)..."
python3 reports/analyze-results.py

echo ""
echo "================================================================================"
echo "  All experiments & figure generation completed successfully!"
echo "  Generated files:"
echo "    - Data Tables: results-baseline.csv, results-priority.csv, comparison-summary.csv"
echo "    - Multi-run Stats: stats-summary-baseline.csv, stats-summary-priority.csv, stats-comparison.csv"
echo "    - Delay CDF Data: emergency-cdf-baseline.csv, emergency-cdf-priority.csv"
echo "    - NetAnim Traces: animation-baseline.xml, animation-priority.xml"
echo "    - Presentation Charts (reports/figures/):"
echo "        * delay_comparison.png (with 95% CI error bars)"
echo "        * pdr_comparison.png (with 95% CI error bars)"
echo "        * throughput_comparison.png (with 95% CI error bars)"
echo "        * packet_loss_comparison.png (with 95% CI error bars)"
echo "        * emergency_delay_cdf.png (delay CDF with theoretical bounds)"
echo "        * dashboard.html (Interactive Presentation Dashboard)"
echo "================================================================================"
