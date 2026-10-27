# Smart Emergency Alert and Priority Messaging Network
**Discrete-Event Network Simulation in ns-3.49**

[![ns-3 Version](https://img.shields.io/badge/ns--3-3.49-blue.svg)](https://www.nsnam.org/)
[![License: GPL-2.0](https://img.shields.io/badge/License-GPLv2-green.svg)](https://www.gnu.org/licenses/gpl-2.0.html)
[![Standard](https://img.shields.io/badge/C%2B%2B-17%2F20-blue.svg)](https://isocpp.org/)
[![Status](https://img.shields.io/badge/Status-Fully%20Audited%20%26%20Verified-brightgreen.svg)]()

A high-performance research simulation demonstrating **Differentiated Services (DiffServ)** and **strict priority queueing (`PrioQueueDisc`)** at the network Traffic Control layer to isolate life-critical emergency alerts from heavy background civilian congestion during disaster scenarios.

---

## 📌 Project Overview

During severe emergencies (natural disasters, mass casualty incidents), telecommunications infrastructure experiences catastrophic traffic overload. Under conventional **First-In-First-Out (FIFO)** queueing, life-critical emergency dispatches are queued behind recreational bulk traffic, suffering excessive bufferbloat delays (>230 ms) and severe tail-drops (~46.6% packet loss).

This project designs, simulates, and evaluates a **Smart Emergency Alert and Priority Messaging Network** on **ns-3.49**, demonstrating that priority scheduling slashes emergency latency by **90.4%** (from 231.60 ms down to 22.26 ms) while guaranteeing **100.0% Packet Delivery Ratio (PDR)** and **zero queue drops** under a 140% link overload condition.

---

## 🌐 Network Topology

The network models an asymmetric 5-node star-bottleneck topology:

```text
[Node 0: Emergency Source]  ---(10 Mbps, 1 ms, 200 Kbps, TOS 0x10)---\
                                                                       +---> [Node 3: Gateway Router] ---(1.0 Mbps, 10 ms, 50-pkt buffer)---> [Node 4: Response Centre]
[Node 1: Ordinary Source 1] ---(10 Mbps, 1 ms, 600 Kbps, TOS 0x00)---+     (PrioQueueDisc / FifoQueueDisc)
                                                                       |
[Node 2: Ordinary Source 2] ---(10 Mbps, 1 ms, 600 Kbps, TOS 0x00)---/
```

- **Ingress Load:** Total offered load is **1.4 Mbps** (Emergency 200 Kbps + Ordinary 1 600 Kbps + Ordinary 2 600 Kbps).
- **Bottleneck Capacity:** **1.0 Mbps**, creating a sustained **40% link overload**. Ordinary traffic alone (1.2 Mbps) saturates the bottleneck.
- **Packet Size:** 512 bytes payload (542 bytes on the wire, including IP/UDP/PPP headers).

---

## 🔬 Key Engineering Contributions

1. **Traffic Control & Device Queue Decoupling (`MaxSize = 1p`):**
   In ns-3 (and Linux), each `PointToPointNetDevice` maintains an internal driver queue (default 100 packets). If unconstrained, packets bypass the Traffic Control layer. Constraining the device queue to `1p` forces all waiting packets into `PrioQueueDisc`, where priority scheduling occurs.

2. **DiffServ IPv4 TOS to Priority Mapping:**
   The emergency sender marks the IPv4 Type of Service octet with `"Tos" = 0x10` (Low Delay / Interactive). ns-3's socket layer maps TOS bits `[6:3]` to Socket Priority 6, tagging packets with `SocketPriorityTag(6)`, which routes packets directly into Band 0 of `PrioQueueDisc`.

3. **Periodicity Breaking (`JitterTrafficApp`):**
   Pure constant-rate CBR traffic causes discrete-event resonance lockstep, where one ordinary flow monopolizes queue slots while the other suffers tail-drop. `JitterTrafficApp` introduces independent $\pm 20\%$ uniform inter-packet jitter, ensuring Ordinary Sources 1 and 2 experience statistically symmetric PDR within $< 2.85\%$ delta across all runs.

4. **Multi-Run Student-$t$ Statistical Rigor:**
   Automated 10 independent simulation runs (`RngRun=1..10`) per mode with sample mean, standard error, and two-tailed 95% Confidence Interval computation ($df=9, t=2.262$).

---

## 📊 Empirical Results (10-Run Mean $\pm$ 95% CI)

| Traffic Class | Metric | Baseline (FIFO) | Priority (`PrioQueueDisc`) | Performance Delta |
| :--- | :--- | :---: | :---: | :---: |
| **Emergency Alert** | **Packet Delivery Ratio** | $53.42 \pm 1.18\%$ | **$100.00 \pm 0.00\%$** | **$+87.2\%$ (Zero Loss)** |
| | **Mean End-to-End Delay** | $231.60 \pm 0.21\text{ ms}$ | **$22.26 \pm 0.02\text{ ms}$** | **$-90.4\%$ (10x Faster)** |
| | **Throughput** | $111.93 \pm 2.49\text{ Kbps}$ | **$210.92 \pm 0.34\text{ Kbps}$** | **$+88.4\%$ (Full Rate)** |
| | **Mean Jitter** | $1.65 \pm 0.04\text{ ms}$ | **$1.41 \pm 0.02\text{ ms}$** | **$-14.5\%$** |
| | **Queue Drops** | $659.30 \pm 16.90\text{ pkts}$ | **$0.00 \pm 0.00\text{ pkts}$** | **Zero Band 0 Drops** |
| **Ordinary Msg 1** | Packet Delivery Ratio | $70.63 \pm 0.65\%$ | $63.15 \pm 0.50\%$ | $-10.6\%$ (Absorbs congestion) |
| | Mean End-to-End Delay | $232.18 \pm 0.11\text{ ms}$ | $288.93 \pm 0.22\text{ ms}$ | $+24.4\%$ |
| **Ordinary Msg 2** | Packet Delivery Ratio | $70.23 \pm 0.45\%$ | $62.17 \pm 0.47\%$ | $-11.5\%$ (Absorbs congestion) |
| | Mean End-to-End Delay | $232.17 \pm 0.07\text{ ms}$ | $288.87 \pm 0.10\text{ ms}$ | $+24.4\%$ |
| **Gateway Router** | **Total Packet Drops** | $3169.70 \pm 10.45\text{ pkts}$ | $3169.60 \pm 10.48\text{ pkts}$ | Congestion fully contained |

---

## 📐 Theoretical Queueing Delay Breakdown

Because physical channel transmission is non-preemptive, priority emergency packets wait only for:
1. **Minimum Transit Time ($D_{\text{min}} = 15.77\text{ ms}$):**
   $$D_{\text{min}} = D_{\text{prop, access}} (1.0) + T_{\text{tx, access}} (0.4336) + D_{\text{prop, bottle}} (10.0) + T_{\text{tx, bottle}} (4.336) = 15.77\text{ ms}$$
2. **Mean Residual of In-Flight Packet ($R = 2.17\text{ ms}$):**
   $$\mathbb{E}[R] = \frac{T_{\text{tx, bottle}}}{2} = \frac{4.336\text{ ms}}{2} = 2.168\text{ ms} \approx 2.17\text{ ms}$$
3. **Committed 1-Packet Device Queue ($T_{\text{committed}} = 4.34\text{ ms}$):**
   One ordinary packet already committed to the hardware transmission register.

$$\text{Expected Mean Priority Delay} = 15.77 + 2.17 + 4.34 = 22.28\text{ ms}$$
$$\text{Theoretical Upper Bound Ceiling} = 15.77 + 4.34 + 4.34 = 24.45\text{ ms} \approx 24.4\text{ ms}$$

*Empirical measurement of $22.26 \pm 0.02\text{ ms}$ matches the theoretical expectation perfectly.*

---

## 📁 Repository Structure

```text
smart-emergency-alert-network/
├── simulation/
│   ├── smart-emergency-priority.cc   # Main multi-run C++ simulation (CBR jitter + PrioQueueDisc)
│   └── emergency-alert.cc            # Single-run baseline simulation
├── scripts/
│   ├── run-experiments.sh            # Automated end-to-end experiment runner
│   └── analyze-results.py            # Statistical plotting (95% CI error bars & Delay CDF)
├── data/
│   ├── results-baseline.csv          # Sample means for FIFO baseline
│   ├── results-priority.csv          # Sample means for PrioQueueDisc priority
│   ├── runs-baseline.csv             # 10 individual run records (baseline)
│   ├── runs-priority.csv             # 10 individual run records (priority)
│   ├── stats-summary-baseline.csv    # 95% CI statistics (baseline)
│   ├── stats-summary-priority.csv    # 95% CI statistics (priority)
│   ├── stats-comparison.csv          # Consolidated statistical comparison
│   ├── comparison-summary.csv        # Delta summary
│   ├── emergency-cdf-baseline.csv    # Empirical delay CDF points (baseline)
│   └── emergency-cdf-priority.csv    # Empirical delay CDF points (priority)
├── figures/
│   ├── delay_comparison.png          # Bar chart with 95% CI error bars
│   ├── pdr_comparison.png            # Bar chart with 95% CI error bars
│   ├── throughput_comparison.png     # Bar chart with 95% CI error bars
│   ├── packet_loss_comparison.png    # Bar chart with 95% CI error bars
│   ├── emergency_delay_cdf.png       # Empirical CDF vs theoretical bounds
│   └── dashboard.html                # Interactive 6-panel Plotly presentation dashboard
├── docs/
│   ├── PROJECT_REPORT.md             # Complete formal academic project report
│   ├── PRESENTATION_SLIDES.md        # Slide deck in Markdown format
│   ├── PRESENTATION_SLIDES.html      # Interactive presenter-ready HTML slides
│   ├── VIVA_PREPARATION.md           # 12-question defense guide + oral preparation
│   ├── DEMO_SCRIPT.md                # Live demonstration walkthrough script
│   └── audit.md                      # Comprehensive evidence-based audit report
└── audit-bundle.zip                  # Complete verified project archive
```

---

## 🚀 How to Reproduce

### Prerequisites
- ns-3.49 discrete-event simulator installed.
- Python 3 with `Pillow` and optional `plotly`.

### Execution
Place `simulation/smart-emergency-priority.cc` into your ns-3 `scratch/` directory, then run:

```bash
# 1. Build the simulation target
./ns3 build smart-emergency-priority

# 2. Run multi-run experiments & generate all charts
./scripts/run-experiments.sh
```

---

## 📜 License
Distributed under the GNU General Public License v2.0. See [`LICENSE`](LICENSE) for details.
