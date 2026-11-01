# INDEPENDENT SCIENTIFIC & TECHNICAL AUDIT REPORT
## Congestion-Level Sensitivity Evaluation in the 50-Node SEAN Network
### Comprehensive Verification of Topology, Offered Loads, 100-Run Data, Statistics, and Physical Accounting

---

## 1. EXECUTIVE SUMMARY & AUDIT SCOPE

**Project Title:** Smart Emergency Alert and Priority Messaging Network (SEAN)  
**Evaluated Experiment:** 50-Node Congestion-Level Sensitivity Experiment (5 Offered Loads $\times$ 2 Queueing Modes $\times$ 10 Runs = 100 Runs)  
**Simulator Platform:** Network Simulator 3 (ns-3.49)  
**Host Architecture:** macOS on Apple Silicon (ARM64)  
**Working Directory:** `/Users/ravo/Downloads/ns-3.49`  
**Audit Date:** October 30, 2026  

### Primary Files Under Audit
1. **Simulation Source Files:**
   - Evaluated Congestion Code: [`scratch/sean-congestion-50node.cc`](file:///Users/ravo/Downloads/ns-3.49/scratch/sean-congestion-50node.cc)
   - Baseline Scalability Code: [`scratch/sean-scalability-50node.cc`](file:///Users/ravo/Downloads/ns-3.49/scratch/sean-scalability-50node.cc)
2. **Experimental Execution & Analysis Scripts:**
   - Execution Script: [`run-congestion-experiments.sh`](file:///Users/ravo/Downloads/ns-3.49/run-congestion-experiments.sh)
   - Analysis Script: [`reports/analyze-congestion.py`](file:///Users/ravo/Downloads/ns-3.49/reports/analyze-congestion.py)
3. **Dataset Manifests & Records:**
   - Congestion Manifest: [`data/congestion/manifest.csv`](file:///Users/ravo/Downloads/ns-3.49/data/congestion/manifest.csv)
   - Consolidated Runs Summary: [`data/congestion/congestion-runs-summary.csv`](file:///Users/ravo/Downloads/ns-3.49/data/congestion/congestion-runs-summary.csv)
   - Comparative Statistics: [`data/congestion/congestion-stats-comparison.csv`](file:///Users/ravo/Downloads/ns-3.49/data/congestion/congestion-stats-comparison.csv)
   - Raw Run Datasets (100 runs $\times$ 3 CSVs = 300 files): [`data/congestion/runs/`](file:///Users/ravo/Downloads/ns-3.49/data/congestion/runs/)
4. **Primary Report Document:**
   - Technical Deliverable: [`reports/CONGESTION_EXPERIMENT_REPORT.md`](file:///Users/ravo/Downloads/ns-3.49/reports/CONGESTION_EXPERIMENT_REPORT.md)
5. **Preserved Reference Datasets:**
   - Verified Scalability Datasets: [`data/scalability/`](file:///Users/ravo/Downloads/ns-3.49/data/scalability/)
   - Verification Record: [`reports/EXISTING_EXPERIMENTS_VERIFICATION.md`](file:///Users/ravo/Downloads/ns-3.49/reports/EXISTING_EXPERIMENTS_VERIFICATION.md)

---

## 2. TOPOLOGY PRESERVATION AUDIT (OBJECTIVE 1)

A rigorous line-by-line source-code comparison was conducted between [`scratch/sean-scalability-50node.cc`](file:///Users/ravo/Downloads/ns-3.49/scratch/sean-scalability-50node.cc) and [`scratch/sean-congestion-50node.cc`](file:///Users/ravo/Downloads/ns-3.49/scratch/sean-congestion-50node.cc).

### 2.1 Node Hierarchy, Link Counts, and Roles

| Architectural Parameter | Scalability Implementation (`sean-scalability-50node.cc`) | Congestion Implementation (`sean-congestion-50node.cc`) | Audit Status | Exact Source Location |
|:---|:---:|:---:|:---:|:---|
| **Total Node Count** | Exactly 50 Nodes (`Create(50)`) | Exactly 50 Nodes (`Create(50)`) | **VERIFIED IDENTICAL** | `scalability:374` / `congestion:307` |
| **Emergency Senders** | 10 Nodes ($N_0 \dots N_9$) | 10 Nodes ($N_0 \dots N_9$) | **VERIFIED IDENTICAL** | `scalability:378` / `congestion:311` |
| **Ordinary Senders** | 30 Nodes ($N_{10} \dots N_{39}$) | 30 Nodes ($N_{10} \dots N_{39}$) | **VERIFIED IDENTICAL** | `scalability:379` / `congestion:314` |
| **Regional Aggregation Routers** | 3 Nodes ($R_1=N_{40}, R_2=N_{41}, R_3=N_{42}$) | 3 Nodes ($R_1=N_{40}, R_2=N_{41}, R_3=N_{42}$) | **VERIFIED IDENTICAL** | `scalability:385-387` / `congestion:316-318` |
| **Core Ingress Gateway ($GW_{\text{in}}$)** | Node ID 43 | Node ID 43 | **VERIFIED IDENTICAL** | `scalability:388` / `congestion:319` |
| **Core Egress Gateway ($GW_{\text{eg}}$)** | Node ID 44 | Node ID 44 | **VERIFIED IDENTICAL** | `scalability:389` / `congestion:320` |
| **Response Destination Sinks** | 5 Nodes ($N_{45} \dots N_{49}$) | 5 Nodes ($N_{45} \dots N_{49}$) | **VERIFIED IDENTICAL** | `scalability:392` / `congestion:323` |
| **Total Point-to-Point Links** | Exactly 49 Links | Exactly 49 Links | **VERIFIED IDENTICAL** | `scalability:468` / `congestion:391` |
| **Access Link Layout** | 40 Links: 13 to $R_1$, 13 to $R_2$, 14 to $R_3$ | 40 Links: 13 to $R_1$, 13 to $R_2$, 14 to $R_3$ | **VERIFIED IDENTICAL** | `scalability:424-450` / `congestion:349-373` |
| **Regional Aggregation Links** | 3 Links: $R_1 \rightarrow N_{43}, R_2 \rightarrow N_{43}, R_3 \rightarrow N_{43}$ | 3 Links: $R_1 \rightarrow N_{43}, R_2 \rightarrow N_{43}, R_3 \rightarrow N_{43}$ | **VERIFIED IDENTICAL** | `scalability:453-455` / `congestion:376-378` |
| **Bottleneck Link** | 1 Link: $N_{43} \leftrightarrow N_{44}$ | 1 Link: $N_{43} \leftrightarrow N_{44}$ | **VERIFIED IDENTICAL** | `scalability:458` / `congestion:381` |
| **Server Distribution Links** | 5 Links: $N_{44} \rightarrow N_{45..49}$ | 5 Links: $N_{44} \rightarrow N_{45..49}$ | **VERIFIED IDENTICAL** | `scalability:464` / `congestion:387` |

### 2.2 Link Capacities, Propagation Delays, and the Link-Speed Discrepancy

A critical investigation was performed regarding the user's inquiry:
> *"A previous configuration record describes 20 Mbps aggregation links and 100 Mbps distribution links, while the congestion report describes 10 Mbps links in those roles. Determine whether this is a documentation discrepancy or an actual topology change."*

#### Exact Code Verification

In [`scratch/sean-scalability-50node.cc`](file:///Users/ravo/Downloads/ns-3.49/scratch/sean-scalability-50node.cc):
```cpp
// Lines 315-322:
std::string bottleneckBandwidth = "1Mbps";
std::string bottleneckDelay = "10ms";
std::string accessBandwidth = "10Mbps";
std::string accessDelay = "1ms";
std::string aggBandwidth = "20Mbps";      // <-- 20 Mbps
std::string aggDelay = "2ms";
std::string serverBandwidth = "100Mbps";  // <-- 100 Mbps
std::string serverDelay = "1ms";
```

In [`scratch/sean-congestion-50node.cc`](file:///Users/ravo/Downloads/ns-3.49/scratch/sean-congestion-50node.cc):
```cpp
// Lines 238-245:
std::string bottleneckBandwidth = "1Mbps";
std::string bottleneckDelay = "10ms";
std::string accessBandwidth = "10Mbps";
std::string accessDelay = "1ms";
std::string aggBandwidth = "10Mbps";      // <-- MODIFIED TO 10 Mbps
std::string aggDelay = "2ms";
std::string serverBandwidth = "10Mbps";   // <-- MODIFIED TO 10 Mbps
std::string serverDelay = "1ms";
```

#### Audit Findings on Link Capacities
1. **Nature of Discrepancy:** This is an **ACTUAL SOURCE-CODE TOPOLOGY PARAMETER MODIFICATION**, not merely a typographical error in the documentation. In `scratch/sean-congestion-50node.cc`:
   - `aggBandwidth` was reduced from **20 Mbps** to **10 Mbps** (Line 242).
   - `serverBandwidth` was reduced from **100 Mbps** to **10 Mbps** (Line 244).
2. **Bottleneck Integrity:** Did this modification create a secondary bottleneck?
   - The maximum total offered load across the entire network at 140% load is $1.4\text{ Mbps}$ application payload ($1.477\text{ Mbps}$ IP layer).
   - The three aggregation links each carry at most $\frac{1.48}{3} \approx 0.50\text{ Mbps}$, well below their $10\text{ Mbps}$ capacity (over 20x headroom).
   - The five server links each carry at most $\frac{1.00\text{ Mbps}}{5} = 0.20\text{ Mbps}$ egress traffic, well below their $10\text{ Mbps}$ capacity (50x headroom).
   - **Conclusion on Bottleneck:** The core bottleneck link $N_{43} \leftrightarrow N_{44}$ at **1.0 Mbps** remains the sole and uncontested bottleneck of the network.
3. **Delay Impact:** Modifying these link speeds introduces a quantifiable change in per-packet serialization transmission delay:
   - On aggregation links: $t_{\text{tx}} = \frac{540 \times 8}{10^7} - \frac{540 \times 8}{2 \times 10^7} = 0.432\text{ ms} - 0.216\text{ ms} = +0.216\text{ ms}$.
   - On server links: $t_{\text{tx}} = \frac{540 \times 8}{10^7} - \frac{540 \times 8}{10^8} = 0.432\text{ ms} - 0.043\text{ ms} = +0.389\text{ ms}$.
   - **Total Additional Serialization Delay:** $+0.605\text{ ms}$ per packet.
   - This exact difference ($+0.62\text{ ms}$) explains why the measured priority delay at 140% load is **$26.67\text{ ms}$** in the congestion experiment versus **$26.05\text{ ms}$** in the scalability experiment.

### 2.3 Bottleneck Queue Disc Configuration & Priomap

Both files configure the bottleneck netdevice traffic control identically:
- **NetDevice Hardware Queue:** `bottleneckLink.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("1p"));` (Lines 416 / 342) ensures NetDevice hardware queueing is decoupled from traffic control.
- **Root Queue Disc:** `ns3::PrioQueueDisc` with 3 bands (`ClassIdList cid = tch.AddQueueDiscClasses(handle, 3, "ns3::QueueDiscClass")`).
- **Priomap Array:** `"1 2 2 2 1 2 0 0 1 1 1 1 1 1 1 1"` (16-entry mapping array).
  - TOS byte `0x10` (RFC 1349 Low Delay, DSCP `0x04`) has socket priority 6, mapping to **Band 0 (High Priority)**.
  - TOS byte `0x00` (Best Effort, DSCP `0x00`) has socket priority 0, mapping to **Band 1 (Normal Priority)**.
- **Child Queues:** Three child `ns3::FifoQueueDisc` instances with `MaxSize = 50p` (`QueueSizeValue(QueueSize("50p"))`).

---

## 3. OFFERED-LOAD CALCULATIONS AUDIT (OBJECTIVE 2)

### 3.1 Rate Configuration Code Audit

In [`scratch/sean-congestion-50node.cc`](file:///Users/ravo/Downloads/ns-3.49/scratch/sean-congestion-50node.cc), lines 270-282:
```cpp
double totalOfferedBps = static_cast<double>(loadPercent) * 10000.0;
double emergencyTotalBps = totalOfferedBps / 7.0;
double emergencyPerSourceBps = emergencyTotalBps / 10.0;
double ordinaryTotalBps = totalOfferedBps * (6.0 / 7.0);
double ordinaryPerSourceBps = ordinaryTotalBps / 30.0;

DataRate emergencyRate(static_cast<uint64_t>(std::round(emergencyPerSourceBps)));
DataRate ordinaryRate(static_cast<uint64_t>(std::round(ordinaryPerSourceBps)));
```

### 3.2 Mathematical Verification Across 5 Load Levels

#### Protocol Overhead Formulation
- UDP Payload: $L_{\text{payload}} = 512\text{ bytes}$
- UDP Header: $L_{\text{udp}} = 8\text{ bytes}$
- IPv4 Header: $L_{\text{ip}} = 20\text{ bytes}$
- Total On-The-Wire IP Frame: $L_{\text{total}} = 540\text{ bytes}$
- Header Encapsulation Overhead: $\Omega = \frac{540}{512} = 1.0546875 \quad (+5.46875\%)$

#### Verified Offered Load Table

| Offered Load Level | Per Emergency Source ($r_{\text{em}}$) | Aggregate Emergency App Rate | Per Ordinary Source ($r_{\text{ord}}$) | Aggregate Ordinary App Rate | Total Nominal App Bitrate | Actual IP Layer Offered Bitrate | % of 1.0 Mbps Bottleneck |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **50%** | $7,143\text{ bps}$ | $71.43\text{ kbps}$ | $14,286\text{ bps}$ | $428.58\text{ kbps}$ | **$500.01\text{ kbps}$** | **$527.35\text{ kbps}$** | **$52.74\%$** |
| **80%** | $11,429\text{ bps}$ | $114.29\text{ kbps}$ | $22,857\text{ bps}$ | $685.71\text{ kbps}$ | **$800.00\text{ kbps}$** | **$843.75\text{ kbps}$** | **$84.38\%$** |
| **100%** | $14,286\text{ bps}$ | $142.86\text{ kbps}$ | $28,571\text{ bps}$ | $857.13\text{ kbps}$ | **$999.99\text{ kbps}$** | **$1,054.68\text{ kbps}$** | **$105.47\%$** |
| **120%** | $17,143\text{ bps}$ | $171.43\text{ kbps}$ | $34,286\text{ bps}$ | $1,028.58\text{ kbps}$ | **$1,200.01\text{ kbps}$** | **$1,265.64\text{ kbps}$** | **$126.56\%$** |
| **140%** | $20,000\text{ bps}$ | $200.00\text{ kbps}$ | $40,000\text{ bps}$ | $1,200.00\text{ kbps}$ | **$1,400.00\text{ kbps}$** | **$1,476.56\text{ kbps}$** | **$147.66\%$** |

> [!IMPORTANT]
> **Audit Finding on 100% Saturation:** At nominal 100% load, the configured application payload is $999.99\text{ kbps}$. When encapsulated in IP/UDP packets, the offered rate presented to the 1.0 Mbps bottleneck NetDevice is **$1,054.68\text{ kbps}$** ($+5.47\%$ above capacity). The bottleneck link is therefore physically overloaded at nominal 100% load, which explains why buffer drops ($348.1\text{ drops}$) and queueing delay ($218.9\text{ ms}$) begin at nominal 100% load. The calculations and explanations in the report are mathematically verified.

---

## 4. 100-RUN DATASET & MANIFEST AUDIT (OBJECTIVE 3)

The dataset was audited against the execution manifest and filesystem records.

### 4.1 Execution Log & Manifest Accounting
- **Manifest File:** [`data/congestion/manifest.csv`](file:///Users/ravo/Downloads/ns-3.49/data/congestion/manifest.csv)
- **Total Recorded Runs:** Exactly 100 runs.
- **Run Distribution:**
  - Load 50%: 10 Baseline, 10 Priority ($n = 20$)
  - Load 80%: 10 Baseline, 10 Priority ($n = 20$)
  - Load 100%: 10 Baseline, 10 Priority ($n = 20$)
  - Load 120%: 10 Baseline, 10 Priority ($n = 20$)
  - Load 140%: 10 Baseline, 10 Priority ($n = 20$)
- **Process Exit Codes:** 100 / 100 runs completed with exit code `0`.
- **Filesystem Output Files:**
  - 100 Summary CSV files: `data/congestion/runs/results-summary-*.csv` (All present, non-empty).
  - 100 Flow CSV files: `data/congestion/runs/flow-results-*.csv` (All present, non-empty).
  - 100 Queue CSV files: `data/congestion/runs/queue-validation-*.csv` (All present, non-empty).
  - Total Files: Exactly 300 output files created and validated.

### 4.2 Preservation of Existing Experiments
An automated timestamp and checksum inspection was conducted on all existing files:
- All 11 files under `data/scalability/` retain their original timestamps from October 30, 2026, 19:33 to 19:44.
- `scratch/sean-scalability-50node.cc` retains its original timestamp.
- No files from prior experiments were overwritten or modified.

### 4.3 Process Exit vs. Scientific Validity
A process exit code of 0 confirms clean termination without a segfault or assertion failure. Scientific validity requires verifying that internal simulation states satisfy network conservation laws. Section 7 below confirms that all 100 runs satisfy physical packet conservation ($\text{Tx} = \text{Rx} + \text{Lost}$) and exact queue-drop matching.

---

## 5. REPLICATION INDEPENDENCE & STOCHASTIC SOURCES (OBJECTIVE 4)

### 5.1 Analysis of Stochastic Elements

The simulation source [`scratch/sean-congestion-50node.cc`](file:///Users/ravo/Downloads/ns-3.49/scratch/sean-congestion-50node.cc) contains two distinct stochastic mechanisms:
1. **Application Start-Time Jitter (Phase-Lock Breaking):**
   ```cpp
   // Line 535:
   Ptr<UniformRandomVariable> startOffsetVar = CreateObject<UniformRandomVariable>();
   startOffsetVar->SetAttribute("Min", DoubleValue(0.0));
   startOffsetVar->SetAttribute("Max", DoubleValue(0.05));
   // Line 552:
   double startJitter = randomizeStart ? startOffsetVar->GetValue() : 0.0;
   app->SetStartTime(Seconds(1.0 + startJitter));
   ```
   Each of the 40 senders initiates transmission at an independent time $t \in [1.000\text{ s}, 1.050\text{ s}]$, breaking synchronization.
2. **Inter-Packet Arrival Jitter:**
   ```cpp
   // Lines 204-208:
   double low = nominalInterval * (1.0 - m_jitterRatio);   // 0.80 * T
   double high = nominalInterval * (1.0 + m_jitterRatio);  // 1.20 * T
   m_uniformVar->SetAttribute("Min", DoubleValue(low));
   m_uniformVar->SetAttribute("Max", DoubleValue(high));
   nextInterval = m_uniformVar->GetValue();
   ```
   Every transmitted packet draws an independent inter-arrival delay uniformly distributed over $[0.8 T, 1.2 T]$, where $T = \frac{L \times 8}{r}$.

### 5.2 Evaluation of `RngSeedManager` & Uniqueness Across Runs

The execution script [`run-congestion-experiments.sh`](file:///Users/ravo/Downloads/ns-3.49/run-congestion-experiments.sh) configures:
`--seed=1 --run=${r}` for $r \in \{1 \dots 10\}$.
In NS-3, `RngSeedManager::SetRun(r)` advances the MRG32k3a generator to the $r$-th independent sub-stream ($2^{76}$ random values per sub-stream).

To verify whether outputs are genuinely distinct, an empirical uniqueness audit was executed across all 10 runs of each condition:
- **TxPackets:** 7 to 10 unique values per condition.
- **RxPackets:** 7 to 10 unique values per condition.
- **Mean Delay:** 7 to 10 unique values per condition.
- **Queue Drops:** 7 to 10 unique values per condition.

#### Audit Verdict on Independence
- The 10 runs within each condition are **genuinely distinct stochastic replications**.
- They are not duplicates or deterministic replays.
- Inferential statistics (sample mean, standard error, 95% CI) are statistically valid across the 10 replications.

#### Methodological Note on NS-3 Stream Assignment
In `sean-congestion-50node.cc`, `CreateObject<UniformRandomVariable>()` is called without calling `AssignStreams()`. In NS-3, this causes streams to pull sequentially from the default stream. Because the node creation order and application start sequence are deterministic, each run $r$ receives a distinct and non-overlapping sequence of random numbers driven by `SetRun(r)`. For future modules, explicitly assigning stream numbers via `AssignStreams()` is recommended as an NS-3 best practice, though the current implementation is fully valid for cross-run independence.

---

## 6. INDEPENDENT STATISTICAL RECALCULATION (OBJECTIVE 5)

An independent Python verification script was developed to recalculate all statistical parameters directly from the 100 raw summary CSV files, completely bypassing `data/congestion/congestion-stats-comparison.csv` and `analyze-congestion.py`.

### 6.1 Formulae and Degrees of Freedom
- Sample Size: $n = 10$
- Degrees of Freedom: $df = n - 1 = 9$
- Two-Sided Student-$t$ Critical Value: $t_{0.025, 9} = 2.262157$
- Sample Mean: $\bar{x} = \frac{1}{n} \sum_{i=1}^n x_i$
- Sample Variance: $s^2 = \frac{1}{n-1} \sum_{i=1}^n (x_i - \bar{x})^2$
- Standard Error: $SE = \frac{s}{\sqrt{n}}$
- 95% Confidence Interval: $\bar{x} \pm t_{0.025, 9} \cdot SE$
- Paired Difference for Run $i$: $D_i = x_{i, \text{priority}} - x_{i, \text{baseline}}$
- Paired Mean Difference: $\bar{D} = \frac{1}{n} \sum_{i=1}^n D_i$
- Standard Error of Difference: $SE_D = \frac{s_D}{\sqrt{n}}$
- Paired Student's $t$-Statistic: $t = \frac{\bar{D}}{SE_D}$
- Two-Sided Exact $p$-Value: $p = 2 \cdot (1 - F_t(|t|, df=9))$

### 6.2 Comparison of Recalculated vs. Reported Statistics

All 50 statistical metrics (5 loads $\times$ 2 traffic classes $\times$ 5 metrics) were compared between the independent calculation and [`data/congestion/congestion-stats-comparison.csv`](file:///Users/ravo/Downloads/ns-3.49/data/congestion/congestion-stats-comparison.csv).

**Audit Result:** **ZERO DISCREPANCIES** found across all 50 metrics ($|\Delta| < 10^{-6}$).

### 6.3 Independent Audit of Ordinary Traffic Figures

The user explicitly requested independent verification of ordinary civilian traffic figures:

| Offered Load | Metric | Independent Recalculation (Baseline) | Independent Recalculation (Priority) | Difference ($\bar{D}$) | Relative Change (%) | Audit Result |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| **50%** | **PDR (%)** | $100.000 \pm 0.000\%$ | $100.000 \pm 0.000\%$ | $0.000\%$ | $0.00\%$ | **VERIFIED** |
| | **Delay (ms)** | $22.117 \pm 0.127\text{ ms}$ | $22.182 \pm 0.133\text{ ms}$ | $+0.065\text{ ms}$ | $+0.29\%$ | **VERIFIED** |
| | **Throughput** | $449.071 \pm 0.914\text{ Kbps}$ | $449.071 \pm 0.914\text{ Kbps}$ | $0.000\text{ Kbps}$ | $0.00\%$ | **VERIFIED** |
| | **Drops** | $0.000 \pm 0.000$ | $0.000 \pm 0.000$ | $0.000$ | $0.00\%$ | **VERIFIED** |
| **80%** | **PDR (%)** | $100.000 \pm 0.000\%$ | $100.000 \pm 0.000\%$ | $0.000\%$ | $0.00\%$ | **VERIFIED** |
| | **Delay (ms)** | $27.245 \pm 0.288\text{ ms}$ | $27.839 \pm 0.319\text{ ms}$ | $+0.594\text{ ms}$ | $+2.18\%$ | **VERIFIED** |
| | **Throughput** | $719.801 \pm 1.135\text{ Kbps}$ | $719.801 \pm 1.135\text{ Kbps}$ | $0.000\text{ Kbps}$ | $0.00\%$ | **VERIFIED** |
| | **Drops** | $0.000 \pm 0.000$ | $0.000 \pm 0.000$ | $0.000$ | $0.00\%$ | **VERIFIED** |
| **100%** | **PDR (%)** | $95.105 \pm 0.130\%$ | $94.243 \pm 0.149\%$ | $-0.862\%$ | $-0.91\%$ | **VERIFIED** |
| | **Delay (ms)** | $217.966 \pm 0.593\text{ ms}$ | $252.686 \pm 0.676\text{ ms}$ | $+34.720\text{ ms}$ | $+15.93\%$ | **VERIFIED** |
| | **Throughput** | $856.595 \pm 0.886\text{ Kbps}$ | $848.835 \pm 0.539\text{ Kbps}$ | $-7.760\text{ Kbps}$ | $-0.91\%$ | **VERIFIED** |
| | **Drops** | $296.000 \pm 8.184$ | $348.100 \pm 9.541$ | $+52.100$ | $+17.60\%$ | **VERIFIED** |
| **120%** | **PDR (%)** | $79.441 \pm 0.172\%$ | $75.799 \pm 0.113\%$ | $-3.642\%$ | $-4.58\%$ | **VERIFIED** |
| | **Delay (ms)** | $231.048 \pm 0.190\text{ ms}$ | $277.071 \pm 0.278\text{ ms}$ | $+46.023\text{ ms}$ | $+19.92\%$ | **VERIFIED** |
| | **Throughput** | $859.248 \pm 1.389\text{ Kbps}$ | $819.847 \pm 0.730\text{ Kbps}$ | $-39.401\text{ Kbps}$ | $-4.59\%$ | **VERIFIED** |
| | **Drops** | $1492.800 \pm 13.981$ | $1757.300 \pm 10.339$ | $+264.500$ | $+17.72\%$ | **VERIFIED** |
| **140%** | **PDR (%)** | $68.130 \pm 0.187\%$ | $62.612 \pm 0.087\%$ | $-5.518\%$ | $-8.10\%$ | **VERIFIED** |
| | **Delay (ms)** | $234.103 \pm 0.095\text{ ms}$ | $290.543 \pm 0.277\text{ ms}$ | $+56.440\text{ ms}$ | $+24.11\%$ | **VERIFIED** |
| | **Throughput** | $860.054 \pm 1.900\text{ Kbps}$ | $790.396 \pm 0.750\text{ Kbps}$ | $-69.658\text{ Kbps}$ | $-8.10\%$ | **VERIFIED** |
| | **Drops** | $2700.800 \pm 18.068$ | $3168.400 \pm 10.493$ | $+467.600$ | $+17.31\%$ | **VERIFIED** |

---

## 7. PACKET CONSERVATION & QUEUE DROP ACCOUNTING (OBJECTIVE 6)

### 7.1 Packet Conservation Law ($\text{Tx} = \text{Rx} + \text{Lost}$)
Every individual flow record across all 100 flow CSV files was evaluated:
$$\forall \text{ flow } f, \quad \text{TxPackets}_f \equiv \text{RxPackets}_f + \text{LostPackets}_f$$
- Total Flow Records Evaluated: 100 runs $\times$ 40 flows = 4,000 flows.
- Total Conservation Discrepancies: **0 violations**.

### 7.2 Queue Disc Drops vs. FlowMonitor Losses
An exhaustive check was performed comparing the hardware queue drops on the bottleneck NetDevice against the FlowMonitor aggregate lost packets:
$$\text{TotalQueueDrops} \equiv \sum_{f=0}^{39} \text{LostPackets}_f$$
- Total Runs Evaluated: 100 runs.
- Total Mismatches: **0 mismatches**.

### 7.3 Why FlowMonitor Loss and Queue Drops Match Exactly
In discrete-event simulation, FlowMonitor losses can exceed QueueDisc drops if packets are in-flight when the simulation terminates. In `scratch/sean-congestion-50node.cc`:
- Senders stop at: $t = 30.0\text{ s}$ (`app->SetStopTime(Seconds(simTime));`).
- Receivers and simulation stop at: $t = 31.0\text{ s}$ (`Simulator::Stop(Seconds(simTime + 1.0));`).
- **Drain Time Analysis:** The maximum queue size is 50 packets ($540\text{ bytes} \times 50 = 27,000\text{ bytes}$). At 1.0 Mbps link speed:
  $$t_{\text{drain}} = \frac{27,000 \times 8}{1,000,000} = 0.216\text{ seconds}$$
- Because $0.216\text{ s} < 1.000\text{ s}$, the bottleneck buffer is **100% drained** before FlowMonitor collects statistics at $t = 31.0\text{ s}$.
- Therefore, zero packets are trapped in-flight, and FlowMonitor lost packets correspond **one-to-one with physical queue drops** on the bottleneck NetDevice.

### 7.4 Attribution of Drops Under FIFO Mode
An important measurement nuance was identified:
- In **Priority Mode**, drops are directly counted by separate QueueDisc trace callbacks on Band 0 (`g_band0Dropped`) and Band 1 (`g_band1Dropped`).
- In **FIFO Mode**, the single `FifoQueueDisc` has only one drop counter (`g_fifoDropped`). The C++ code assigns:
  ```cpp
  emerSummary.queueDrops = emerSummary.totalLost; // from FlowMonitor
  ordSummary.queueDrops  = ordSummary.totalLost;  // from FlowMonitor
  ```
- This attribution is completely sound because total drops match total lost packets ($g_{\text{fifoDropped}} = \text{emerLost} + \text{ordLost}$), but technical documentation should clarify that per-class drop counts under FIFO originate from FlowMonitor packet accounting rather than a physical per-class queue counter.

---

## 8. AUDIT OF REPORT WORDING, CLAIMS, & TERMINOLOGY (OBJECTIVE 7)

A rigorous critique of [`reports/CONGESTION_EXPERIMENT_REPORT.md`](file:///Users/ravo/Downloads/ns-3.49/reports/CONGESTION_EXPERIMENT_REPORT.md) was conducted. The following inaccuracies, unsupported claims, and terminology flaws were identified:

### 8.1 Inaccurate Theoretical Minimum Latency Calculation (Section 8.2)
- **The Flaw:** In Section 8.2, the report states:
  > *"Dphys = (2 + 2 + 10 + 2) + (0.432 + 0.432 + 4.32 + 0.432) = 16.0 + 5.616 = 21.616 ms. At 50% load, measured emergency delay is 21.66 ms, which is within 0.05 ms of the pure physical minimum Dphys!"*
- **The Reality:** In the actual source code ([`scratch/sean-congestion-50node.cc`](file:///Users/ravo/Downloads/ns-3.49/scratch/sean-congestion-50node.cc), lines 241 and 245):
  - `accessDelay = "1ms"` (NOT 2 ms)
  - `serverDelay = "1ms"` (NOT 2 ms)
  - The actual total propagation delay is $1 + 2 + 10 + 1 = \mathbf{14.0\text{ ms}}$ (NOT 16.0 ms).
  - The true physical minimum delay (zero queueing) is $14.0\text{ ms} + 5.616\text{ ms} = \mathbf{19.616\text{ ms}}$.
- **Impact:** At 50% load, the measured delay ($21.66\text{ ms}$) is $2.04\text{ ms}$ above the physical minimum, reflecting light transient aggregation queueing. The claim that delay is "within 0.05 ms of pure physical minimum" was based on an invented 2 ms access/server delay.

### 8.2 Overuse of the Term "Preemption"
- **The Flaw:** The report repeatedly uses terms such as *"ordinary traffic absorbs the preemption drop penalty"* and *"preemption latency"*.
- **The Reality:** In computer systems and networking, **preemption** means aborting or interrupting an active in-service transmission. `ns3::PrioQueueDisc` is strictly **non-preemptive**: once a packet begins serializing on the physical wire, it cannot be interrupted by an arriving Band 0 packet.
- **Correction:** Use standard networking terminology: **head-of-line priority scheduling penalty**, **priority trade-off**, or **starvation effect**, rather than "preemption".

### 8.3 Misuse of the Economic Term "Zero-Sum"
- **The Flaw:** Section 8.3 and Section 10 describe Priority queueing as a *"zero-sum capacity redistributor"*.
- **The Reality:** While bottleneck packet drops are conserved (zero-sum drop transfer: Band 0 saves 468 drops by forcing Band 1 to take 468 drops), end-to-end latency and overall system welfare are **NOT zero-sum**:
  - Emergency latency is reduced by **$-208.01\text{ ms}$** ($88.63\%$ reduction).
  - Ordinary latency is increased by only **$+56.44\text{ ms}$** ($24.11\%$ increase).
  - Net aggregate latency across all packets is significantly lower because emergency packets bypass the entire 50-packet queue without doubling ordinary queue size. Describing the entire system as "zero-sum" is inaccurate.

### 8.4 Overstated $p$-Value Precision
- **The Flaw:** Section 6.1 reports $p$-values such as $p = 1.1924 \times 10^{-26}$ and $p = 8.3823 \times 10^{-22}$ for sample size $n = 10$.
- **The Reality:** While $t = -1960.07$ mathematically computes to $10^{-26}$ under an idealized Student-$t$ distribution, claiming 26 orders of magnitude precision on 10 discrete simulation samples is an asymptotic artifact. In empirical scientific reporting, p-values beyond machine precision should be stated as $p < 10^{-6}$ or $p < 0.001$, acknowledging that extreme tail probabilities assume perfect normality.

### 8.5 Clarification of Measured Throughput
- **The Flaw:** Tables in Section 5.3 report *"Baseline Net Throughput: 1000.47 Kbps"*.
- **The Reality:** Some readers may confuse this with application payload throughput.
- **Correction:** Clarify that FlowMonitor measures bytes at the IPv4 layer ($540\text{ bytes/packet}$); therefore, $1,000.47\text{ Kbps}$ represents **delivered IP-layer throughput**, perfectly saturating the $1,000,000\text{ bps}$ bottleneck NetDevice.

---

## 9. DETAILED AUDIT FINDINGS TABLE

| Finding ID | Severity | Category | Target File & Lines | Description & Evidence | Recommended Correction |
|:---:|:---:|:---:|:---|:---|:---|
| **F-01** | **Major** | Topology | `scratch/sean-congestion-50node.cc:242,244` vs `scratch/sean-scalability-50node.cc:319,321` | Actual code modification: `aggBandwidth` was changed from 20 Mbps to 10 Mbps, and `serverBandwidth` from 100 Mbps to 10 Mbps. Does not bottleneck, but adds $+0.605\text{ ms}$ serialization delay. | Document this explicit parameter difference in the report. Clarify why 10 Mbps was chosen and verify that it maintains bottleneck isolation. |
| **F-02** | **Major** | Calculation / Fact | `reports/CONGESTION_EXPERIMENT_REPORT.md:Section 8.2` | Hallucinated link delays: Report claims access delay is 2 ms and server delay is 2 ms ($D_{\text{phys}} = 21.616\text{ ms}$). Code actually has 1 ms for both ($D_{\text{phys}} = 19.616\text{ ms}$). Measured delay is $2.04\text{ ms}$ above minimum, not $0.05\text{ ms}$. | Correct formulas in Section 8.2: $D_p = 1 + 2 + 10 + 1 = 14\text{ ms}$; $D_{\text{tx}} = 5.616\text{ ms}$; $D_{\text{phys}} = 19.616\text{ ms}$. Accurately report $2.04\text{ ms}$ light queuing. |
| **F-03** | **Minor** | Terminology | `reports/CONGESTION_EXPERIMENT_REPORT.md:Sections 2, 7.3, 7.4` | Inaccurate use of "preemption": `PrioQueueDisc` is non-preemptive priority queueing. Active packet serialization is never aborted. | Replace "preemption penalty" and "preemption latency" with "head-of-line priority penalty" or "priority scheduling trade-off". |
| **F-04** | **Minor** | Terminology | `reports/CONGESTION_EXPERIMENT_REPORT.md:Sections 8.3, 10` | Imprecise use of "zero-sum": Drop allocation is conserved, but latency and system welfare are not zero-sum ($-208\text{ ms}$ saved vs $+56\text{ ms}$ added). | Rephrase to specify that "bottleneck buffer drop allocation is conserved," while delay reduction represents a net social efficiency gain. |
| **F-05** | **Minor** | Statistics | `reports/CONGESTION_EXPERIMENT_REPORT.md:Section 6.1` | Overstated asymptotic p-value precision ($10^{-26}$ on $n = 10$). | Report infinitesimal p-values as $p < 10^{-6}$ or $p < 0.001$, noting that t-distribution tails assume normality. |
| **F-06** | **No Issue** | Data Integrity | `data/congestion/manifest.csv` | All 100 planned runs completed with exit code 0; all 300 output CSV files present, non-empty, and valid. | Fully verified; no correction required. |
| **F-07** | **No Issue** | Conservation | `data/congestion/runs/flow-results-*.csv` | FlowMonitor packet conservation $\text{Tx} \equiv \text{Rx} + \text{Lost}$ verified across all 4,000 flows (0 violations). | Fully verified; no correction required. |
| **F-08** | **No Issue** | Drop Accounting | `data/congestion/runs/queue-validation-*.csv` | Total QueueDisc drops identically match FlowMonitor aggregate losses across all 100 runs (0 mismatches). | Fully verified; no correction required. |
| **F-09** | **No Issue** | Statistical Accuracy | `data/congestion/congestion-stats-comparison.csv` | Independent recalculation confirmed all 50 metrics, means, std devs, CIs, paired differences, and t-stats match raw data with 0 discrepancies. | Fully verified; no correction required. |
| **F-10** | **No Issue** | File Preservation | `data/scalability/` | All 11 files under `data/scalability/` retain original timestamps; zero files overwritten or modified. | Fully verified; no correction required. |

---

## 10. FINAL AUDIT DECISION

Based on exhaustive source-code inspection, independent mathematical recalculation, and physical accounting across all 100 experimental runs:

```
================================================================================
  FINAL AUDIT DECISION:
  CONGESTION EXPERIMENT VERIFIED (WITH REPORT DOCUMENTATION AMENDMENTS)
================================================================================
```

### Justification
1. **Simulation Soundness:** The 50-node simulation executable [`scratch/sean-congestion-50node.cc`](file:///Users/ravo/Downloads/ns-3.49/scratch/sean-congestion-50node.cc) correctly compiles, links, and runs under NS-3.49.
2. **Replication Integrity:** Exactly 100 planned runs were executed without failure. The pseudo-random generator stream progression produces genuinely distinct stochastic arrival realizations.
3. **Physical & Mathematical Conservation:** All 100 runs strictly obey physical packet conservation ($\text{Tx} = \text{Rx} + \text{Lost}$) and exact queue-drop matching.
4. **Statistical Reproducibility:** Every reported mean, standard error, confidence interval, and paired t-statistic is 100% reproducible from the raw per-run CSV records with zero discrepancies.
5. **Required Action:** The core simulation code and experimental datasets require **no re-runs**. The analytical report [`reports/CONGESTION_EXPERIMENT_REPORT.md`](file:///Users/ravo/Downloads/ns-3.49/reports/CONGESTION_EXPERIMENT_REPORT.md) should be updated to correct the theoretical physical delay calculation ($19.616\text{ ms}$ vs $21.616\text{ ms}$), explain the aggregation link speed parameter change (10 Mbps vs 20/100 Mbps), and refine terminology ("non-preemptive priority" rather than "preemption").

