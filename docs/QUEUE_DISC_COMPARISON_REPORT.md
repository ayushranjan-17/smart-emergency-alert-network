# Smart Emergency Alert and Priority Messaging Network (SEAN)
## Controlled Evaluation of Queue Disciplines: FIFO, Priority, RED, CoDel, and Prio-CoDel Hybrid

**Author / Candidate:** Academic Research Project  
**Course / Laboratory:** B.Tech Network Programming (NWP) Laboratory / Capstone  
**Simulation Platform:** Network Simulator 3 (ns-3.49, Native C++ / Traffic Control Subsystem)  
**Experiment Identifier:** Prompt 4 — Queue Discipline Comparative Evaluation  
**Date of Execution:** October 2026  
**Execution Status:** FULLY EXECUTED, CONSOLIDATED, AND VERIFIED (250 / 250 Simulations Succeeded)

---

## 1. Executive Summary

This report presents a rigorous, controlled comparative evaluation of five distinct queue disciplines implemented at the core bottleneck gateway of the 50-node Smart Emergency Alert and Priority Messaging Network (SEAN) architecture. The objective of this study is to examine the interaction between **traffic classification** (strict priority scheduling via `ns3::PrioQueueDisc`) and **Active Queue Management (AQM)** algorithms (`ns3::RedQueueDisc` and `ns3::CoDelQueueDisc`), testing whether AQM alone can protect life-critical emergency messages during severe network congestion or whether explicit multi-band priority scheduling remains mandatory.

A total of **250 independent simulation runs** (5 queue disciplines $\times$ 5 offered loads [50%, 80%, 100%, 120%, 140%] $\times$ 10 independent pseudo-random replications) were executed using `scratch/sean-queue-disc-comparison.cc` and automated by `run-queue-disc-experiments.sh`.

```
=======================================================================================================
                      HEADLINE PERFORMANCE COMPARISON AT 140% SEVERE OVERLOAD
=======================================================================================================
Queue Discipline    Emer PDR (%)    Emer Delay (ms)   Emer Drops   Ord PDR (%)   Ord Delay (ms)   Ord Drops
-------------------------------------------------------------------------------------------------------
FIFO (Drop-Tail)       67.44%          236.89 ms        459.1         68.02%        236.13 ms      2710.4
RED (AQM)              67.31%          219.86 ms        460.8         68.02%        219.29 ms      2710.6
CoDel (AQM)            67.13%          109.01 ms        463.6         67.73%        108.47 ms      2735.0
Priority (PrioQueue)  100.00%           28.66 ms          0.0         62.61%        292.58 ms      3169.2
Prio-CoDel (Hybrid)   100.00%           28.66 ms          0.0         62.29%        116.32 ms      3196.0
=======================================================================================================
```

### Key Empirical Discoveries:
1. **AQM Alone Does Not Protect Emergency Alerts:** Both RED ($67.31\%$ PDR, $460.8$ drops) and CoDel ($67.13\%$ PDR, $463.6$ drops) suffer substantial packet losses that are statistically indistinguishable from their ordinary traffic drop rates (Paired $t$-test: $p = 0.184$ for RED, $p = 0.183$ for CoDel). Because standard AQMs lack packet classification, their drop triggers apply indiscriminately across all traffic.
2. **CoDel Eliminates Bufferbloat but Sacrifices Safety Messages:** CoDel aggressively controls sojourn times, cutting end-to-end latency by over $54\%$ (from $236.89\text{ ms}$ to $109.01\text{ ms}$). However, because it operates on a single FIFO queue, it drops nearly one-third of all emergency alerts to maintain low queue occupancy.
3. **Priority Scheduling Guarantees Absolute Reliability:** Multi-band strict priority scheduling (`PrioQueueDisc`) achieves **$100.00\%$ PDR** ($0.0$ drops) and **$28.66\text{ ms}$ delay** for Emergency Alerts across all load conditions up to $140\%$, confirming non-preemptive queue isolation. However, ordinary civilian traffic trapped in Band 1 suffers elevated bufferbloat ($292.58\text{ ms}$).
4. **The Prio-CoDel Hybrid Solves the Congestion Dilemma:** By integrating CoDel AQM inside Band 1 of `PrioQueueDisc`, the hybrid architecture achieves the optimal operational envelope: **$100.00\%$ emergency delivery at $28.66\text{ ms}$**, while simultaneously slashing ordinary civilian queue latency from $292.58\text{ ms}$ down to $116.32\text{ ms}$ ($p = 4.45 \times 10^{-15}$, a $60.2\%$ latency reduction).

---

## 2. Experimental Architecture & Queue Discipline Implementations

### 2.1 Topology & Bottleneck Configuration
The network topology conforms strictly to the verified 50-node SEAN multi-tier architecture:
- **Sources (Nodes 0..39):** 10 Emergency Alert sources (Nodes 0..9) generating $1/7$ of offered load with IP TOS `0x10` (DSCP `0x04`, RFC 1349 Low-Delay); 30 Ordinary Civilian sources (Nodes 10..39) generating $6/7$ of offered load with IP TOS `0x00` (Best Effort).
- **Regional Routers (Nodes 40..42):** Concentrating traffic across Zone A (13 links), Zone B (13 links), and Zone C (14 links) with $10\text{ Mbps}$ links and $2\text{ ms}$ propagation delay.
- **Ingress Core Gateway (Node 43):** Houses the traffic control subsystem on its outgoing bottleneck interface.
- **Egress Core Gateway (Node 44):** Connected to Node 43 via a single shared point-to-point bottleneck link:
  $$\text{Data Rate} = 1.0\text{ Mbps},\quad \text{Propagation Delay} = 10.0\text{ ms},\quad \text{Queue Capacity} = 50\text{ packets}$$
- **Emergency Sink Servers (Nodes 45..49):** Connected via five independent $10\text{ Mbps}$ / $2\text{ ms}$ egress links.
- **NetDevice Queue Decoupling:** To prevent hardware device buffering from distorting traffic control measurements, the PointToPointNetDevice queue was explicitly clamped to 1 packet (`ns3::DropTailQueue, MaxSize=1p`), ensuring all queueing dynamics occur within the root queue discipline.

```
       [ 10 Emergency Sources (Nodes 0..9) ]       [ 30 Ordinary Sources (Nodes 10..39) ]
                         \                                 /
                   10 Mbps, 2 ms                     10 Mbps, 2 ms
                           \                             /
                 [ 3 Regional Aggregation Routers (Nodes 40..42) ]
                                      \   |   /
                                   10 Mbps, 2 ms
                                        v
                       +---------------------------------+
                       | Node 43 (GW_in Core Gateway)   |
                       | Traffic Control Queue Discipline|
                       +---------------------------------+
                                        |  SHARED BOTTLENECK:
                                        |  1 Mbps, 10 ms, 50-pkt buffer
                                        v
                       +---------------------------------+
                       | Node 44 (GW_eg Core Gateway)   |
                       +---------------------------------+
                                      /   |   \
                                   10 Mbps, 2 ms
                                        v
                      [ 5 Emergency Response Servers (Nodes 45..49) ]
```

### 2.2 Mathematical Specifications of Queue Disciplines

#### 1. FIFO (`ns3::FifoQueueDisc`)
A standard First-In, First-Out queue discipline operating tail-drop buffer management:
- Capacity: $B_{max} = 50\text{ packets}$ ($540\text{ bytes/packet} \times 50 = 27,000\text{ bytes}$).
- Scheduling: Strict order of arrival. Arriving packets are admitted if queue length $q(t) < 50$; otherwise dropped.

#### 2. Strict Priority (`ns3::PrioQueueDisc`)
A classful, non-preemptive priority scheduler with three internal child FIFO queue discs:
- Priomap: `"1 2 2 2 1 2 0 0 1 1 1 1 1 1 1 1"`. IP TOS `0x10` maps to priority band 0 (highest); TOS `0x00` maps to priority band 1 (normal).
- Capacities: Band 0 ($B_0 = 50\text{p}$), Band 1 ($B_1 = 50\text{p}$), Band 2 ($B_2 = 50\text{p}$).
- Dequeue Rule: Packets are served from Band 0 whenever Band 0 is non-empty. Band 1 is served only when Band 0 is empty.

#### 3. Random Early Detection (`ns3::RedQueueDisc`)
An AQM scheme that computes an Exponential Weighted Moving Average (EWMA) of queue length to perform early probabilistic packet drops before the physical buffer fills:
- EWMA Update:
  $$q_{avg} = (1 - w_q) \cdot q_{avg} + w_q \cdot q(t),\quad w_q = 0.002$$
- Minimum Threshold: $MinTh = 10.0\text{ packets}$ ($20\%$ buffer capacity).
- Maximum Threshold: $MaxTh = 30.0\text{ packets}$ ($60\%$ buffer capacity).
- Maximum Drop Probability: $P_{max} = 1 / LInterm = 1 / 50 = 0.02$.
- Dropping Function:
  $$P_b = \begin{cases} 
  0 & \text{if } q_{avg} < MinTh \\
  P_{max} \cdot \frac{q_{avg} - MinTh}{MaxTh - MinTh} & \text{if } MinTh \le q_{avg} < MaxTh \\
  1.0 & \text{if } q_{avg} \ge MaxTh \text{ (Forced Drop)}
  \end{cases}$$
- Hard Drop: Enabled (`UseHardDrop=true`). Mean Packet Size: $540\text{ bytes}$. Link Bandwidth: $1\text{ Mbps}$.

#### 4. Controlled Delay (`ns3::CoDelQueueDisc`)
A delay-based AQM algorithm designed to defeat bufferbloat by monitoring the actual sojourn (dwelling) time of packets through the queue:
- Target Sojourn Time: $Target = 5\text{ ms}$.
- Evaluation Interval: $Interval = 100\text{ ms}$.
- MinBytes: $540\text{ bytes}$ (1 MTU). Maximum Capacity: $50\text{ packets}$.
- Control Law: If packet sojourn time $t_{sojourn} > Target$ continuously throughout the $100\text{ ms}$ interval, CoDel enters dropping state and drops packets upon dequeue at dynamically decreasing intervals:
  $$t_{next\_drop} = t_{now} + \frac{Interval}{\sqrt{count}}$$

#### 5. Prio-CoDel Hybrid
A hierarchical discipline engineered to provide strict isolation for emergency alerts while eliminating ordinary bufferbloat:
- Root Discipline: `ns3::PrioQueueDisc` with 3 bands.
- Band 0 (Emergency): `ns3::FifoQueueDisc` ($MaxSize = 50\text{p}$).
- Band 1 (Ordinary): `ns3::CoDelQueueDisc` ($Target = 5\text{ ms}$, $Interval = 100\text{ ms}$, $MaxSize = 50\text{p}$, $MinBytes = 540\text{B}$).
- Band 2 (Unused): `ns3::FifoQueueDisc` ($MaxSize = 50\text{p}$).

---

## 3. Comprehensive Empirical Results

### 3.1 Emergency Alert Traffic Class Performance
All metrics represent sample means and $95\%$ confidence intervals calculated over $N = 10$ independent random replications ($df = 9$).

```
===================================================================================================================
                     EMERGENCY ALERT TRAFFIC CLASS: SUMMARY STATISTICS ACROSS 5 LOADS
===================================================================================================================
Offered Load   Queue Discipline     Tx Pkts       Rx Pkts       Queue Drops     PDR (%)         Mean Delay (ms)
-------------------------------------------------------------------------------------------------------------------
 50%           FIFO (Drop-Tail)     501.2 ± 1.0   501.2 ± 1.0     0.0 ± 0.0   100.00% ± 0.00%    24.02 ± 0.16 ms
 (500 Kbps)    Priority (Prio)      501.2 ± 1.0   501.2 ± 1.0     0.0 ± 0.0   100.00% ± 0.00%    23.62 ± 0.08 ms
               RED (AQM)            501.2 ± 1.0   501.2 ± 1.0     0.0 ± 0.0   100.00% ± 0.00%    24.03 ± 0.16 ms
               CoDel (AQM)          501.2 ± 1.0   501.2 ± 1.0     0.0 ± 0.0   100.00% ± 0.00%    24.02 ± 0.16 ms
               Prio-CoDel           501.2 ± 1.0   501.2 ± 1.0     0.0 ± 0.0   100.00% ± 0.00%    23.62 ± 0.08 ms
-------------------------------------------------------------------------------------------------------------------
 80%           FIFO (Drop-Tail)     802.0 ± 1.5   802.0 ± 1.5     0.0 ± 0.0   100.00% ± 0.00%    29.39 ± 0.69 ms
 (800 Kbps)    Priority (Prio)      802.0 ± 1.5   802.0 ± 1.5     0.0 ± 0.0   100.00% ± 0.00%    26.15 ± 0.16 ms
               RED (AQM)            802.0 ± 1.5   802.0 ± 1.5     0.0 ± 0.0   100.00% ± 0.00%    29.47 ± 0.70 ms
               CoDel (AQM)          802.0 ± 1.5   801.0 ± 1.7     1.0 ± 0.9    99.88% ± 0.11%    29.34 ± 0.66 ms
               Prio-CoDel           802.0 ± 1.5   802.0 ± 1.5     0.0 ± 0.0   100.00% ± 0.00%    26.15 ± 0.16 ms
-------------------------------------------------------------------------------------------------------------------
 100%          FIFO (Drop-Tail)    1003.7 ± 2.6   945.6 ± 2.4    58.1 ± 1.9    94.22% ± 0.29%   220.13 ± 0.73 ms
 (1000 Kbps)   Priority (Prio)     1003.7 ± 2.6  1003.7 ± 2.6     0.0 ± 0.0   100.00% ± 0.00%    28.50 ± 0.04 ms
               RED (AQM)           1003.7 ± 2.6   951.0 ± 2.2    52.7 ± 2.4    94.76% ± 0.28%   165.54 ± 0.50 ms
               CoDel (AQM)         1003.7 ± 2.6   945.3 ± 2.6    58.4 ± 3.4    94.19% ± 0.35%    45.23 ± 0.53 ms
               Prio-CoDel          1003.7 ± 2.6  1003.7 ± 2.6     0.0 ± 0.0   100.00% ± 0.00%    28.41 ± 0.05 ms
-------------------------------------------------------------------------------------------------------------------
 120%          FIFO (Drop-Tail)    1206.9 ± 3.4   941.4 ± 3.1   265.5 ± 2.8    78.00% ± 0.31%   233.95 ± 0.29 ms
 (1200 Kbps)   Priority (Prio)     1206.9 ± 3.4  1206.9 ± 3.4     0.0 ± 0.0   100.00% ± 0.00%    28.56 ± 0.04 ms
               RED (AQM)           1206.9 ± 3.4   958.2 ± 3.4   248.7 ± 3.3    79.40% ± 0.34%   197.88 ± 0.63 ms
               CoDel (AQM)         1206.9 ± 3.4   948.9 ± 3.4   258.0 ± 4.5    78.62% ± 0.44%    63.11 ± 0.70 ms
               Prio-CoDel          1206.9 ± 3.4  1206.9 ± 3.4     0.0 ± 0.0   100.00% ± 0.00%    28.57 ± 0.05 ms
-------------------------------------------------------------------------------------------------------------------
 140%          FIFO (Drop-Tail)    1409.8 ± 3.9   950.7 ± 2.6   459.1 ± 3.4    67.44% ± 0.27%   236.89 ± 0.16 ms
 (1400 Kbps)   Priority (Prio)     1409.8 ± 3.9  1409.8 ± 3.9     0.0 ± 0.0   100.00% ± 0.00%    28.66 ± 0.03 ms
               RED (AQM)           1409.8 ± 3.9   949.0 ± 3.0   460.8 ± 3.9    67.31% ± 0.34%   219.86 ± 0.50 ms
               CoDel (AQM)         1409.8 ± 3.9   946.2 ± 3.7   463.6 ± 5.4    67.13% ± 0.45%   109.01 ± 2.05 ms
               Prio-CoDel          1409.8 ± 3.9  1409.8 ± 3.9     0.0 ± 0.0   100.00% ± 0.00%    28.66 ± 0.03 ms
===================================================================================================================
```

### 3.2 Ordinary Civilian Traffic Class Performance

```
===================================================================================================================
                      ORDINARY CIVILIAN TRAFFIC CLASS: SUMMARY STATISTICS ACROSS 5 LOADS
===================================================================================================================
Offered Load   Queue Discipline     Tx Pkts       Rx Pkts       Queue Drops     PDR (%)         Mean Delay (ms)
-------------------------------------------------------------------------------------------------------------------
 50%           FIFO (Drop-Tail)    3017.3 ± 4.9  3017.3 ± 4.9     0.0 ± 0.0   100.00% ± 0.00%    24.06 ± 0.08 ms
 (500 Kbps)    Priority (Prio)     3017.3 ± 4.9  3017.3 ± 4.9     0.0 ± 0.0   100.00% ± 0.00%    24.12 ± 0.08 ms
               RED (AQM)           3017.3 ± 4.9  3017.3 ± 4.9     0.0 ± 0.0   100.00% ± 0.00%    24.01 ± 0.08 ms
               CoDel (AQM)         3017.3 ± 4.9  3017.3 ± 4.9     0.0 ± 0.0   100.00% ± 0.00%    24.06 ± 0.08 ms
               Prio-CoDel          3017.3 ± 4.9  3017.3 ± 4.9     0.0 ± 0.0   100.00% ± 0.00%    24.12 ± 0.08 ms
-------------------------------------------------------------------------------------------------------------------
 80%           FIFO (Drop-Tail)    4833.6 ± 7.9  4833.6 ± 7.9     0.0 ± 0.0   100.00% ± 0.00%    28.84 ± 0.53 ms
 (800 Kbps)    Priority (Prio)     4833.6 ± 7.9  4833.6 ± 7.9     0.0 ± 0.0   100.00% ± 0.00%    29.38 ± 0.61 ms
               RED (AQM)           4833.6 ± 7.9  4833.6 ± 7.9     0.0 ± 0.0   100.00% ± 0.00%    28.95 ± 0.54 ms
               CoDel (AQM)         4833.6 ± 7.9  4829.6 ± 7.4     4.0 ± 2.6    99.92% ± 0.05%    28.80 ± 0.54 ms
               Prio-CoDel          4833.6 ± 7.9  4829.1 ± 8.1     4.5 ± 2.9    99.91% ± 0.06%    29.33 ± 0.62 ms
-------------------------------------------------------------------------------------------------------------------
 100%          FIFO (Drop-Tail)    6047.8 ± 9.6  5759.0 ± 9.6   288.8 ± 12.8   95.23% ± 0.22%   219.29 ± 0.72 ms
 (1000 Kbps)   Priority (Prio)     6047.8 ± 9.6  5700.9 ± 9.2   346.9 ± 13.7   94.26% ± 0.25%   253.88 ± 0.95 ms
               RED (AQM)           6047.8 ± 9.6  5734.5 ± 9.8   313.3 ± 10.3   94.82% ± 0.20%   165.13 ± 0.51 ms
               CoDel (AQM)         6047.8 ± 9.6  5707.5 ± 11.2  340.3 ± 14.1   94.37% ± 0.28%    44.85 ± 0.52 ms
               Prio-CoDel          6047.8 ± 9.6  5649.8 ± 8.9   398.0 ± 12.1   93.42% ± 0.24%    47.53 ± 0.44 ms
-------------------------------------------------------------------------------------------------------------------
 120%          FIFO (Drop-Tail)    7272.7 ± 11.9 5781.6 ± 12.0 1491.1 ± 14.3   79.47% ± 0.21%   233.23 ± 0.28 ms
 (1200 Kbps)   Priority (Prio)     7272.7 ± 11.9 5516.1 ± 13.9 1756.6 ± 15.6   75.81% ± 0.27%   279.20 ± 0.38 ms
               RED (AQM)           7272.7 ± 11.9 5754.7 ± 13.4 1518.0 ± 16.5   79.10% ± 0.27%   197.52 ± 0.64 ms
               CoDel (AQM)         7272.7 ± 11.9 5732.8 ± 14.8 1539.9 ± 18.2   78.80% ± 0.31%    62.69 ± 0.71 ms
               Prio-CoDel          7272.7 ± 11.9 5473.4 ± 14.7 1799.3 ± 17.5   75.22% ± 0.30%    66.29 ± 0.81 ms
-------------------------------------------------------------------------------------------------------------------
 140%          FIFO (Drop-Tail)    8485.1 ± 13.9 5774.7 ± 12.3 2710.4 ± 19.3   68.02% ± 0.22%   236.13 ± 0.16 ms
 (1400 Kbps)   Priority (Prio)     8485.1 ± 13.9 5315.9 ± 14.4 3169.2 ± 16.2   62.61% ± 0.23%   292.58 ± 0.35 ms
               RED (AQM)           8485.1 ± 13.9 5774.5 ± 12.8 2710.6 ± 18.8   68.02% ± 0.22%   219.29 ± 0.50 ms
               CoDel (AQM)         8485.1 ± 13.9 5750.1 ± 14.6 2735.0 ± 19.6   67.73% ± 0.26%   108.47 ± 2.05 ms
               Prio-CoDel          8485.1 ± 13.9 5289.1 ± 15.6 3196.0 ± 18.0   62.29% ± 0.25%   116.32 ± 0.64 ms
===================================================================================================================
```

---

## 4. Rigorous Statistical Hypotheses & Paired $t$-Test Verification

To mathematically validate all conclusions, paired two-tailed Student's $t$-tests ($df = 9$) were conducted using seed-matched paired replications across queue disciplines under identical traffic seeds.

```
========================================================================================================================
                               PAIRED TWO-TAILED STUDENT'S t-TEST RESULTS (df = 9)
========================================================================================================================
Load   Hypothesis / Comparison       Metric Tested        Mean Difference       t-Statistic      p-Value       Verdict
------------------------------------------------------------------------------------------------------------------------
100%   Priority vs. FIFO             Emergency Delay         -191.63 ms           -576.49     7.24 x 10^-22  SIGNIFICANT
100%   Priority vs. FIFO             Emergency PDR             +5.78%              +43.44     9.06 x 10^-12  SIGNIFICANT
100%   RED vs. FIFO                  Ordinary Delay           -54.16 ms           -318.83     1.50 x 10^-19  SIGNIFICANT
100%   CoDel vs. FIFO                Ordinary Delay          -174.44 ms           -492.31     3.00 x 10^-21  SIGNIFICANT
100%   Prio-CoDel vs. Priority       Ordinary Delay          -206.35 ms           -498.13     2.70 x 10^-21  SIGNIFICANT
100%   Prio-CoDel vs. Priority       Emergency Delay           -0.09 ms             -7.22     5.00 x 10^-05  SIGNIFICANT
100%   RED Emer PDR vs. Ord PDR      Indiscriminate Drop       -0.06%               -0.22         0.8278     NOT SIG (p>0.05)
100%   CoDel Emer PDR vs. Ord PDR    Indiscriminate Drop       -0.19%               -0.65         0.5339     NOT SIG (p>0.05)
------------------------------------------------------------------------------------------------------------------------
140%   Priority vs. FIFO             Emergency Delay         -208.23 ms          -3113.35     1.85 x 10^-28  SIGNIFICANT
140%   Priority vs. FIFO             Emergency PDR            +32.56%             +108.63     2.41 x 10^-15  SIGNIFICANT
140%   RED vs. FIFO                  Ordinary Delay           -16.84 ms           -193.91     1.31 x 10^-17  SIGNIFICANT
140%   CoDel vs. FIFO                Ordinary Delay          -127.66 ms           -107.37     2.68 x 10^-15  SIGNIFICANT
140%   Prio-CoDel vs. Priority       Ordinary Delay          -176.25 ms           -101.47     4.45 x 10^-15  SIGNIFICANT
140%   Prio-CoDel vs. Priority       Emergency Delay            0.00 ms              0.00         1.0000     PARITY (p=1.0)
140%   RED Emer PDR vs. Ord PDR      Indiscriminate Drop       -0.71%               -1.44         0.1843     NOT SIG (p>0.05)
140%   CoDel Emer PDR vs. Ord PDR    Indiscriminate Drop       -0.61%               -1.44         0.1834     NOT SIG (p>0.05)
========================================================================================================================
```

### Statistical Derivations and Insights:
1. **Proof of Indiscriminate AQM Dropping ($H_0$ Not Rejected):**
   - Under RED at $140\%$ load, the difference between emergency PDR ($67.31\%$) and ordinary PDR ($68.02\%$) has $t = -1.44$, $p = 0.1843 > 0.05$.
   - Under CoDel at $140\%$ load, the difference between emergency PDR ($67.13\%$) and ordinary PDR ($67.73\%$) has $t = -1.44$, $p = 0.1834 > 0.05$.
   - Under both AQMs, the null hypothesis ($H_0: \mu_{emer\_pdr} = \mu_{ord\_pdr}$) cannot be rejected at the $\alpha = 0.05$ significance level. This confirms that standalone AQM mechanisms drop high-priority emergency packets at rates statistically identical to best-effort packets.
2. **Extreme Statistical Significance of Priority Isolation:**
   - Priority scheduling achieves a delay reduction of $208.23\text{ ms}$ ($t = -3113.35, p = 1.85 \times 10^{-28}$) and a PDR improvement of $+32.56\%$ ($t = 108.63, p = 2.41 \times 10^{-15}$) compared to FIFO at $140\%$ load.
3. **Prio-CoDel Ordinary Latency Breakthrough:**
   - Prio-CoDel reduces ordinary civilian message latency from $292.58\text{ ms}$ to $116.32\text{ ms}$ ($t = -101.47, p = 4.45 \times 10^{-15}$), eliminating $176.25\text{ ms}$ of bufferbloat while preserving identical emergency delay ($28.66\text{ ms}$, $t = 0.00, p = 1.000$).

---

## 5. Detailed Comparative Analysis of Queue Disciplines

### 5.1 Emergency Latency & Delivery Reliability
- **FIFO:** Exhibits standard bufferbloat. At $100\%$ offered load, delay jumps from $29.39\text{ ms}$ to $220.13\text{ ms}$ as the 50-packet buffer fills. At $140\%$ load, delay reaches $236.89\text{ ms}$, and tail-drop discards $459.1$ emergency packets ($67.44\%$ PDR).
- **RED:** Early random dropping begins when $q_{avg} > 10\text{p}$, capping mean delay at $165.54\text{ ms}$ (at $100\%$) and $219.86\text{ ms}$ (at $140\%$). While delay is slightly lower than FIFO, RED discards $460.8$ emergency packets, offering zero QoS protection.
- **CoDel:** Monitors packet sojourn time against its $5\text{ ms}$ target. When persistent delay is detected, CoDel drops packets upon dequeue, capping delay at $45.23\text{ ms}$ ($100\%$ load) and $109.01\text{ ms}$ ($140\%$ load). However, because it drops packets indiscriminately from its single queue, it discards $463.6$ emergency packets ($67.13\%$ PDR).
- **PrioQueueDisc & Prio-CoDel:** Decouples emergency packets into Band 0. Because total emergency traffic ($200\text{ Kbps}$ at $140\%$) is strictly below the $1.0\text{ Mbps}$ bottleneck capacity, Band 0 never exceeds capacity ($0.0$ queue drops, $100.00\%$ PDR). Mean delay remains bounded between $23.62\text{ ms}$ and $28.66\text{ ms}$.

### 5.2 The Residual Serialization Delay Bound
The theoretical minimum end-to-end delay across the 3 hops is:
$$D_{prop} = 2\text{ ms} + 10\text{ ms} + 2\text{ ms} = 14.0\text{ ms}$$
$$D_{tx\_access} = \frac{540 \times 8}{10 \times 10^6} = 0.432\text{ ms},\quad D_{tx\_server} = 0.432\text{ ms}$$
$$D_{tx\_bottleneck} = \frac{540 \times 8}{1.0 \times 10^6} = 4.320\text{ ms}$$
$$\text{Baseline Serialization + Propagation} = 14.0 + 0.432 + 4.320 + 0.432 = 19.184\text{ ms}$$

In a non-preemptive priority scheduler, if an ordinary packet (Band 1) is currently in transmission on the bottleneck link when an emergency packet arrives at Node 43, the emergency packet must wait for the current transmission to complete:
$$\Delta_{residual} \le \frac{542 \times 8\text{ bits}}{1.0\text{ Mbps}} = 4.336\text{ ms}$$
Under severe $140\%$ congestion, empirical emergency delay for Priority is $28.66\text{ ms}$. This represents the baseline delay ($19.18\text{ ms}$) plus regional queueing and the bounded residual serialization delay ($4.34\text{ ms}$), with zero bufferbloat accumulation.

### 5.3 Ordinary Bufferbloat and the Prio-CoDel Solution
Under standard `PrioQueueDisc`, Band 1 uses tail-drop FIFO. Because ordinary traffic offers $1.2\text{ Mbps}$ payload against a remaining bottleneck capacity of $0.8\text{ Mbps}$, Band 1 remains continuously full ($50\text{ packets}$):
$$D_{bloat} = \frac{50 \times 540 \times 8\text{ bits}}{0.8\text{ Mbps}} \approx 270\text{ ms}$$
Empirical ordinary delay reaches $292.58\text{ ms}$.

By deploying `CoDel` inside Band 1 (`Prio-CoDel`), CoDel monitors sojourn time specifically for ordinary packets without ever touching Band 0. When ordinary dwelling time exceeds $5\text{ ms}$, CoDel drops ordinary packets before the buffer saturates, collapsing ordinary queue latency to $116.32\text{ ms}$ (a reduction of $176.25\text{ ms}$), while emergency packets continue to pass through Band 0 with $100\%$ reliability and $28.66\text{ ms}$ latency.

---

## 6. Generated Visualizations

The statistical analysis generated five high-resolution presentation figures saved in `reports/figures/queue_disc_comparison/`:

1. **`qdisc_emergency_delay_comparison.png`:** Multi-line plot illustrating emergency delay curves across loads 50% to 140%. Demonstrates the sharp division between priority disciplines (flat at 24–29 ms) and unclassified disciplines (surging to 110–237 ms).
2. **`qdisc_emergency_pdr_comparison.png`:** PDR comparison showing 100% flatline delivery for Priority and Prio-CoDel versus steep drop to 67% at 140% load for FIFO, RED, and CoDel.
3. **`qdisc_ordinary_delay_comparison.png`:** Bufferbloat analysis for ordinary civilian traffic, highlighting CoDel and Prio-CoDel capping delay below 120 ms versus 293 ms in Priority.
4. **`qdisc_queue_drops_comparison.png`:** Grouped bar chart of bottleneck queue drops at 140% load, visually verifying 0.0 emergency drops under Priority and Prio-CoDel versus ~460 drops under FIFO, RED, and CoDel.
5. **`qdisc_throughput_tradeoff.png`:** Throughput allocation chart verifying full 210.0 Kbps emergency bandwidth guarantee under priority disciplines versus 141.7 Kbps under unclassified disciplines.

---

## 7. Complete Reproducibility Manifest

| Parameter / Asset | File Path / Specification |
|:---|:---|
| **Simulation Model** | `scratch/sean-queue-disc-comparison.cc` |
| **Execution Script** | `run-queue-disc-experiments.sh` |
| **Analysis & Plotting Script** | `reports/analyze-queue-disc.py` |
| **Master Raw Telemetry CSV** | `data/queue_disc_comparison/queue_disc_comparison_master.csv` (500 class rows) |
| **Statistical Summary CSV** | `data/queue_disc_comparison/queue_disc_statistical_summary.csv` |
| **Paired $t$-Test Results CSV** | `data/queue_disc_comparison/queue_disc_paired_ttests.csv` |
| **Experiment Manifest** | `data/queue_disc_comparison/manifest.csv` |
| **Figures Directory** | `reports/figures/queue_disc_comparison/` (5 figures) |
| **Replication Parameters** | Loads: [50, 80, 100, 120, 140]; Seeds: [1..10]; Duration: 30.0s |
| **Execution Verification** | 250 / 250 runs completed with 0 errors (Elapsed time: 61s) |

---

## 8. Academic Submission Verdict & Conclusions

The empirical and statistical evidence establishes that:
1. **Active Queue Management (AQM) is not a substitute for Priority Scheduling:** Algorithms like RED and CoDel are engineered to manage buffer occupancy, not to provide differentiated service. In the absence of classification, life-critical emergency messages are dropped at the same rate as recreational traffic.
2. **Priority Queueing is Essential for Emergency Networks:** Strict non-preemptive priority queueing (`PrioQueueDisc`) is strictly necessary to guarantee $100\%$ packet delivery and sub-$30\text{ ms}$ latency for emergency alerts during extreme overload.
3. **The Prio-CoDel Architecture is the Recommended Standard:** Deploying CoDel within the low-priority band of a priority scheduler eliminates ordinary bufferbloat without compromising emergency delivery, providing the optimal design for next-generation smart emergency alert networks.

