# Smart Emergency Alert and Priority Messaging Network (SEAN)
## Comprehensive Viva Voce Defense Guide & Technical Q&A (38 Master Questions)
### Simulator Platform: Network Simulator 3 (ns-3.49) | Workspace: `~/Downloads/ns-3.49`

---

## PART 1: CORE SIMULATION CONCEPTS & SYSTEM ARCHITECTURE

### Q1: What is the main objective of your SEAN project?
**Answer:** The objective of the **Smart Emergency Alert and Priority Messaging Network (SEAN)** project is to build and evaluate an IP network Quality of Service (QoS) system that protects life-critical emergency messages during acute network congestion. When disaster strikes, civilian messaging surges by 300% to 500%, flooding routers and causing standard FIFO queues to drop evacuation alerts and delay them by hundreds of milliseconds. Our project implements Differentiated Services (DiffServ) and priority queueing in **ns-3.49** to guarantee that emergency alerts arrive with 100% reliability and minimum physical delay, while measuring the exact performance cost absorbed by ordinary civilian traffic.

---

### Q2: Why did you use ns-3.49 instead of Cisco Packet Tracer or Mininet?
**Answer:**
1. **Packet Tracer** is an educational drag-and-drop tool for learning Cisco IOS commands. It models network behavior at a very high level, but it cannot simulate low-level queuing disciplines, custom socket options, or discrete packet drops with microsecond accuracy.
2. **Mininet** runs real Linux network namespaces. While great for software-defined networking, Mininet’s timings depend on host CPU load and operating system scheduling, which causes random timing noise.
3. **ns-3.49** is a discrete-event simulator written in C++. It simulates the entire network stack down to individual bytes and packet headers on an exact simulated clock. It provides **100% reproducible experiments** using controllable random seeds (`RngSeedManager`).

---

### Q3: Why did you choose a wired point-to-point network rather than Wi-Fi or cellular?
**Answer:** In wireless networks, packet loss and delay are heavily influenced by radio frequency noise, signal attenuation, hidden terminals, and CSMA/CA contention backoff. We chose a wired point-to-point topology to isolate **pure IP-layer queuing and scheduling dynamics**. This ensures that every dropped packet and every microsecond of delay is strictly caused by router buffer overflow and queue discipline behavior, not by wireless channel flakiness.

---

### Q4: How is traffic generated in your simulation, and how do you prevent artificial synchronization?
**Answer:** Senders run a custom UDP application called `JitteredCbrApp`. Senders transmit 512-byte UDP payloads at constant bitrates (e.g., 20 kbps for emergency alerts, 40 kbps for civilian messages). If all 40 senders transmitted at exact clock ticks, packets would collide artificially at the same microsecond. To prevent this "phase lock," our application applies **$\pm 20\%$ uniform random jitter** to packet inter-arrival times:
$$T_{\text{interval}} = \text{nominal\_interval} \times (1.0 + U(-0.20, +0.20))$$
This creates realistic, staggered arrivals matching real-world asynchronous traffic.

---

### Q5: What is the difference between nominal application payload and actual wire frame size?
**Answer:**
- Application payload: **512 bytes**.
- Protocol headers: **20 bytes** for IPv4 header + **8 bytes** for UDP header = **28 bytes overhead**.
- Total physical frame: $512 + 28 = \mathbf{540\text{ bytes}}$ ($4,320\text{ bits}$).
- This introduces a $+5.47\%$ wire framing expansion:
  $$\frac{540}{512} = 1.0546875$$
A 1,000 kbps nominal application payload actually generates **$1,054.68\text{ kbps}$ on the physical wire**.

---

## PART 2: QUEUE DISCIPLINES, BUFFERBLOAT, & SCHEDULING

### Q6: What is bufferbloat, and why does it happen?
**Answer:** Bufferbloat is the excessive latency caused by oversized router buffers that remain permanently full under heavy load. When network traffic exceeds link capacity, a standard FIFO buffer absorbs packets instead of dropping them. Surfacing packets wait behind dozens of older packets. On our 1.0 Mbps link with a 50-packet buffer, queueing delay alone reaches:
$$\text{Delay} = \frac{50 \times 540 \times 8 \text{ bits}}{1,000,000 \text{ bps}} = \mathbf{216\text{ ms}}$$
Total delay exceeds $230\text{ ms}$, which is fatal for early-warning earthquake or tsunami alarms.

---

### Q7: What is the fundamental difference between FIFO and Priority Queueing?
**Answer:**
- **FIFO (`FifoQueueDisc`):** Operates a single shared drop-tail buffer. Packets are processed in strict arrival order. When the buffer is full, incoming packets are dropped regardless of importance.
- **Priority Queueing (`PrioQueueDisc`):** Divides the queue into multiple sub-queues called **bands** (Band 0 = High, Band 1 = Normal, Band 2 = Low), each with its own 50-packet capacity. The router always serves Band 0 first. Band 1 is only served when Band 0 is completely empty.

---

### Q8: Is `PrioQueueDisc` preemptive or non-preemptive? Does an emergency packet interrupt an ongoing transmission?
**Answer:** It is **non-preemptive**. Once a packet has begun transmitting onto the physical wire, it cannot be interrupted or aborted. If a 540-byte civilian packet is already transmitting when an emergency packet arrives, the emergency packet must wait for that single transmission to finish.
The maximum residual waiting time is:
$$\tau_{\text{residual}} = \frac{540 \times 8 \text{ bits}}{1,000,000 \text{ bps}} = \mathbf{4.32\text{ ms}}$$
Once the wire is free, the emergency packet is transmitted immediately before any other packet in the queue.

---

### Q9: What happens if emergency traffic itself exceeds bottleneck capacity?
**Answer:** The zero-drop guarantee holds **only while aggregate emergency offered load is less than bottleneck capacity**:
$$\sum R_{\text{emergency}} < C_{\text{bottleneck}}$$
In our 50-node network, 10 emergency sources send 20 kbps each = 200 kbps, which is well below the 1.0 Mbps link limit (20% utilization). If emergency traffic exceeded 1.0 Mbps, Band 0 itself would overflow, and lower bands would suffer 100% starvation.

---

### Q10: What is the purpose of setting NetDevice queue capacity to 1 packet (`1p`)?
**Answer:** In operating systems and ns-3, there are two queuing layers: the **Traffic Control layer** (where queue disciplines like `PrioQueueDisc` and `CoDel` run) and the **hardware device ring buffer** (NetDevice queue). If the NetDevice queue is large, packets bypass Traffic Control scheduling and queue up in the hardware FIFO ring buffer. By setting the NetDevice queue to 1 packet (`1p`), we force all queuing backlog into the Traffic Control layer, ensuring that our priority scheduler makes all scheduling decisions.

---

## PART 3: FIVE-NODE BASELINE & CONGESTION SWEEP (STUDIES 1 & 2)

### Q11: What are the key results of Study 1 (the 5-node baseline)?
**Answer:** Under 140% bottleneck load ($1.4\text{ Mbps}$ offered load vs. $1.0\text{ Mbps}$ capacity):
- **FIFO Baseline:** Emergency PDR was only **$54.16\%$** ($648.9$ drops), and delay was **$231.67\text{ ms}$**.
- **Priority Mode:** Emergency PDR reached **$100.00\%$** ($0.0$ drops), and delay dropped to **$22.28\text{ ms}$**—a **$90.38\%$ latency reduction** ($-209.39\text{ ms}$, $p < 10^{-20}$).
- **Civilian Cost:** Ordinary civilian PDR degraded from $70.28\%$ to $62.66\%$, and civilian delay increased from $232.17\text{ ms}$ to $288.66\text{ ms}$.

---

### Q12: Why was there a reported discrepancy between 54.16% and 66.22% for baseline emergency PDR?
**Answer:** The authoritative value on disk has always been **$54.16\%$** in the 5-node experiment. The figure $66.22\%$ was an isolated conversational calculation error by an earlier AI assistant who divided a 50-node reception count ($\approx 937$ received packets) by the 5-node transmission count ($1,415$ transmitted packets): $\frac{937.3}{1415.5} \times 100 = 66.22\%$. In the 50-node network at 140% load, the baseline emergency PDR is actually **$66.82\%$**. Both numbers are verified and belong to different topologies.

---

### Q13: At what offered load does packet loss and bufferbloat begin in Study 2?
**Answer:** Packet loss and bufferbloat begin at **nominal 100% load** ($1,000\text{ kbps}$ application payload). Because of the 28-byte IP/UDP header overhead, 1,000 kbps of payload generates **$1,054.68\text{ kbps}$ on the wire**—a $+5.47\%$ line overload. In FIFO, drops start at 100% load ($13.8$ drops) and delay surges from $31.8\text{ ms}$ to $198.8\text{ ms}$. Under Priority scheduling, emergency alerts remain 100% protected with zero drops and $28.53\text{ ms}$ delay across all loads.

---

## PART 4: STEADY VS. BURSTY TRAFFIC DYNAMICS (STUDY 3)

### Q14: How did you configure bursty traffic in Study 3?
**Answer:** We evaluated two traffic patterns for ordinary civilian traffic with **identical long-run average offered load ($1,200\text{ kbps}$)**:
- **Steady:** 30 sources $\times 40\text{ kbps} = 1,200\text{ kbps}$ continuously.
- **Bursty:** 30 sources sending in periodic ON/OFF bursts: $0.5\text{ s}$ ON at $80\text{ kbps}$, followed by $0.5\text{ s}$ OFF at $0\text{ kbps}$ (50% duty cycle). During the ON phase, ordinary traffic alone generates **$2,400\text{ kbps}$** (240% overload alone; 260% with emergency). Over time, the average rate is exactly $1,200\text{ kbps}$.

---

### Q15: Why does bursty FIFO appear to have higher emergency PDR than steady FIFO?
**Answer:** In bursty FIFO, time-averaged emergency PDR is **$75.26\%$**, compared to **$67.44\%$** in steady FIFO. This is because during the 0.5-second OFF intervals, ordinary traffic stops completely. The queue drains in $\approx 217\text{ ms}$, leaving the bottleneck empty for $\approx 283\text{ ms}$. Emergency packets arriving during this quiet window experience **100% admission and near-zero delay**.

---

### Q16: What is the downside of bursty FIFO for emergency alerts?
**Answer:** While average PDR is higher, **jitter explodes by +1,917%** ($5.07\text{ ms} \rightarrow \mathbf{102.25\text{ ms}}$). Delay oscillates in a severe periodic saw-tooth:
- Packets arriving during OFF intervals experience $\approx \mathbf{21.5\text{ ms}}$ delay.
- Packets arriving during ON bursts experience $\approx \mathbf{240\text{ ms}}$ delay.
This high latency variance makes bursty FIFO completely unreliable for emergency systems that require predictable timing.

---

### Q17: How does Priority Queueing handle bursty civilian traffic?
**Answer:** `PrioQueueDisc` completely isolates emergency traffic from civilian bursts. Emergency packets are enqueued in Band 0 and served first. Emergency alerts achieve **$100.00\%$ PDR, bounded $27.18\text{ ms}$ latency, and $1.89\text{ ms}$ jitter** in both steady and bursty conditions ($p < 10^{-17}$). Civilian traffic absorbs the entire burst penalty, dropping to $48.39\%$ PDR.

---

## PART 5: ACTIVE QUEUE MANAGEMENT (RED & CODEL) (STUDY 4)

### Q18: What is RED, and what parameters did you configure?
**Answer:** Random Early Detection (RED) is an active queue management algorithm that drops packets probabilistically based on average queue size $\bar{q}$.
Our configuration:
- `MinTh = 5 packets`: below this, no packets are dropped.
- `MaxTh = 15 packets`: above this, all incoming packets are dropped.
- Queue weight $w_q = 0.002$ and maximum drop probability $p_{\max} = 0.10$.

---

### Q19: What is CoDel, and how does it differ from RED?
**Answer:** Controlled Delay (CoDel) measures **packet sojourn time** (time spent waiting in queue) rather than queue length.
- Parameters: `target = 5 ms`, `interval = 100 ms`.
- If minimum packet delay over the 100 ms window exceeds 5 ms, CoDel assumes persistent bufferbloat, enters dropping state, and drops packets at an increasing rate until delay drops below 5 ms. Unlike RED, CoDel requires no manual threshold tuning.

---

### Q20: Can standalone RED or CoDel protect emergency alerts without priority scheduling?
**Answer:** **No, absolutely not.** Standalone RED and CoDel drop packets based on total queue occupancy without inspecting packet priority.
- Under 140% load, RED emergency PDR is **$65.85\%$** (statistically identical to FIFO $66.82\%$, $p = 0.184$).
- CoDel emergency PDR is **$65.87\%$** ($p = 0.183$).
While CoDel succeeds in cutting delay to $22.25\text{ ms}$, it drops emergency alerts at the exact same rate as ordinary civilian messages.

---

### Q21: What is Hybrid Prio-CoDel, and why is it the optimal queue discipline?
**Answer:** Hybrid Prio-CoDel combines strict priority scheduling with CoDel active queue management:
- **Band 0 (High Priority):** FIFO queue discipline dedicated to Emergency Alerts.
- **Bands 1 & 2 (Lower Priorities):** CoDel queue disciplines for civilian traffic.
**Results:** Emergency Alerts achieve **$100.00\%$ PDR, zero drops, and $28.71\text{ ms}$ latency** in Band 0, while CoDel in lower bands eliminates $176.25\text{ ms}$ of bufferbloat latency for civilian traffic ($237.00\text{ ms} \rightarrow 60.75\text{ ms}$).

---

## PART 6: DSCP CLASSIFICATION & PRIORITY MAPPING (STUDY 5)

### Q22: What is the difference between DSCP and the IPv4 TOS octet?
**Answer:**
- **DSCP (Differentiated Services Code Point):** A 6-bit value ($0 \dots 63$) defining Per-Hop Behavior (RFC 2474).
- **IPv4 TOS/DS Field:** An 8-bit byte. The 6 DSCP bits occupy the **high-order 6 bits**, and the 2 ECN bits occupy the **low-order 2 bits**.
- To set DSCP $D$ on an application socket without ECN, the value must be shifted left by 2 bits:
  $$\text{TOS Byte} = D \ll 2 = D \times 4$$

---

### Q23: What are the exact hex and decimal byte values for EF, AF41, BE, and CS6?
**Answer:**
1. **Best Effort (BE):** DSCP 0 $\rightarrow 0 \ll 2 = 0 =$ **`0x00`**.
2. **Legacy Baseline:** TOS `0x10` $\rightarrow$ RFC 1349 Low-Delay / DSCP 4.
3. **Assured Forwarding (AF41):** DSCP 34 $\rightarrow 34 \ll 2 = 136 =$ **`0x88`**.
4. **Expedited Forwarding (EF):** DSCP 46 $\rightarrow 46 \ll 2 = 184 =$ **`0xB8`**.
5. **Class Selector 6 (CS6):** DSCP 48 $\rightarrow 48 \ll 2 = 192 =$ **`0xC0`**.

---

### Q24: What happens if you forget to shift DSCP 46 by 2 bits and write raw 46 (`0x2E`) to the socket?
**Answer:** It causes **catastrophic priority inversion**. In hex, $46 = \text{0x2E}$. When ns-3 translates `0x2E` via `Socket::IpTos2Priority`, `0x2E & 0x1e = 14`, which shifts to 7 and maps to Priority 2 (`BULK`). Under the default Priomap, Priority 2 is routed to **Band 2 (bulk queue)**. Meanwhile, ordinary traffic goes to Band 1. Emergency alerts are placed in the lowest priority queue and starved: emergency PDR collapses to **$1.10\%$** ($1,329.9$ dropped packets).

---

### Q25: How does ns-3.49 translate the TOS byte into internal socket priority?
**Answer:** Through `Socket::IpTos2Priority(uint8_t ipTos)` in `src/network/model/socket.cc`:
```cpp
uint8_t Socket::IpTos2Priority(uint8_t ipTos) {
    uint8_t prio = NS3_PRIO_BESTEFFORT;
    ipTos &= 0x1e;
    switch (ipTos >> 1) { ... }
}
```
The bitmask `ipTos & 0x1e` masks only bits 1, 2, 3, and 4. It discards the top 3 precedence bits (bits 5–7).

---

### Q26: Why does Class Selector 6 (CS6: `0xC0`) fail to achieve priority in ns-3.49?
**Answer:** Because CS6 has byte value `0xC0` (`1100 0000` in binary). Bits 1–4 are all zero. When the function executes `0xC0 & 0x1e`, the result is **`0x00`**. The switch statement hits `case 0` and maps CS6 to **`NS3_PRIO_BESTEFFORT = 0`**, routing it to Band 1 with Best Effort traffic ($94.77\%$ PDR).

---

### Q27: What is the "Default Linux Priomap Trap" when using standard DSCP EF?
**Answer:** In ns-3 and Linux, the default Priomap is:
`"1 2 2 2 1 2 0 0 1 1 1 1 1 1 1 1"`
- DSCP EF (`0xB8`) maps to socket priority 4 (`NS3_PRIO_INTERACTIVE_BULK`).
- Array index 4 maps to **Band 1 (Normal Priority)**.
- Civilian Best Effort (`0x00`) ALSO maps to **Band 1**!
- Telemetry AF41 (`0x88`) maps to priority 2, which maps to **Band 2 (Bulk Queue)**!
**Consequence:** EF packets are trapped in Band 1 with civilian traffic ($94.77\%$ PDR, $216.58\text{ ms}$ delay), while telemetry packets are starved in Band 2 ($1.08\%$ PDR).

---

### Q28: How does the Custom DiffServ Priomap solve this problem?
**Answer:** We configure the Custom Priomap:
`"2 2 1 2 0 2 0 0 2 2 2 2 2 2 2 2"`
This explicitly routes:
- Priority 4 (EF `0xB8`) $\rightarrow$ **Band 0** (High Priority: 100.00% PDR, $28.61\text{ ms}$ delay).
- Priority 6 (Legacy TOS `0x10`) $\rightarrow$ **Band 0** (High Priority: 100.00% PDR, $28.61\text{ ms}$ delay).
- Priority 2 (AF41 `0x88`) $\rightarrow$ **Band 1** (Medium Priority: 100.00% PDR, $32.03\text{ ms}$ delay).
- Priority 0 (BE `0x00`) $\rightarrow$ **Band 2** (Low Priority: 44.57% PDR, $585.47\text{ ms}$ delay).
This achieves genuine 3-tier Quality of Service.

---

### Q29: Does packet marking alone provide any benefit in a standard FIFO router?
**Answer:** **No, zero benefit.** Comparing `fifo_diffserv` (EF emergency packets) against `fifo_be` (all best effort), the PDR difference is exactly $0.0000\text{ pp}$ and delay difference is $0.0000\text{ ms}$ on every matched seed ($p = 1.000$). A FIFO queue never inspects priority tags. Marking without priority queueing is completely inert.

---

## PART 7: STATISTICAL INFERENCE, CONSERVATION, & DROPS

### Q30: How many total simulation runs did you execute across the project?
**Answer:** Exactly **490 simulation runs**:
- Study 1 (5-node baseline): 20 runs (10 FIFO, 10 Priority).
- Study 2 (50-node congestion sweep): 100 runs (5 loads $\times$ 2 modes $\times$ 10 seeds).
- Study 3 (steady vs. bursty): 40 runs (2 traffic patterns $\times$ 2 modes $\times$ 10 seeds).
- Study 4 (queue discipline comparison): 250 runs (5 queue disciplines $\times$ 5 loads $\times$ 10 seeds).
- Study 5 (DSCP classification): 80 runs (8 conditions $\times$ 10 seeds).
All 490 runs executed with 100% success and exit code 0.

---

### Q31: What is Common Random Numbers (CRN), and why did you use paired $t$-tests?
**Answer:** Common Random Numbers means that when comparing two queueing modes (e.g. FIFO vs. Priority), both runs use the **exact same pseudo-random seed** ($r = 1 \dots 10$). This ensures that both runs experience identical packet generation times, initial start delays, and traffic arrival patterns. By evaluating the differences $D_i = X_{\text{Prio}, i} - X_{\text{FIFO}, i}$, arrival noise cancels out, allowing a **paired Student's $t$-test with 9 degrees of freedom ($df = 9$)** to detect true scheduling effects with extreme statistical significance ($p < 10^{-9}$).

---

### Q32: What is the physical packet conservation law, and did your data satisfy it?
**Answer:** The fundamental network conservation law states that every packet transmitted must either be received or lost:
$$\text{TxPackets} = \text{RxPackets} + \text{LostPackets}$$
Across all 18,860 flow records in all 490 simulation runs, there were **zero packet conservation violations**.

---

### Q33: Why did FlowMonitor report 50 more lost packets than the queue disc drop counter in starved queues?
**Answer:** In Study 5 conditions where Band 2 was starved (`prio_default_diffserv`, `prio_default_cs6`, `prio_default_unshifted`), FlowMonitor reported `total_lost` as exactly 50 packets greater than queue disc drops ($2,755.4$ vs $2,705.4$).
**Root Cause:** The Band 2 buffer capacity is 50 packets (`MaxSize = 50p`). Because Band 1 was continuously active, Band 2 stopped dequeueing. When the simulation stopped at $T = 30.0\text{ s}$, **50 packets remained stranded inside the buffer**. FlowMonitor marks packets that never reached the destination as lost, while the queue disc drop counter only counts active discard events:
$$\text{Total Lost} (2,755.4) = \text{Queue Drops} (2,705.4) + \text{Stranded in Buffer} (50.0)$$
This accounts for every single packet down to the unit level.

---

### Q34: What is drop conservation, and why does priority queueing not eliminate total drops?
**Answer:** Priority queueing is a **capacity redistributor, not a bandwidth generator**. If a bottleneck can carry 1.0 Mbps and the offered load is 1.4 Mbps, approximately 0.4 Mbps of packets must be dropped regardless of scheduling. Priority queueing does not eliminate drops; it **reallocates all drops to lower priority queues**. In Study 1, total drops are conserved at 3,170 (FIFO) vs 3,171 (Priority). Under Priority, Band 0 drops 0 packets, while ordinary civilian queues absorb all 3,171 drops.

---

## PART 8: LIMITATIONS, REPRODUCIBILITY, & FUTURE WORK

### Q35: What are the main limitations of your simulation model?
**Answer:**
1. **Wired Bottleneck:** The simulation models wired point-to-point links. It excludes wireless phenomena (multipath fading, Doppler shift, CSMA/CA backoff).
2. **Fixed Packet Sizing:** Evaluated with 512-byte UDP payloads. Variable MTU sizes or TCP congestion control window dynamics may change buffer drain slopes.
3. **Emergency Capacity Bound:** If emergency traffic exceeds link capacity ($> 1.0\text{ Mbps}$), priority queueing cannot prevent Band 0 packet loss.

---

### Q36: How would you prevent emergency traffic from completely starving civilian traffic?
**Answer:** By implementing a **Token Bucket Filter (TBF) or rate-limiter on Band 0**. This enforces a traffic contract: emergency alerts are granted strict priority up to a committed information rate (e.g., 300 kbps). Any emergency traffic exceeding this rate is either shaped or policed, guaranteeing that at least 700 kbps remains available for civilian communication.

---

### Q37: How can your entire project be reproduced from scratch?
**Answer:** The repository includes automated shell runners for every study:
```bash
./run-experiments.sh                    # Study 1 (20 runs)
./run-congestion-experiments.sh         # Study 2 (100 runs)
./run-steady-bursty-experiments.sh      # Study 3 (40 runs)
./run-queue-disc-experiments.sh         # Study 4 (250 runs)
./run-dscp-classification-experiments.sh # Study 5 (80 runs)
```
Each runner compiles the corresponding C++ script, executes the seeds, generates a manifest CSV, and launches Python analysis scripts that regenerate all summary tables and 29 high-resolution figures.

---

### Q38: What are the top three takeaways you want the examination committee to remember?
**Answer:**
1. **Priority Scheduling Is Mandatory:** Best-effort FIFO and standalone AQM (RED/CoDel) fail to protect emergency alerts during congestion. Non-preemptive priority queueing delivers $100.00\%$ delivery and bounded $\approx 28.6\text{ ms}$ latency under 140% overload.
2. **Hybrid Prio-CoDel Is the Optimal Architecture:** Strict priority in Band 0 protects emergency alerts, while CoDel in lower bands eliminates $176\text{ ms}$ of bufferbloat for civilian cross-traffic.
3. **DSCP Requires Classifier Configuration:** Packet marking alone does nothing. Furthermore, default Linux Priomaps trap standard DSCP EF in Band 1; deploying a **Custom Priomap** is strictly required to achieve genuine multi-tier Quality of Service.
