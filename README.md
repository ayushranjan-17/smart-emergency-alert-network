# Smart Emergency Alert and Priority Messaging Network (SEAN)
**Discrete-Event Network Simulation & Scientific Quality of Service Evaluation in ns-3.49**

[![ns-3 Version](https://img.shields.io/badge/ns--3-3.49-blue.svg)](https://www.nsnam.org/)
[![License: GPL-2.0](https://img.shields.io/badge/License-GPLv2-green.svg)](https://www.gnu.org/licenses/gpl-2.0.html)
[![Standard](https://img.shields.io/badge/C%2B%2B-17%2F20-blue.svg)](https://isocpp.org/)
[![Status](https://img.shields.io/badge/Status-Fully%20Audited%20%26%20Verified-brightgreen.svg)]()
[![Simulation Runs](https://img.shields.io/badge/Replications-490%20Runs-orange.svg)]()
[![Academic Report](https://img.shields.io/badge/Report-17--Page%20Publication%20PDF-purple.svg)](docs/SEAN_FINAL_PROJECT_REPORT.pdf)

A comprehensive discrete-event network research simulation evaluating **Differentiated Services (DiffServ)**, **strict priority queueing (`PrioQueueDisc`)**, **Active Queue Management (RED, CoDel, Hybrid Prio-CoDel)**, **traffic burstiness**, and **IPv4 DSCP/Priomap packet classification** at the network Traffic Control layer to isolate life-critical emergency alerts from heavy civilian congestion during disaster scenarios.

---

## 📌 Project Overview

During severe emergencies (natural disasters, mass casualty incidents), telecommunications infrastructure experiences catastrophic traffic overload. Under conventional **First-In-First-Out (FIFO)** queueing, life-critical emergency dispatches are queued behind recreational civilian bulk traffic, suffering excessive bufferbloat delays (>230 ms) and severe tail-drops (~46% packet loss).

This project designs, simulates, benchmarks, and independently audits the **Smart Emergency Alert and Priority Messaging Network (SEAN)** on **ns-3.49**. Across **5 distinct experimental studies** and **490 independent simulation replications** (18,860 monitored flow records), SEAN proves that:
1. Non-preemptive priority queueing slashes emergency latency by **90.4%** ($231.67\text{ ms} \rightarrow 22.28\text{ ms}$) while guaranteeing **100.00% Packet Delivery Ratio (PDR)** and **zero queue drops** under 140% link overload.
2. Drop onset occurs at nominal 100% offered load due to $+5.47\%$ wire framing overhead (IP/UDP/Ethernet headers pushing wire load to $1,054.68\text{ kbps}$ on a $1.0\text{ Mbps}$ bottleneck).
3. Ordinary traffic burstiness causes emergency jitter to explode by $+1,917\%$ in FIFO, while priority queueing completely eliminates jitter.
4. Standalone Active Queue Management (RED and CoDel) drops emergency alerts indiscriminately ($p = 0.184$ vs. FIFO), whereas **Hybrid Prio-CoDel** delivers 100% emergency delivery while eliminating $176\text{ ms}$ of civilian bufferbloat.
5. In FIFO, DSCP marking alone provides **$0.00\%$ benefit** ($p = 1.000$). Under the default Linux Priomap, Expedited Forwarding (EF: `0xB8`) is trapped in Band 1 with civilian traffic, while **Custom DiffServ Priomap** (`"2 2 1 2 0 2 0 0 2 2 2 2 2 2 2 2"`) restores full 3-tier Quality of Service.

---

## 🔬 Five Experimental Studies Overview

| Study ID | Research Question | Topology | Conditions Evaluated | Replications | Primary Source Code |
|:---|:---|:---|:---|:---:|:---|
| **Study 1** | Baseline overload reproduction | 5-node dumbbell (3 clients, 1 bottleneck gateway) | FIFO vs. Priority (140% load) | 20 runs | [`simulation/smart-emergency-priority.cc`](simulation/smart-emergency-priority.cc) |
| **Study 2** | Multi-load congestion sweep | 50-node hierarchical tree (40 clients, 3 aggregators) | 5 offered loads (50%, 80%, 100%, 120%, 140%) | 100 runs | [`simulation/sean-congestion-50node.cc`](simulation/sean-congestion-50node.cc) |
| **Study 3** | Traffic burstiness dynamics | 50-node hierarchical tree | Steady vs. Bursty (Load-matched 1,200 kbps) | 40 runs | [`simulation/sean-steady-bursty.cc`](simulation/sean-steady-bursty.cc) |
| **Study 4** | Queue discipline comparison | 50-node hierarchical tree | FIFO, Priority, RED, CoDel, Prio-CoDel | 250 runs | [`simulation/sean-queue-disc-comparison.cc`](simulation/sean-queue-disc-comparison.cc) |
| **Study 5** | DSCP classification & Priomap | 50-node hierarchical tree | 8 TOS/DSCP & Linux Priomap configurations | 80 runs | [`simulation/sean-dscp-classification.cc`](simulation/sean-dscp-classification.cc) |

---

## 📊 Consolidated Cross-Study Results

| Metric Dimension | Study 1: 5-Node Baseline | Study 2: 50-Node Congestion | Study 3: Traffic Burstiness | Study 4: Queue Disciplines | Study 5: Multi-Tier DSCP |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Simulated Runs** | 20 runs | 100 runs | 40 runs | 250 runs | 80 runs |
| **Total Senders** | 3 senders | 40 senders | 40 senders | 40 senders | 40 senders (3 classes) |
| **FIFO Emergency PDR** | **$54.16\% \pm 0.62\%$** | **$66.82\% \pm 0.54\%$** | $67.44\%$ (std) / $75.26\%$ (bst)| **$66.82\% \pm 0.54\%$** | **$67.74\% \pm 0.49\%$** |
| **Priority Emergency PDR** | **$100.00\% \pm 0.00\%$** | **$100.00\% \pm 0.00\%$** | **$100.00\% \pm 0.00\%$** | **$100.00\% \pm 0.00\%$** | **$100.00\% \pm 0.00\%$** (Custom) |
| **FIFO Emergency Delay** | $231.67\text{ ms}$ | $234.09\text{ ms}$ | $237.02\text{ ms}$ / $118.13\text{ ms}$ | $234.09\text{ ms}$ | $232.43\text{ ms}$ |
| **Priority Emergency Delay**| **$22.28\text{ ms}$** | **$28.66\text{ ms}$** | **$27.18\text{ ms}$** | **$28.66\text{ ms}$** | **$28.61\text{ ms}$** |
| **Band 0 Queue Drops** | **0.0** | **0.0** | **0.0** | **0.0** | **0.0** |
| **Civilian Trade-off** | PDR: $70.3\% \rightarrow 62.7\%$ | PDR: $68.1\% \rightarrow 62.6\%$ | PDR: $52.4\% \rightarrow 48.4\%$ | Ordinary Delay: $290\text{ ms}$ | PDR: $68.5\% \rightarrow 44.6\%$ |

---

## 🔑 Key Engineering & Scientific Contributions

1. **Traffic Control & Hardware Ring Decoupling (`MaxSize = 1p`):**  
   In ns-3 (and Linux), each `PointToPointNetDevice` maintains an internal driver queue (default 100 packets). If unconstrained, packets bypass the Traffic Control layer. Constraining the device queue to `1p` forces all waiting packets into `PrioQueueDisc`, where priority scheduling occurs.

2. **The Default Linux Priomap Trap Uncovered:**  
   Under the standard Linux kernel Priomap (`{1, 2, 2, 2, 1, 2, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1}`), standard DSCP Expedited Forwarding (EF: `0xB8`) maps to socket priority 4, which indexes into **Band 1**—sharing queue space with civilian traffic and suffering $94.77\%$ PDR and $216.58\text{ ms}$ delay. Telemetry (AF41: `0x88`) is mapped to Band 2 and starved ($1.08\%$ PDR). Configuring a **Custom DiffServ Priomap** (`"2 2 1 2 0 2 0 0 2 2 2 2 2 2 2 2"`) restores full 3-tier Quality of Service.

3. **Indiscriminate Active Queue Management Discard Behavior:**  
   Proved that standalone AQM schemes (RED and CoDel) discard emergency packets indiscriminately ($p = 0.184$ vs. FIFO) because they make drop decisions based solely on queue occupancy without priority inspection. **Hybrid Prio-CoDel** provides the optimal combined solution: Band 0 strict priority protects alerts, while CoDel in lower bands eliminates $176\text{ ms}$ of civilian bufferbloat ($237.00\text{ ms} \rightarrow 60.75\text{ ms}$).

4. **Drop Conservation Invariant:**  
   Across all 490 runs, priority scheduling never creates bandwidth; total dropped packets are strictly conserved:
   $$\text{TxPackets} = \text{RxPackets} + \text{QueueDrops} + \text{StrandedInBuffer}$$
   Zero packet leaks or phantom drops were detected.

5. **Theoretical Non-Preemptive Delay Upper Bound:**  
   Because physical link transmission cannot be aborted, arriving emergency packets experience a bounded residual serialization delay:
   $$t_{\text{residual, max}} = \frac{542 \times 8 \text{ bits}}{1,000,000 \text{ bps}} = 4.336\text{ ms} \quad (\text{mean } 2.168\text{ ms})$$
   $$\text{Maximum Upper Bound Delay} = D_{\text{min}} (15.77\text{ ms}) + T_{\text{committed}} (4.34\text{ ms}) + t_{\text{residual, max}} (4.34\text{ ms}) = 24.45\text{ ms}$$

---

## 📁 Repository Structure

```text
smart-emergency-alert-network/
├── README.md                          # Comprehensive master project documentation
├── .gitignore                         # Standard git ignore rules
├── audit-bundle.zip                   # Complete reproducible bundle (15 MB)
├── simulation/                        # C++ ns-3 simulation models
│   ├── smart-emergency-priority.cc    # Study 1: 5-Node Baseline
│   ├── emergency-alert.cc             # Study 1: Single-run baseline
│   ├── sean-congestion-50node.cc      # Study 2: 50-Node Congestion Sweep (50%–140%)
│   ├── sean-scalability-50node.cc     # Study 2: Single-run 50-node scalability
│   ├── sean-steady-bursty.cc          # Study 3: Steady vs. Bursty Dynamics
│   ├── sean-queue-disc-comparison.cc  # Study 4: Queue Disciplines (FIFO, Priority, RED, CoDel, Prio-CoDel)
│   └── sean-dscp-classification.cc    # Study 5: Multi-Tier DSCP & Priomap Evaluation
├── scripts/                           # Shell experiment runners and Python analysis pipelines
│   ├── run-experiments.sh             # Study 1 automated runner (20 runs)
│   ├── run-congestion-experiments.sh  # Study 2 automated runner (100 runs)
│   ├── run-steady-bursty-experiments.sh # Study 3 automated runner (40 runs)
│   ├── run-queue-disc-experiments.sh  # Study 4 automated runner (250 runs)
│   ├── run-dscp-classification-experiments.sh # Study 5 automated runner (80 runs)
│   ├── run-scalability-experiments.sh # Study 2 single-run runner
│   ├── analyze-results.py             # Study 1 analysis & plotting
│   ├── analyze-scalability.py         # Study 2 scalability analysis
│   ├── analyze-congestion.py          # Study 2 multi-load analysis
│   ├── analyze-steady-bursty.py       # Study 3 burstiness analysis
│   ├── analyze-queue-disc.py          # Study 4 queue discipline analysis
│   ├── analyze-dscp-classification.py # Study 5 DSCP classification analysis
│   └── convert_to_pdf.py              # Publication PDF compilation pipeline
├── docs/                              # Formal academic reports, audits, and defense guides
│   ├── SEAN_FINAL_PROJECT_REPORT.pdf  # Publication-grade 17-page compiled PDF report
│   ├── SEAN_FINAL_PROJECT_REPORT.md   # Consolidated 20-section academic capstone report
│   ├── VIVA_PREPARATION.md            # 38-question comprehensive oral defense guide
│   ├── PRESENTATION_SLIDES.md         # 18-slide B.Tech presentation deck
│   ├── FINAL_PRE_SUBMISSION_VERIFICATION.md # Final pre-submission verification audit
│   ├── FINAL_SUBMISSION_CHECKLIST.md  # Final submission checklist
│   ├── FINAL_INTEGRATION_AUDIT.md     # Pre-integration audit report
│   ├── FINAL_AUDIT_DISCREPANCY_RESOLUTION.md # Forensic Study 1 PDR reconciliation
│   ├── CONGESTION_EXPERIMENT_REPORT.md # Study 2 report
│   ├── CONGESTION_EXPERIMENT_AUDIT.md  # Study 2 audit
│   ├── STEADY_BURSTY_EXPERIMENT_REPORT.md # Study 3 report
│   ├── STEADY_BURSTY_INDEPENDENT_AUDIT.md # Study 3 audit
│   ├── QUEUE_DISC_COMPARISON_REPORT.md # Study 4 report
│   ├── DSCP_CLASSIFICATION_REPORT.md  # Study 5 report
│   └── DSCP_CLASSIFICATION_INDEPENDENT_AUDIT.md # Study 5 audit
├── figures/                           # 35 high-resolution 300 DPI analytical figures
│   ├── delay_comparison.png
│   ├── pdr_comparison.png
│   ├── throughput_comparison.png
│   ├── packet_loss_comparison.png
│   ├── emergency_delay_cdf.png
│   ├── congestion/                    # Study 2 figures (5 charts)
│   ├── steady_bursty/                 # Study 3 figures (5 charts)
│   ├── queue_disc_comparison/         # Study 4 figures (5 charts)
│   └── dscp_classification/           # Study 5 figures (5 charts)
└── data/                              # Raw CSV datasets across all 490 runs
    ├── results-baseline.csv
    ├── results-priority.csv
    ├── runs-baseline.csv
    ├── runs-priority.csv
    ├── stats-summary-baseline.csv
    ├── stats-summary-priority.csv
    ├── stats-comparison.csv
    ├── congestion/                    # Study 2 CSV datasets (100 runs)
    ├── steady_bursty/                 # Study 3 CSV datasets (40 runs)
    ├── queue_disc_comparison/         # Study 4 CSV datasets (250 runs)
    └── dscp_classification/           # Study 5 CSV datasets (80 runs)
```

---

## 🚀 How to Reproduce All Experiments

### Prerequisites
- Network Simulator 3 (**ns-3.49**).
- Python 3 with `numpy`, `scipy`, `pandas`, `Pillow`, and `mistune`.
- Headless Google Chrome (for automated PDF report generation).

### Reproduction Commands

Copy the simulation models to your ns-3 `scratch/` directory, then execute the automated runners:

```bash
# 1. Study 1: Five-Node Baseline Overload (20 runs)
./scripts/run-experiments.sh
python3 scripts/analyze-results.py

# 2. Study 2: Fifty-Node Congestion Sweep (100 runs)
./scripts/run-congestion-experiments.sh
python3 scripts/analyze-congestion.py

# 3. Study 3: Steady vs. Bursty Traffic Dynamics (40 runs)
./scripts/run-steady-bursty-experiments.sh
python3 scripts/analyze-steady-bursty.py

# 4. Study 4: Queue Disciplines Comparison (250 runs)
./scripts/run-queue-disc-experiments.sh
python3 scripts/analyze-queue-disc.py

# 5. Study 5: DSCP Classification & Priomap (80 runs)
./scripts/run-dscp-classification-experiments.sh
python3 scripts/analyze-dscp-classification.py

# 6. Recompile Publication PDF Report (17 Pages)
python3 scripts/convert_to_pdf.py
```

---

## 📜 Academic Deliverables

- **Compiled PDF Report:** [`docs/SEAN_FINAL_PROJECT_REPORT.pdf`](docs/SEAN_FINAL_PROJECT_REPORT.pdf) (17 pages, standard A4, embedded figures, KaTeX formulas).
- **Viva Voce Defense Guide:** [`docs/VIVA_PREPARATION.md`](docs/VIVA_PREPARATION.md) (38 spoken English questions covering all theoretical, architectural, and statistical nuances).
- **Presentation Slide Deck:** [`docs/PRESENTATION_SLIDES.md`](docs/PRESENTATION_SLIDES.md) (18 slides with visual chart mappings).
- **Pre-Submission Verification Report:** [`docs/FINAL_PRE_SUBMISSION_VERIFICATION.md`](docs/FINAL_PRE_SUBMISSION_VERIFICATION.md) (100% PASS audit across 8 verification criteria).

---

## 📜 License
Distributed under the GNU General Public License v2.0. See [`LICENSE`](LICENSE) for details.
