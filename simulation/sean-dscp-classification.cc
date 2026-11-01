/*
 * Smart Emergency Alert and Priority Messaging Network (SEAN)
 * Prompt 5: Packet Classification, Alternative DSCP Markings, and Priority Mapping Evaluation
 *
 * File: scratch/sean-dscp-classification.cc
 *
 * Description:
 * Evaluates the impact of IPv4 TOS/DSCP marking and queue classification in
 * a 50-node topology with 3 distinct traffic classes:
 *   1. Emergency Alert Traffic (10 flows, 20 kbps each = 200 kbps)
 *   2. First-Responder Telemetry Traffic (10 flows, 40 kbps each = 400 kbps)
 *   3. Ordinary Civilian Messaging (20 flows, 40 kbps each = 800 kbps)
 * Total nominal payload = 1,400 kbps on a 1.0 Mbps bottleneck (140% overload).
 *
 * Compares:
 *   - Legacy SEAN TOS 0x10 (RFC 1349 Low-Delay)
 *   - Expedited Forwarding (EF): DSCP 46, DS field byte 0xB8 (46 << 2)
 *   - Assured Forwarding AF41: DSCP 34, DS field byte 0x88 (34 << 2)
 *   - Best Effort: DSCP 0, DS field byte 0x00
 *   - Class Selector 6 (CS6): DSCP 48, DS field byte 0xC0 (48 << 2)
 *   - Unshifted DSCP values (e.g. 0x2E, 0x22) to evaluate RFC 2474 bit-shift effects
 *   - FIFO QueueDisc vs. PrioQueueDisc (Default Linux Priomap vs. Custom DiffServ Priomap)
 *
 * Network Simulator 3 (ns-3.49)
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
#include <iomanip>
#include <vector>
#include <string>
#include <map>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SeanDscpClassification");

// =============================================================================
// GLOBAL QUEUE DISC METRIC COUNTERS
// =============================================================================
// Track enqueues and drops broken down by traffic class
// Traffic classes: 0 = Emergency, 1 = Telemetry, 2 = Ordinary
static uint32_t g_fifoEnqueueCount[3] = {0, 0, 0};
static uint32_t g_fifoDropCount[3]    = {0, 0, 0};

static uint32_t g_bandEnqueueCount[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}; // [band][class]
static uint32_t g_bandDropCount[3][3]    = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}; // [band][class]

// Observed TOS bytes per class
static std::map<uint8_t, uint32_t> g_observedTos[3];

// Helper to determine traffic class from IPv4 source address
// Source addresses are assigned as: 10.1.(nodeId + 1).1
// Nodes 0..9   -> 10.1.1.1  .. 10.1.10.1 -> Class 0 (Emergency)
// Nodes 10..19 -> 10.1.11.1 .. 10.1.20.1 -> Class 1 (Telemetry)
// Nodes 20..39 -> 10.1.21.1 .. 10.1.40.1 -> Class 2 (Ordinary)
static int GetTrafficClassFromIp(Ipv4Address src)
{
    uint32_t ip = src.Get();
    uint8_t octet3 = (ip >> 8) & 0xFF; // Subnet index k
    if (octet3 >= 1 && octet3 <= 10)
    {
        return 0; // Emergency Alert
    }
    else if (octet3 >= 11 && octet3 <= 20)
    {
        return 1; // First Responder Telemetry
    }
    else if (octet3 >= 21 && octet3 <= 40)
    {
        return 2; // Ordinary Civilian
    }
    return 2; // Default fallback
}

// Trace callbacks for FIFO QueueDisc
static void TraceFifoEnqueue(Ptr<const QueueDiscItem> item)
{
    Ptr<const Ipv4QueueDiscItem> ipv4Item = DynamicCast<const Ipv4QueueDiscItem>(item);
    if (ipv4Item)
    {
        int cls = GetTrafficClassFromIp(ipv4Item->GetHeader().GetSource());
        g_fifoEnqueueCount[cls]++;
        g_observedTos[cls][ipv4Item->GetHeader().GetTos()]++;
    }
}

static void TraceFifoDrop(Ptr<const QueueDiscItem> item)
{
    Ptr<const Ipv4QueueDiscItem> ipv4Item = DynamicCast<const Ipv4QueueDiscItem>(item);
    if (ipv4Item)
    {
        int cls = GetTrafficClassFromIp(ipv4Item->GetHeader().GetSource());
        g_fifoDropCount[cls]++;
    }
}

// Trace callbacks for PrioQueueDisc Bands (0, 1, 2)
static void TraceBand0Enqueue(Ptr<const QueueDiscItem> item)
{
    Ptr<const Ipv4QueueDiscItem> ipv4Item = DynamicCast<const Ipv4QueueDiscItem>(item);
    if (ipv4Item)
    {
        int cls = GetTrafficClassFromIp(ipv4Item->GetHeader().GetSource());
        g_bandEnqueueCount[0][cls]++;
        g_observedTos[cls][ipv4Item->GetHeader().GetTos()]++;
    }
}

static void TraceBand0Drop(Ptr<const QueueDiscItem> item)
{
    Ptr<const Ipv4QueueDiscItem> ipv4Item = DynamicCast<const Ipv4QueueDiscItem>(item);
    if (ipv4Item)
    {
        int cls = GetTrafficClassFromIp(ipv4Item->GetHeader().GetSource());
        g_bandDropCount[0][cls]++;
    }
}

static void TraceBand1Enqueue(Ptr<const QueueDiscItem> item)
{
    Ptr<const Ipv4QueueDiscItem> ipv4Item = DynamicCast<const Ipv4QueueDiscItem>(item);
    if (ipv4Item)
    {
        int cls = GetTrafficClassFromIp(ipv4Item->GetHeader().GetSource());
        g_bandEnqueueCount[1][cls]++;
        g_observedTos[cls][ipv4Item->GetHeader().GetTos()]++;
    }
}

static void TraceBand1Drop(Ptr<const QueueDiscItem> item)
{
    Ptr<const Ipv4QueueDiscItem> ipv4Item = DynamicCast<const Ipv4QueueDiscItem>(item);
    if (ipv4Item)
    {
        int cls = GetTrafficClassFromIp(ipv4Item->GetHeader().GetSource());
        g_bandDropCount[1][cls]++;
    }
}

static void TraceBand2Enqueue(Ptr<const QueueDiscItem> item)
{
    Ptr<const Ipv4QueueDiscItem> ipv4Item = DynamicCast<const Ipv4QueueDiscItem>(item);
    if (ipv4Item)
    {
        int cls = GetTrafficClassFromIp(ipv4Item->GetHeader().GetSource());
        g_bandEnqueueCount[2][cls]++;
        g_observedTos[cls][ipv4Item->GetHeader().GetTos()]++;
    }
}

static void TraceBand2Drop(Ptr<const QueueDiscItem> item)
{
    Ptr<const Ipv4QueueDiscItem> ipv4Item = DynamicCast<const Ipv4QueueDiscItem>(item);
    if (ipv4Item)
    {
        int cls = GetTrafficClassFromIp(ipv4Item->GetHeader().GetSource());
        g_bandDropCount[2][cls]++;
    }
}

// =============================================================================
// DATA STRUCTURES FOR FLOW DEFINITION & RESULTS
// =============================================================================
struct FlowConfig
{
    uint32_t flowIndex;
    uint32_t sourceNodeId;
    uint32_t destServerNodeId;
    uint32_t serverIndex;
    std::string trafficClass; // "Emergency Alert", "Telemetry", "Ordinary Msg"
    Ipv4Address destIp;
    uint16_t destPort;
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
    uint8_t tos;
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
    uint8_t configuredTos;
    uint32_t totalTx;
    uint32_t totalRx;
    uint32_t totalLost;
    double aggregatePdr;
    double aggregateThroughputKbps;
    double weightedMeanDelayMs;
    double meanJitterMs;
    uint32_t queueDrops;
    uint32_t band0Enqueued;
    uint32_t band1Enqueued;
    uint32_t band2Enqueued;
    uint32_t band0Dropped;
    uint32_t band1Dropped;
    uint32_t band2Dropped;
};

// =============================================================================
// JITTERED TRAFFIC APPLICATION
// =============================================================================
class DscpTrafficApp : public Application
{
public:
    DscpTrafficApp()
        : m_socket(nullptr),
          m_peer(),
          m_packetSize(512),
          m_dataRate("40kb/s"),
          m_tos(0x00),
          m_jitterRatio(0.20),
          m_running(false),
          m_packetsSent(0)
    {
    }

    ~DscpTrafficApp() override
    {
        m_socket = nullptr;
    }

    void Setup(Address peer, uint32_t packetSize, DataRate dataRate, uint8_t tos, double jitter = 0.20)
    {
        m_peer = peer;
        m_packetSize = packetSize;
        m_dataRate = dataRate;
        m_tos = tos;
        m_jitterRatio = jitter;
    }

private:
    void StartApplication() override
    {
        m_running = true;
        m_packetsSent = 0;

        m_socket = Socket::CreateSocket(GetNode(), UdpSocketFactory::GetTypeId());
        m_socket->Bind();
        m_socket->Connect(m_peer);
        m_socket->SetIpTos(m_tos);

        m_uniformVar = CreateObject<UniformRandomVariable>();

        // Uniform random start jitter across first 1.0 second to avoid synchronization
        double startJitter = m_uniformVar->GetValue(0.0, 1.0);
        m_sendEvent = Simulator::Schedule(Seconds(startJitter), &DscpTrafficApp::SendPacket, this);
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

        ScheduleNextTx();
    }

    void ScheduleNextTx()
    {
        if (!m_running) return;

        double nominalInterval = static_cast<double>(m_packetSize * 8) / m_dataRate.GetBitRate();
        double nextInterval = nominalInterval;

        if (m_jitterRatio > 0.0)
        {
            double low = nominalInterval * (1.0 - m_jitterRatio);
            double high = nominalInterval * (1.0 + m_jitterRatio);
            m_uniformVar->SetAttribute("Min", DoubleValue(low));
            m_uniformVar->SetAttribute("Max", DoubleValue(high));
            nextInterval = m_uniformVar->GetValue();
        }

        m_sendEvent = Simulator::Schedule(Seconds(nextInterval), &DscpTrafficApp::SendPacket, this);
    }

    Ptr<Socket> m_socket;
    Address m_peer;
    uint32_t m_packetSize;
    DataRate m_dataRate;
    uint8_t m_tos;
    double m_jitterRatio;
    bool m_running;
    uint32_t m_packetsSent;
    EventId m_sendEvent;
    Ptr<UniformRandomVariable> m_uniformVar;
};

// =============================================================================
// MAIN SIMULATION ENTRY POINT
// =============================================================================
int
main(int argc, char* argv[])
{
    std::string conditionName = "prio_custom_diffserv";
    std::string qdiscType = "prio"; // "fifo" or "prio"
    std::string priomapStr = "2 2 1 2 0 2 0 0 2 2 2 2 2 2 2 2"; // Custom DiffServ Priomap
    std::string emTosStr = "0xB8";    // Expedited Forwarding (EF) DSCP 46 << 2
    std::string telemTosStr = "0x88"; // Assured Forwarding (AF41) DSCP 34 << 2
    std::string ordTosStr = "0x00";   // Best Effort (BE) DSCP 0 << 2
    uint32_t runIndex = 1;
    uint32_t seedValue = 1;
    double simTime = 30.0;
    uint32_t packetSize = 512;
    std::string emRateStr = "20kb/s";
    std::string telemRateStr = "40kb/s";
    std::string ordRateStr = "40kb/s";
    std::string outDir = "data/dscp_classification/runs";

    CommandLine cmd(__FILE__);
    cmd.AddValue("condition", "Condition name identifier", conditionName);
    cmd.AddValue("qdiscType", "Queue discipline type (fifo or prio)", qdiscType);
    cmd.AddValue("priomap", "Priomap string for PrioQueueDisc (16 integers, space/comma separated)", priomapStr);
    cmd.AddValue("emTos", "Emergency Alert IPv4 TOS/DS field byte (hex or dec)", emTosStr);
    cmd.AddValue("telemTos", "Telemetry IPv4 TOS/DS field byte (hex or dec)", telemTosStr);
    cmd.AddValue("ordTos", "Ordinary Civilian IPv4 TOS/DS field byte (hex or dec)", ordTosStr);
    cmd.AddValue("run", "Replication run index (1..10)", runIndex);
    cmd.AddValue("seed", "RNG seed value (1..10)", seedValue);
    cmd.AddValue("simTime", "Simulation duration in seconds", simTime);
    cmd.AddValue("packetSize", "UDP payload packet size in bytes", packetSize);
    cmd.AddValue("emRate", "Individual Emergency source data rate", emRateStr);
    cmd.AddValue("telemRate", "Individual Telemetry source data rate", telemRateStr);
    cmd.AddValue("ordRate", "Individual Ordinary source data rate", ordRateStr);
    cmd.AddValue("outDir", "Output directory for CSV records", outDir);
    cmd.Parse(argc, argv);

    // Normalize priomapStr: replace commas, underscores, or quotes with spaces
    for (char& ch : priomapStr)
    {
        if (ch == ',' || ch == '_' || ch == '\'' || ch == '\"')
        {
            ch = ' ';
        }
    }
    // Trim and normalize multiple spaces
    std::istringstream prioIss(priomapStr);
    std::string token;
    std::vector<std::string> prioTokens;
    while (prioIss >> token)
    {
        prioTokens.push_back(token);
    }
    if (prioTokens.size() == 16)
    {
        std::ostringstream oss;
        for (size_t i = 0; i < 16; ++i)
        {
            if (i > 0) oss << " ";
            oss << prioTokens[i];
        }
        priomapStr = oss.str();
    }

    uint8_t emTos = static_cast<uint8_t>(std::stoul(emTosStr, nullptr, 0) & 0xFF);
    uint8_t telemTos = static_cast<uint8_t>(std::stoul(telemTosStr, nullptr, 0) & 0xFF);
    uint8_t ordTos = static_cast<uint8_t>(std::stoul(ordTosStr, nullptr, 0) & 0xFF);

    bool isPrio = (qdiscType == "prio");

    // Configure RNG Seeds
    SeedManager::SetSeed(seedValue);
    SeedManager::SetRun(runIndex);

    std::cout << "================================================================================" << std::endl;
    std::cout << "  SEAN Prompt 5: Packet Classification & Alternative DSCP Markings (ns-3.49)    " << std::endl;
    std::cout << "  Condition Name   : " << conditionName << std::endl;
    std::cout << "  Queue Discipline : " << (isPrio ? "PrioQueueDisc" : "FifoQueueDisc") << std::endl;
    if (isPrio)
    {
        std::cout << "  Priomap String   : \"" << priomapStr << "\"" << std::endl;
    }
    std::cout << "  Markings (TOS)   : Emergency=0x" << std::hex << (int)emTos
              << " (DSCP " << std::dec << (emTos >> 2) << ")"
              << ", Telemetry=0x" << std::hex << (int)telemTos
              << " (DSCP " << std::dec << (telemTos >> 2) << ")"
              << ", Ordinary=0x" << std::hex << (int)ordTos
              << " (DSCP " << std::dec << (ordTos >> 2) << ")" << std::endl;
    std::cout << "  Replication Run  : " << runIndex << " (Seed: " << seedValue << ")" << std::endl;
    std::cout << "  Bottleneck Link  : 1 Mbps, 10 ms, Queue Capacity: 50 packets" << std::endl;
    std::cout << "================================================================================" << std::endl;

    // Reset metric counters
    for (int c = 0; c < 3; ++c)
    {
        g_fifoEnqueueCount[c] = 0;
        g_fifoDropCount[c] = 0;
        g_observedTos[c].clear();
        for (int b = 0; b < 3; ++b)
        {
            g_bandEnqueueCount[b][c] = 0;
            g_bandDropCount[b][c] = 0;
        }
    }

    // -------------------------------------------------------------------------
    // 1. NODE TOPOLOGY CREATION (50 NODES)
    // -------------------------------------------------------------------------
    NodeContainer clientNodes;
    clientNodes.Create(40); // 0..39

    NodeContainer r1Node, r2Node, r3Node;
    r1Node.Create(1); // 40 (Region 1 Aggregator)
    r2Node.Create(1); // 41 (Region 2 Aggregator)
    r3Node.Create(1); // 42 (Region 3 Aggregator)

    NodeContainer gwInNode, gwEgNode;
    gwInNode.Create(1); // 43 (Core Ingress Gateway)
    gwEgNode.Create(1); // 44 (Core Egress Gateway)

    NodeContainer serverNodes;
    serverNodes.Create(5); // 45..49 (Servers S0..S4)

    NodeContainer allNodes;
    allNodes.Add(clientNodes);
    allNodes.Add(r1Node);
    allNodes.Add(r2Node);
    allNodes.Add(r3Node);
    allNodes.Add(gwInNode);
    allNodes.Add(gwEgNode);
    allNodes.Add(serverNodes);

    NS_ASSERT_MSG(allNodes.GetN() == 50, "FATAL ERROR: Topology must contain exactly 50 nodes!");

    // -------------------------------------------------------------------------
    // 2. POINT-TO-POINT LINK INSTALLATION (49 LINKS)
    // -------------------------------------------------------------------------
    PointToPointHelper accessLink;
    accessLink.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    accessLink.SetChannelAttribute("Delay", StringValue("2ms"));
    accessLink.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue(QueueSize("1p")));

    PointToPointHelper aggLink;
    aggLink.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    aggLink.SetChannelAttribute("Delay", StringValue("2ms"));
    aggLink.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue(QueueSize("1p")));

    PointToPointHelper bottleneckLink;
    bottleneckLink.SetDeviceAttribute("DataRate", StringValue("1Mbps"));
    bottleneckLink.SetChannelAttribute("Delay", StringValue("10ms"));
    bottleneckLink.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue(QueueSize("1p")));

    PointToPointHelper serverLink;
    serverLink.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    serverLink.SetChannelAttribute("Delay", StringValue("2ms"));
    serverLink.SetQueue("ns3::DropTailQueue<Packet>", "MaxSize", QueueSizeValue(QueueSize("1p")));

    // Connect 40 Clients to Aggregators
    std::vector<NetDeviceContainer> accessDevs(40);
    uint32_t linkCount = 0;
    for (uint32_t i = 0; i < 40; ++i)
    {
        if (i < 14)
        {
            accessDevs[i] = accessLink.Install(clientNodes.Get(i), r1Node.Get(0));
        }
        else if (i < 27)
        {
            accessDevs[i] = accessLink.Install(clientNodes.Get(i), r2Node.Get(0));
        }
        else
        {
            accessDevs[i] = accessLink.Install(clientNodes.Get(i), r3Node.Get(0));
        }
        linkCount++;
    }

    // Connect Aggregators to Core Gateway Ingress
    NetDeviceContainer r1_gwIn = aggLink.Install(r1Node.Get(0), gwInNode.Get(0)); linkCount++;
    NetDeviceContainer r2_gwIn = aggLink.Install(r2Node.Get(0), gwInNode.Get(0)); linkCount++;
    NetDeviceContainer r3_gwIn = aggLink.Install(r3Node.Get(0), gwInNode.Get(0)); linkCount++;

    // Bottleneck Link: GW_in -> GW_eg
    NetDeviceContainer gwIn_gwEg = bottleneckLink.Install(gwInNode.Get(0), gwEgNode.Get(0)); linkCount++;

    // Server Links: GW_eg -> Servers S0..S4
    std::vector<NetDeviceContainer> serverDevs(5);
    for (uint32_t s = 0; s < 5; ++s)
    {
        serverDevs[s] = serverLink.Install(gwEgNode.Get(0), serverNodes.Get(s));
        linkCount++;
    }

    NS_ASSERT_MSG(linkCount == 49, "FATAL ERROR: Expected exactly 49 point-to-point links!");

    // -------------------------------------------------------------------------
    // 3. INTERNET STACK & IP ADDRESSING (49 SUBNETS)
    // -------------------------------------------------------------------------
    InternetStackHelper stack;
    stack.Install(allNodes);

    Ipv4AddressHelper address;

    // Subnets for 40 Access Links: 10.1.k.0/24 (k = 1..40)
    std::vector<Ipv4InterfaceContainer> accessIfs(40);
    for (uint32_t i = 0; i < 40; ++i)
    {
        std::ostringstream subnet;
        subnet << "10.1." << (i + 1) << ".0";
        address.SetBase(subnet.str().c_str(), "255.255.255.0");
        accessIfs[i] = address.Assign(accessDevs[i]);
    }

    // Subnets for 3 Regional Aggregation Links
    address.SetBase("10.2.1.0", "255.255.255.0");
    address.Assign(r1_gwIn);
    address.SetBase("10.2.2.0", "255.255.255.0");
    address.Assign(r2_gwIn);
    address.SetBase("10.2.3.0", "255.255.255.0");
    address.Assign(r3_gwIn);

    // Subnet for Shared Bottleneck Link: 10.3.1.0/24
    address.SetBase("10.3.1.0", "255.255.255.0");
    address.Assign(gwIn_gwEg);

    // Subnets for 5 Server Distribution Links
    std::vector<Ipv4InterfaceContainer> serverIfs(5);
    std::vector<Ipv4Address> serverIps(5);
    for (uint32_t s = 0; s < 5; ++s)
    {
        std::ostringstream subnet;
        subnet << "10.4." << (s + 1) << ".0";
        address.SetBase(subnet.str().c_str(), "255.255.255.0");
        serverIfs[s] = address.Assign(serverDevs[s]);
        serverIps[s] = serverIfs[s].GetAddress(1); // Server IP
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // -------------------------------------------------------------------------
    // 4. TRAFFIC CONTROL AT CORE BOTTLENECK GATEWAY (NODE 43 EGRESS)
    // -------------------------------------------------------------------------
    Ptr<NetDevice> devBottleneck = gwIn_gwEg.Get(0);
    TrafficControlHelper tch;
    tch.Uninstall(devBottleneck);

    Ptr<QueueDisc> rootQdisc = nullptr;
    std::string qSizeStr = "50p";

    if (isPrio)
    {
        uint16_t handle = tch.SetRootQueueDisc(
            "ns3::PrioQueueDisc",
            "Priomap",
            StringValue(priomapStr));

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
    else
    {
        tch.SetRootQueueDisc("ns3::FifoQueueDisc", "MaxSize", QueueSizeValue(QueueSize(qSizeStr)));
        QueueDiscContainer qdiscs = tch.Install(devBottleneck);
        rootQdisc = qdiscs.Get(0);

        rootQdisc->TraceConnectWithoutContext("Enqueue", MakeCallback(&TraceFifoEnqueue));
        rootQdisc->TraceConnectWithoutContext("Drop", MakeCallback(&TraceFifoDrop));
    }

    // -------------------------------------------------------------------------
    // 5. APPLICATION FLOW DEFINITIONS & INSTALLATION (40 FLOWS, 3 CLASSES)
    // -------------------------------------------------------------------------
    PacketSinkHelper sinkHelper("ns3::UdpSocketFactory", Address());
    ApplicationContainer sinkApps;
    std::vector<FlowConfig> flowConfigs(40);

    for (uint32_t i = 0; i < 40; ++i)
    {
        FlowConfig fc;
        fc.flowIndex = i;
        fc.sourceNodeId = i;

        if (i < 10)
        {
            // Class 0: Emergency Alert Source (Nodes 0..9)
            // Destination servers S0..S4 (ports 9000, 9001)
            fc.serverIndex = i / 2;
            fc.destServerNodeId = 45 + fc.serverIndex;
            fc.destIp = serverIps[fc.serverIndex];
            fc.destPort = 9000 + (i % 2);
            fc.trafficClass = "Emergency Alert";
            fc.tos = emTos;
            fc.rate = DataRate(emRateStr);
        }
        else if (i < 20)
        {
            // Class 1: First-Responder Telemetry Source (Nodes 10..19)
            // Destination servers S0..S4 (ports 9002, 9003)
            uint32_t telemIdx = i - 10;
            fc.serverIndex = telemIdx / 2;
            fc.destServerNodeId = 45 + fc.serverIndex;
            fc.destIp = serverIps[fc.serverIndex];
            fc.destPort = 9002 + (telemIdx % 2);
            fc.trafficClass = "Telemetry";
            fc.tos = telemTos;
            fc.rate = DataRate(telemRateStr);
        }
        else
        {
            // Class 2: Ordinary Civilian Messaging (Nodes 20..39)
            // Destination servers S0..S4 (ports 9004..9007)
            uint32_t ordIdx = i - 20;
            fc.serverIndex = ordIdx / 4;
            fc.destServerNodeId = 45 + fc.serverIndex;
            fc.destIp = serverIps[fc.serverIndex];
            fc.destPort = 9004 + (ordIdx % 4);
            fc.trafficClass = "Ordinary Msg";
            fc.tos = ordTos;
            fc.rate = DataRate(ordRateStr);
        }

        flowConfigs[i] = fc;

        sinkHelper.SetAttribute("Local", AddressValue(InetSocketAddress(Ipv4Address::GetAny(), fc.destPort)));
        sinkApps.Add(sinkHelper.Install(serverNodes.Get(fc.serverIndex)));
    }

    sinkApps.Start(Seconds(0.5));
    sinkApps.Stop(Seconds(simTime + 1.0));

    // Install Sender Applications
    ApplicationContainer clientApps;
    for (uint32_t i = 0; i < 40; ++i)
    {
        Ptr<DscpTrafficApp> app = CreateObject<DscpTrafficApp>();
        Address remoteAddress(InetSocketAddress(flowConfigs[i].destIp, flowConfigs[i].destPort));
        app->Setup(remoteAddress, packetSize, flowConfigs[i].rate, flowConfigs[i].tos, 0.20);
        clientNodes.Get(i)->AddApplication(app);
        app->SetStartTime(Seconds(1.0));
        app->SetStopTime(Seconds(simTime));
        clientApps.Add(app);
    }

    // -------------------------------------------------------------------------
    // 6. FLOW MONITOR SETUP & SIMULATION RUN
    // -------------------------------------------------------------------------
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();
    monitor->SetAttribute("DelayBinWidth", DoubleValue(0.001));

    Simulator::Stop(Seconds(simTime + 1.0));
    Simulator::Run();

    // -------------------------------------------------------------------------
    // 7. TELEMETRY EXTRACTION & METRIC CALCULATION
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
        flowRecords[i].tos = flowConfigs[i].tos;
        flowRecords[i].txPackets = 0;
        flowRecords[i].rxPackets = 0;
        flowRecords[i].lostPackets = 0;
        flowRecords[i].pdrPercent = 0.0;
        flowRecords[i].throughputKbps = 0.0;
        flowRecords[i].meanDelayMs = 0.0;
        flowRecords[i].meanJitterMs = 0.0;
    }

    const double standardActiveDuration = simTime - 1.0; // 29.0s active transmission window

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

    // Aggregate Metrics per Class
    ClassSummaryMetrics classSummaries[3] = {
        {"Emergency Alert", 10, emTos,    0, 0, 0, 0.0, 0.0, 0.0, 0.0, 0, 0, 0, 0, 0, 0, 0},
        {"Telemetry",       10, telemTos, 0, 0, 0, 0.0, 0.0, 0.0, 0.0, 0, 0, 0, 0, 0, 0, 0},
        {"Ordinary Msg",    20, ordTos,   0, 0, 0, 0.0, 0.0, 0.0, 0.0, 0, 0, 0, 0, 0, 0, 0}
    };

    double delayWeightSum[3] = {0.0, 0.0, 0.0};
    double jitterSum[3]      = {0.0, 0.0, 0.0};

    for (uint32_t i = 0; i < 40; ++i)
    {
        const auto& r = flowRecords[i];
        int c = (i < 10) ? 0 : ((i < 20) ? 1 : 2);

        classSummaries[c].totalTx += r.txPackets;
        classSummaries[c].totalRx += r.rxPackets;
        classSummaries[c].totalLost += r.lostPackets;
        classSummaries[c].aggregateThroughputKbps += r.throughputKbps;

        delayWeightSum[c] += r.meanDelayMs * r.rxPackets;
        jitterSum[c] += r.meanJitterMs;
    }

    for (int c = 0; c < 3; ++c)
    {
        auto& cs = classSummaries[c];
        if (cs.totalTx > 0)
        {
            cs.aggregatePdr = (100.0 * cs.totalRx) / cs.totalTx;
        }
        if (cs.totalRx > 0)
        {
            cs.weightedMeanDelayMs = delayWeightSum[c] / cs.totalRx;
        }
        if (cs.numFlows > 0)
        {
            cs.meanJitterMs = jitterSum[c] / cs.numFlows;
        }

        if (isPrio)
        {
            cs.band0Enqueued = g_bandEnqueueCount[0][c];
            cs.band1Enqueued = g_bandEnqueueCount[1][c];
            cs.band2Enqueued = g_bandEnqueueCount[2][c];
            cs.band0Dropped  = g_bandDropCount[0][c];
            cs.band1Dropped  = g_bandDropCount[1][c];
            cs.band2Dropped  = g_bandDropCount[2][c];
            cs.queueDrops    = cs.band0Dropped + cs.band1Dropped + cs.band2Dropped;
        }
        else
        {
            cs.band0Enqueued = 0;
            cs.band1Enqueued = g_fifoEnqueueCount[c];
            cs.band2Enqueued = 0;
            cs.band0Dropped  = 0;
            cs.band1Dropped  = g_fifoDropCount[c];
            cs.band2Dropped  = 0;
            cs.queueDrops    = g_fifoDropCount[c];
        }
    }

    // Print Console Summary Table
    std::cout << "\n========================================================================================================================" << std::endl;
    std::cout << "                 50-NODE SIMULATION RESULTS SUMMARY (Condition: " << conditionName << ", Run: " << runIndex << ")            " << std::endl;
    std::cout << "========================================================================================================================" << std::endl;
    std::cout << std::left
              << std::setw(18) << "Traffic Class"
              << std::setw(8)  << "TOS/DSCP"
              << std::setw(7)  << "Flows"
              << std::setw(10) << "Tx (pkts)"
              << std::setw(10) << "Rx (pkts)"
              << std::setw(11) << "Lost (pkts)"
              << std::setw(10) << "PDR (%)"
              << std::setw(13) << "Tput (Kbps)"
              << std::setw(14) << "Mean Dly (ms)"
              << std::setw(12) << "Jitter (ms)"
              << std::setw(14) << "Queue Drops"
              << "Band Placements (B0/B1/B2)"
              << std::endl;
    std::cout << "------------------------------------------------------------------------------------------------------------------------" << std::endl;

    for (int c = 0; c < 3; ++c)
    {
        const auto& cs = classSummaries[c];
        std::ostringstream tosStr, bandStr;
        tosStr << "0x" << std::hex << std::uppercase << (int)cs.configuredTos << "/" << std::dec << (cs.configuredTos >> 2);
        bandStr << cs.band0Enqueued << "/" << cs.band1Enqueued << "/" << cs.band2Enqueued;

        std::cout << std::left
                  << std::setw(18) << cs.trafficClass
                  << std::setw(8)  << tosStr.str()
                  << std::setw(7)  << cs.numFlows
                  << std::setw(10) << cs.totalTx
                  << std::setw(10) << cs.totalRx
                  << std::setw(11) << cs.totalLost
                  << std::fixed << std::setprecision(2)
                  << std::setw(10) << cs.aggregatePdr
                  << std::setw(13) << cs.aggregateThroughputKbps
                  << std::setw(14) << cs.weightedMeanDelayMs
                  << std::setw(12) << cs.meanJitterMs
                  << std::setw(14) << cs.queueDrops
                  << bandStr.str()
                  << std::endl;
    }
    std::cout << "========================================================================================================================" << std::endl;

    // -------------------------------------------------------------------------
    // 8. WRITE CSV FILES
    // -------------------------------------------------------------------------
    // A. Class Summary CSV
    std::ostringstream summaryFilename;
    summaryFilename << outDir << "/results-summary-" << conditionName << "-run-" << runIndex << ".csv";
    std::ofstream summaryFile(summaryFilename.str().c_str());
    if (summaryFile.is_open())
    {
        summaryFile << "condition,qdisc,priomap,run,seed,traffic_class,configured_tos,dscp,num_flows,"
                    << "total_tx,total_rx,total_lost,pdr_percent,throughput_kbps,mean_delay_ms,mean_jitter_ms,"
                    << "queue_drops,band0_enqueued,band1_enqueued,band2_enqueued,band0_dropped,band1_dropped,band2_dropped\n";
        for (int c = 0; c < 3; ++c)
        {
            const auto& cs = classSummaries[c];
            summaryFile << conditionName << ","
                        << qdiscType << ","
                        << "\"" << priomapStr << "\","
                        << runIndex << ","
                        << seedValue << ","
                        << cs.trafficClass << ","
                        << "0x" << std::hex << (int)cs.configuredTos << std::dec << ","
                        << (cs.configuredTos >> 2) << ","
                        << cs.numFlows << ","
                        << cs.totalTx << ","
                        << cs.totalRx << ","
                        << cs.totalLost << ","
                        << std::fixed << std::setprecision(4)
                        << cs.aggregatePdr << ","
                        << cs.aggregateThroughputKbps << ","
                        << cs.weightedMeanDelayMs << ","
                        << cs.meanJitterMs << ","
                        << cs.queueDrops << ","
                        << cs.band0Enqueued << ","
                        << cs.band1Enqueued << ","
                        << cs.band2Enqueued << ","
                        << cs.band0Dropped << ","
                        << cs.band1Dropped << ","
                        << cs.band2Dropped << "\n";
        }
        summaryFile.close();
        std::cout << "[Saved] Summary CSV: " << summaryFilename.str() << std::endl;
    }
    else
    {
        std::cerr << "ERROR: Failed to open " << summaryFilename.str() << " for writing!" << std::endl;
    }

    // B. Flow-level CSV
    std::ostringstream flowFilename;
    flowFilename << outDir << "/flow-results-" << conditionName << "-run-" << runIndex << ".csv";
    std::ofstream flowFile(flowFilename.str().c_str());
    if (flowFile.is_open())
    {
        flowFile << "condition,qdisc,run,seed,flow_index,source_node,dest_server,traffic_class,"
                 << "configured_tos,dscp,dest_ip,dest_port,tx_packets,rx_packets,lost_packets,"
                 << "pdr_percent,throughput_kbps,mean_delay_ms,mean_jitter_ms\n";
        for (uint32_t i = 0; i < 40; ++i)
        {
            const auto& r = flowRecords[i];
            flowFile << conditionName << ","
                     << qdiscType << ","
                     << runIndex << ","
                     << seedValue << ","
                     << r.flowIndex << ","
                     << r.sourceNodeId << ","
                     << r.destServerNodeId << ","
                     << r.trafficClass << ","
                     << "0x" << std::hex << (int)r.tos << std::dec << ","
                     << (r.tos >> 2) << ","
                     << r.destIp << ","
                     << r.destPort << ","
                     << r.txPackets << ","
                     << r.rxPackets << ","
                     << r.lostPackets << ","
                     << std::fixed << std::setprecision(4)
                     << r.pdrPercent << ","
                     << r.throughputKbps << ","
                     << r.meanDelayMs << ","
                     << r.meanJitterMs << "\n";
        }
        flowFile.close();
        std::cout << "[Saved] Flow CSV:    " << flowFilename.str() << std::endl;
    }

    // C. Queue Band Mapping CSV
    std::ostringstream qbFilename;
    qbFilename << outDir << "/queue-bands-" << conditionName << "-run-" << runIndex << ".csv";
    std::ofstream qbFile(qbFilename.str().c_str());
    if (qbFile.is_open())
    {
        qbFile << "condition,qdisc,priomap,run,seed,traffic_class,configured_tos,dscp,"
               << "band_0_enq,band_1_enq,band_2_enq,band_0_drop,band_1_drop,band_2_drop,total_enq,total_drop\n";
        for (int c = 0; c < 3; ++c)
        {
            const auto& cs = classSummaries[c];
            uint32_t totEnq = cs.band0Enqueued + cs.band1Enqueued + cs.band2Enqueued;
            uint32_t totDrop = cs.band0Dropped + cs.band1Dropped + cs.band2Dropped;
            qbFile << conditionName << ","
                   << qdiscType << ","
                   << "\"" << priomapStr << "\","
                   << runIndex << ","
                   << seedValue << ","
                   << cs.trafficClass << ","
                   << "0x" << std::hex << (int)cs.configuredTos << std::dec << ","
                   << (cs.configuredTos >> 2) << ","
                   << cs.band0Enqueued << ","
                   << cs.band1Enqueued << ","
                   << cs.band2Enqueued << ","
                   << cs.band0Dropped << ","
                   << cs.band1Dropped << ","
                   << cs.band2Dropped << ","
                   << totEnq << ","
                   << totDrop << "\n";
        }
        qbFile.close();
        std::cout << "[Saved] Queue Bands: " << qbFilename.str() << std::endl;
    }

    Simulator::Destroy();
    return 0;
}
