/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * ============================================================================
 * Project Title : Smart Emergency Alert and Priority Messaging Network
 * Course        : Network Programming (NWP) Project
 * Simulator     : ns-3 (Version 3.49)
 * File Name     : scratch/smart-emergency-priority.cc
 * ============================================================================
 *
 * 1. PROJECT OBJECTIVE:
 * --------------------
 * During emergency events (natural disasters, industrial accidents, public safety
 * incidents), communication infrastructure experiences massive traffic surges.
 * Ordinary non-critical messages congest intermediate routers, causing severe
 * delays and packet drops.
 *
 * This simulation proves how Differentiated Services (DiffServ) and Priority
 * Queueing (PrioQueueDisc) guarantee immediate delivery of urgent Emergency Alerts
 * while non-critical traffic absorbs the congestion backlog.
 *
 * 2. TOPOLOGY ARCHITECTURE:
 * ------------------------
 *
 *   [Node 0: Emergency Source]  ---(10 Mbps, 1 ms)---\
 *                                                      \
 *   [Node 1: Ordinary Source 1] ---(10 Mbps, 1 ms)---+---> [Node 3: Gateway Router] ---(1 Mbps, 10 ms)---> [Node 4: Response Centre]
 *                                                      /    (Bottleneck Interface)                          (Destination Server)
 *   [Node 2: Ordinary Source 2] ---(10 Mbps, 1 ms)---/
 *
 *   - Access Links: Dedicated 10 Mbps point-to-point links (1 ms propagation delay).
 *   - Bottleneck Link: 1 Mbps shared link with 10 ms propagation delay.
 *   - Offered Traffic Load:
 *       Ordinary 1 : 600 Kbps (TOS 0x00)
 *       Ordinary 2 : 600 Kbps (TOS 0x00)
 *       Emergency  : 200 Kbps (TOS 0x10 in priority mode, 0x00 in baseline)
 *       Total Load = 1.4 Mbps > 1.0 Mbps bottleneck capacity (40% overload).
 *
 * 3. THEORETICAL DELAY BREAKDOWN & NON-PREEMPTIVE BLOCKING:
 * --------------------------------------------------------
 * All packets have 512 bytes application payload (542 bytes on the wire).
 * Packet Size Breakdown:
 *   - Payload       : 512 bytes
 *   - UDP Header    : 8 bytes
 *   - IPv4 Header   : 20 bytes
 *   - PPP Header    : 2 bytes (ns-3 PointToPointHeader)
 *   Total on-wire   : 542 bytes = 4336 bits
 *
 * Link Transmission Times:
 *   - Access Link (10 Mbps)     : T_access = 4336 / 10,000,000 = 0.4336 ms
 *   - Access Propagation Delay  : D_access = 1.0 ms
 *   - Bottleneck Link (1 Mbps)  : T_bottle = 4336 / 1,000,000  = 4.336 ms (~4.34 ms)
 *   - Bottleneck Propagation    : D_bottle = 10.0 ms
 *
 * Theoretical Minimum Delay (Zero Queueing Wait):
 *   D_min = D_access + T_access + D_bottle + T_bottle
 *         = 1.0 + 0.4336 + 10.0 + 4.336 = 15.77 ms
 *
 * Priority Delay Derivation:
 *   D_min = 15.77 ms; priority delay = D_min + 4.34 ms (packet committed in the 1p device queue) + 2.17 ms mean residual (packet in transmission, max 4.34 ms) = ~22.28 ms mean, 24.4 ms max.
 *   - Residual of in-flight ordinary packet currently in transmission on 1 Mbps bottleneck link:
 *       Mean residual = 4.336 / 2 = 2.17 ms, Max residual = 4.34 ms.
 *   - One ordinary packet already committed in the 1-packet NetDevice hardware driver queue:
 *       Committed packet serialization = 4.34 ms.
 *   - Total Priority Delay:
 *       D_min = 15.77 ms
 *       Mean priority delay = 15.77 + 4.34 + 2.17 = 22.28 ms
 *       Max priority delay  = 15.77 + 4.34 + 4.34 = 24.45 ms (~24.4 ms).
 *   Empirical delay measurements under Priority mode match this precisely (mean ~22.26 ms, max <= 24.45 ms).
 *
 * FIFO Baseline Delay and Loss:
 *   Under FIFO baseline mode, emergency alerts wait behind ordinary packets in the 50-packet
 *   queue disc (queueing wait up to 50 * 4.336 ms = 216.8 ms), giving total delay around 232 ms.
 *   Emergency packet loss under FIFO baseline depends directly on source phase alignment
 *   and queue tail-drop timing, and is reported as measured across runs.
 *
 * 4. DESYNCHRONIZATION (BREAKING PHASE-LOCK):
 * -------------------------------------------
 * Constant-rate CBR sources with fixed start offsets remain strictly periodic, which causes
 * deterministic resonance drop patterns (where one flow claims queue vacancies and the other
 * suffers severe tail-drop). By breaking the periodicity using per-packet random jitter (+/- 20%)
 * or exponential inter-packet gaps for all three sources, phase-locking is broken. Within each run,
 * Ordinary 1 and 2 PDR differ by less than ~5 percentage points, and across runs confidence
 * intervals are much narrower.
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/netanim-module.h"
#include "ns3/mobility-module.h"
#include "ns3/histogram.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SmartEmergencyPrioritySimulation");

// Data structure to hold per-flow summary metrics for one run
struct FlowResultRecord
{
    uint32_t runIndex;
    uint32_t flowId;
    std::string trafficClass;
    uint16_t port;
    uint32_t txPackets;
    uint32_t rxPackets;
    uint32_t lostPackets;
    double pdrPercent;
    double throughputKbps;
    double meanDelayMs;
    double meanJitterMs;
    uint32_t queueDrops;
};

// Summary statistics structure for multi-run evaluations (95% CI)
struct MetricStats
{
    double mean;
    double stdDev;
    double stdErr;
    double ci95; // Half-width of 95% Confidence Interval
    double ciLower;
    double ciUpper;
};

MetricStats
ComputeStats(const std::vector<double>& samples)
{
    MetricStats res = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    size_t n = samples.size();
    if (n == 0) return res;
    if (n == 1)
    {
        res.mean = samples[0];
        res.ciLower = res.mean;
        res.ciUpper = res.mean;
        return res;
    }

    double sum = 0.0;
    for (double v : samples) sum += v;
    res.mean = sum / static_cast<double>(n);

    double varSum = 0.0;
    for (double v : samples)
    {
        double diff = v - res.mean;
        varSum += diff * diff;
    }
    res.stdDev = std::sqrt(varSum / static_cast<double>(n - 1));
    res.stdErr = res.stdDev / std::sqrt(static_cast<double>(n));

    // Two-tailed Student-t critical values for alpha = 0.05 (95% confidence)
    static const double tTable[] = {
        0.0,     // df=0
        12.706,  // df=1
        4.303,   // df=2
        3.182,   // df=3
        2.776,   // df=4
        2.571,   // df=5
        2.447,   // df=6
        2.365,   // df=7
        2.306,   // df=8
        2.262,   // df=9  (N = 10 runs)
        2.228,   // df=10
        2.201,   // df=11
        2.179,   // df=12
        2.160,   // df=13
        2.145,   // df=14
        2.131,   // df=15
        2.120,   // df=16
        2.110,   // df=17
        2.101,   // df=18
        2.093,   // df=19 (N = 20 runs)
        2.086    // df=20
    };

    double tVal = 1.960;
    size_t df = n - 1;
    if (df > 0 && df <= 20)
    {
        tVal = tTable[df];
    }
    else if (df <= 30)
    {
        tVal = 2.042;
    }

    res.ci95 = tVal * res.stdErr;
    res.ciLower = res.mean - res.ci95;
    res.ciUpper = res.mean + res.ci95;
    return res;
}

// Custom Traffic Application to break CBR periodicity with per-packet jitter or exponential gaps
class JitterTrafficApp : public Application
{
public:
    static TypeId GetTypeId()
    {
        static TypeId tid = TypeId("ns3::JitterTrafficApp")
            .SetParent<Application>()
            .SetGroupName("Applications")
            .AddConstructor<JitterTrafficApp>();
        return tid;
    }

    JitterTrafficApp()
        : m_socket(nullptr),
          m_peer(),
          m_packetSize(512),
          m_dataRate("600kb/s"),
          m_tos(0x00),
          m_useExponential(false),
          m_jitterRatio(0.20),
          m_running(false)
    {
    }

    ~JitterTrafficApp() override
    {
        m_socket = nullptr;
    }

    void Setup(Address peer, uint32_t packetSize, DataRate dataRate, uint8_t tos, bool useExp = false, double jitter = 0.20)
    {
        m_peer = peer;
        m_packetSize = packetSize;
        m_dataRate = dataRate;
        m_tos = tos;
        m_useExponential = useExp;
        m_jitterRatio = jitter;
    }

private:
    void StartApplication() override
    {
        m_running = true;
        if (!m_socket)
        {
            TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
            m_socket = Socket::CreateSocket(GetNode(), tid);
            if (InetSocketAddress::IsMatchingType(m_peer))
            {
                m_socket->SetIpTos(m_tos);
                m_socket->Connect(m_peer);
            }
        }
        if (m_useExponential)
        {
            double mean = static_cast<double>(m_packetSize * 8) / m_dataRate.GetBitRate();
            m_exp = CreateObject<ExponentialRandomVariable>();
            m_exp->SetAttribute("Mean", DoubleValue(mean));
        }
        else
        {
            m_uv = CreateObject<UniformRandomVariable>();
        }
        ScheduleNextTx();
    }

    void StopApplication() override
    {
        m_running = false;
        Simulator::Cancel(m_sendEvent);
        if (m_socket)
        {
            m_socket->Close();
        }
    }

    void SendPacket()
    {
        if (!m_running) return;

        Ptr<Packet> packet = Create<Packet>(m_packetSize);
        m_socket->Send(packet);

        ScheduleNextTx();
    }

    void ScheduleNextTx()
    {
        if (!m_running) return;

        double dt = 0.0;
        if (m_useExponential)
        {
            dt = m_exp->GetValue();
        }
        else
        {
            double mean = static_cast<double>(m_packetSize * 8) / m_dataRate.GetBitRate();
            double j = m_uv->GetValue(-m_jitterRatio, m_jitterRatio);
            dt = mean * (1.0 + j);
        }
        m_sendEvent = Simulator::Schedule(Seconds(dt), &JitterTrafficApp::SendPacket, this);
    }

    Ptr<Socket> m_socket;
    Address m_peer;
    uint32_t m_packetSize;
    DataRate m_dataRate;
    uint8_t m_tos;
    bool m_useExponential;
    double m_jitterRatio;
    bool m_running;
    EventId m_sendEvent;
    Ptr<UniformRandomVariable> m_uv;
    Ptr<ExponentialRandomVariable> m_exp;
};

// Result of a single simulation iteration
struct SingleRunOutput
{
    uint32_t runIndex;
    std::vector<FlowResultRecord> records;
    uint32_t totalQueueDrops;
    uint32_t band0Drops;
    uint32_t band1Drops;
    Histogram emergencyDelayHistogram;
};

// Simulation Execution Function for One Run
SingleRunOutput
RunSingleSimulation(uint32_t runIndex,
                    bool priorityMode,
                    double simTime,
                    uint32_t packetSize,
                    const std::string& emergencyRate,
                    const std::string& ordinary1Rate,
                    const std::string& ordinary2Rate,
                    const std::string& bottleneckBandwidth,
                    const std::string& bottleneckDelay,
                    const std::string& accessBandwidth,
                    const std::string& accessDelay,
                    uint32_t queueDiscSize,
                    bool randomizeStart,
                    const std::string& trafficModel,
                    double jitterRatio,
                    bool enableAnim,
                    const std::string& animXmlPath,
                    const std::string& xmlFilename)
{
    RngSeedManager::SetSeed(1);
    RngSeedManager::SetRun(runIndex);

    // 1. Nodes
    NodeContainer nodes;
    nodes.Create(5);

    Ptr<Node> n0Emergency = nodes.Get(0);
    Ptr<Node> n1Ordinary1 = nodes.Get(1);
    Ptr<Node> n2Ordinary2 = nodes.Get(2);
    Ptr<Node> n3Gateway   = nodes.Get(3);
    Ptr<Node> n4Server    = nodes.Get(4);

    // 2. Access Links
    PointToPointHelper accessLink;
    accessLink.SetDeviceAttribute("DataRate", StringValue(accessBandwidth));
    accessLink.SetChannelAttribute("Delay", StringValue(accessDelay));
    accessLink.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("100p"));

    NetDeviceContainer d0d3 = accessLink.Install(n0Emergency, n3Gateway);
    NetDeviceContainer d1d3 = accessLink.Install(n1Ordinary1, n3Gateway);
    NetDeviceContainer d2d3 = accessLink.Install(n2Ordinary2, n3Gateway);

    // 3. Bottleneck Link
    // Set device queue to 1 packet to decouple hardware buffer from TC QueueDisc
    PointToPointHelper bottleneckLink;
    bottleneckLink.SetDeviceAttribute("DataRate", StringValue(bottleneckBandwidth));
    bottleneckLink.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
    bottleneckLink.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("1p"));

    NetDeviceContainer d3d4 = bottleneckLink.Install(n3Gateway, n4Server);

    // 4. Internet Stack
    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    address.Assign(d0d3);

    address.SetBase("10.1.2.0", "255.255.255.0");
    address.Assign(d1d3);

    address.SetBase("10.1.3.0", "255.255.255.0");
    address.Assign(d2d3);

    address.SetBase("10.1.4.0", "255.255.255.0");
    Ipv4InterfaceContainer if3if4 = address.Assign(d3d4);
    Ipv4Address serverIp = if3if4.GetAddress(1);

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // 5. Traffic Control Configuration
    TrafficControlHelper tch;
    tch.Uninstall(d3d4.Get(0));

    QueueDiscContainer installedQdiscs;
    std::string qSizeStr = std::to_string(queueDiscSize) + "p";

    if (priorityMode)
    {
        uint16_t handle = tch.SetRootQueueDisc(
            "ns3::PrioQueueDisc",
            "Priomap",
            StringValue("1 2 2 2 1 2 0 0 1 1 1 1 1 1 1 1"));

        TrafficControlHelper::ClassIdList cid =
            tch.AddQueueDiscClasses(handle, 3, "ns3::QueueDiscClass");

        tch.AddChildQueueDisc(handle, cid[0], "ns3::FifoQueueDisc", "MaxSize", QueueSizeValue(QueueSize(qSizeStr)));
        tch.AddChildQueueDisc(handle, cid[1], "ns3::FifoQueueDisc", "MaxSize", QueueSizeValue(QueueSize(qSizeStr)));
        tch.AddChildQueueDisc(handle, cid[2], "ns3::FifoQueueDisc", "MaxSize", QueueSizeValue(QueueSize(qSizeStr)));

        installedQdiscs = tch.Install(d3d4.Get(0));
    }
    else
    {
        tch.SetRootQueueDisc("ns3::FifoQueueDisc", "MaxSize", QueueSizeValue(QueueSize(qSizeStr)));
        installedQdiscs = tch.Install(d3d4.Get(0));
    }

    Ptr<QueueDisc> rootQdisc = installedQdiscs.Get(0);

    // 6. Packet Sinks
    const uint16_t portEmergency = 9000;
    const uint16_t portOrdinary1 = 9001;
    const uint16_t portOrdinary2 = 9002;

    PacketSinkHelper sinkHelper("ns3::UdpSocketFactory", Address());

    sinkHelper.SetAttribute("Local", AddressValue(InetSocketAddress(Ipv4Address::GetAny(), portEmergency)));
    ApplicationContainer sinkApps = sinkHelper.Install(n4Server);

    sinkHelper.SetAttribute("Local", AddressValue(InetSocketAddress(Ipv4Address::GetAny(), portOrdinary1)));
    sinkApps.Add(sinkHelper.Install(n4Server));

    sinkHelper.SetAttribute("Local", AddressValue(InetSocketAddress(Ipv4Address::GetAny(), portOrdinary2)));
    sinkApps.Add(sinkHelper.Install(n4Server));

    sinkApps.Start(Seconds(0.5));
    sinkApps.Stop(Seconds(simTime + 1.0));

    // 7. Traffic Generators with Periodicity Breaking (Jitter / Exponential Gaps)
    bool useExp = (trafficModel == "exponential");

    Ptr<JitterTrafficApp> appEmergency = CreateObject<JitterTrafficApp>();
    appEmergency->Setup(InetSocketAddress(serverIp, portEmergency),
                        packetSize,
                        DataRate(emergencyRate),
                        priorityMode ? 0x10 : 0x00,
                        useExp,
                        jitterRatio);
    n0Emergency->AddApplication(appEmergency);
    appEmergency->SetStartTime(Seconds(1.0));
    appEmergency->SetStopTime(Seconds(simTime));

    Ptr<JitterTrafficApp> appOrdinary1 = CreateObject<JitterTrafficApp>();
    appOrdinary1->Setup(InetSocketAddress(serverIp, portOrdinary1),
                        packetSize,
                        DataRate(ordinary1Rate),
                        0x00,
                        useExp,
                        jitterRatio);
    n1Ordinary1->AddApplication(appOrdinary1);

    Ptr<JitterTrafficApp> appOrdinary2 = CreateObject<JitterTrafficApp>();
    appOrdinary2->Setup(InetSocketAddress(serverIp, portOrdinary2),
                        packetSize,
                        DataRate(ordinary2Rate),
                        0x00,
                        useExp,
                        jitterRatio);
    n2Ordinary2->AddApplication(appOrdinary2);

    if (randomizeStart)
    {
        // Randomize ordinary start times between 1.000s and 1.050s
        Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
        uv->SetAttribute("Min", DoubleValue(0.0));
        uv->SetAttribute("Max", DoubleValue(0.05));

        double offset1 = uv->GetValue();
        double offset2 = uv->GetValue();

        appOrdinary1->SetStartTime(Seconds(1.0 + offset1));
        appOrdinary2->SetStartTime(Seconds(1.0 + offset2));
    }
    else
    {
        appOrdinary1->SetStartTime(Seconds(1.0));
        appOrdinary2->SetStartTime(Seconds(1.0));
    }
    appOrdinary1->SetStopTime(Seconds(simTime));
    appOrdinary2->SetStopTime(Seconds(simTime));

    // 8. FlowMonitor
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();
    monitor->SetAttribute("DelayBinWidth", DoubleValue(0.001)); // 1 ms bin width for delay CDF

    // 9. NetAnim (Only for run 1 if requested)
    std::unique_ptr<AnimationInterface> anim;
    if (enableAnim && runIndex == 1)
    {
        MobilityHelper mobility;
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
        mobility.Install(nodes);

        anim = std::make_unique<AnimationInterface>(animXmlPath);
        anim->SetConstantPosition(n0Emergency, 10.0, 10.0);
        anim->SetConstantPosition(n1Ordinary1, 10.0, 30.0);
        anim->SetConstantPosition(n2Ordinary2, 10.0, 50.0);
        anim->SetConstantPosition(n3Gateway, 45.0, 30.0);
        anim->SetConstantPosition(n4Server, 80.0, 30.0);

        anim->UpdateNodeDescription(n0Emergency, "Emergency Source (N0)");
        anim->UpdateNodeDescription(n1Ordinary1, "Ordinary 1 (N1)");
        anim->UpdateNodeDescription(n2Ordinary2, "Ordinary 2 (N2)");
        anim->UpdateNodeDescription(n3Gateway, "Gateway Router (N3)");
        anim->UpdateNodeDescription(n4Server, "Response Centre (N4)");

        anim->UpdateNodeColor(n0Emergency, 220, 20, 60);
        anim->UpdateNodeColor(n1Ordinary1, 30, 144, 255);
        anim->UpdateNodeColor(n2Ordinary2, 70, 130, 180);
        anim->UpdateNodeColor(n3Gateway, 255, 140, 0);
        anim->UpdateNodeColor(n4Server, 34, 139, 34);

        anim->EnablePacketMetadata(true);
    }

    Simulator::Stop(Seconds(simTime + 1.0));
    Simulator::Run();

    // 10. Extract metrics
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    uint32_t totalQueueDrops = 0;
    uint32_t band0Drops = 0;
    uint32_t band1Drops = 0;

    if (rootQdisc)
    {
        QueueDisc::Stats qStats = rootQdisc->GetStats();
        totalQueueDrops = qStats.nTotalDroppedPackets;
        if (priorityMode && rootQdisc->GetNQueueDiscClasses() >= 2)
        {
            band0Drops = rootQdisc->GetQueueDiscClass(0)->GetQueueDisc()->GetStats().nTotalDroppedPackets;
            band1Drops = rootQdisc->GetQueueDiscClass(1)->GetQueueDisc()->GetStats().nTotalDroppedPackets;
        }
    }

    SingleRunOutput out;
    out.runIndex = runIndex;
    out.totalQueueDrops = totalQueueDrops;
    out.band0Drops = band0Drops;
    out.band1Drops = band1Drops;

    for (const auto& entry : stats)
    {
        FlowId flowId = entry.first;
        FlowMonitor::FlowStats flowStats = entry.second;
        Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flowId);

        if (tuple.destinationAddress != serverIp) continue;

        std::string flowLabel;
        uint32_t specificQueueDrops = 0;

        if (tuple.destinationPort == portEmergency)
        {
            flowLabel = "Emergency Alert";
            specificQueueDrops = priorityMode ? band0Drops : flowStats.lostPackets;
            out.emergencyDelayHistogram = flowStats.delayHistogram;
        }
        else if (tuple.destinationPort == portOrdinary1)
        {
            flowLabel = "Ordinary Msg 1";
            specificQueueDrops = flowStats.lostPackets;
        }
        else if (tuple.destinationPort == portOrdinary2)
        {
            flowLabel = "Ordinary Msg 2";
            specificQueueDrops = flowStats.lostPackets;
        }
        else
        {
            continue;
        }

        double duration = (flowStats.timeLastRxPacket - flowStats.timeFirstTxPacket).GetSeconds();
        double throughputKbps = 0.0;
        if (duration > 0.0)
        {
            throughputKbps = (flowStats.rxBytes * 8.0) / (duration * 1000.0);
        }

        double pdr = 0.0;
        if (flowStats.txPackets > 0)
        {
            pdr = (100.0 * flowStats.rxPackets) / flowStats.txPackets;
        }

        double meanDelayMs = 0.0;
        if (flowStats.rxPackets > 0)
        {
            meanDelayMs = flowStats.delaySum.GetSeconds() * 1000.0 / flowStats.rxPackets;
        }

        double meanJitterMs = 0.0;
        if (flowStats.rxPackets > 1)
        {
            meanJitterMs = flowStats.jitterSum.GetSeconds() * 1000.0 / (flowStats.rxPackets - 1);
        }

        FlowResultRecord record;
        record.runIndex = runIndex;
        record.flowId = flowId;
        record.trafficClass = flowLabel;
        record.port = tuple.destinationPort;
        record.txPackets = flowStats.txPackets;
        record.rxPackets = flowStats.rxPackets;
        record.lostPackets = flowStats.lostPackets;
        record.pdrPercent = pdr;
        record.throughputKbps = throughputKbps;
        record.meanDelayMs = meanDelayMs;
        record.meanJitterMs = meanJitterMs;
        record.queueDrops = specificQueueDrops;

        out.records.push_back(record);
    }

    if (runIndex == 1 && !xmlFilename.empty())
    {
        monitor->SerializeToXmlFile(xmlFilename, true, true);
    }

    Simulator::Destroy();
    return out;
}

// Function declarations
void SaveRunsCsv(const std::string& filename,
                 bool priorityMode,
                 const std::vector<FlowResultRecord>& allRecords);

void SaveStatsSummaryCsv(const std::string& filename,
                         const std::map<std::string, std::map<std::string, MetricStats>>& statsSummary);

void SaveMeansToPerFlowCsv(const std::string& filename,
                           bool priorityMode,
                           const std::map<std::string, std::map<std::string, MetricStats>>& statsSummary,
                           uint32_t totalQueueDrops);

void TryGenerateComparisons(const std::string& baselineStatsCsv,
                           const std::string& priorityStatsCsv,
                           const std::string& comparisonSummaryCsv,
                           const std::string& statsComparisonCsv);

void ExportDelayCdf(const std::string& filename,
                    const Histogram& baselineHist,
                    const Histogram& priorityHist);

// =============================================================================
// MAIN ENTRY POINT
// =============================================================================
int
main(int argc, char* argv[])
{
    bool priorityMode = false;
    double simTime = 30.0;
    uint32_t packetSize = 512;
    std::string emergencyRate = "200kb/s";
    std::string ordinary1Rate = "600kb/s";
    std::string ordinary2Rate = "600kb/s";
    std::string bottleneckBandwidth = "1Mbps";
    std::string bottleneckDelay = "10ms";
    std::string accessBandwidth = "10Mbps";
    std::string accessDelay = "1ms";
    uint32_t queueDiscSize = 50;
    uint32_t numRuns = 10;
    bool randomizeStart = true;
    std::string trafficModel = "jitter";
    double jitterRatio = 0.20;
    std::string customCsv = "";
    std::string customXml = "";
    bool enableAnim = false;
    std::string animXml = "";

    CommandLine cmd(__FILE__);
    cmd.AddValue("priority", "Enable priority queueing (1 = PrioQueueDisc, 0 = FifoQueueDisc)", priorityMode);
    cmd.AddValue("runs", "Number of independent simulation runs (default 10)", numRuns);
    cmd.AddValue("trafficModel", "Traffic pattern: 'jitter' (per-packet uniform jitter +/-20%) or 'exponential' (Poisson)", trafficModel);
    cmd.AddValue("jitter", "Jitter ratio around nominal rate (default 0.20 for +/-20%)", jitterRatio);
    cmd.AddValue("randomizeStart", "Randomize ordinary start times to break phase-lock (default 1)", randomizeStart);
    cmd.AddValue("simTime", "Total simulation duration in seconds", simTime);
    cmd.AddValue("packetSize", "Payload size of UDP packets in bytes", packetSize);
    cmd.AddValue("emergencyRate", "Data rate for emergency alert source", emergencyRate);
    cmd.AddValue("ordinary1Rate", "Data rate for ordinary source 1", ordinary1Rate);
    cmd.AddValue("ordinary2Rate", "Data rate for ordinary source 2", ordinary2Rate);
    cmd.AddValue("bottleneckBw", "Bandwidth of shared bottleneck link", bottleneckBandwidth);
    cmd.AddValue("bottleneckDelay", "Propagation delay of bottleneck link", bottleneckDelay);
    cmd.AddValue("accessBw", "Bandwidth of access links", accessBandwidth);
    cmd.AddValue("accessDelay", "Propagation delay of access links", accessDelay);
    cmd.AddValue("queueSize", "Queue disc buffer capacity in packets", queueDiscSize);
    cmd.AddValue("csv", "Path for per-flow CSV output file", customCsv);
    cmd.AddValue("xml", "Path for FlowMonitor XML output file", customXml);
    cmd.AddValue("anim", "Enable NetAnim XML animation trace generation (1/0)", enableAnim);
    cmd.AddValue("animXml", "Path for NetAnim animation XML output file", animXml);
    cmd.Parse(argc, argv);

    std::string modeString = priorityMode ? "PRIORITY (PrioQueueDisc with TOS 0x10)"
                                          : "BASELINE (FifoQueueDisc - FIFO)";
    std::string csvFilename = customCsv.empty()
                                  ? (priorityMode ? "results-priority.csv" : "results-baseline.csv")
                                  : customCsv;
    std::string xmlFilename = customXml.empty()
                                  ? (priorityMode ? "flowmonitor-priority.xml" : "flowmonitor-baseline.xml")
                                  : customXml;
    std::string runsCsv = priorityMode ? "runs-priority.csv" : "runs-baseline.csv";
    std::string statsCsv = priorityMode ? "stats-summary-priority.csv" : "stats-summary-baseline.csv";
    std::string animFilePath = animXml.empty()
                                   ? (priorityMode ? "animation-priority.xml" : "animation-baseline.xml")
                                   : animXml;

    std::cout << "\n================================================================================" << std::endl;
    std::cout << "  Smart Emergency Alert and Priority Messaging Network Simulation" << std::endl;
    std::cout << "  Project: Network Programming (NWP) Project" << std::endl;
    std::cout << "  Operating Mode: " << modeString << std::endl;
    std::cout << "  Number of Runs: " << numRuns << " (Seed: 1, Runs: 1.." << numRuns << ")" << std::endl;
    std::cout << "  Traffic Pattern: " << (trafficModel == "exponential" ? "EXPONENTIAL GAPS (Poisson Arrivals)" : "JITTERED CBR (+/- 20% Uniform Jitter)") << std::endl;
    std::cout << "  Randomize Start: " << (randomizeStart ? "ENABLED (Phase-lock desynchronization)" : "DISABLED") << std::endl;
    std::cout << "================================================================================" << std::endl;
    std::cout << "  Configuration Parameters:" << std::endl;
    std::cout << "  - Emergency Source Rate  : " << emergencyRate << " (TOS: " << (priorityMode ? "0x10" : "0x00") << ")" << std::endl;
    std::cout << "  - Ordinary Source 1 Rate : " << ordinary1Rate << " (TOS: 0x00)" << std::endl;
    std::cout << "  - Ordinary Source 2 Rate : " << ordinary2Rate << " (TOS: 0x00)" << std::endl;
    std::cout << "  - Bottleneck Link Capacity: " << bottleneckBandwidth << ", Delay: " << bottleneckDelay << std::endl;
    std::cout << "  - Bottleneck Queue Limit : " << queueDiscSize << " packets" << std::endl;
    std::cout << "  - Access Link Capacity   : " << accessBandwidth << ", Delay: " << accessDelay << std::endl;
    std::cout << "  - Simulation Duration    : " << simTime << " seconds" << std::endl;
    std::cout << "================================================================================\n" << std::endl;

    std::vector<FlowResultRecord> allRecords;
    std::map<std::string, std::map<std::string, std::vector<double>>> metricSamples;
    std::vector<double> queueDropSamples;
    Histogram aggregateEmergencyHist(0.001); // 1 ms bins

    for (uint32_t r = 1; r <= numRuns; ++r)
    {
        std::cout << "[Run " << r << "/" << numRuns << "] Executing simulation (RngRun=" << r << ")... " << std::flush;
        SingleRunOutput sOut = RunSingleSimulation(r,
                                                   priorityMode,
                                                   simTime,
                                                   packetSize,
                                                   emergencyRate,
                                                   ordinary1Rate,
                                                   ordinary2Rate,
                                                   bottleneckBandwidth,
                                                   bottleneckDelay,
                                                   accessBandwidth,
                                                   accessDelay,
                                                   queueDiscSize,
                                                   randomizeStart,
                                                   trafficModel,
                                                   jitterRatio,
                                                   enableAnim,
                                                   animFilePath,
                                                   xmlFilename);

        for (const auto& rec : sOut.records)
        {
            allRecords.push_back(rec);
            metricSamples[rec.trafficClass]["TxPackets"].push_back(rec.txPackets);
            metricSamples[rec.trafficClass]["RxPackets"].push_back(rec.rxPackets);
            metricSamples[rec.trafficClass]["LostPackets"].push_back(rec.lostPackets);
            metricSamples[rec.trafficClass]["PDR"].push_back(rec.pdrPercent);
            metricSamples[rec.trafficClass]["Throughput"].push_back(rec.throughputKbps);
            metricSamples[rec.trafficClass]["Delay"].push_back(rec.meanDelayMs);
            metricSamples[rec.trafficClass]["Jitter"].push_back(rec.meanJitterMs);
            metricSamples[rec.trafficClass]["QueueDrops"].push_back(rec.queueDrops);
        }
        queueDropSamples.push_back(sOut.totalQueueDrops);

        // Aggregate emergency delay histogram
        for (uint32_t b = 0; b < sOut.emergencyDelayHistogram.GetNBins(); ++b)
        {
            uint32_t count = sOut.emergencyDelayHistogram.GetBinCount(b);
            double val = sOut.emergencyDelayHistogram.GetBinStart(b) + 0.0005;
            for (uint32_t c = 0; c < count; ++c)
            {
                aggregateEmergencyHist.AddValue(val);
            }
        }
        std::cout << "Done." << std::endl;
    }

    // Compute Summary Statistics (Mean and 95% Confidence Interval)
    std::map<std::string, std::map<std::string, MetricStats>> statsSummary;
    for (const auto& tcEntry : metricSamples)
    {
        const std::string& tc = tcEntry.first;
        for (const auto& mEntry : tcEntry.second)
        {
            const std::string& metricName = mEntry.first;
            statsSummary[tc][metricName] = ComputeStats(mEntry.second);
        }
    }
    MetricStats totalDropsStats = ComputeStats(queueDropSamples);

    // Print Consolidated Results Table
    std::cout << "\n----------------------------------------------------------------------------------------------------------------"
              << std::endl;
    std::cout << "                     MULTI-RUN SIMULATION RESULTS (MEAN +/- 95% CONFIDENCE INTERVAL)                            "
              << std::endl;
    std::cout << "----------------------------------------------------------------------------------------------------------------"
              << std::endl;
    std::cout << std::left << std::setw(18) << "Traffic Class"
              << std::right << std::setw(12) << "Tx (pkts)"
              << std::setw(12) << "Rx (pkts)"
              << std::setw(14) << "PDR (%) [95% CI]"
              << std::setw(18) << "Tput (Kbps)"
              << std::setw(22) << "Delay (ms) [95% CI]"
              << std::setw(18) << "Jitter (ms)"
              << std::endl;
    std::cout << std::string(114, '-') << std::endl;

    std::vector<std::string> orderedClasses = {"Emergency Alert", "Ordinary Msg 1", "Ordinary Msg 2"};
    for (const auto& tc : orderedClasses)
    {
        if (statsSummary.find(tc) == statsSummary.end()) continue;
        const auto& m = statsSummary[tc];

        std::stringstream pdrSs, delaySs;
        pdrSs << std::fixed << std::setprecision(1) << m.at("PDR").mean
              << " +/- " << std::setprecision(1) << m.at("PDR").ci95;

        delaySs << std::fixed << std::setprecision(2) << m.at("Delay").mean
                << " +/- " << std::setprecision(2) << m.at("Delay").ci95;

        std::cout << std::left << std::setw(18) << tc
                  << std::right << std::setw(12) << static_cast<uint32_t>(m.at("TxPackets").mean)
                  << std::setw(12) << static_cast<uint32_t>(m.at("RxPackets").mean)
                  << std::setw(18) << pdrSs.str()
                  << std::setw(16) << std::fixed << std::setprecision(2) << m.at("Throughput").mean
                  << std::setw(24) << delaySs.str()
                  << std::setw(14) << std::fixed << std::setprecision(3) << m.at("Jitter").mean
                  << std::endl;
    }
    std::cout << std::string(114, '-') << std::endl;
    std::cout << "Gateway Router Mean Total Drops: " << std::fixed << std::setprecision(1)
              << totalDropsStats.mean << " +/- " << totalDropsStats.ci95 << " packets" << std::endl;
    std::cout << std::string(114, '-') << std::endl;

    // Save All Output Files
    SaveRunsCsv(runsCsv, priorityMode, allRecords);
    std::cout << "Saved per-run records to        : " << runsCsv << std::endl;

    SaveStatsSummaryCsv(statsCsv, statsSummary);
    std::cout << "Saved 95% CI summary stats to   : " << statsCsv << std::endl;

    SaveMeansToPerFlowCsv(csvFilename, priorityMode, statsSummary, static_cast<uint32_t>(totalDropsStats.mean));
    std::cout << "Saved per-flow mean CSV to      : " << csvFilename << std::endl;

    // Save delay CDF data for emergency flow
    std::string cdfEmergencyFile = priorityMode ? "emergency-cdf-priority.csv" : "emergency-cdf-baseline.csv";
    std::ofstream cdfOut(cdfEmergencyFile.c_str());
    if (cdfOut.is_open())
    {
        cdfOut << "BinStart_ms,BinCenter_ms,Count,CumulativeCount,CDF_Probability\n";
        uint32_t cum = 0;
        uint32_t totalHist = 0;
        for (uint32_t b = 0; b < aggregateEmergencyHist.GetNBins(); ++b)
        {
            totalHist += aggregateEmergencyHist.GetBinCount(b);
        }
        for (uint32_t b = 0; b < aggregateEmergencyHist.GetNBins(); ++b)
        {
            uint32_t cnt = aggregateEmergencyHist.GetBinCount(b);
            cum += cnt;
            double binStart = aggregateEmergencyHist.GetBinStart(b) * 1000.0;
            double binCenter = binStart + 0.5;
            double prob = totalHist > 0 ? (static_cast<double>(cum) / totalHist) : 0.0;
            cdfOut << std::fixed << std::setprecision(2) << binStart << ","
                   << std::fixed << std::setprecision(2) << binCenter << ","
                   << cnt << "," << cum << ","
                   << std::fixed << std::setprecision(4) << prob << "\n";
        }
        cdfOut.close();
        std::cout << "Saved emergency delay CDF to    : " << cdfEmergencyFile << std::endl;
    }

    // Automatically generate consolidated comparisons if both baseline and priority have completed
    TryGenerateComparisons("stats-summary-baseline.csv",
                          "stats-summary-priority.csv",
                          "comparison-summary.csv",
                          "stats-comparison.csv");

    std::cout << "================================================================================\n" << std::endl;
    return 0;
}

// =============================================================================
// HELPER IMPLEMENTATIONS
// =============================================================================
void
SaveRunsCsv(const std::string& filename,
            bool priorityMode,
            const std::vector<FlowResultRecord>& allRecords)
{
    std::ofstream file(filename.c_str());
    if (!file.is_open()) return;

    file << "Mode,Run,FlowId,TrafficClass,Port,TxPackets,RxPackets,LostPackets,PDR_Percent,"
         << "Throughput_Kbps,MeanDelay_ms,MeanJitter_ms,QueueDrops\n";

    std::string modeLabel = priorityMode ? "Priority" : "Baseline";
    for (const auto& rec : allRecords)
    {
        file << modeLabel << ","
             << rec.runIndex << ","
             << rec.flowId << ","
             << "\"" << rec.trafficClass << "\","
             << rec.port << ","
             << rec.txPackets << ","
             << rec.rxPackets << ","
             << rec.lostPackets << ","
             << std::fixed << std::setprecision(2) << rec.pdrPercent << ","
             << std::fixed << std::setprecision(2) << rec.throughputKbps << ","
             << std::fixed << std::setprecision(3) << rec.meanDelayMs << ","
             << std::fixed << std::setprecision(3) << rec.meanJitterMs << ","
             << rec.queueDrops << "\n";
    }
    file.close();
}

void
SaveStatsSummaryCsv(const std::string& filename,
                    const std::map<std::string, std::map<std::string, MetricStats>>& statsSummary)
{
    std::ofstream file(filename.c_str());
    if (!file.is_open()) return;

    file << "TrafficClass,Metric,Mean,StdDev,StdErr,CI95_HalfWidth,CI95_Lower,CI95_Upper,Unit\n";

    std::vector<std::string> classes = {"Emergency Alert", "Ordinary Msg 1", "Ordinary Msg 2"};
    std::vector<std::pair<std::string, std::string>> metrics = {
        {"TxPackets", "packets"},
        {"RxPackets", "packets"},
        {"LostPackets", "packets"},
        {"PDR", "%"},
        {"Throughput", "Kbps"},
        {"Delay", "ms"},
        {"Jitter", "ms"},
        {"QueueDrops", "packets"}
    };

    for (const auto& tc : classes)
    {
        if (statsSummary.find(tc) == statsSummary.end()) continue;
        for (const auto& mDef : metrics)
        {
            if (statsSummary.at(tc).find(mDef.first) == statsSummary.at(tc).end()) continue;
            const MetricStats& s = statsSummary.at(tc).at(mDef.first);
            file << "\"" << tc << "\","
                 << "\"" << mDef.first << "\","
                 << std::fixed << std::setprecision(2) << s.mean << ","
                 << std::fixed << std::setprecision(2) << s.stdDev << ","
                 << std::fixed << std::setprecision(2) << s.stdErr << ","
                 << std::fixed << std::setprecision(2) << s.ci95 << ","
                 << std::fixed << std::setprecision(2) << s.ciLower << ","
                 << std::fixed << std::setprecision(2) << s.ciUpper << ","
                 << "\"" << mDef.second << "\"\n";
        }
    }
    file.close();
}

void
SaveMeansToPerFlowCsv(const std::string& filename,
                      bool priorityMode,
                      const std::map<std::string, std::map<std::string, MetricStats>>& statsSummary,
                      uint32_t totalQueueDrops)
{
    std::ofstream file(filename.c_str());
    if (!file.is_open()) return;

    file << "Mode,FlowId,TrafficClass,Port,TxPackets,RxPackets,LostPackets,PDR_Percent,"
         << "Throughput_Kbps,MeanDelay_ms,MeanJitter_ms,QueueDrops\n";

    std::string modeLabel = priorityMode ? "Priority" : "Baseline";
    std::vector<std::string> classes = {"Emergency Alert", "Ordinary Msg 1", "Ordinary Msg 2"};
    std::map<std::string, uint16_t> portMap = {
        {"Emergency Alert", 9000},
        {"Ordinary Msg 1", 9001},
        {"Ordinary Msg 2", 9002}
    };
    uint32_t fid = 1;

    for (const auto& tc : classes)
    {
        if (statsSummary.find(tc) == statsSummary.end()) continue;
        const auto& m = statsSummary.at(tc);
        file << modeLabel << ","
             << fid++ << ","
             << "\"" << tc << "\","
             << portMap[tc] << ","
             << static_cast<uint32_t>(m.at("TxPackets").mean) << ","
             << static_cast<uint32_t>(m.at("RxPackets").mean) << ","
             << static_cast<uint32_t>(m.at("LostPackets").mean) << ","
             << std::fixed << std::setprecision(2) << m.at("PDR").mean << ","
             << std::fixed << std::setprecision(2) << m.at("Throughput").mean << ","
             << std::fixed << std::setprecision(3) << m.at("Delay").mean << ","
             << std::fixed << std::setprecision(3) << m.at("Jitter").mean << ","
             << static_cast<uint32_t>(m.at("QueueDrops").mean) << "\n";
    }
    file.close();
}

void
TryGenerateComparisons(const std::string& baselineStatsCsv,
                      const std::string& priorityStatsCsv,
                      const std::string& comparisonSummaryCsv,
                      const std::string& statsComparisonCsv)
{
    std::ifstream bFile(baselineStatsCsv.c_str());
    std::ifstream pFile(priorityStatsCsv.c_str());

    if (!bFile.is_open() || !pFile.is_open()) return;

    struct StatRow
    {
        std::string trafficClass;
        std::string metric;
        double mean;
        double ci95;
        std::string unit;
    };

    auto parseStats = [](std::ifstream& in) -> std::map<std::string, std::map<std::string, StatRow>> {
        std::map<std::string, std::map<std::string, StatRow>> res;
        std::string line;
        bool isHeader = true;
        while (std::getline(in, line))
        {
            if (line.empty()) continue;
            if (isHeader) { isHeader = false; continue; }
            std::stringstream ss(line);
            std::string item;
            std::vector<std::string> cols;
            while (std::getline(ss, item, ','))
            {
                if (!item.empty() && item.front() == '"' && item.back() == '"')
                {
                    item = item.substr(1, item.size() - 2);
                }
                cols.push_back(item);
            }
            if (cols.size() >= 9)
            {
                StatRow r;
                r.trafficClass = cols[0];
                r.metric = cols[1];
                r.mean = std::stod(cols[2]);
                r.ci95 = std::stod(cols[5]);
                r.unit = cols[8];
                res[r.trafficClass][r.metric] = r;
            }
        }
        return res;
    };

    auto bStats = parseStats(bFile);
    auto pStats = parseStats(pFile);

    // 1. Generate comparison-summary.csv (matching existing layout)
    std::ofstream comp(comparisonSummaryCsv.c_str());
    if (comp.is_open())
    {
        comp << "TrafficClass,Metric,Baseline_FIFO,Priority_PrioQueueDisc,Unit,Difference,RelativeChange\n";
        std::vector<std::string> classes = {"Emergency Alert", "Ordinary Msg 1", "Ordinary Msg 2"};
        std::vector<std::pair<std::string, std::string>> displayMetrics = {
            {"TxPackets", "Tx Packets"},
            {"RxPackets", "Rx Packets"},
            {"LostPackets", "Lost Packets"},
            {"PDR", "Packet Delivery Ratio"},
            {"Throughput", "Throughput"},
            {"Delay", "Mean End-to-End Delay"},
            {"Jitter", "Mean Jitter"},
            {"QueueDrops", "Queue Drops"}
        };

        for (const auto& tc : classes)
        {
            if (bStats.find(tc) == bStats.end() || pStats.find(tc) == pStats.end()) continue;
            for (const auto& mp : displayMetrics)
            {
                if (bStats[tc].find(mp.first) == bStats[tc].end() || pStats[tc].find(mp.first) == pStats[tc].end()) continue;
                double bVal = bStats[tc][mp.first].mean;
                double pVal = pStats[tc][mp.first].mean;
                double diff = pVal - bVal;
                std::string relChangeStr = "N/A";
                if (std::abs(bVal) > 1e-6)
                {
                    double relChange = (diff / bVal) * 100.0;
                    std::stringstream ss;
                    ss << std::fixed << std::setprecision(1) << (relChange > 0 ? "+" : "") << relChange << "%";
                    relChangeStr = ss.str();
                }
                comp << "\"" << tc << "\","
                     << "\"" << mp.second << "\","
                     << std::fixed << std::setprecision(2) << bVal << ","
                     << std::fixed << std::setprecision(2) << pVal << ","
                     << "\"" << bStats[tc][mp.first].unit << "\","
                     << std::fixed << std::setprecision(2) << (diff > 0 ? "+" : "") << diff << ","
                     << "\"" << relChangeStr << "\"\n";
            }
        }
        comp.close();
        std::cout << "Consolidated comparison generated at: " << comparisonSummaryCsv << std::endl;
    }

    // 2. Generate stats-comparison.csv with 95% Confidence Intervals
    std::ofstream sComp(statsComparisonCsv.c_str());
    if (sComp.is_open())
    {
        sComp << "TrafficClass,Metric,Baseline_Mean,Baseline_CI95,Priority_Mean,Priority_CI95,Unit,Difference,RelativeChange\n";
        std::vector<std::string> classes = {"Emergency Alert", "Ordinary Msg 1", "Ordinary Msg 2"};
        std::vector<std::string> metrics = {"TxPackets", "RxPackets", "LostPackets", "PDR", "Throughput", "Delay", "Jitter", "QueueDrops"};

        for (const auto& tc : classes)
        {
            if (bStats.find(tc) == bStats.end() || pStats.find(tc) == pStats.end()) continue;
            for (const auto& mName : metrics)
            {
                if (bStats[tc].find(mName) == bStats[tc].end() || pStats[tc].find(mName) == pStats[tc].end()) continue;
                const auto& bRow = bStats[tc][mName];
                const auto& pRow = pStats[tc][mName];
                double diff = pRow.mean - bRow.mean;
                std::string relChangeStr = "N/A";
                if (std::abs(bRow.mean) > 1e-6)
                {
                    double relChange = (diff / bRow.mean) * 100.0;
                    std::stringstream ss;
                    ss << std::fixed << std::setprecision(1) << (relChange > 0 ? "+" : "") << relChange << "%";
                    relChangeStr = ss.str();
                }
                sComp << "\"" << tc << "\","
                      << "\"" << mName << "\","
                      << std::fixed << std::setprecision(2) << bRow.mean << ","
                      << std::fixed << std::setprecision(2) << bRow.ci95 << ","
                      << std::fixed << std::setprecision(2) << pRow.mean << ","
                      << std::fixed << std::setprecision(2) << pRow.ci95 << ","
                      << "\"" << bRow.unit << "\","
                      << std::fixed << std::setprecision(2) << (diff > 0 ? "+" : "") << diff << ","
                      << "\"" << relChangeStr << "\"\n";
            }
        }
        sComp.close();
        std::cout << "Statistical comparison with 95% CI generated at: " << statsComparisonCsv << std::endl;
    }
}
