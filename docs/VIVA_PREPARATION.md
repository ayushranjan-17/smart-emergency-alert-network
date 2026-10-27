# Smart Emergency Alert and Priority Messaging Network
## Comprehensive Viva Preparation Guide & Technical Q&A

**Course:** B.Tech Network Programming (NWP) Laboratory / Mini-Project  
**Simulator Platform:** ns-3 (Version 3.49)  
**Deliverable File:** [`reports/VIVA_PREPARATION.md`](reports/VIVA_PREPARATION.md)

---

## Part 1: Fifteen Likely Viva Questions & Concise Answers

### Q1: What is the primary objective of your project?
**Answer:** The objective is to design, simulate, and validate a network-layer Quality of Service (QoS) mechanism using Differentiated Services (DiffServ) and priority queueing (`PrioQueueDisc`) in ns-3.49. It ensures life-critical emergency alerts experience near-zero queueing delay and zero packet drops during severe network congestion, while ordinary civilian traffic absorbs the required backlog.

---

### Q2: Why did you choose ns-3 instead of other simulators like Cisco Packet Tracer or Mininet?
**Answer:** ns-3 is a high-fidelity, discrete-event network simulator written in C++ that models the complete protocol stack down to packet-by-packet, queue-by-queue execution. Packet Tracer is a high-level educational emulator without true queue discipline manipulation, while Mininet uses OS-level Linux network namespaces which introduce variable host scheduling latencies. ns-3 allows 100% deterministic, reproducible experimentation.

---

### Q3: What is the difference between FIFO queueing and Priority Queueing?
**Answer:**
- **FIFO (First-In, First-Out):** Maintains a single queue. All packets are serviced strictly in arrival order. High-priority packets must wait behind low-priority packets already in the queue.
- **Priority Queueing (`PrioQueueDisc`):** Partitions the queue into multiple priority bands (Bands 0, 1, 2). High-priority packets enter Band 0 and are dequeued first. Lower bands are only serviced when all higher-priority bands are empty.

---

### Q4: How is a packet classified as "Emergency" in your simulation?
**Answer:** At the application layer, the emergency alert application sets the IPv4 Type of Service attribute `"Tos" = 0x10` (Low Delay/Interactive). ns-3's socket layer maps TOS bits `[6:3]` to Socket Priority 6 via `Socket::IpTos2Priority()`. The socket tags the packet with `SocketPriorityTag(6)`, which `PrioQueueDisc` reads to assign the packet to Band 0.

---

### Q5: How did you create network congestion?
**Answer:** We created an asymmetric bottleneck topology:
- Three ingress sources send traffic across 10 Mbps access links to a gateway router.
- The router connects to the destination server over a shared **1.0 Mbps bottleneck link**.
- The two ordinary sources generate 600 Kbps each (1.2 Mbps total), which alone exceeds the 1.0 Mbps bottleneck by 20%.
- The emergency source generates 200 Kbps, resulting in a total offered load of **1.4 Mbps (40% overload)**.

---

### Q6: What was the measured delay improvement for emergency alerts?
**Answer:** In Baseline FIFO mode, emergency alerts suffered an average delay of **230.55 ms** due to queue backlog. In Priority mode, delay dropped to **22.27 ms**—a **90.3% latency reduction** (over 10 times faster delivery).

---

### Q7: Why did ordinary traffic delay increase in Priority mode?
**Answer:** Priority queueing enforces strict preemption. Whenever an emergency packet arrives in Band 0, the gateway router immediately pauses the transmission of Band 1 (ordinary packets). Because ordinary packets must wait in queue while emergency packets jump ahead, ordinary message delay increased by +24.8% (from ~231 ms to ~288 ms).

---

### Q8: Why did you set the bottleneck NetDevice queue size to 1 packet (`1p`)?
**Answer:** In Linux and ns-3 network architecture, each NetDevice has an internal hardware/driver queue. If that driver queue is left at the default size of 100 packets, packets immediately fill the driver FIFO queue, bypassing the Traffic Control layer. Setting `MaxSize = 1p` on the NetDevice forces all waiting packets to queue inside `PrioQueueDisc`, where priority scheduling decisions actually occur.

---

### Q9: Did Priority mode drop any emergency packets?
**Answer:** No. Emergency alerts achieved **100.0% Packet Delivery Ratio (PDR)** and **zero queue drops** in Band 0. All 3,170 dropped packets occurred exclusively within Band 1 (Ordinary traffic: 3169.6 drops).

---

### Q10: Why did you use UDP instead of TCP for this simulation?
**Answer:**
1. Real-world emergency broadcasts and short telemetry alerts typically use UDP to avoid connection setup latency (3-way handshake) and head-of-line blocking.
2. TCP congestion control (AIMD/CUBIC) would dynamically throttle sender rates upon detecting packet drops, obscuring the raw queueing and drop behavior of the network bottleneck. UDP allows us to evaluate pure network-layer queueing under constant, sustained overload.

---

### Q11: What is the theoretical delay breakdown in Priority mode?
**Answer:** All packets have 512 B payload (542 B on the wire = 4,336 bits):
- Access Link (10 Mbps): $D_{\text{prop}} = 1.0\text{ ms}$, $T_{\text{tx}} = 0.4336\text{ ms}$.
- Bottleneck Link (1 Mbps): $D_{\text{prop}} = 10.0\text{ ms}$, $T_{\text{tx}} = 4.336\text{ ms} \approx 4.34\text{ ms}$.
- Theoretical minimum transit delay: $D_{\text{min}} = 1.0 + 0.4336 + 10.0 + 4.336 = 15.77\text{ ms}$.
- Priority delay = $D_{\text{min}} + 4.34\text{ ms}$ (packet committed in the 1p device queue) $+ 2.17\text{ ms}$ mean residual (packet in transmission, max 4.34 ms) = ~22.28 ms mean, 24.4 ms max.
Empirical measurements match this: emergency alert mean delay is 22.26 ms (bounded at <= 24.45 ms).

---

### Q12: How does `FlowMonitor` track packets across the network?
**Answer:** `FlowMonitor` installs measurement probes at the network layer of every node. When a packet is transmitted, forwarded, or received, probes inspect IP headers, packet UIDs, and timestamps. At simulation completion, `FlowMonitor` aggregates per-flow statistics including Tx/Rx packets, lost packets, delay sums, and jitter histograms.

---

### Q13: What is Jitter, and how did your priority mechanism affect it?
**Answer:** Jitter (defined by RFC 3393) is the statistical variation in packet transit delay. Emergency alerts maintained a low, stable jitter of **1.41 ms** (vs 1.65 ms in baseline), whereas ordinary messages experienced increased jitter (rising from ~1.82 ms up to ~2.90–2.91 ms) because their dequeuing was repeatedly interrupted by emergency packet arrivals.

---

### Q14: What is the command to run your complete experiment suite?
**Answer:** We created an automated script [`run-experiments.sh`](file:///Users/ravo/Downloads/ns-3.49/run-experiments.sh), which compiles the C++ code, runs both FIFO baseline and Priority modes, generates XML and CSV outputs, and automatically executes the Python analysis script to produce all charts in `reports/figures/`.

---

### Q15: Is this system deployed in real-world emergency dispatching?
**Answer:** No. This is an academic discrete-event network simulation designed to study network scheduling and queue dynamics. In real-world networks, emergency communication also depends on physical infrastructure resilience, power backup, wireless spectrum availability, and authentication protocols.

---

## Part 2: Five Challenging Questions for Viva & Defense

### Challenge 1 (Queueing Theory): What happens if the emergency alert traffic itself exceeds the bottleneck link capacity?
**Answer:**
In strict priority queueing (`PrioQueueDisc`), Band 0 is drained unconditionally before Band 1 is ever served. If emergency traffic offered load were to exceed the bottleneck capacity (e.g., emergency rate increased to 1.2 Mbps on a 1.0 Mbps link):
1. Band 0 itself would experience queue backlog, and emergency packets would suffer tail-drop within Band 0 once the 50-packet limit is reached.
2. Band 1 (Ordinary traffic) would experience **complete starvation** (0% throughput, 100% loss) because the router would never find Band 0 empty.
To prevent this in production networks, routers deploy **Class-Based Weighted Fair Queueing (CBWFQ)** or **Deficit Round Robin (DRR)**, which guarantee emergency traffic a minimum dedicated bandwidth while allocating a small non-zero share to prevent ordinary traffic starvation.

---

### Challenge 2 (Transport Layer): In the single-run test, Ordinary Source 1 and Ordinary Source 2 originally showed huge PDR asymmetry. Why did randomizing start times not fully solve it, and how did you finally fix it?
**Answer:**
Both ordinary sources transmit at 600 Kbps (1 packet every 6.82 ms nominal). In a discrete-event simulator with constant bit rate (CBR) traffic and a drop-tail FIFO queue, packets experience **Phase Synchronization**:
- Merely randomizing the start time offset only sets a constant phase shift; the CBR sources remain strictly periodic thereafter. Once deterministic lockstep phase is established in a run, one interface consistently claims buffer slots when they open, causing the other flow to take all tail-drops (e.g. 100% vs 40.9% PDR).
- **The True Resolution:** We broke the source periodicity by introducing **per-packet random jitter** ($\pm 20\%$) or exponential inter-packet gaps across all three sources.
- With per-packet desynchronization, packet arrival intervals continuously vary independently. Within every single run, Ordinary 1 and Ordinary 2 PDR differ by **less than 5 percentage points** (measured delta $\approx 0.5\%$), and across 10 runs confidence intervals are tightly bounded.

---

### Challenge 3 (Queueing Theory): What is the theoretical delay breakdown of your topology, and how do you calculate the priority emergency delay?
**Answer:**
1. **Theoretical Minimum ($D_{\text{min}} = 15.77\text{ ms}$):**
   - Application payload: 512 bytes + UDP (8 B) + IP (20 B) + PPP (2 B) = 542 bytes on the wire (4,336 bits).
   - Access link ($10\text{ Mbps}, 1\text{ ms}$): Serialization = $0.4336\text{ ms}$, Propagation = $1.0\text{ ms}$.
   - Bottleneck link ($1\text{ Mbps}, 10\text{ ms}$): Serialization = $4.336\text{ ms} \approx 4.34\text{ ms}$, Propagation = $10.0\text{ ms}$.
   - Absolute minimum transit time = $1.0 + 0.4336 + 10.0 + 4.336 = 15.77\text{ ms}$.
2. **Priority Delay Derivation ($D_{\text{min}} = 15.77\text{ ms}$, Mean $22.28\text{ ms}$, Max $24.4\text{ ms}$):**
   - Packet transmission across physical interfaces is strictly **non-preemptive**: once serialization begins on the 1 Mbps channel, transmission cannot be interrupted.
   - All packets in the network are 512 B payload (542 B on wire).
   - When an emergency packet arrives at Gateway Node 3, it waits for:
     a) **Residual of the packet in transmission:** Mean residual = $\frac{4.34}{2} = 2.17\text{ ms}$, Max residual = $4.34\text{ ms}$.
     b) **The one ordinary packet committed in the 1-packet device queue:** The NetDevice hardware driver queue capacity is set to `1p`. One ordinary packet has already been dequeued from the Traffic Control layer and committed into this buffer; it cannot be preempted, taking exactly $4.34\text{ ms}$ of serialization.
   - **Total Priority Delay:**
     - $D_{\text{min}} = 15.77\text{ ms}$
     - Mean priority delay = $15.77 + 2.17 + 4.34 = 22.28\text{ ms}$
     - Maximum priority delay = $15.77 + 4.34 + 4.34 = 24.45\text{ ms} \approx 24.4\text{ ms}$
   - Empirical measurements under Priority mode match this precisely (mean $\approx 22.26\text{ ms}$, max $\le 24.45\text{ ms}$), as proven by the empirical Delay CDF.
   - Under FIFO baseline mode, emergency alerts wait behind ordinary packets in the 50-packet queue disc ($\approx 232\text{ ms}$ delay), and emergency loss depends directly on source phase alignment and queue tail-drop timing, reported as measured.

---

### Challenge 3 (Routing & Forwarding): When Node 3 forwards an IP packet, does it retain the original socket tag, or does it re-classify the packet?
**Answer:**
In ns-3, socket tags (`SocketPriorityTag`) are ephemeral and stripped when packets exit the originating host's net device. When intermediate router Node 3 receives the packet:
1. `Ipv4L3Protocol::Receive()` decapsulates the packet and looks up the routing table.
2. Inside `Ipv4L3Protocol::SendRealOut()`, ns-3 explicitly preserves the IPv4 header's Type of Service byte (`ipHeader.GetTos()`).
3. ns-3 calls `uint8_t priority = Socket::IpTos2Priority(ipHeader.GetTos());`.
4. If priority is non-zero, it re-creates and attaches a new `SocketPriorityTag` to the forwarded packet before passing it down to the Traffic Control layer of the outgoing bottleneck interface.
This confirms that DiffServ prioritization works hop-by-hop across intermediate routers without requiring stateful reservations.

---

### Challenge 4 (Performance Measurement): Why does FlowMonitor report 6 flows in its XML output when your simulation only has 3 sender applications?
**Answer:**
`FlowMonitor` classifies flows using the IP 5-tuple: `(Source IP, Destination IP, Protocol, Source Port, Destination Port)`.
In our simulation, there are 3 forward UDP application flows:
- `10.1.1.1 -> 10.1.4.2` (Port 9000)
- `10.1.2.1 -> 10.1.4.2` (Port 9001)
- `10.1.3.1 -> 10.1.4.2` (Port 9002)
The additional 3 flows recorded in `FlowMonitor` represent reverse routing control or ICMP probes / probe-tracking structures initialized during network setup. In our C++ analysis loop, we filter strictly for `tuple.destinationAddress == serverIp` and destination ports `9000`, `9001`, and `9002` to isolate the exact forward application telemetry.

---

### Challenge 5 (Architecture & Standards): How does your TOS byte `0x10` relate to modern DiffServ DSCP (Differentiated Services Code Point) standards?
**Answer:**
Historically, RFC 791 defined the IPv4 TOS octet using 3 bits for Precedence and 4 bits for Type of Service (Low Delay, High Throughput, High Reliability). TOS value `0x10` corresponds to the "Low Delay" bit (bit 4) set.
RFC 2474 updated the TOS byte into the **6-bit Differentiated Services Code Point (DSCP)** field:
- A TOS byte of `0x10` (`00010000` in binary) shifts the upper 6 bits to DSCP value `000100` (binary 4), known as **CS2 (Class Selector 2)** or low-latency interactive class.
ns-3's `Socket::IpTos2Priority()` maintains backwards compatibility by parsing both classic TOS bitmasks and RFC 2474 DiffServ byte mappings to determine socket priority bands.
