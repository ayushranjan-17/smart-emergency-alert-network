# Smart Emergency Alert and Priority Messaging Network
## 5 to 7 Minute College Presentation & Demonstration Script

**Target Audience:** Professor, Project Guide, External Examiner  
**Estimated Time:** 6 Minutes (4 mins talk + 2 mins live demo/questions)  
**Deliverable File:** [`reports/DEMO_SCRIPT.md`](reports/DEMO_SCRIPT.md)

---

### Minute 0:00 – 1:00 | Introduction & The Real-World Problem
*(Open Presentation Slide 1 or Slide 2)*

> "Respected Guide and Examiners, good morning.  
> Today, I am presenting my B.Tech Network Programming project titled **'Smart Emergency Alert and Priority Messaging Network'**, implemented and evaluated in **ns-3.49**.
>
> During major public disasters—such as earthquakes, cyclones, or floods—communication infrastructure faces a critical paradox: civilian messaging spikes dramatically, creating severe network congestion, while at the exact same moment, first responders and disaster management authorities must broadcast life-critical evacuation orders.
>
> In default Best-Effort networks, routers use **First-In, First-Out (FIFO)** queueing. Emergency alerts are treated no differently than casual social media messages. As a result, critical evacuation notices get trapped behind hundreds of non-critical packets, experiencing hundreds of milliseconds of delay or getting dropped during buffer overflow.
>
> Our objective is to design a network-layer Quality of Service mechanism using **Differentiated Services (DiffServ)** and **Priority Queueing (`PrioQueueDisc`)** in ns-3.49 to guarantee near-zero queueing delay and 100% packet delivery for emergency alerts during severe bottleneck congestion."

---

### Minute 1:00 – 2:00 | Architecture & Congestion Setup
*(Switch to Slide 3 or Slide 4)*

> "To evaluate this, we constructed a 5-node topology in ns-3:
> - **Node 0** is the Emergency Source, sending urgent alerts.
> - **Nodes 1 and 2** are Ordinary Sources, generating civilian message traffic.
> - **Node 3** is the Intermediate Gateway Router, which aggregates traffic over 10 Mbps access links and routes it across a shared **1.0 Mbps bottleneck link** to **Node 4**, the Emergency Response Centre.
>
> To model realistic overload:
> - Ordinary sources transmit at 600 Kbps each—which alone equals 1.2 Mbps, exceeding the 1.0 Mbps bottleneck by 20%.
> - The Emergency source transmits at 200 Kbps.
> - Total offered load is **1.4 Mbps**, creating a continuous **40% overload** on the bottleneck link.
>
> Crucially, at the gateway router, we configured the network device driver queue to just **1 packet (`1p`)**. This decouples hardware buffers and ensures all buffer queueing decisions occur strictly inside the ns-3 Traffic Control layer."

---

### Minute 2:00 – 3:30 | Priority Classification Pipeline & Live Demo
*(Switch to Terminal / Run Live Demo)*

> "Now, I will trigger our automated test runner:
> ```bash
> ./run-experiments.sh
> ```
> *(Point to terminal output)*
>
> While it runs, notice the classification pipeline:
> 1. At the application layer, the Emergency alert sets the IPv4 Type of Service byte to `0x10`.
> 2. The ns-3 socket layer maps TOS `0x10` to **Socket Priority 6**.
> 3. The gateway router preserves this TOS tag during IP forwarding.
> 4. At the bottleneck interface, ns-3's **`PrioQueueDisc`** maps Priority 6 to **Band 0 (Highest Priority)**, while ordinary messages map to **Band 1**.
> 5. The router operates on strict priority: Band 0 is drained before any packet in Band 1 is eligible for transmission."

---

### Minute 3:30 – 5:00 | Results Breakdown & Discussion
*(Open Slide 5, comparison-summary.csv, or reports/figures/delay_comparison.png)*

> "Let us examine the empirical results measured by `FlowMonitor`:
> 
> 1. **End-to-End Delay:**
>    - In Baseline FIFO mode, emergency alerts waited behind ordinary packets, suffering **231.60 ms** average latency and severe loss (~53.4% PDR, 659.3 lost packets).
>    - In Priority mode, emergency latency plummeted to **22.26 ms**—a **90.4% latency reduction**—with **100.0% PDR** and zero drops in Band 0.
>    - The ~22.28 ms mean delay matches the theoretical breakdown: minimum transit delay $D_{\text{min}} = 15.77\text{ ms}$ plus 4.34 ms committed in the 1p device queue plus 2.17 ms mean residual in-flight frame.
>
> 2. **Packet Delivery Ratio & Loss:**
>    - Emergency alerts achieved **100.0% PDR** with **zero drops in Band 0**.
>    - Meanwhile, ordinary traffic absorbed the congestion penalty: ordinary latency increased by +24.4% (232 ms $\rightarrow$ 289 ms), and Band 1 absorbed all 3,170 buffer overflow drops (3169.6 drops).
>
> 3. **Channel Efficiency:**
>    - Total delivered throughput was 997.7 Kbps across the 1.0 Mbps link. Priority queueing reorganized packets without wasting any channel capacity."

---

### Minute 5:00 – 6:00 | Conclusion & Q&A
*(Switch to Slide 10)*

> "To conclude:
> - We proved in ns-3.49 that network-level priority queueing successfully shields emergency communications from extreme public congestion.
> - All tests, code, and figures are 100% reproducible through our single automated script.
> - In future work, we plan to extend this with Weighted Fair Queueing to avoid ordinary starvation, and evaluate the protocol over IEEE 802.11bd vehicular ad-hoc networks.
>
> Thank you, and I am now ready for your questions."

---

### Presentation Preparation Checklist
- [ ] Laptop connected to projector with Terminal and Browser open.
- [ ] Terminal directory set to `~/Downloads/ns-3.49`.
- [ ] Browser tab 1: [`reports/PRESENTATION_SLIDES.html`](file:///Users/ravo/Downloads/ns-3.49/reports/PRESENTATION_SLIDES.html).
- [ ] Browser tab 2: [`reports/figures/dashboard.html`](file:///Users/ravo/Downloads/ns-3.49/reports/figures/dashboard.html).
- [ ] Terminal font size set large enough for back-row visibility.
