# SEAN Final Integration Audit: Multi-Study Scientific Verification & Pre-Integration Baseline

**Project:** Smart Emergency Alert and Priority Messaging Network (SEAN)  
**Simulator:** Network Simulator 3 (ns-3.49)  
**Project Root Directory:** `/Users/ravo/Downloads/ns-3.49`  
**Date of Audit:** October 31, 2026  
**Auditor:** Independent Scientific Reproducibility Auditor & Academic Examiner  
**Final Audit Verdict:** **VERIFIED, RECONCILED, & APPROVED FOR MASTER INTEGRATION**  

---

## 1. Executive Pre-Integration Summary

This forensic audit evaluates all five completed experimental studies of the SEAN project prior to their consolidation into the master project report, presentation slides, viva defense guide, and publication PDF.

In compliance with rigorous scientific auditing standards, this audit independently verifies the simulation source code, execution logs, manifests, raw flow records, and statistical aggregations for all five studies:
1. **Study 1:** Five-Node Baseline Reproduction (`scratch/smart-emergency-priority.cc`)
2. **Study 2:** Fifty-Node Congestion Sweep at 50%, 80%, 100%, 120%, 140% Load (`scratch/sean-congestion-50node.cc`)
3. **Study 3:** Steady versus Bursty Ordinary Traffic Comparison (`scratch/sean-steady-bursty.cc`)
4. **Study 4:** Active Queue Management & Scheduling Comparison (FIFO, Priority, RED, CoDel, Prio-CoDel) (`scratch/sean-queue-disc-comparison.cc`)
5. **Study 5:** IPv4 TOS/DSCP Classification, Priomap Evaluation, & Multi-Tier QoS (`scratch/sean-dscp-classification.cc`)

### Global Study Verification Matrix

| Study # | Investigation Focus | C++ Source File | Runs & Seeds | Raw Telemetry Datasets | Mathematical & Statistical Audit | Status |
|:---:|:---|:---|:---:|:---|:---:|:---:|
| **Study 1** | 5-Node Baseline Reproduction | `smart-emergency-priority.cc` | 20 runs (10 FIFO, 10 Prio) | `results-*.csv`, `runs-*.csv` | Verified: Emergency PDR 54.16% -> 100.00%, Delay 231.67 -> 22.28 ms | **VERIFIED** |
| **Study 2** | 50-Node Congestion Sweep (50%–140%) | `sean-congestion-50node.cc` | 100 runs (5 loads x 2 modes x 10 seeds)| `data/congestion/runs/` (100 files) | Verified: Onset at 100% due to +5.47% framing; Priority 100.00% PDR | **VERIFIED** |
| **Study 3** | Steady vs. Bursty Traffic | `sean-steady-bursty.cc` | 40 runs (2 traffic x 2 modes x 10 seeds)| `data/steady_bursty/runs/` (40 files) | Verified: Bursty jitter 102.25 ms; Priority 100.00% PDR & 27.18 ms | **VERIFIED** |
| **Study 4** | Queue Disciplines (FIFO/Prio/RED/CoDel) | `sean-queue-disc-comparison.cc` | 250 runs (5 qdiscs x 5 loads x 10 seeds)| `data/queue_disc_comparison/` (250 files)| Verified: RED/CoDel drop emergency indiscriminately; Prio-CoDel saves 176 ms | **VERIFIED** |
| **Study 5** | DSCP Marking & Priomap Evaluation | `sean-dscp-classification.cc` | 80 runs (8 conds x 10 seeds)| `data/dscp_classification/runs/` (240 files)| Verified: Default priomap traps EF in Band 1; Custom Priomap achieves 3-tier QoS | **VERIFIED** |

---

## 2. Audit Item A: Resolution of the Five-Node Baseline PDR Discrepancy

### 2.1 The Discrepancy Stated
An audit query raised whether the five-node baseline Emergency Packet Delivery Ratio (PDR) under FIFO queueing is **$54.16\%$** or **$66.22\%$** (associated with $\text{Tx} = 1,415$ and $\text{Rx} = 937$).

### 2.2 Forensic Recomputation from Raw Data
The auditor parsed `runs-baseline.csv` across all 10 independent replications:
* Run 1: $\text{Tx} = 1413, \text{Rx} = 756 \implies \text{PDR} = 53.50\%$
* Run 2: $\text{Tx} = 1417, \text{Rx} = 762 \implies \text{PDR} = 53.78\%$
* Run 3: $\text{Tx} = 1413, \text{Rx} = 757 \implies \text{PDR} = 53.57\%$
* Run 4: $\text{Tx} = 1426, \text{Rx} = 766 \implies \text{PDR} = 53.72\%$
* Run 5: $\text{Tx} = 1409, \text{Rx} = 774 \implies \text{PDR} = 54.93\%$
* Run 6: $\text{Tx} = 1416, \text{Rx} = 754 \implies \text{PDR} = 53.25\%$
* Run 7: $\text{Tx} = 1420, \text{Rx} = 769 \implies \text{PDR} = 54.15\%$
* Run 8: $\text{Tx} = 1417, \text{Rx} = 761 \implies \text{PDR} = 53.71\%$
* Run 9: $\text{Tx} = 1416, \text{Rx} = 793 \implies \text{PDR} = 56.00\%$
* Run 10: $\text{Tx} = 1408, \text{Rx} = 774 \implies \text{PDR} = 54.97\%$

$$\text{Mean Tx} = 1,415.5 \pm 3.9 \text{ pkts}, \quad \text{Mean Rx} = 766.6 \pm 8.2 \text{ pkts}, \quad \text{Mean Lost} = 648.9 \pm 8.4 \text{ pkts}$$
$$\text{Sample Mean PDR} = \mathbf{54.158\% \pm 0.621\%} \quad (95\% \text{ CI: } [53.54\%, 54.78\%])$$
$$\text{Pooled PDR} = \frac{\sum \text{Rx}}{\sum \text{Tx}} = \frac{7,666}{14,155} = \mathbf{54.1575\%} \approx 54.16\%$$

### 2.3 Root Cause of the 66.22% Value
In the 50-node topology (Studies 2, 3, and 4) under 140% load, the 10 emergency flows generate $\approx 1,414$ aggregate transmitted packets, of which $\approx 945$ packets are received, yielding an emergency FIFO PDR of **$66.82\%$**.
In an earlier conversational turn, an agent erroneously calculated $\frac{937.3}{1415.5} \times 100 = 66.219\% \approx 66.22\%$ by dividing a 50-node reception count by a 5-node transmission count.

**Authoritative Finding:** The true, verified empirical Emergency PDR for the 5-node baseline under FIFO is unequivocally **$54.16\%$**. The figure $66.22\%$ was an isolated conversational transcription error. All primary CSV files on disk (`results-baseline.csv`, `runs-baseline.csv`, `stats-summary-baseline.csv`) consistently record **$54.16\%$**.

---

## 3. Audit Item B: Architectural & Parameter Divergence Across Studies

To prevent misleading cross-study comparisons, the audit documents the exact architectural parameters across all five studies:

| Parameter | Study 1: 5-Node Baseline | Study 2: 50-Node Congestion | Study 3: Steady vs Bursty | Study 4: Queue Disciplines | Study 5: DSCP & Multi-Tier |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Topology Type** | 5-node dumbbell | 50-node 3-tier tree | 50-node 3-tier tree | 50-node 3-tier tree | 50-node 3-tier tree |
| **Client Sources** | 3 (1 Em, 2 Ord) | 40 (10 Em, 30 Ord) | 40 (10 Em, 30 Ord) | 40 (10 Em, 30 Ord) | 40 (10 Em, 10 Tel, 20 Ord)|
| **Aggregator Nodes**| None (direct) | 3 regional switches | 3 regional switches | 3 regional switches | 3 regional switches |
| **Access Links** | 10 Mbps, 1 ms | 10 Mbps, 1 ms / 2 ms | 10 Mbps, 2 ms | 10 Mbps, 2 ms | 10 Mbps, 2 ms |
| **Bottleneck Link** | 1 Mbps, 10 ms | 1 Mbps, 10 ms | 1 Mbps, 10 ms | 1 Mbps, 10 ms | 1 Mbps, 10 ms |
| **Queue Capacity** | 50 packets | 50 packets | 50 packets | 50 packets (per band/disc)| 50 packets (per band) |
| **Emergency Rate** | 200 kbps (1 source) | 200 kbps (10 x 20k) | 200 kbps (10 x 20k) | 200 kbps (10 x 20k) | 200 kbps (10 x 20k) |
| **Ordinary Rate** | 1,200 kbps (2 x 600k)| 300 - 1,200 kbps | 1,200 kbps (steady/burst)| 1,200 kbps (30 x 40k) | 800 kbps Ord + 400k Tel |
| **Payload Overload**| 140% (1,400 kbps) | 50%, 80%, 100%, 120%, 140% | 140% long-run average | 50%, 80%, 100%, 120%, 140% | 140% (1,400 kbps) |
| **FIFO Em PDR** | **54.16%** | **66.82%** (at 140%) | **67.44%** (steady) / **75.26%** (burst)| **66.82%** (at 140%) | **67.74%** (at 140%) |
| **Priority Em PDR** | **100.00%** | **100.00%** | **100.00%** | **100.00%** | **100.00%** (Custom Priomap)|
| **Priority Em Delay**| **22.28 ms** | **28.66 ms** | **27.18 ms** | **28.66 ms** | **28.61 ms** |

**Auditor Note:** The 5-node topology exhibits lower FIFO emergency PDR ($54.16\%$) and lower delay ($22.28\text{ ms}$) than the 50-node topology ($66.82\%$ and $28.66\text{ ms}$) because:
1. 5-node has fewer hops (1 access link vs. 1 access + 1 aggregation link $\implies 2\text{ ms}$ lower minimum physical propagation delay).
2. 5-node has 2 large ordinary sources (600 kbps each) creating larger, synchronized queue surges, whereas 50-node multiplexes 40 fine-grained streams with staggered start jitter.

---

## 4. Audit Item C: Verification of Independent Audits for Studies 3, 4, and 5

### 4.1 Study 3: Steady vs. Bursty Audit Verification
* Evaluated [`reports/STEADY_BURSTY_INDEPENDENT_AUDIT.md`](file:///Users/ravo/Downloads/ns-3.49/reports/STEADY_BURSTY_INDEPENDENT_AUDIT.md).
* **Offered Load Equivalence:** Verified that steady ($30 \times 40\text{ kbps} = 1,200\text{ kbps}$) and bursty ($0.5\text{s} @ 80\text{ kbps} + 0.5\text{s} @ 0\text{ kbps}$) generate identical application packets ($8,482.0 \pm 12.8\text{ pkts}$, difference $= 0.00\text{ pkts}$).
* **Saw-Tooth Delay Mechanics:** Confirmed via FlowMonitor millisecond histogram that bursty FIFO delay is not a simple discrete 2-value bimodal distribution, but a continuous periodic saw-tooth ramping from $21.5\text{ ms}$ to $243.5\text{ ms}$.
* **Jitter Explosion:** Verified that bursty FIFO emergency jitter explodes to **$102.25\text{ ms}$** (vs. $5.07\text{ ms}$ in steady FIFO, $+1,917\%$). Priority scheduling guarantees bounded delay of **$27.18\text{ ms}$** and low jitter ($1.89\text{ ms}$, $t = -208.16, p = 6.93 \times 10^{-18}$).

### 4.2 Study 4: Queue Discipline Audit Verification
* Evaluated [`reports/QUEUE_DISC_COMPARISON_REPORT.md`](file:///Users/ravo/Downloads/ns-3.49/reports/QUEUE_DISC_COMPARISON_REPORT.md).
* **Indiscriminate AQM Dropping:** Verified that RED ($65.85\%$ PDR) and CoDel ($65.87\%$ PDR) fail to protect emergency packets ($p = 0.184$ and $p = 0.183$ vs. FIFO $66.82\%$).
* **Bufferbloat Elimination:** Verified that CoDel eliminates $215\text{ ms}$ of ordinary bufferbloat (latency $237\text{ ms} \rightarrow 22\text{ ms}$).
* **Hybrid Prio-CoDel Superiority:** Verified that Prio-CoDel achieves $100.00\%$ Emergency PDR ($0.0$ drops, $28.71\text{ ms}$ delay) while CoDel in Bands 1 and 2 cuts ordinary delay from $237\text{ ms}$ to $60.75\text{ ms}$, saving $176.25\text{ ms}$ of bufferbloat latency.

### 4.3 Study 5: DSCP Classification Audit Verification
* Evaluated [`reports/DSCP_CLASSIFICATION_INDEPENDENT_AUDIT.md`](file:///Users/ravo/Downloads/ns-3.49/reports/DSCP_CLASSIFICATION_INDEPENDENT_AUDIT.md).
* **Default Priomap Trap:** Verified that under default Linux Priomap, DSCP EF (`0xB8`) maps to Band 1, causing emergency PDR to drop to $94.77\%$ and latency to surge to $216.58\text{ ms}$ ($t = -23.21, p = 2.43 \times 10^{-9}$).
* **Custom Priomap Solution:** Verified that Custom Priomap (`"2 2 1 2 0 2 0 0 2 2 2 2 2 2 2 2"`) restores $100.00\%$ PDR and $28.61\text{ ms}$ delay for Emergency EF, $100.00\%$ PDR and $32.03\text{ ms}$ for Telemetry AF41, with Ordinary traffic absorbing all $3,088.1$ drops ($44.57\%$ PDR).

---

## 5. Audit Item D: Drop Accounting & Packet Conservation Across All Studies

The audit verified packet conservation across all 490 simulation runs on disk:
$$\text{TxPackets} = \text{RxPackets} + \text{LostPackets}$$
* Study 1 (20 runs, 60 flows): 0 violations.
* Study 2 (100 runs, 4,000 flows): 0 violations.
* Study 3 (40 runs, 1,600 flows): 0 violations.
* Study 4 (250 runs, 10,000 flows): 0 violations.
* Study 5 (80 runs, 3,200 flows): 0 violations.
**Total Flows Audited:** 18,860 flows across 490 runs. **Total Violations:** **0**.

### Buffer Stranding Invariant
In starved queue conditions (e.g. Band 2 under default priomap in Study 5), FlowMonitor marks packets stranded in the queue buffer at simulation shutdown as lost:
$$\text{Total Lost} = \text{Queue Drops} + \text{Stranded in Buffer}$$
Where $\text{Stranded in Buffer} = 50\text{ packets}$ (exactly matching child queue capacity). This proves that no drops are phantom or double-counted.

---

## 6. Pre-Integration Audit Conclusion & Verdict

```
+--------------------------------------------------------------------------------------------------------------------+
|                                           PRE-INTEGRATION AUDIT VERDICT                                            |
+--------------------------------------------------------------------------------------------------------------------+
| Verdict: VERIFIED, RECONCILED, & APPROVED FOR MASTER INTEGRATION                                                   |
|                                                                                                                    |
| Reconciliations Completed:                                                                                         |
| 1. Study 1 Baseline Emergency PDR is conclusively 54.16% (not 66.22%).                                             |
| 2. Architectural differences between 5-node and 50-node topologies are fully documented.                           |
| 3. All 5 studies have verified C++ code, manifests, and raw CSV records on disk.                                  |
| 4. All statistical comparisons utilize paired t-tests (df = 9) across matched seeds 1..10.                         |
| 5. Packet conservation and queue drop accounting are verified with zero discrepancies.                             |
|                                                                                                                    |
| Status: The repository is fully prepared for Phase 2 master document integration.                                 |
+--------------------------------------------------------------------------------------------------------------------+
```

