/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Smart Emergency Alert and Priority Messaging Network (SEAN)
 * Queue Discipline Evaluation: FIFO vs Priority vs RED vs CoDel (vs Prio-CoDel)
 *
 * Course Title : B.Tech Network Programming (NWP) Laboratory / Capstone
 * Simulator    : Network Simulator 3 (ns-3.49)
 * Architecture : 50-Node Multi-Tier Point-to-Point Network (49 Links)
 * Target       : Controlled comparative evaluation of Queue Disciplines on Node 43 Bottleneck Link
 *
 * Queue Disciplines Evaluated:
 *   1. FIFO   (ns3::FifoQueueDisc) - Classless tail-drop baseline, 50-packet capacity
 *   2. Priority (ns3::PrioQueueDisc) - 3-band non-preemptive strict priority (50p per band)
 *   3. RED    (ns3::RedQueueDisc) - Active Queue Management with EWMA average queue size
 *             MinTh=10p, MaxTh=30p, MaxSize=50p, QW=0.002, MeanPktSize=540B
 *   4. CoDel  (ns3::CoDelQueueDisc) - Delay-based AQM controlling packet sojourn time
 *             Target=5ms, Interval=100ms, MaxSize=50p, MinBytes=540B
 *   5. Prio-CoDel (ns3::PrioQueueDisc with CoDel in Band 1) - Hybrid priority scheduling
 *             Band 0: FifoQueueDisc (50p) for Emergency Alerts
 *             Band 1: CoDelQueueDisc (50p, 5ms/100ms) for Ordinary Civilian Traffic
 *
 * Traffic Distribution:
 *   - 10 Emergency Alert Sources (Nodes 0..9)  -> 1/7 of total offered load (TOS 0x10)
 *   - 30 Ordinary Civilian Sources (Nodes 10..39) -> 6/7 of total offered load (TOS 0x00)
 *   - 3 Regional Routers (Nodes 40..42)
 *   - 1 Ingress Core Gateway (Node 43, GW_in with QueueDisc)
 *   - 1 Egress Core Gateway (Node 44, GW_eg)
 *   - 5 Emergency Response Center Sink Servers (Nodes 45..49)
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/traffic-control-module.h"
#include "ns3/flow-monitor-module.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <iomanip>
#include <cmath>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SeanQueueDiscComparison");

// =============================================================================
// GLOBAL QUEUE VALIDATION COUNTERS
// =============================================================================
static uint32_t g_band0Enqueued = 0;
static uint32_t g_band1Enqueued = 0;
static uint32_t g_band2Enqueued = 0;
static uint32_t g_band0Dropped  = 0;
static uint32_t g_band1Dropped  = 0;
static uint32_t g_band2Dropped  = 0;

static uint32_t g_genericEmergencyEnqueued = 0;
static uint32_t g_genericOrdinaryEnqueued  = 0;
static uint32_t g_genericEmergencyDropped  = 0;
static uint32_t g_genericOrdinaryDropped   = 0;

static void TraceBand0Enqueue(Ptr<const QueueDiscItem> item) { g_band0Enqueued++; }
static void TraceBand1Enqueue(Ptr<const QueueDiscItem> item) { g_band1Enqueued++; }
static void TraceBand2Enqueue(Ptr<const QueueDiscItem> item) { g_band2Enqueued++; }
static void TraceBand0Drop(Ptr<const QueueDiscItem> item)    { g_band0Dropped++; }
static void TraceBand1Drop(Ptr<const QueueDiscItem> item)    { g_band1Dropped++; }
static void TraceBand2Drop(Ptr<const QueueDiscItem> item)    { g_band2Dropped++; }

static void TraceGenericEnqueue(Ptr<const QueueDiscItem> item)
{
    uint8_t dsfield = 0;
    if (item->GetUint8Value(QueueDiscItem::IP_DSFIELD, dsfield) && (dsfield & 0x10) != 0)
    {
        g_genericEmergencyEnqueued++;
    }
    else
    {
        g_genericOrdinaryEnqueued++;
    }
}

static void TraceGenericDrop(Ptr<const QueueDiscItem> item)
{
    uint8_t dsfield = 0;
    if (item->GetUint8Value(QueueDiscItem::IP_DSFIELD, dsfield) && (dsfield & 0x10) != 0)
    {
        g_genericEmergencyDropped++;
    }
    else
    {
        g_genericOrdinaryDropped++;
    }
}

// =============================================================================
// TRAFFIC CONFIGURATION & FLOW STRUCTURES
// =============================================================================
struct FlowConfig
{
    uint32_t sourceNodeId;
    uint32_t destServerNodeId;
    uint32_t serverIndex;
    Ipv4Address destIp;
    uint16_t destPort;
    std::string trafficClass;
    uint8_t tos;
    DataRate rate;
};

struct FlowResultRecord
{
    uint32_t flowIndex;
    uint32_t sourceNodeId;
    uint32_t destServerNodeId;
    std::string trafficClass;
    Ipv4Address destIp;
    uint16_t destPort;
    uint32_t txPackets;
    uint32_t rxPackets;
    uint32_t lostPackets;
    double pdrPercent;
    double throughputKbps;
    double meanDelayMs;
    double meanJitterMs;
};

struct ClassSummaryMetrics
{
    std::string trafficClass;
    uint32_t numFlows;
    uint32_t totalTx;
    uint32_t totalRx;
    uint32_t totalLost;
    double aggregatePdr;
    double aggregateThroughputKbps;
    double weightedMeanDelayMs;
    double meanJitterMs;
    uint32_t queueDrops;
};

// =============================================================================
// JITTERED TRAFFIC APPLICATION
// =============================================================================
class CongestionTrafficApp : public Application
{
public:
    CongestionTrafficApp()
        : m_socket(nullptr),
          m_peer(),
          m_packetSize(512),
          m_dataRate("40kb/s"),
          m_tos(0x00),
          m_useExponential(false),
          m_jitterRatio(0.20),
          m_running(false),
          m_packetsSent(0)
    {
    }

    ~CongestionTrafficApp() override
    {
        m_socket = nullptr;
    }

    void Setup(Address peer, uint32_t packetSize, DataRate dataRate, uint8_t tos,
               bool useExp = false, double jitter = 0.20)
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
        m_packetsSent = 0;
        if (!m_socket)
        {
            TypeId tid = TypeId::LookupByName("ns3::UdpSocketFactory");
            m_socket = Socket::CreateSocket(GetNode(), tid);
            if (InetSocketAddress::IsMatchingType(m_peer))
            {
                m_socket->SetIpTos(m_tos);
            }
            m_socket->Bind();
            m_socket->Connect(m_peer);
        }

        if (m_useExponential)
        {
            m_expVariable = CreateObject<ExponentialRandomVariable>();
            double meanInterval = static_cast<double>(m_packetSize * 8) / static_cast<double>(m_dataRate.GetBitRate());
            m_expVariable->SetAttribute("Mean", DoubleValue(meanInterval));
        }
        else
        {
            m_uniformJitter = CreateObject<UniformRandomVariable>();
            m_uniformJitter->SetAttribute("Min", DoubleValue(1.0 - m_jitterRatio));
            m_uniformJitter->SetAttribute("Max", DoubleValue(1.0 + m_jitterRatio));
        }

        ScheduleNextPacket();
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
        m_packetsSent++;

        ScheduleNextPacket();
    }

    void ScheduleNextPacket()
    {
        if (!m_running) return;

        double nominalInterval = static_cast<double>(m_packetSize * 8) / static_cast<double>(m_dataRate.GetBitRate());
        double actualInterval = nominalInterval;

        if (m_useExponential && m_expVariable)
        {
            actualInterval = m_expVariable->GetValue();
        }
        else if (m_uniformJitter)
        {
            actualInterval = nominalInterval * m_uniformJitter->GetValue();
        }

        m_sendEvent = Simulator::Schedule(Seconds(actualInterval), &CongestionTrafficApp::SendPacket, this);
    }

    Ptr<Socket> m_socket;
    Address m_peer;
    uint32_t m_packetSize;
    DataRate m_dataRate;
    uint8_t m_tos;
    bool m_useExponential;
    double m_jitterRatio;
    bool m_running;
    uint32_t m_packetsSent;
    EventId m_sendEvent;
    Ptr<ExponentialRandomVariable> m_expVariable;
    Ptr<UniformRandomVariable> m_uniformJitter;
};

// =============================================================================
// MAIN SIMULATION PROGRAM
// =============================================================================
int main(int argc, char* argv[])
{
    // Simulation parameters
    std::string qdiscType = "prio"; // "fifo", "prio", "red", "codel", "prio-codel"
    uint32_t loadPercent = 100;    // 50, 80, 100, 120, 140
    uint32_t runIndex = 1;
    uint32_t seedValue = 1;
    double simTime = 30.0;
    uint32_t packetSize = 512;
    uint32_t queueDiscSize = 50;

    // RED AQM parameters
    double redMinTh = 10.0;
    double redMaxTh = 30.0;
    double redQW = 0.002;

    // CoDel AQM parameters
    uint32_t codelTargetMs = 5;
    uint32_t codelIntervalMs = 100;

    // Traffic Jitter Model
    bool useExponential = false;
    double jitterRatio = 0.20;
    bool randomizeStart = true;

    // Custom CSV output paths
    std::string customSummaryCsv = "";
    std::string customFlowCsv = "";
    std::string customQueueCsv = "";

    CommandLine cmd(__FILE__);
    cmd.AddValue("qdisc", "Queue discipline: fifo, prio, red, codel, prio-codel", qdiscType);
    cmd.AddValue("load", "Offered load percentage relative to bottleneck (50, 80, 100, 120, 140)", loadPercent);
    cmd.AddValue("run", "Simulation replication run index (1..10)", runIndex);
    cmd.AddValue("seed", "Random number generator base seed", seedValue);
    cmd.AddValue("simTime", "Simulation duration in seconds", simTime);
    cmd.AddValue("packetSize", "UDP payload size in bytes", packetSize);
    cmd.AddValue("queueDiscSize", "Queue disc capacity in packets", queueDiscSize);
    cmd.AddValue("redMinTh", "RED minimum queue threshold in packets", redMinTh);
    cmd.AddValue("redMaxTh", "RED maximum queue threshold in packets", redMaxTh);
    cmd.AddValue("redQW", "RED queue weight parameter for EWMA", redQW);
    cmd.AddValue("codelTarget", "CoDel target sojourn time in ms", codelTargetMs);
    cmd.AddValue("codelInterval", "CoDel interval window in ms", codelIntervalMs);
    cmd.AddValue("useExp", "Use Poisson/exponential arrivals instead of jittered CBR", useExponential);
    cmd.AddValue("jitter", "Jitter fraction for CBR intervals [0.0..1.0]", jitterRatio);
    cmd.AddValue("randomStart", "Randomize client application start times across 50ms window", randomizeStart);
    cmd.AddValue("summaryCsv", "Explicit path for summary CSV output", customSummaryCsv);
    cmd.AddValue("flowCsv", "Explicit path for per-flow CSV output", customFlowCsv);
    cmd.AddValue("queueCsv", "Explicit path for queue validation CSV output", customQueueCsv);
    cmd.Parse(argc, argv);

    // Normalize qdisc string to lowercase
    for (char &c : qdiscType) c = std::tolower(c);

    // Network topology link parameters
    const std::string accessBandwidth     = "10Mbps";
    const std::string accessDelay         = "2ms";
    const std::string aggBandwidth        = "10Mbps";
    const std::string aggDelay            = "2ms";
    const std::string bottleneckBandwidth = "1Mbps";
    const std::string bottleneckDelay     = "10ms";
    const std::string serverBandwidth     = "10Mbps";
    const std::string serverDelay         = "2ms";

    // Total nominal application offered rate in bits per second:
    double totalOfferedBps = static_cast<double>(loadPercent) * 10000.0; // 100% -> 1,000,000 bps
    // Emergency traffic is exactly 1/7 of total offered load:
    double emergencyTotalBps = totalOfferedBps / 7.0;
    // 10 Emergency sources:
    double emergencyPerSourceBps = emergencyTotalBps / 10.0;
    // Ordinary traffic is exactly 6/7 of total offered load:
    double ordinaryTotalBps = totalOfferedBps * (6.0 / 7.0);
    // 30 Ordinary sources:
    double ordinaryPerSourceBps = ordinaryTotalBps / 30.0;

    DataRate emergencyRate(static_cast<uint64_t>(std::round(emergencyPerSourceBps)));
    DataRate ordinaryRate(static_cast<uint64_t>(std::round(ordinaryPerSourceBps)));

    // Configure exact random stream seed and run
    RngSeedManager::SetSeed(seedValue);
    RngSeedManager::SetRun(runIndex);

    // Reset validation counters
    g_band0Enqueued = 0; g_band1Enqueued = 0; g_band2Enqueued = 0;
    g_band0Dropped  = 0; g_band1Dropped  = 0; g_band2Dropped  = 0;
    g_genericEmergencyEnqueued = 0; g_genericOrdinaryEnqueued = 0;
    g_genericEmergencyDropped  = 0; g_genericOrdinaryDropped  = 0;

    std::cout << "\n================================================================================" << std::endl;
    std::cout << "  SEAN Queue Discipline Evaluation: 50-Node Topology                           " << std::endl;
    std::cout << "  Queue Discipline : " << qdiscType << std::endl;
    std::cout << "  Offered Load     : " << loadPercent << "% (" << (totalOfferedBps / 1000.0) << " Kbps nominal app payload)" << std::endl;
    std::cout << "  Replication      : Run " << runIndex << " (Seed: " << seedValue << ")" << std::endl;
    std::cout << "  Emergency Rate   : " << (emergencyTotalBps / 1000.0) << " Kbps total (" << emergencyRate.GetBitRate() << " bps/src x 10)" << std::endl;
    std::cout << "  Ordinary Rate    : " << (ordinaryTotalBps / 1000.0) << " Kbps total (" << ordinaryRate.GetBitRate() << " bps/src x 30)" << std::endl;
    std::cout << "  Packet Size      : " << packetSize << " bytes (IP packet: " << (packetSize + 28) << " bytes)" << std::endl;
    std::cout << "================================================================================" << std::endl;

    // -------------------------------------------------------------------------
    // 1. NODE TOPOLOGY CREATION (50 NODES)
    // -------------------------------------------------------------------------
    NodeContainer allNodes;
    allNodes.Create(50);

    Ptr<Node> r1Node   = allNodes.Get(40); // Regional Router 1 (Zone A)
    Ptr<Node> r2Node   = allNodes.Get(41); // Regional Router 2 (Zone B)
    Ptr<Node> r3Node   = allNodes.Get(42); // Regional Router 3 (Zone C)
    Ptr<Node> gwInNode = allNodes.Get(43); // Core Bottleneck Ingress Gateway
    Ptr<Node> gwEgNode = allNodes.Get(44); // Core Bottleneck Egress Gateway

    NodeContainer serverNodes;
    for (uint32_t s = 45; s < 50; ++s) serverNodes.Add(allNodes.Get(s));

    // -------------------------------------------------------------------------
    // 2. POINT-TO-POINT CHANNELS & DEVICES (49 LINKS)
    // -------------------------------------------------------------------------
    PointToPointHelper accessLink;
    accessLink.SetDeviceAttribute("DataRate", StringValue(accessBandwidth));
    accessLink.SetChannelAttribute("Delay", StringValue(accessDelay));
    accessLink.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("100p"));

    PointToPointHelper aggLink;
    aggLink.SetDeviceAttribute("DataRate", StringValue(aggBandwidth));
    aggLink.SetChannelAttribute("Delay", StringValue(aggDelay));
    aggLink.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("100p"));

    // Bottleneck link: set NetDevice queue strictly to 1 packet to decouple hardware buffer
    PointToPointHelper bottleneckLink;
    bottleneckLink.SetDeviceAttribute("DataRate", StringValue(bottleneckBandwidth));
    bottleneckLink.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
    bottleneckLink.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("1p"));

    PointToPointHelper serverLink;
    serverLink.SetDeviceAttribute("DataRate", StringValue(serverBandwidth));
    serverLink.SetChannelAttribute("Delay", StringValue(serverDelay));
    serverLink.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("100p"));

    // A. 40 Access Links: Connect Sources to Regional Routers
    std::vector<NetDeviceContainer> accessDevs(40);

    for (uint32_t i = 0; i < 40; ++i)
    {
        Ptr<Node> srcNode = allNodes.Get(i);
        Ptr<Node> targetRouter;

        if ((i <= 2) || (i >= 10 && i <= 19))
        {
            targetRouter = r1Node; // Zone A: 3 Emergency + 10 Ordinary = 13 links
        }
        else if ((i >= 3 && i <= 5) || (i >= 20 && i <= 29))
        {
            targetRouter = r2Node; // Zone B: 3 Emergency + 10 Ordinary = 13 links
        }
        else
        {
            targetRouter = r3Node; // Zone C: 4 Emergency + 10 Ordinary = 14 links
        }

        accessDevs[i] = accessLink.Install(srcNode, targetRouter);
    }

    // B. 3 Regional Aggregation Links: R1, R2, R3 -> GW_in
    NetDeviceContainer r1_gwIn = aggLink.Install(r1Node, gwInNode);
    NetDeviceContainer r2_gwIn = aggLink.Install(r2Node, gwInNode);
    NetDeviceContainer r3_gwIn = aggLink.Install(r3Node, gwInNode);

    // C. 1 Shared Bottleneck Link: GW_in -> GW_eg
    NetDeviceContainer gwIn_gwEg = bottleneckLink.Install(gwInNode, gwEgNode);

    // D. 5 Server Access Links: GW_eg -> Servers 0..4
    std::vector<NetDeviceContainer> serverDevs(5);
    for (uint32_t s = 0; s < 5; ++s)
    {
        serverDevs[s] = serverLink.Install(gwEgNode, serverNodes.Get(s));
    }

    // -------------------------------------------------------------------------
    // 3. INTERNET STACK & IP ADDRESS ASSIGNMENT
    // -------------------------------------------------------------------------
    InternetStackHelper internet;
    internet.Install(allNodes);

    Ipv4AddressHelper address;

    // A. Assign Access Subnets (10.1.1.0/24 .. 10.1.40.0/24)
    std::vector<Ipv4InterfaceContainer> accessIfs(40);
    for (uint32_t i = 0; i < 40; ++i)
    {
        std::ostringstream subnet;
        subnet << "10.1." << (i + 1) << ".0";
        address.SetBase(subnet.str().c_str(), "255.255.255.0");
        accessIfs[i] = address.Assign(accessDevs[i]);
    }

    // B. Assign Aggregation Subnets (10.2.1.0/24, 10.2.2.0/24, 10.2.3.0/24)
    address.SetBase("10.2.1.0", "255.255.255.0");
    Ipv4InterfaceContainer r1_gwInIf = address.Assign(r1_gwIn);
    address.SetBase("10.2.2.0", "255.255.255.0");
    Ipv4InterfaceContainer r2_gwInIf = address.Assign(r2_gwIn);
    address.SetBase("10.2.3.0", "255.255.255.0");
    Ipv4InterfaceContainer r3_gwInIf = address.Assign(r3_gwIn);

    // C. Assign Bottleneck Subnet (10.3.1.0/24)
    address.SetBase("10.3.1.0", "255.255.255.0");
    Ipv4InterfaceContainer gwIn_gwEgIf = address.Assign(gwIn_gwEg);

    // D. Assign Server Subnets (10.4.1.0/24 .. 10.4.5.0/24)
    std::vector<Ipv4InterfaceContainer> serverIfs(5);
    std::vector<Ipv4Address> serverIps(5);
    for (uint32_t s = 0; s < 5; ++s)
    {
        std::ostringstream subnet;
        subnet << "10.4." << (s + 1) << ".0";
        address.SetBase(subnet.str().c_str(), "255.255.255.0");
        serverIfs[s] = address.Assign(serverDevs[s]);
        serverIps[s] = serverIfs[s].GetAddress(1);
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // -------------------------------------------------------------------------
    // 4. TRAFFIC CONTROL AT CORE BOTTLENECK GATEWAY (NODE 43)
    // -------------------------------------------------------------------------
    Ptr<NetDevice> devBottleneck = gwIn_gwEg.Get(0);
    TrafficControlHelper tch;
    tch.Uninstall(devBottleneck);

    Ptr<QueueDisc> rootQdisc = nullptr;
    std::string qSizeStr = std::to_string(queueDiscSize) + "p";

    bool isPriorityMode = (qdiscType == "prio" || qdiscType == "prio-codel");

    if (qdiscType == "prio")
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

        QueueDiscContainer qdiscs = tch.Install(devBottleneck);
        rootQdisc = qdiscs.Get(0);

        Ptr<QueueDisc> band0Qdisc = rootQdisc->GetQueueDiscClass(0)->GetQueueDisc();
        Ptr<QueueDisc> band1Qdisc = rootQdisc->GetQueueDiscClass(1)->GetQueueDisc();
        Ptr<QueueDisc> band2Qdisc = rootQdisc->GetQueueDiscClass(2)->GetQueueDisc();

        band0Qdisc->TraceConnectWithoutContext("Enqueue", MakeCallback(&TraceBand0Enqueue));
        band1Qdisc->TraceConnectWithoutContext("Enqueue", MakeCallback(&TraceBand1Enqueue));
        band2Qdisc->TraceConnectWithoutContext("Enqueue", MakeCallback(&TraceBand2Enqueue));

        band0Qdisc->TraceConnectWithoutContext("Drop", MakeCallback(&TraceBand0Drop));
        band1Qdisc->TraceConnectWithoutContext("Drop", MakeCallback(&TraceBand1Drop));
        band2Qdisc->TraceConnectWithoutContext("Drop", MakeCallback(&TraceBand2Drop));
    }
    else if (qdiscType == "prio-codel")
    {
        uint16_t handle = tch.SetRootQueueDisc(
            "ns3::PrioQueueDisc",
            "Priomap",
            StringValue("1 2 2 2 1 2 0 0 1 1 1 1 1 1 1 1"));

        TrafficControlHelper::ClassIdList cid =
            tch.AddQueueDiscClasses(handle, 3, "ns3::QueueDiscClass");

        tch.AddChildQueueDisc(handle, cid[0], "ns3::FifoQueueDisc", "MaxSize", QueueSizeValue(QueueSize(qSizeStr)));
        tch.AddChildQueueDisc(handle, cid[1], "ns3::CoDelQueueDisc",
                              "MaxSize", QueueSizeValue(QueueSize(qSizeStr)),
                              "Target", TimeValue(MilliSeconds(codelTargetMs)),
                              "Interval", TimeValue(MilliSeconds(codelIntervalMs)),
                              "MinBytes", UintegerValue(540),
                              "UseEcn", BooleanValue(false));
        tch.AddChildQueueDisc(handle, cid[2], "ns3::FifoQueueDisc", "MaxSize", QueueSizeValue(QueueSize(qSizeStr)));

        QueueDiscContainer qdiscs = tch.Install(devBottleneck);
        rootQdisc = qdiscs.Get(0);

        Ptr<QueueDisc> band0Qdisc = rootQdisc->GetQueueDiscClass(0)->GetQueueDisc();
        Ptr<QueueDisc> band1Qdisc = rootQdisc->GetQueueDiscClass(1)->GetQueueDisc();
        Ptr<QueueDisc> band2Qdisc = rootQdisc->GetQueueDiscClass(2)->GetQueueDisc();

        band0Qdisc->TraceConnectWithoutContext("Enqueue", MakeCallback(&TraceBand0Enqueue));
        band1Qdisc->TraceConnectWithoutContext("Enqueue", MakeCallback(&TraceBand1Enqueue));
        band2Qdisc->TraceConnectWithoutContext("Enqueue", MakeCallback(&TraceBand2Enqueue));

        band0Qdisc->TraceConnectWithoutContext("Drop", MakeCallback(&TraceBand0Drop));
        band1Qdisc->TraceConnectWithoutContext("Drop", MakeCallback(&TraceBand1Drop));
        band2Qdisc->TraceConnectWithoutContext("Drop", MakeCallback(&TraceBand2Drop));
    }
    else if (qdiscType == "red")
    {
        tch.SetRootQueueDisc(
            "ns3::RedQueueDisc",
            "MaxSize", QueueSizeValue(QueueSize(qSizeStr)),
            "MinTh", DoubleValue(redMinTh),
            "MaxTh", DoubleValue(redMaxTh),
            "MeanPktSize", UintegerValue(540),
            "LinkBandwidth", DataRateValue(DataRate(bottleneckBandwidth)),
            "LinkDelay", TimeValue(MilliSeconds(10)),
            "UseHardDrop", BooleanValue(true),
            "UseEcn", BooleanValue(false),
            "QW", DoubleValue(redQW));

        QueueDiscContainer qdiscs = tch.Install(devBottleneck);
        rootQdisc = qdiscs.Get(0);

        rootQdisc->TraceConnectWithoutContext("Enqueue", MakeCallback(&TraceGenericEnqueue));
        rootQdisc->TraceConnectWithoutContext("Drop", MakeCallback(&TraceGenericDrop));
    }
    else if (qdiscType == "codel")
    {
        tch.SetRootQueueDisc(
            "ns3::CoDelQueueDisc",
            "MaxSize", QueueSizeValue(QueueSize(qSizeStr)),
            "Target", TimeValue(MilliSeconds(codelTargetMs)),
            "Interval", TimeValue(MilliSeconds(codelIntervalMs)),
            "MinBytes", UintegerValue(540),
            "UseEcn", BooleanValue(false));

        QueueDiscContainer qdiscs = tch.Install(devBottleneck);
        rootQdisc = qdiscs.Get(0);

        rootQdisc->TraceConnectWithoutContext("Enqueue", MakeCallback(&TraceGenericEnqueue));
        rootQdisc->TraceConnectWithoutContext("Drop", MakeCallback(&TraceGenericDrop));
    }
    else // default "fifo"
    {
        qdiscType = "fifo";
        tch.SetRootQueueDisc("ns3::FifoQueueDisc", "MaxSize", QueueSizeValue(QueueSize(qSizeStr)));
        QueueDiscContainer qdiscs = tch.Install(devBottleneck);
        rootQdisc = qdiscs.Get(0);

        rootQdisc->TraceConnectWithoutContext("Enqueue", MakeCallback(&TraceGenericEnqueue));
        rootQdisc->TraceConnectWithoutContext("Drop", MakeCallback(&TraceGenericDrop));
    }

    // -------------------------------------------------------------------------
    // 5. APPLICATION FLOW DEFINITIONS & INSTALLATION (40 CONCURRENT FLOWS)
    // -------------------------------------------------------------------------
    PacketSinkHelper sinkHelper("ns3::UdpSocketFactory", Address());
    ApplicationContainer sinkApps;
    std::vector<FlowConfig> flowConfigs(40);

    for (uint32_t i = 0; i < 40; ++i)
    {
        FlowConfig fc;
        fc.sourceNodeId = i;

        if (i < 10)
        {
            // Emergency Alert Source: port 9000 (even) and 9001 (odd)
            fc.serverIndex = i / 2;
            fc.destServerNodeId = 45 + fc.serverIndex;
            fc.destIp = serverIps[fc.serverIndex];
            fc.destPort = 9000 + (i % 2);
            fc.trafficClass = "Emergency Alert";
            fc.tos = 0x10; // DSCP 0x04 / Low-Delay. Inspected by PrioQueueDisc, preserved across FIFO/RED/CoDel
            fc.rate = emergencyRate;
        }
        else
        {
            // Ordinary Civilian Source: ports 9002..9007
            uint32_t ordIdx = i - 10;
            fc.serverIndex = ordIdx / 6;
            fc.destServerNodeId = 45 + fc.serverIndex;
            fc.destIp = serverIps[fc.serverIndex];
            fc.destPort = 9002 + (ordIdx % 6);
            fc.trafficClass = "Ordinary Msg";
            fc.tos = 0x00;
            fc.rate = ordinaryRate;
        }

        flowConfigs[i] = fc;

        sinkHelper.SetAttribute("Local", AddressValue(InetSocketAddress(Ipv4Address::GetAny(), fc.destPort)));
        sinkApps.Add(sinkHelper.Install(serverNodes.Get(fc.serverIndex)));
    }

    sinkApps.Start(Seconds(0.5));
    sinkApps.Stop(Seconds(simTime + 1.0));

    // Install Sender Applications with phase-lock breaking jitter
    ApplicationContainer clientApps;
    Ptr<UniformRandomVariable> startOffsetVar = CreateObject<UniformRandomVariable>();
    startOffsetVar->SetAttribute("Min", DoubleValue(0.0));
    startOffsetVar->SetAttribute("Max", DoubleValue(0.05));

    for (uint32_t i = 0; i < 40; ++i)
    {
        const FlowConfig& fc = flowConfigs[i];
        Ptr<CongestionTrafficApp> app = CreateObject<CongestionTrafficApp>();
        app->Setup(InetSocketAddress(fc.destIp, fc.destPort),
                   packetSize,
                   fc.rate,
                   fc.tos,
                   useExponential,
                   jitterRatio);

        allNodes.Get(fc.sourceNodeId)->AddApplication(app);

        double startJitter = randomizeStart ? startOffsetVar->GetValue() : 0.0;
        app->SetStartTime(Seconds(1.0 + startJitter));
        app->SetStopTime(Seconds(simTime));
        clientApps.Add(app);
    }

    // -------------------------------------------------------------------------
    // 6. FLOW MONITOR SETUP & SIMULATION EXECUTION
    // -------------------------------------------------------------------------
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();
    monitor->SetAttribute("DelayBinWidth", DoubleValue(0.001));

    Simulator::Stop(Seconds(simTime + 1.0));
    Simulator::Run();

    // -------------------------------------------------------------------------
    // 7. FLOW MONITOR DATA EXTRACTION & ANALYSIS
    // -------------------------------------------------------------------------
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    std::map<std::pair<Ipv4Address, uint16_t>, uint32_t> flowLookup;
    for (uint32_t i = 0; i < 40; ++i)
    {
        flowLookup[{flowConfigs[i].destIp, flowConfigs[i].destPort}] = i;
    }

    std::vector<FlowResultRecord> flowRecords(40);
    for (uint32_t i = 0; i < 40; ++i)
    {
        flowRecords[i].flowIndex = i;
        flowRecords[i].sourceNodeId = flowConfigs[i].sourceNodeId;
        flowRecords[i].destServerNodeId = flowConfigs[i].destServerNodeId;
        flowRecords[i].trafficClass = flowConfigs[i].trafficClass;
        flowRecords[i].destIp = flowConfigs[i].destIp;
        flowRecords[i].destPort = flowConfigs[i].destPort;
        flowRecords[i].txPackets = 0;
        flowRecords[i].rxPackets = 0;
        flowRecords[i].lostPackets = 0;
        flowRecords[i].pdrPercent = 0.0;
        flowRecords[i].throughputKbps = 0.0;
        flowRecords[i].meanDelayMs = 0.0;
        flowRecords[i].meanJitterMs = 0.0;
    }

    const double standardActiveDuration = simTime - 1.0; // 29.0s active window

    for (const auto& entry : stats)
    {
        FlowId fid = entry.first;
        FlowMonitor::FlowStats fStats = entry.second;
        Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(fid);

        auto it = flowLookup.find({tuple.destinationAddress, tuple.destinationPort});
        if (it == flowLookup.end()) continue;

        uint32_t idx = it->second;
        flowRecords[idx].txPackets = fStats.txPackets;
        flowRecords[idx].rxPackets = fStats.rxPackets;
        flowRecords[idx].lostPackets = fStats.lostPackets;

        if (fStats.txPackets > 0)
        {
            flowRecords[idx].pdrPercent = (100.0 * fStats.rxPackets) / fStats.txPackets;
        }
        if (fStats.rxPackets > 0)
        {
            flowRecords[idx].meanDelayMs = (fStats.delaySum.GetSeconds() * 1000.0) / fStats.rxPackets;
        }
        if (fStats.rxPackets > 1)
        {
            flowRecords[idx].meanJitterMs = (fStats.jitterSum.GetSeconds() * 1000.0) / (fStats.rxPackets - 1);
        }
        if (standardActiveDuration > 0.0)
        {
            flowRecords[idx].throughputKbps = (fStats.rxBytes * 8.0) / (standardActiveDuration * 1000.0);
        }
    }

    // Compute Class Aggregates
    ClassSummaryMetrics emerSummary  = {"Emergency Alert", 10, 0, 0, 0, 0.0, 0.0, 0.0, 0.0, 0};
    ClassSummaryMetrics ordSummary   = {"Ordinary Msg",    30, 0, 0, 0, 0.0, 0.0, 0.0, 0.0, 0};
    double emerDelayWeightSum = 0.0;
    double ordDelayWeightSum  = 0.0;

    for (uint32_t i = 0; i < 40; ++i)
    {
        const auto& r = flowRecords[i];
        if (r.trafficClass == "Emergency Alert")
        {
            emerSummary.totalTx += r.txPackets;
            emerSummary.totalRx += r.rxPackets;
            emerSummary.totalLost += r.lostPackets;
            emerSummary.aggregateThroughputKbps += r.throughputKbps;
            emerDelayWeightSum += (r.meanDelayMs * r.rxPackets);
            emerSummary.meanJitterMs += r.meanJitterMs;
        }
        else
        {
            ordSummary.totalTx += r.txPackets;
            ordSummary.totalRx += r.rxPackets;
            ordSummary.totalLost += r.lostPackets;
            ordSummary.aggregateThroughputKbps += r.throughputKbps;
            ordDelayWeightSum += (r.meanDelayMs * r.rxPackets);
            ordSummary.meanJitterMs += r.meanJitterMs;
        }
    }

    if (emerSummary.totalTx > 0)
    {
        emerSummary.aggregatePdr = (100.0 * emerSummary.totalRx) / emerSummary.totalTx;
    }
    if (emerSummary.totalRx > 0)
    {
        emerSummary.weightedMeanDelayMs = emerDelayWeightSum / emerSummary.totalRx;
    }
    emerSummary.meanJitterMs /= 10.0;

    if (ordSummary.totalTx > 0)
    {
        ordSummary.aggregatePdr = (100.0 * ordSummary.totalRx) / ordSummary.totalTx;
    }
    if (ordSummary.totalRx > 0)
    {
        ordSummary.weightedMeanDelayMs = ordDelayWeightSum / ordSummary.totalRx;
    }
    ordSummary.meanJitterMs /= 30.0;

    if (isPriorityMode)
    {
        emerSummary.queueDrops = g_band0Dropped;
        ordSummary.queueDrops  = g_band1Dropped;
    }
    else
    {
        emerSummary.queueDrops = g_genericEmergencyDropped;
        ordSummary.queueDrops  = g_genericOrdinaryDropped;
    }

    // -------------------------------------------------------------------------
    // 8. DATA EXPORT TO CSV
    // -------------------------------------------------------------------------
    std::string summaryCsvFile = customSummaryCsv.empty()
        ? ("data/queue_disc_comparison/runs/results-summary-" + qdiscType + "-load-" + std::to_string(loadPercent) + "-run-" + std::to_string(runIndex) + ".csv")
        : customSummaryCsv;

    std::string flowCsvFile = customFlowCsv.empty()
        ? ("data/queue_disc_comparison/runs/flow-results-" + qdiscType + "-load-" + std::to_string(loadPercent) + "-run-" + std::to_string(runIndex) + ".csv")
        : customFlowCsv;

    std::string queueCsvFile = customQueueCsv.empty()
        ? ("data/queue_disc_comparison/runs/queue-validation-" + qdiscType + "-load-" + std::to_string(loadPercent) + "-run-" + std::to_string(runIndex) + ".csv")
        : customQueueCsv;

    // 1. Write Summary CSV
    std::ofstream sOut(summaryCsvFile.c_str());
    if (sOut.is_open())
    {
        sOut << "QDisc,Run,Seed,LoadPercent,TrafficClass,FlowCount,TxPackets,RxPackets,LostPackets,PDR_Percent,"
             << "Throughput_Kbps,MeanDelay_ms,MeanJitter_ms,QueueDrops\n";
        sOut << qdiscType << "," << runIndex << "," << seedValue << "," << loadPercent << ","
             << emerSummary.trafficClass << "," << emerSummary.numFlows << ","
             << emerSummary.totalTx << "," << emerSummary.totalRx << "," << emerSummary.totalLost << ","
             << std::fixed << std::setprecision(2) << emerSummary.aggregatePdr << ","
             << emerSummary.aggregateThroughputKbps << ","
             << emerSummary.weightedMeanDelayMs << ","
             << emerSummary.meanJitterMs << ","
             << emerSummary.queueDrops << "\n";
        sOut << qdiscType << "," << runIndex << "," << seedValue << "," << loadPercent << ","
             << ordSummary.trafficClass << "," << ordSummary.numFlows << ","
             << ordSummary.totalTx << "," << ordSummary.totalRx << "," << ordSummary.totalLost << ","
             << std::fixed << std::setprecision(2) << ordSummary.aggregatePdr << ","
             << ordSummary.aggregateThroughputKbps << ","
             << ordSummary.weightedMeanDelayMs << ","
             << ordSummary.meanJitterMs << ","
             << ordSummary.queueDrops << "\n";
        sOut.close();
    }

    // 2. Write Per-Flow CSV
    std::ofstream fOut(flowCsvFile.c_str());
    if (fOut.is_open())
    {
        fOut << "QDisc,Run,Seed,LoadPercent,FlowIndex,SourceNode,DestServer,TrafficClass,DestIP,DestPort,"
             << "TxPackets,RxPackets,LostPackets,PDR_Percent,Throughput_Kbps,MeanDelay_ms,MeanJitter_ms\n";
        for (uint32_t i = 0; i < 40; ++i)
        {
            const auto& r = flowRecords[i];
            fOut << qdiscType << "," << runIndex << "," << seedValue << "," << loadPercent << ","
                 << r.flowIndex << "," << r.sourceNodeId << "," << r.destServerNodeId << ","
                 << r.trafficClass << "," << r.destIp << "," << r.destPort << ","
                 << r.txPackets << "," << r.rxPackets << "," << r.lostPackets << ","
                 << std::fixed << std::setprecision(2) << r.pdrPercent << ","
                 << r.throughputKbps << "," << r.meanDelayMs << "," << r.meanJitterMs << "\n";
        }
        fOut.close();
    }

    // 3. Write Queue Validation CSV
    std::ofstream qOut(queueCsvFile.c_str());
    if (qOut.is_open())
    {
        qOut << "QDisc,Run,Seed,LoadPercent,Band0Enqueued,Band0Dropped,Band1Enqueued,Band1Dropped,"
             << "EmerEnqueued,EmerDropped,OrdEnqueued,OrdDropped,TotalLost\n";
        qOut << qdiscType << "," << runIndex << "," << seedValue << "," << loadPercent << ","
             << g_band0Enqueued << "," << g_band0Dropped << ","
             << g_band1Enqueued << "," << g_band1Dropped << ","
             << g_genericEmergencyEnqueued << "," << g_genericEmergencyDropped << ","
             << g_genericOrdinaryEnqueued << "," << g_genericOrdinaryDropped << ","
             << (emerSummary.totalLost + ordSummary.totalLost) << "\n";
        qOut.close();
    }

    // Console Output Summary
    std::cout << "\n--------------------------------------------------------------------------------" << std::endl;
    std::cout << "  SIMULATION RESULTS SUMMARY: " << qdiscType << " (Load: " << loadPercent << "%, Run: " << runIndex << ")" << std::endl;
    std::cout << "--------------------------------------------------------------------------------" << std::endl;
    std::cout << std::left << std::setw(18) << "Traffic Class"
              << std::setw(8)  << "Tx Pkts"
              << std::setw(8)  << "Rx Pkts"
              << std::setw(8)  << "Lost"
              << std::setw(10) << "PDR (%)"
              << std::setw(14) << "Thput (Kbps)"
              << std::setw(12) << "Delay (ms)"
              << std::setw(12) << "Jitter (ms)"
              << std::setw(10) << "Q-Drops" << std::endl;
    std::cout << "--------------------------------------------------------------------------------" << std::endl;

    std::cout << std::left << std::setw(18) << emerSummary.trafficClass
              << std::setw(8)  << emerSummary.totalTx
              << std::setw(8)  << emerSummary.totalRx
              << std::setw(8)  << emerSummary.totalLost
              << std::setw(10) << std::fixed << std::setprecision(2) << emerSummary.aggregatePdr
              << std::setw(14) << std::fixed << std::setprecision(2) << emerSummary.aggregateThroughputKbps
              << std::setw(12) << std::fixed << std::setprecision(2) << emerSummary.weightedMeanDelayMs
              << std::setw(12) << std::fixed << std::setprecision(2) << emerSummary.meanJitterMs
              << std::setw(10) << emerSummary.queueDrops << std::endl;

    std::cout << std::left << std::setw(18) << ordSummary.trafficClass
              << std::setw(8)  << ordSummary.totalTx
              << std::setw(8)  << ordSummary.totalRx
              << std::setw(8)  << ordSummary.totalLost
              << std::setw(10) << std::fixed << std::setprecision(2) << ordSummary.aggregatePdr
              << std::setw(14) << std::fixed << std::setprecision(2) << ordSummary.aggregateThroughputKbps
              << std::setw(12) << std::fixed << std::setprecision(2) << ordSummary.weightedMeanDelayMs
              << std::setw(12) << std::fixed << std::setprecision(2) << ordSummary.meanJitterMs
              << std::setw(10) << ordSummary.queueDrops << std::endl;
    std::cout << "--------------------------------------------------------------------------------\n" << std::endl;

    Simulator::Destroy();
    return 0;
}
