# Smart Emergency Alert and Priority Messaging Network (SEAN)
## Academic Capstone Presentation Deck — ns-3.49 Simulation & Statistical Audit
### Quality of Service Prioritization, Scalability, Traffic Dynamics, and Queue Management in Congested IP Networks

---

## Slide 1: Title & Academic Project Overview
- **Project Title:** Smart Emergency Alert and Priority Messaging Network (SEAN)
- **Subtitle:** Mitigating Bufferbloat and Guaranteeing QoS for Mission-Critical Telemetry Using ns-3.49
- **Course:** Computer Networks / Advanced Network Programming Capstone Project
- **Simulator Platform:** Network Simulator 3 (Release ns-3.49), macOS ARM64 Toolchain
- **Presenter:** `[Student Name]` | Roll No: `[Roll Number / Student ID]`
- **Department & Institution:** `[Department]`, `[College / University Name]`
- **Supervised by:** `[Project Supervisor / Guide Name]`
- **Scope:** 5 Controlled Experimental Studies, 490 Simulation Runs, 18,860 Flow Records

---

## Slide 2: Motivation & The Disaster Bufferbloat Problem
- **The Disaster Communication Dilemma:**
  - During emergencies (earthquakes, flash floods), civilian traffic surges by 300%–500% (video streaming, status checks, calls).
  - Mission-critical early-warning alerts must traverse the same regional aggregation links.
- **The Flaw of Best-Effort FIFO Drop-Tail:**
  - Standard routers queue packets First-In, First-Out regardless of semantic urgency.
  - Arriving evacuation alerts wait behind hundreds of recreational packets (**Bufferbloat**).
  - A 50-packet FIFO queue on a 1.0 Mbps link introduces $\mathbf{216\text{ ms}}$ of pure queueing delay.
  - Arriving packets hit full queues and suffer indiscriminate tail-drop discard.
- **The Core Scientific Question:**
  - Can network-layer DiffServ priority queueing deterministically protect life-critical telemetry during sustained overload without physical over-provisioning?

---

## Slide 3: Project Objectives & 5-Study Experimental Architecture
1. **Study 1 (Five-Node Baseline):** Reproduce baseline starvation and verify priority scheduling gains under 140% overload.
2. **Study 2 (Fifty-Node Congestion Sweep):** Sweep offered load across 50%, 80%, 100%, 120%, and 140% to map the exact onset of drops and bufferbloat (100 runs).
3. **Study 3 (Steady vs. Bursty Traffic):** Evaluate the impact of periodic ON/OFF civilian traffic burstiness under load-equivalent conditions (40 runs).
4. **Study 4 (Active Queue Management Comparison):** Compare FIFO, Strict Priority, RED, CoDel, and Hybrid Prio-CoDel across 250 runs.
5. **Study 5 (DSCP Classification & Priority Mapping):** Evaluate IPv4 TOS/DSCP marking, kernel `ip_tos2prio` translation, and default vs. custom Linux Priomaps (80 runs).

---

## Slide 4: DiffServ Architecture, DSCP vs. DS Field Byte, & Priomap Mechanics
- **The Crucial 2-Bit Shift Distinction (RFC 2474 / RFC 3168):**
  - DSCP occupies high 6 bits (bits 0–5); ECN occupies low 2 bits (bits 6–7).
  - Socket APIs accept an 8-bit byte: $\text{TOS Byte} = \text{DSCP} \ll 2$.
  - Expedited Forwarding (EF: DSCP 46) $\rightarrow 46 \ll 2 = 184 = \text{0xB8}$.
  - Assured Forwarding (AF41: DSCP 34) $\rightarrow 34 \ll 2 = 136 = \text{0x88}$.
- **ns-3.49 / Linux Socket Translation (`Socket::IpTos2Priority`):**
  - Bitmask `ipTos & 0x1e` masks bits 1–4, discarding the top 3 precedence bits!
  - EF (`0xB8`) maps to Priority 4 (`NS3_PRIO_INTERACTIVE_BULK`).
  - AF41 (`0x88`) maps to Priority 2 (`NS3_PRIO_BULK`).
  - CS6 (`0xC0`) maps to Priority 0 (`NS3_PRIO_BESTEFFORT`).
- **The Default Linux Priomap Trap:**
  - Standard Priomap (`"1 2 2 2 1 2 0 0 1 1 1 1 1 1 1 1"`) routes Priority 4 (EF) to **Band 1** with civilian traffic!
  - A **Custom DiffServ Priomap** (`"2 2 1 2 0 2 0 0 2 2 2 2 2 2 2 2"`) is mandatory to route EF to Band 0.

---

## Slide 5: Fifty-Node Hierarchical Network Topology
- **Client Tier:** 40 client nodes (Nodes 0–39). Access links: 10 Mbps, 2 ms delay, NetDevice queue = 1p.
- **Aggregation Tier:** 3 regional aggregation switches (Nodes 40–42). Aggregation links: 10 Mbps, 2 ms delay.
- **Core Bottleneck Gateway Tier:**
  - Ingress Gateway (Node 43) $\rightarrow$ Egress Gateway (Node 44).
  - **Bottleneck Link:** 1.0 Mbps bandwidth, 10 ms delay, 50-packet capacity queue discipline.
- **Server Tier:** 5 destination servers (Nodes 45–49). Distribution links: 10 Mbps, 2 ms delay.
- **Traffic Generation:** Custom `JitteredCbrApp` with $\pm 20\%$ uniform inter-arrival jitter.

---

## Slide 6: Study 1 — Five-Node Baseline Reproduction
- **Configuration:** 1 Emergency source (200 kbps), 2 Ordinary sources (600 kbps each). Total = 1,400 kbps payload (140% overload).
- **Key Empirical Results (10 matched runs, df = 9):**
  - **FIFO Baseline:** Emergency PDR = $\mathbf{54.16\% \pm 0.62\%}$, Mean Delay = $\mathbf{231.67 \pm 0.16\text{ ms}}$, Emergency Drops = $648.9$.
  - **Priority Mode:** Emergency PDR = $\mathbf{100.00\% \pm 0.00\%}$, Mean Delay = $\mathbf{22.28 \pm 0.03\text{ ms}}$, Emergency Drops = $\mathbf{0.0}$.
- **Performance Impact:**
  - Delay reduced by $\mathbf{90.38\%}$ ($-209.39\text{ ms}$, paired $t = -2,849.57, p < 10^{-20}$).
  - Drops conserved: ordinary traffic absorbs all $3,171.0$ drops ($70.28\% \rightarrow 62.66\%$ PDR).

---

## Slide 7: Study 2 — Fifty-Node Congestion Sweep (50% to 140% Load)
- **Design:** Swept offered load across 5 levels (500 kbps to 1,400 kbps) across 100 runs.
- **Empirical Findings:**
  - **50% Load:** 100% PDR for all classes, delay $\approx 27.3\text{ ms}$, zero drops.
  - **80% Load:** 100% PDR for all classes, delay $\approx 31.8\text{ ms}$, zero drops.
  - **100% Load (Drop Onset):** FIFO drops $13.8$ packets; latency surges to $198.81\text{ ms}$; emergency PDR falls to $94.81\%$. Priority maintains $100.00\%$ PDR and $28.53\text{ ms}$ delay.
  - **140% Load (Severe Overload):** FIFO Emergency PDR drops to $66.82\%$, delay reaches $234.09\text{ ms}$. Priority delivers $100.00\%$ PDR at $28.66\text{ ms}$.

---

## Slide 8: Framing-Induced Congestion Onset at 100% Load
- **The Framing Expansion Mechanism:**
  - Application payload: 512 bytes.
  - IP/UDP encapsulation: 20 bytes IPv4 + 8 bytes UDP = 28 bytes overhead.
  - Physical wire frame: 540 bytes ($4,320\text{ bits}$).
  - Framing expansion factor: $\frac{540}{512} = 1.0546875$ (+5.47% overhead).
- **Physical Consequence:**
  - At nominal 100% application load (1,000 kbps), actual wire rate is $\mathbf{1,054.68\text{ kbps}}$.
  - The 1.0 Mbps bottleneck is saturated by $5.47\%$, triggering bufferbloat and tail-drop.
  - Priority scheduling completely insulates emergency alerts from framing-induced drops.

---

## Slide 9: Study 3 — Steady vs. Bursty Traffic Dynamics & Saw-Tooth Latency
- **Experimental Design:** Equivalent long-run average ordinary load ($1,200\text{ kbps}$):
  - Steady: continuous 40 kbps per source.
  - Bursty: 0.5s ON @ 80 kbps (2,400 kbps peak), 0.5s OFF @ 0 kbps (50% duty cycle).
- **The FIFO Bursty Illusion:**
  - Time-averaged emergency PDR is higher in bursty FIFO ($75.26\%$ vs $67.44\%$), and mean delay is lower ($118.13\text{ ms}$ vs $237.02\text{ ms}$).
  - **The Reality:** Queue oscillates in a periodic saw-tooth ($21.5\text{ ms} \dots 243.5\text{ ms}$).
  - **Emergency jitter explodes by +1,917%** ($5.07\text{ ms} \rightarrow \mathbf{102.25\text{ ms}}$).
- **Priority Resolution:** Guarantees $100.00\%$ PDR, bounded $27.18\text{ ms}$ latency, and $1.89\text{ ms}$ jitter ($p < 10^{-17}$).

---

## Slide 10: Study 4 — Active Queue Management Comparison (FIFO, Priority, RED, CoDel)
- **Research Question:** Can AQM (RED / CoDel) protect emergency alerts without priority scheduling?
- **Empirical Results at 140% Load (250 runs, df = 9):**
  - **FIFO:** Emergency PDR = $66.82\%$, Delay = $234.09\text{ ms}$.
  - **RED:** Emergency PDR = $65.85\%$, Delay = $61.27\text{ ms}$ ($p = 0.184$ vs FIFO).
  - **CoDel:** Emergency PDR = $65.87\%$, Delay = $22.25\text{ ms}$ ($p = 0.183$ vs FIFO).
  - **Priority:** Emergency PDR = $\mathbf{100.00\%}$, Delay = $\mathbf{28.66\text{ ms}}$.
- **Key Insight:** Standalone AQM mechanisms drop packets based on total queue occupancy without inspecting priority tags. They fail to protect emergency packets from packet loss.

---

## Slide 11: Hybrid Prio-CoDel: Optimal Emergency Isolation & Bufferbloat Mitigation
- **The Architecture:**
  - Band 0: Strict Priority FIFO (dedicated to Emergency Alerts).
  - Bands 1 & 2: CoDel QueueDiscs (target = 5 ms, interval = 100 ms) for civilian traffic.
- **Empirical Performance at 140% Load:**
  - **Emergency Alert:** $\mathbf{100.00\% \pm 0.00\%}$ PDR, $\mathbf{28.71 \pm 0.04\text{ ms}}$ delay, **0.0 drops**.
  - **Ordinary Civilian:** Latency drops from $237.00\text{ ms}$ to $\mathbf{60.75\text{ ms}}$—saving $\mathbf{176.25\text{ ms}}$ of bufferbloat delay!
- **Conclusion:** Combines the zero-loss guarantee of strict priority with the bufferbloat mitigation of CoDel.

---

## Slide 12: Study 5 — The Default Linux Priomap Trap with Standard DSCP EF
- **The Experiment:** Applied standard Expedited Forwarding (EF: DSCP 46, byte `0xB8`) to emergency alerts using the default Linux Priomap (`"1 2 2 2 1 2 0 0 1 1 1 1 1 1 1 1"`).
- **The Catastrophic Result:**
  - `IpTos2Priority(0xB8)` returns Priority 4 (`NS3_PRIO_INTERACTIVE_BULK`).
  - Default Priomap routes Priority 4 to **Band 1** (Normal Priority)!
  - Emergency Alerts share Band 1 with Best Effort civilian traffic.
  - Emergency PDR drops to $\mathbf{94.77\%}$ ($73.0$ drops); delay surges to $\mathbf{216.58\text{ ms}}$ ($t = -23.21, p = 2.43 \times 10^{-9}$).
  - Telemetry (AF41: `0x88`) is routed to Band 2 and starved ($\mathbf{1.08\%}$ PDR).
- **Takeaway:** Changing DSCP marking alone without reconfiguring the queue classifier fails completely.

---

## Slide 13: Study 5 — Custom DiffServ Priomap & 3-Tier Multi-Class QoS
- **The Solution:** Custom DiffServ Priomap (`"2 2 1 2 0 2 0 0 2 2 2 2 2 2 2 2"`).
- **Empirical Performance (80 runs, df = 9):**
  - **Tier 1 (Band 0): Emergency Alert (EF `0xB8`):** $\mathbf{100.00\% \pm 0.00\%}$ PDR, $\mathbf{28.61 \pm 0.04\text{ ms}}$ delay, **0.0 drops**.
  - **Tier 2 (Band 1): Telemetry (AF41 `0x88`):** $\mathbf{100.00\% \pm 0.00\%}$ PDR, $\mathbf{32.03 \pm 0.17\text{ ms}}$ delay, **0.0 drops**.
  - **Tier 3 (Band 2): Ordinary Msg (BE `0x00`):** $\mathbf{44.57\% \pm 0.21\%}$ PDR, $585.47\text{ ms}$ delay, $3,088.1$ drops (absorbs congestion).
- **Dual Compatibility:** Also supports legacy TOS `0x10` alerts in Band 0 ($100.00\%$).

---

## Slide 14: Drop Conservation & Buffer Occupancy Verification
- **Conservation of Dropped Packets:**
  - Priority queueing does not eliminate packet drops; it reallocates drops to lower bands.
  - Across all 490 runs, $\text{Tx} = \text{Rx} + \text{Lost}$ with **zero violations**.
- **The 50-Packet Stranded Buffer Resolution:**
  - In starved queue conditions (e.g. Band 2 under default priomap), FlowMonitor lost packets exceeded queue drops by exactly 50 packets.
  - **Root Cause:** Band 2 buffer capacity is 50 packets (`MaxSize = 50p`). Because Band 1 was continuously saturated, Band 2 stopped dequeueing. At simulation shutdown ($T = 30.0\text{ s}$), 50 packets remained stranded inside the buffer.
  - $\text{Total Lost} = \text{Drops} (2,705.4) + \text{Stranded in Buffer} (50.0)$. Verified to the packet.

---

## Slide 15: Cross-Study Comparative Summary

| Metric Dimension | Study 1: 5-Node | Study 2: Congestion | Study 3: Burstiness | Study 4: Queue Discs | Study 5: Multi-Tier DSCP |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **Simulated Runs** | 20 runs | 100 runs | 40 runs | 250 runs | 80 runs |
| **Total Senders** | 3 senders | 40 senders | 40 senders | 40 senders | 40 senders (3 classes) |
| **FIFO Emergency PDR** | **54.16%** | **66.82%** | 67.44% (std) / 75.26% (bst) | **66.82%** | **67.74%** |
| **Priority Emergency PDR** | **100.00%** | **100.00%** | **100.00%** | **100.00%** | **100.00%** (Custom) |
| **FIFO Emergency Delay** | 231.67 ms | 234.09 ms | 237.02 ms / 118.13 ms | 234.09 ms | 232.43 ms |
| **Priority Emergency Delay**| **22.28 ms** | **28.66 ms** | **27.18 ms** | **28.66 ms** | **28.61 ms** |
| **Band 0 Queue Drops** | **0.0** | **0.0** | **0.0** | **0.0** | **0.0** |
| **Civilian Trade-off** | PDR: 70.3% -> 62.7% | PDR: 68.1% -> 62.6% | PDR: 52.4% -> 48.4% | Delay: 290 ms | PDR: 68.5% -> 44.6% |

---

## Slide 16: Civilian Trade-Offs & Preemption Boundaries
- **Zero-Sum Capacity Redistribution:**
  - Priority scheduling guarantees service to Band 0 by starving or degrading lower bands.
  - In Study 5, achieving 100% delivery for emergency and telemetry pushes civilian PDR down to $44.57\%$.
- **Non-Preemptive Serialization Boundary:**
  - Band 0 cannot interrupt a packet currently in transmission on the wire.
  - Maximum residual serialization delay $= \frac{540 \times 8 \text{ bits}}{1,000,000 \text{ bps}} = \mathbf{4.32\text{ ms}}$.
- **Emergency Over-Subscription Constraint:**
  - Immunity holds only while emergency offered load $< C_{\text{bottleneck}}$ ($200\text{ kbps} < 1,000\text{ kbps}$).
  - If emergency traffic exceeds line rate, Band 0 itself overflows.

---

## Slide 17: Limitations & Threats to Validity
1. **Wired Point-to-Point Topology:** Wireless physical layer phenomena (channel fading, CSMA/CA backoff, hidden nodes) are excluded to isolate pure queueing dynamics.
2. **Fixed Packet Sizing:** Evaluated with 512-byte UDP payloads. Variable MTU sizes and TCP congestion window dynamics may modify queue ramp slopes.
3. **Hardware Queue Ring Decoupling:** Hardware NIC ring buffers were modeled with capacity 1 packet (`1p`). Deep driver rings in physical hardware can add unmanaged serialization latency before the Traffic Control layer.

---

## Slide 18: Conclusions, Key Contributions, & Viva Summary
1. **Priority Scheduling Is Essential:** Eliminates bufferbloat and guarantees 100.00% emergency delivery with bounded $\approx 28.6\text{ ms}$ latency under 140% overload.
2. **AQM Alone Fails Emergency QoS:** RED and CoDel drop emergency alerts indiscriminately; **Hybrid Prio-CoDel** provides the optimal dual architecture.
3. **Marking Alone Does Not Work:** Packet marking without router queue scheduling provides $0.00\%$ benefit.
4. **Default Priomap Trap Uncovered:** Standard DSCP EF (`0xB8`) requires a **Custom Priomap** (`"2 2 1 2 0 2 0 0 2 2 2 2 2 2 2 2"`) to avoid being dumped into Band 1.
5. **Complete Reproducibility:** 490 runs, 100% matched seeds, zero packet conservation violations, and audited scripts in ns-3.49.
