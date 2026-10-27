# Smart Emergency Alert and Priority Messaging Network
## B.Tech Network Programming Presentation Deck

---

## Slide 1: Title Slide
- **Project Title:** Smart Emergency Alert and Priority Messaging Network
- **Subtitle:** Mitigating Network Congestion for Critical Communication Using ns-3.49
- **Course:** Network Programming (NWP) Project
- **Presented by:** B.Tech Student
- **Platform:** ns-3 (Release 3.49), macOS Apple Silicon

---

## Slide 2: Real-World Motivation & Problem Statement
- **The Disaster Communication Dilemma:**
  - During natural disasters, civilian traffic spikes by 300%–500% (messaging, video, status checks).
  - First responders and emergency alert broadcasts must pass through the same public gateways.
- **The Flaw of Best-Effort FIFO:**
  - Standard routers queue packets First-In, First-Out regardless of urgency.
  - Life-critical evacuation orders get stuck behind hundreds of civilian packets, experiencing high delay and buffer drop.
- **The Core Question:**
  - How can we guarantee immediate delivery of emergency alerts across a congested bottleneck link without adding physical bandwidth?

---

## Slide 3: Objectives
1. Implement an asymmetric bottleneck network topology in **ns-3.49**.
2. Induce persistent congestion by offering **1.4 Mbps** of traffic across a **1.0 Mbps bottleneck** (40% overload).
3. Implement Differentiated Services (DiffServ) using IPv4 Type of Service (TOS `0x10`) and `PrioQueueDisc`.
4. Quantitatively evaluate Baseline (FIFO) vs. Priority (`PrioQueueDisc`) using `FlowMonitor`.
5. Prove that priority scheduling minimizes emergency delay while maintaining 100% link utilization.

---

## Slide 4: Network Topology & Architecture
- **Ingress Sources (Access Links: 10 Mbps, 1 ms delay):**
  - **Node 0 (Emergency Source):** 200 Kbps, TOS `0x10` (Low Delay)
  - **Node 1 (Ordinary Source 1):** 600 Kbps, TOS `0x00` (Best Effort)
  - **Node 2 (Ordinary Source 2):** 600 Kbps, TOS `0x00` (Best Effort)
- **Intermediate Gateway (Node 3 Router):**
  - Connects the 3 access subnets to the destination server.
  - Hosts the Traffic Control Queue Discipline on its outgoing interface.
  - NetDevice queue size set to **1 packet (`1p`)** to enforce queueing in the Traffic Control layer.
- **Destination (Node 4 Response Centre):**
  - Listens on distinct UDP ports: `9000` (Emergency), `9001` (Ordinary 1), `9002` (Ordinary 2).
- **Bottleneck Link:** 1.0 Mbps, 10 ms delay.

---

## Slide 5: The Priority Pipeline (End-to-End Classification)
1. **Application Layer:**
   - Emergency `OnOffApplication` sets attribute `"Tos" = 0x10`.
2. **Socket Layer:**
   - `Socket::SetIpTos(0x10)` invokes `Socket::IpTos2Priority(0x10)`.
   - Maps to **Socket Priority 6** (`NS3_PRIO_INTERACTIVE`).
   - Socket attaches `SocketPriorityTag(6)` to packet.
3. **Gateway Router Forwarding:**
   - Router preserves TOS byte from IPv4 header and re-tags `SocketPriorityTag(6)`.
4. **Traffic Control (`PrioQueueDisc`):**
   - Priomap maps Priority 6 $\rightarrow$ **Band 0 (Highest Priority)**.
   - Priomap maps Priority 0 $\rightarrow$ **Band 1 (Normal Priority)**.
   - Dequeue policy: Band 0 is drained before Band 1 is ever served.

---

## Slide 6: Experimental Results Summary
*All numbers measured over 10 independent simulation runs (Mean +/- 95% Confidence Interval):*

| Performance Metric | Baseline (FIFO) | Priority (`PrioQueueDisc`) | Impact / Change |
| :--- | :---: | :---: | :--- |
| **Emergency Mean Delay** | **231.60 +/- 0.21 ms** | **22.26 +/- 0.02 ms** | **-90.4% (10x Latency Drop)** |
| **Emergency Jitter** | 1.65 +/- 0.04 ms | 1.41 +/- 0.02 ms | -14.5% (Low jitter) |
| **Emergency PDR** | 53.42 +/- 1.18% | **100.00 +/- 0.00%** | +87.2% (100% Reliable Delivery) |
| **Emergency Throughput** | 111.93 +/- 2.49 Kbps | **210.92 +/- 0.34 Kbps** | Full line rate preserved |
| **Ordinary Msg 1 Delay** | 232.18 +/- 0.11 ms | 288.93 +/- 0.22 ms | +24.4% (Queued behind emergency) |
| **Ordinary Msg 2 Delay** | 232.17 +/- 0.07 ms | 288.87 +/- 0.10 ms | +24.4% (Queued behind emergency) |
| **Ordinary Msg 1 PDR** | 70.63 +/- 0.65% | 63.15 +/- 0.50% | Statistically identical contention |
| **Ordinary Msg 2 PDR** | 70.23 +/- 0.45% | 62.17 +/- 0.47% | Statistically identical contention |
| **Emergency Queue Drops**| 659.3 +/- 16.9 pkts | **0 pkts** | Protected in Band 0 |
| **Ordinary Queue Drops** | 2510.4 +/- 26.6 pkts | **3169.6 +/- 10.5 pkts** | Absorbs 100% of buffer drops |

---

## Slide 7: Chart 1 — End-to-End Delay Analysis
- **Key Observation:**
  - Emergency Alert delay drops from **231.60 ms** to **22.26 ms** (**90.4% reduction**).
  - In FIFO mode, emergency alerts sat in a 50-packet queue behind civilian traffic ($50 \times 4.1\text{ ms} \approx 205\text{ ms}$ queueing delay).
  - In Priority mode, the router serves Band 0 immediately upon packet arrival.
  - Minimum delay ($D_{\text{min}} = 15.77\text{ ms}$) + residual in-flight frame (mean $2.17\text{ ms}$, max $4.34\text{ ms}$) + committed 1p device queue packet ($4.34\text{ ms}$) explains the mean $22.28\text{ ms}$ (max $24.4\text{ ms}$).

---

## Slide 8: Chart 2 — Reliability & Packet Loss
- **Key Observation:**
  - Emergency Alert achieves **100.0% PDR** and **zero drops** in Band 0.
  - The shared bottleneck was 40% overloaded ($1.4\text{ Mbps} > 1.0\text{ Mbps}$).
  - Ordinary message flows absorbed 100% of buffer drops (3,170 packets dropped in Band 1; 3169.6 drops).
  - Proves that QoS protects critical traffic while containing congestion impact within non-critical flows.

---

## Slide 9: Chart 3 — Throughput & Network Efficiency
- **Key Observation:**
  - Emergency Alert throughput remained stable at **210.9 Kbps** (100% of offered rate).
  - Total delivered throughput across the bottleneck link was **997.7 Kbps** (~99.8% link utilization).
  - Priority queueing is work-conserving: it reorganized packet order without wasting any bottleneck bandwidth.

---

## Slide 10: Trade-Off Analysis (Why Priority is Not Magic)
- **QoS does not create physical bandwidth; it enforces scheduling discipline:**
  1. **Ordinary Delay Penalty:** Ordinary messages experienced a +57 ms (+24.8%) increase in latency because the gateway router pauses Band 1 whenever Band 0 contains packets.
  2. **Selective Packet Dropping:** Band 1 buffer filled to capacity and dropped excess packets, while Band 0 remained protected.
- **Engineering Conclusion:** The trade-off is optimal for disaster scenarios: non-critical messaging tolerates delays, while life safety alerts require immediate delivery.

---

## 11. Slide 11: Demo Architecture & Automation
- **Single Master Script:** `./run-experiments.sh`
  1. Compiles C++ code (`./ns3 build smart-emergency-priority`)
  2. Executes Baseline FIFO simulation (`--priority=0 --anim=1`)
  3. Executes Priority simulation (`--priority=1 --anim=1`)
  4. Generates comparison tables (`comparison-summary.csv`)
  5. Executes Python analysis script (`reports/analyze-results.py`)
  6. Generates PNG figures & interactive HTML dashboard in `reports/figures/`
  7. Generates NetAnim XML traces (`animation-priority.xml`)

---

## 12. Slide 12: Conclusion & Future Scope
- **Conclusion:**
  - Implemented and proven in ns-3.49: network-layer DiffServ priority queueing successfully shields emergency communications from extreme civilian congestion.
  - Slashed alert latency by **90.3%** and achieved **100% delivery reliability**.
- **Future Enhancements:**
  - Implement Deficit Round Robin (DRR) to prevent ordinary starvation under runaway high-priority load.
  - Port to IEEE 802.11p / 802.11bd ad-hoc vehicular disaster networks.
  - Add dynamic routing failover across redundant satellite or microwave backhaul links.

---

## Slide 13: Q&A / Thank You
- **Smart Emergency Alert and Priority Messaging Network**
- Questions & Discussions Welcome.
