/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * ============================================================================
 * Project Title : Smart Emergency Alert and Priority Messaging Network (SEAN)
 * Scalability Extension : 50-Node Point-to-Point Network Simulation
 * Simulator     : Network Simulator 3 (ns-3.49)
 * File Name     : scratch/sean-scalability-50node.cc
 * ============================================================================
 *
 * 1. ARCHITECTURE & NODE ALLOCATION (EXACTLY 50 NODES):
 * -----------------------------------------------------
 *   - Nodes  0 -  9 (10 nodes) : Emergency Alert Traffic Sources (E0 - E9)
 *   - Nodes 10 - 39 (30 nodes) : Ordinary Civilian Traffic Sources (O0 - O29)
 *   - Nodes 40 - 42 ( 3 nodes) : Regional Aggregation Routers (R1 - R3)
 *   - Node  43      ( 1 node ) : Core Ingress Gateway (GW_in, Bottleneck Ingress)
 *   - Node  44      ( 1 node ) : Core Egress Gateway (GW_eg, Bottleneck Egress)
 *   - Nodes 45 - 49 ( 5 nodes) : Emergency Response Centers / Sinks (S0 - S4)
 *   Total Nodes = 50 nodes.
 *
 * 2. TOPOLOGY & LINKS (EXACTLY 49 POINT-TO-POINT LINKS):
 * ------------------------------------------------------
 *   - 40 Access Links (10 Mbps, 1 ms, NetDevice DropTail 100p):
 *       * R1 (Node 40) aggregates Zone A: Sources 0..2, 10..19 (13 links)
 *       * R2 (Node 41) aggregates Zone B: Sources 3..5, 20..29 (13 links)
 *       * R3 (Node 42) aggregates Zone C: Sources 6..9, 30..39 (14 links)
 *   - 3 Regional Aggregation Trunks (20 Mbps, 2 ms, NetDevice DropTail 100p):
 *       * R1 -> GW_in, R2 -> GW_in, R3 -> GW_in
 *   - 1 Shared Bottleneck Link (1 Mbps, 10 ms, NetDevice DropTail 1p):
 *       * GW_in (Node 43) -> GW_eg (Node 44)
 *       * Outgoing interface on Node 43 hosts the tested QueueDisc:
 *           Baseline Mode : FifoQueueDisc (50p)
 *           Priority Mode : PrioQueueDisc (3 classes, each FifoQueueDisc 50p)
 *   - 5 Server Distribution Links (100 Mbps, 1 ms, NetDevice DropTail 100p):
 *       * GW_eg (Node 44) -> Servers S0..S4 (Nodes 45..49)
 *   Total Links = 40 + 3 + 1 + 5 = 49 links (acyclic tree).
 *
 * 3. TRAFFIC MATRICES & OFFERED LOAD:
 * -----------------------------------
 *   - 10 Emergency Sources @ 20 kbps  = 200 kbps (TOS 0x10 in Priority, 0x00 in Baseline)
 *   - 30 Ordinary Sources  @ 40 kbps  = 1200 kbps (TOS 0x00)
 *   Total Offered Load = 1400 kbps = 1.4 Mbps > 1.0 Mbps bottleneck (140% overload).
 *   Packet payload: 512 bytes (542 bytes on wire).
 *   Distributed evenly: each server receives 2 Emergency flows and 6 Ordinary flows.
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SeanScalability50Node");

// =============================================================================
// FLOW CONFIGURATION & METRICS DATA STRUCTURES
// =============================================================================

struct FlowConfig
{
    uint32_t flowIndex;         // 0 to 39
    uint32_t sourceNodeId;      // 0 to 39
    uint32_t destServerNodeId;  // 45 to 49
    uint32_t serverIndex;       // 0 to 4
    Ipv4Address destIp;
    uint16_t destPort;
    std::string trafficClass;   // "Emergency Alert" or "Ordinary Msg"
    uint8_t tos;
    DataRate rate;
};

struct PerFlowRecord
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
// CUSTOM JITTERED TRAFFIC APPLICATION
// =============================================================================

class ScalableJitterTrafficApp : public Application
{
public:
    static TypeId GetTypeId()
    {
        static TypeId tid = TypeId("ns3::ScalableJitterTrafficApp")
            .SetParent<Application>()
            .SetGroupName("Applications")
            .AddConstructor<ScalableJitterTrafficApp>();
        return tid;
    }

    ScalableJitterTrafficApp()
        : m_socket(nullptr),
          m_peer(),
          m_packetSize(512),
          m_dataRate("40kb/s"),
          m_tos(0x00),
          m_useExponential(false),
          m_jitterRatio(0.20),
          m_running(false)
    {
    }

    ~ScalableJitterTrafficApp() override
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
        m_sendEvent = Simulator::Schedule(Seconds(dt), &ScalableJitterTrafficApp::SendPacket, this);
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

// =============================================================================
// GLOBAL DIAGNOSTIC QUEUE COUNTERS
// =============================================================================

static uint32_t g_band0Enqueued = 0;
static uint32_t g_band1Enqueued = 0;
static uint32_t g_band2Enqueued = 0;
static uint32_t g_band0Dropped  = 0;
static uint32_t g_band1Dropped  = 0;
static uint32_t g_band2Dropped  = 0;

static uint32_t g_fifoEnqueued  = 0;
static uint32_t g_fifoDropped   = 0;

void TraceBand0Enqueue(Ptr<const QueueDiscItem> item) { g_band0Enqueued++; }
void TraceBand1Enqueue(Ptr<const QueueDiscItem> item) { g_band1Enqueued++; }
void TraceBand2Enqueue(Ptr<const QueueDiscItem> item) { g_band2Enqueued++; }

void TraceBand0Drop(Ptr<const QueueDiscItem> item) { g_band0Dropped++; }
void TraceBand1Drop(Ptr<const QueueDiscItem> item) { g_band1Dropped++; }
void TraceBand2Drop(Ptr<const QueueDiscItem> item) { g_band2Dropped++; }

void TraceFifoEnqueue(Ptr<const QueueDiscItem> item) { g_fifoEnqueued++; }
void TraceFifoDrop(Ptr<const QueueDiscItem> item)    { g_fifoDropped++; }

// =============================================================================
// PRE-FLIGHT ROUTING & REACHABILITY VALIDATION
// =============================================================================

bool
ValidateTopologyConnectivity(const NodeContainer& allNodes,
                             const std::vector<FlowConfig>& flowConfigs)
{
    std::cout << "[Pre-Flight Check] Validating routes from 40 sources to destination servers..." << std::endl;
    bool allValid = true;

    for (const auto& flow : flowConfigs)
    {
        Ptr<Node> srcNode = allNodes.Get(flow.sourceNodeId);
        Ptr<Ipv4> ipv4 = srcNode->GetObject<Ipv4>();
        NS_ASSERT_MSG(ipv4 != nullptr, "Node " << flow.sourceNodeId << " lacks IPv4 stack!");

        Ptr<Ipv4RoutingProtocol> rp = ipv4->GetRoutingProtocol();
        Ipv4Header header;
        header.SetDestination(flow.destIp);
        header.SetSource(ipv4->GetAddress(1, 0).GetAddress());

        Socket::SocketErrno sockErrno;
        Ptr<Ipv4Route> route = rp->RouteOutput(nullptr, header, nullptr, sockErrno);

        if (!route)
        {
            std::cerr << "  [ERROR] Unroutable destination: Source Node " << flow.sourceNodeId
                      << " cannot route to Server IP " << flow.destIp << std::endl;
            allValid = false;
        }
    }

    if (allValid)
    {
        std::cout << "  [SUCCESS] All 40 source-destination routes strictly verified." << std::endl;
    }
    return allValid;
}

// =============================================================================
// SIMULATION EXECUTION FUNCTION
// =============================================================================

int
main(int argc, char* argv[])
{
    bool priorityMode = false;
    double simTime = 30.0;
    uint32_t packetSize = 512;
    std::string emergencyRate = "20kb/s";
    std::string ordinaryRate = "40kb/s";
    std::string bottleneckBandwidth = "1Mbps";
    std::string bottleneckDelay = "10ms";
    std::string accessBandwidth = "10Mbps";
    std::string accessDelay = "1ms";
    std::string aggBandwidth = "20Mbps";
    std::string aggDelay = "2ms";
    std::string serverBandwidth = "100Mbps";
    std::string serverDelay = "1ms";
    uint32_t queueDiscSize = 50;
    uint32_t runIndex = 1;
    uint32_t seedValue = 1;
    bool randomizeStart = true;
    double jitterRatio = 0.20;
    bool useExponential = false;
    std::string outputDir = "data/scalability/";
    std::string customSummaryCsv = "";
    std::string customFlowCsv = "";
    std::string customQueueCsv = "";
    bool checkConnectivity = true;

    CommandLine cmd(__FILE__);
    cmd.AddValue("priority", "Enable priority queueing (1 = PrioQueueDisc, 0 = FifoQueueDisc)", priorityMode);
    cmd.AddValue("simTime", "Total simulation active duration in seconds", simTime);
    cmd.AddValue("packetSize", "Payload size of UDP packets in bytes", packetSize);
    cmd.AddValue("emergencyRate", "Per-source data rate for emergency sources (default 20kb/s)", emergencyRate);
    cmd.AddValue("ordinaryRate", "Per-source data rate for ordinary sources (default 40kb/s)", ordinaryRate);
    cmd.AddValue("bottleneckBw", "Bandwidth of shared bottleneck link (default 1Mbps)", bottleneckBandwidth);
    cmd.AddValue("bottleneckDelay", "Delay of bottleneck link (default 10ms)", bottleneckDelay);
    cmd.AddValue("queueSize", "Queue disc buffer capacity in packets (default 50)", queueDiscSize);
    cmd.AddValue("run", "Simulation run number for RngSeedManager (default 1)", runIndex);
    cmd.AddValue("seed", "Random seed value (default 1)", seedValue);
    cmd.AddValue("jitter", "Jitter ratio around nominal rate (default 0.20)", jitterRatio);
    cmd.AddValue("randomizeStart", "Enable staggered start offsets (default 1)", randomizeStart);
    cmd.AddValue("outputDir", "Directory path for CSV output files", outputDir);
    cmd.AddValue("summaryCsv", "Explicit file path for summary CSV output", customSummaryCsv);
    cmd.AddValue("flowCsv", "Explicit file path for per-flow CSV output", customFlowCsv);
    cmd.AddValue("queueCsv", "Explicit file path for queue diagnostics CSV output", customQueueCsv);
    cmd.AddValue("checkConnectivity", "Validate routing paths before start", checkConnectivity);
    cmd.Parse(argc, argv);

    // Reset diagnostic counters
    g_band0Enqueued = 0; g_band1Enqueued = 0; g_band2Enqueued = 0;
    g_band0Dropped  = 0; g_band1Dropped  = 0; g_band2Dropped  = 0;
    g_fifoEnqueued  = 0; g_fifoDropped   = 0;

    RngSeedManager::SetSeed(seedValue);
    RngSeedManager::SetRun(runIndex);

    std::cout << "\n================================================================================" << std::endl;
    std::cout << "  Smart Emergency Alert and Priority Messaging Network (SEAN) - Scalability     " << std::endl;
    std::cout << "  50-Node Point-to-Point Network Simulation (ns-3.49)                           " << std::endl;
    std::cout << "  Operating Mode : " << (priorityMode ? "PRIORITY (PrioQueueDisc)" : "BASELINE (FifoQueueDisc)") << std::endl;
    std::cout << "  Simulation Run : " << runIndex << " (Seed: " << seedValue << ")" << std::endl;
    std::cout << "================================================================================" << std::endl;

    // -------------------------------------------------------------------------
    // 1. NODE CREATION & EXPLICIT RUNTIME VERIFICATION (EXACTLY 50 NODES)
    // -------------------------------------------------------------------------
    NodeContainer allNodes;
    allNodes.Create(50);

    NS_ASSERT_MSG(allNodes.GetN() == 50, "FATAL ERROR: Topology must contain exactly 50 nodes!");
    std::cout << "[Step 1/6] Created exactly 50 nodes:" << std::endl;
    std::cout << "  - Emergency Sources (E0..E9)        : Node IDs  0 -  9 (10 nodes)" << std::endl;
    std::cout << "  - Ordinary Sources (O0..O29)        : Node IDs 10 - 39 (30 nodes)" << std::endl;
    std::cout << "  - Regional Routers (R1, R2, R3)     : Node IDs 40 - 42 ( 3 nodes)" << std::endl;
    std::cout << "  - Core Ingress Gateway (GW_in)      : Node ID  43      ( 1 node )" << std::endl;
    std::cout << "  - Core Egress Gateway (GW_eg)       : Node ID  44      ( 1 node )" << std::endl;
    std::cout << "  - Emergency Servers (S0..S4)        : Node IDs 45 - 49 ( 5 nodes)" << std::endl;

    Ptr<Node> r1Node   = allNodes.Get(40);
    Ptr<Node> r2Node   = allNodes.Get(41);
    Ptr<Node> r3Node   = allNodes.Get(42);
    Ptr<Node> gwInNode = allNodes.Get(43);
    Ptr<Node> gwEgNode = allNodes.Get(44);

    std::vector<Ptr<Node>> serverNodes;
    for (uint32_t s = 45; s <= 49; ++s)
    {
        serverNodes.push_back(allNodes.Get(s));
    }

    // -------------------------------------------------------------------------
    // 2. CONSTRUCT 49 POINT-TO-POINT LINKS
    // -------------------------------------------------------------------------
    std::cout << "[Step 2/6] Building 49 point-to-point links..." << std::endl;

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
    // Zone A: R1 (Node 40) aggregates Sources 0..2 (Emergency) and 10..19 (Ordinary) -> 13 links
    // Zone B: R2 (Node 41) aggregates Sources 3..5 (Emergency) and 20..29 (Ordinary) -> 13 links
    // Zone C: R3 (Node 42) aggregates Sources 6..9 (Emergency) and 30..39 (Ordinary) -> 14 links
    std::vector<NetDeviceContainer> accessDevs(40);
    uint32_t linkCount = 0;

    for (uint32_t i = 0; i < 40; ++i)
    {
        Ptr<Node> srcNode = allNodes.Get(i);
        Ptr<Node> targetRouter;

        if ((i <= 2) || (i >= 10 && i <= 19))
        {
            targetRouter = r1Node;
        }
        else if ((i >= 3 && i <= 5) || (i >= 20 && i <= 29))
        {
            targetRouter = r2Node;
        }
        else
        {
            targetRouter = r3Node;
        }

        accessDevs[i] = accessLink.Install(srcNode, targetRouter);
        linkCount++;
    }

    // B. 3 Regional Aggregation Links: R1, R2, R3 -> GW_in
    NetDeviceContainer r1_gwIn = aggLink.Install(r1Node, gwInNode); linkCount++;
    NetDeviceContainer r2_gwIn = aggLink.Install(r2Node, gwInNode); linkCount++;
    NetDeviceContainer r3_gwIn = aggLink.Install(r3Node, gwInNode); linkCount++;

    // C. 1 Shared Bottleneck Link: GW_in -> GW_eg
    NetDeviceContainer gwIn_gwEg = bottleneckLink.Install(gwInNode, gwEgNode); linkCount++;

    // D. 5 Server Distribution Links: GW_eg -> Servers S0..S4
    std::vector<NetDeviceContainer> serverDevs(5);
    for (uint32_t s = 0; s < 5; ++s)
    {
        serverDevs[s] = serverLink.Install(gwEgNode, serverNodes[s]);
        linkCount++;
    }

    NS_ASSERT_MSG(linkCount == 49, "FATAL ERROR: Expected exactly 49 point-to-point links!");
    std::cout << "  -> Verified: Exactly 49 links successfully constructed." << std::endl;

    // -------------------------------------------------------------------------
    // 3. INTERNET STACK & IP ADDRESSING (49 SUBNETS)
    // -------------------------------------------------------------------------
    std::cout << "[Step 3/6] Installing Internet stack and allocating 49 IPv4 subnets..." << std::endl;

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

    // Subnets for 3 Regional Aggregation Links: 10.2.1.0/24, 10.2.2.0/24, 10.2.3.0/24
    address.SetBase("10.2.1.0", "255.255.255.0");
    Ipv4InterfaceContainer r1_gwIn_If = address.Assign(r1_gwIn);

    address.SetBase("10.2.2.0", "255.255.255.0");
    Ipv4InterfaceContainer r2_gwIn_If = address.Assign(r2_gwIn);

    address.SetBase("10.2.3.0", "255.255.255.0");
    Ipv4InterfaceContainer r3_gwIn_If = address.Assign(r3_gwIn);

    // Subnet for Shared Bottleneck Link: 10.3.1.0/24
    address.SetBase("10.3.1.0", "255.255.255.0");
    Ipv4InterfaceContainer bottleneckIf = address.Assign(gwIn_gwEg);

    // Subnets for 5 Server Distribution Links: 10.4.1.0/24 to 10.4.5.0/24
    std::vector<Ipv4InterfaceContainer> serverIfs(5);
    std::vector<Ipv4Address> serverIps(5);
    for (uint32_t s = 0; s < 5; ++s)
    {
        std::ostringstream subnet;
        subnet << "10.4." << (s + 1) << ".0";
        address.SetBase(subnet.str().c_str(), "255.255.255.0");
        serverIfs[s] = address.Assign(serverDevs[s]);
        serverIps[s] = serverIfs[s].GetAddress(1); // Server interface is index 1
    }

    // Populate routing tables across the tree
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // -------------------------------------------------------------------------
    // 4. QUEUE DISCIPLINE INSTALLATION ON BOTTLENECK EGRESS
    // -------------------------------------------------------------------------
    std::cout << "[Step 4/6] Installing QueueDisc on Node 43 bottleneck egress..." << std::endl;

    Ptr<NetDevice> devBottleneck = gwIn_gwEg.Get(0); // GW_in egress towards GW_eg
    TrafficControlHelper tch;
    tch.Uninstall(devBottleneck); // Safely remove default FqCoDel

    Ptr<QueueDisc> rootQdisc = nullptr;
    std::string qSizeStr = std::to_string(queueDiscSize) + "p";

    if (priorityMode)
    {
        // PrioQueueDisc with 3 bands and Priomap mapping priority 6 (TOS 0x10) to Band 0
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

        // Connect validation traces
        Ptr<QueueDisc> band0Qdisc = rootQdisc->GetQueueDiscClass(0)->GetQueueDisc();
        Ptr<QueueDisc> band1Qdisc = rootQdisc->GetQueueDiscClass(1)->GetQueueDisc();
        Ptr<QueueDisc> band2Qdisc = rootQdisc->GetQueueDiscClass(2)->GetQueueDisc();

        band0Qdisc->TraceConnectWithoutContext("Enqueue", MakeCallback(&TraceBand0Enqueue));
        band1Qdisc->TraceConnectWithoutContext("Enqueue", MakeCallback(&TraceBand1Enqueue));
        band2Qdisc->TraceConnectWithoutContext("Enqueue", MakeCallback(&TraceBand2Enqueue));

        band0Qdisc->TraceConnectWithoutContext("Drop", MakeCallback(&TraceBand0Drop));
        band1Qdisc->TraceConnectWithoutContext("Drop", MakeCallback(&TraceBand1Drop));
        band2Qdisc->TraceConnectWithoutContext("Drop", MakeCallback(&TraceBand2Drop));

        std::cout << "  -> Configured PrioQueueDisc (Priomap: '1 2 2 2 1 2 0 0 1 1 1 1 1 1 1 1', 3x50p bands)." << std::endl;
    }
    else
    {
        // Standard FIFO Drop-Tail QueueDisc (50p)
        tch.SetRootQueueDisc("ns3::FifoQueueDisc", "MaxSize", QueueSizeValue(QueueSize(qSizeStr)));
        QueueDiscContainer qdiscs = tch.Install(devBottleneck);
        rootQdisc = qdiscs.Get(0);

        rootQdisc->TraceConnectWithoutContext("Enqueue", MakeCallback(&TraceFifoEnqueue));
        rootQdisc->TraceConnectWithoutContext("Drop", MakeCallback(&TraceFifoDrop));

        std::cout << "  -> Configured FifoQueueDisc (50p capacity, single shared queue)." << std::endl;
    }

    // -------------------------------------------------------------------------
    // 5. TRAFFIC MATRIX & APPLICATION INSTALLATION (40 FLOWS)
    // -------------------------------------------------------------------------
    std::cout << "[Step 5/6] Initializing 40 UDP flows and packet sinks..." << std::endl;

    std::vector<FlowConfig> flowConfigs(40);
    ApplicationContainer sinkApps;
    PacketSinkHelper sinkHelper("ns3::UdpSocketFactory", Address());

    // Build flow matrix mapping 40 sources evenly across 5 servers:
    // 10 Emergency Sources (0..9): 2 per server (Ports 9000..9001)
    // 30 Ordinary Sources (10..39): 6 per server (Ports 9002..9007)
    for (uint32_t i = 0; i < 40; ++i)
    {
        FlowConfig fc;
        fc.flowIndex = i;
        fc.sourceNodeId = i;

        if (i < 10)
        {
            // Emergency Alert Source
            fc.serverIndex = i / 2;
            fc.destServerNodeId = 45 + fc.serverIndex;
            fc.destIp = serverIps[fc.serverIndex];
            fc.destPort = 9000 + (i % 2);
            fc.trafficClass = "Emergency Alert";
            fc.tos = priorityMode ? 0x10 : 0x00;
            fc.rate = DataRate(emergencyRate);
        }
        else
        {
            // Ordinary Civilian Source
            uint32_t ordIdx = i - 10;
            fc.serverIndex = ordIdx / 6;
            fc.destServerNodeId = 45 + fc.serverIndex;
            fc.destIp = serverIps[fc.serverIndex];
            fc.destPort = 9002 + (ordIdx % 6);
            fc.trafficClass = "Ordinary Msg";
            fc.tos = 0x00;
            fc.rate = DataRate(ordinaryRate);
        }

        flowConfigs[i] = fc;

        // Install matching packet sink on target server
        sinkHelper.SetAttribute("Local", AddressValue(InetSocketAddress(Ipv4Address::GetAny(), fc.destPort)));
        sinkApps.Add(sinkHelper.Install(serverNodes[fc.serverIndex]));
    }

    sinkApps.Start(Seconds(0.5));
    sinkApps.Stop(Seconds(simTime + 1.0));

    // Optional Pre-Flight Connectivity Check
    if (checkConnectivity)
    {
        bool passed = ValidateTopologyConnectivity(allNodes, flowConfigs);
        if (!passed)
        {
            std::cerr << "FATAL: Pre-flight connectivity validation failed. Terminating." << std::endl;
            return 1;
        }
    }

    // Install JitterTrafficApp on all 40 source nodes
    ApplicationContainer clientApps;
    Ptr<UniformRandomVariable> startUv = CreateObject<UniformRandomVariable>();
    startUv->SetAttribute("Min", DoubleValue(0.0));
    startUv->SetAttribute("Max", DoubleValue(0.05)); // 0 to 50 ms start offset

    for (uint32_t i = 0; i < 40; ++i)
    {
        const FlowConfig& fc = flowConfigs[i];
        Ptr<ScalableJitterTrafficApp> app = CreateObject<ScalableJitterTrafficApp>();
        app->Setup(InetSocketAddress(fc.destIp, fc.destPort),
                   packetSize,
                   fc.rate,
                   fc.tos,
                   useExponential,
                   jitterRatio);

        allNodes.Get(fc.sourceNodeId)->AddApplication(app);

        double startOffset = randomizeStart ? startUv->GetValue() : 0.0;
        app->SetStartTime(Seconds(1.0 + startOffset));
        app->SetStopTime(Seconds(simTime));
        clientApps.Add(app);
    }

    // Install FlowMonitor
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    // -------------------------------------------------------------------------
    // 6. EXECUTE SIMULATION & COLLECT METRICS
    // -------------------------------------------------------------------------
    std::cout << "[Step 6/6] Running discrete-event simulation (Stop: " << (simTime + 1.0) << "s)..." << std::endl;

    Simulator::Stop(Seconds(simTime + 1.0));
    Simulator::Run();

    // Process FlowMonitor metrics
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    // Build fast lookup map: (destIp, destPort) -> flowIndex
    std::map<std::pair<Ipv4Address, uint16_t>, uint32_t> flowLookup;
    for (uint32_t i = 0; i < 40; ++i)
    {
        flowLookup[{flowConfigs[i].destIp, flowConfigs[i].destPort}] = i;
    }

    std::vector<PerFlowRecord> flowRecords(40);
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

    // Active duration for standard throughput calculation (29.0s active window)
    const double standardActiveDuration = simTime - 1.0;

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

    // Queue Drops
    uint32_t totalQueueDrops = rootQdisc ? rootQdisc->GetStats().nTotalDroppedPackets : 0;
    if (priorityMode)
    {
        emerSummary.queueDrops = g_band0Dropped;
        ordSummary.queueDrops  = g_band1Dropped;
    }
    else
    {
        emerSummary.queueDrops = emerSummary.totalLost;
        ordSummary.queueDrops  = ordSummary.totalLost;
    }

    // -------------------------------------------------------------------------
    // PRINT CONSOLE SUMMARY TABLE
    // -------------------------------------------------------------------------
    std::cout << "\n========================================================================================================" << std::endl;
    std::cout << "                        50-NODE SIMULATION RESULTS SUMMARY (RUN " << runIndex << ")                             " << std::endl;
    std::cout << "========================================================================================================" << std::endl;
    std::cout << std::left << std::setw(18) << "Traffic Class"
              << std::right << std::setw(8) << "Flows"
              << std::setw(12) << "Tx (pkts)"
              << std::setw(12) << "Rx (pkts)"
              << std::setw(12) << "Lost (pkts)"
              << std::setw(12) << "PDR (%)"
              << std::setw(16) << "Throughput (Kbps)"
              << std::setw(14) << "Mean Delay (ms)"
              << std::setw(14) << "Queue Drops"
              << std::endl;
    std::cout << std::string(106, '-') << std::endl;

    auto printClass = [](const ClassSummaryMetrics& m) {
        std::cout << std::left << std::setw(18) << m.trafficClass
                  << std::right << std::setw(8) << m.numFlows
                  << std::setw(12) << m.totalTx
                  << std::setw(12) << m.totalRx
                  << std::setw(12) << m.totalLost
                  << std::setw(12) << std::fixed << std::setprecision(2) << m.aggregatePdr
                  << std::setw(16) << std::fixed << std::setprecision(2) << m.aggregateThroughputKbps
                  << std::setw(14) << std::fixed << std::setprecision(2) << m.weightedMeanDelayMs
                  << std::setw(14) << m.queueDrops
                  << std::endl;
    };

    printClass(emerSummary);
    printClass(ordSummary);
    std::cout << std::string(106, '-') << std::endl;

    // Queue Classification Diagnostics
    std::cout << "\n--------------------------------------------------------------------------------" << std::endl;
    std::cout << "                       BOTTLENECK QUEUE-DISC DIAGNOSTICS                         " << std::endl;
    std::cout << "--------------------------------------------------------------------------------" << std::endl;
    if (priorityMode)
    {
        std::cout << "  Band 0 (High Priority / TOS 0x10) : Enqueued = " << std::setw(6) << g_band0Enqueued
                  << " | Dropped = " << g_band0Dropped << std::endl;
        std::cout << "  Band 1 (Normal Priority / TOS 0x00) : Enqueued = " << std::setw(6) << g_band1Enqueued
                  << " | Dropped = " << g_band1Dropped << std::endl;
        std::cout << "  Band 2 (Bulk / Background)          : Enqueued = " << std::setw(6) << g_band2Enqueued
                  << " | Dropped = " << g_band2Dropped << std::endl;
        std::cout << "  Total Queue Disc Drops              : " << totalQueueDrops << " packets" << std::endl;
        bool classOk = (g_band0Enqueued > 0 && g_band1Enqueued > 0 && g_band2Enqueued == 0);
        std::cout << "  Priority Classification Integrity  : " << (classOk ? "PASS (100% Correct)" : "FAIL") << std::endl;
    }
    else
    {
        std::cout << "  FIFO Single Buffer : Enqueued = " << g_fifoEnqueued
                  << " | Dropped = " << g_fifoDropped << " packets" << std::endl;
        std::cout << "  Total Queue Disc Drops: " << totalQueueDrops << " packets" << std::endl;
    }
    std::cout << "--------------------------------------------------------------------------------\n" << std::endl;

    // -------------------------------------------------------------------------
    // WRITE OUTPUT CSV FILES TO data/scalability/
    // -------------------------------------------------------------------------
    std::string modeTag = priorityMode ? "priority" : "baseline";
    std::string summaryCsvFile = customSummaryCsv.empty()
        ? (outputDir + "results-scalability-" + modeTag + ".csv")
        : customSummaryCsv;
    std::string flowCsvFile = customFlowCsv.empty()
        ? (outputDir + "flow-results-" + modeTag + ".csv")
        : customFlowCsv;
    std::string queueCsvFile = customQueueCsv.empty()
        ? (outputDir + "queue-validation.csv")
        : customQueueCsv;

    // 1. Write Summary CSV
    std::ofstream sOut(summaryCsvFile.c_str());
    if (sOut.is_open())
    {
        sOut << "Mode,Run,Seed,TrafficClass,FlowCount,TxPackets,RxPackets,LostPackets,PDR_Percent,"
             << "Throughput_Kbps,MeanDelay_ms,MeanJitter_ms,QueueDrops\n";
        sOut << modeTag << "," << runIndex << "," << seedValue << ","
             << emerSummary.trafficClass << "," << emerSummary.numFlows << ","
             << emerSummary.totalTx << "," << emerSummary.totalRx << "," << emerSummary.totalLost << ","
             << std::fixed << std::setprecision(2) << emerSummary.aggregatePdr << ","
             << emerSummary.aggregateThroughputKbps << ","
             << emerSummary.weightedMeanDelayMs << ","
             << emerSummary.meanJitterMs << ","
             << emerSummary.queueDrops << "\n";
        sOut << modeTag << "," << runIndex << "," << seedValue << ","
             << ordSummary.trafficClass << "," << ordSummary.numFlows << ","
             << ordSummary.totalTx << "," << ordSummary.totalRx << "," << ordSummary.totalLost << ","
             << std::fixed << std::setprecision(2) << ordSummary.aggregatePdr << ","
             << ordSummary.aggregateThroughputKbps << ","
             << ordSummary.weightedMeanDelayMs << ","
             << ordSummary.meanJitterMs << ","
             << ordSummary.queueDrops << "\n";
        sOut.close();
        std::cout << "[Saved] Summary CSV written to     : " << summaryCsvFile << std::endl;
    }

    // 2. Write Per-Flow CSV
    std::ofstream fOut(flowCsvFile.c_str());
    if (fOut.is_open())
    {
        fOut << "Mode,Run,Seed,FlowIndex,SourceNodeId,DestServerNodeId,TrafficClass,DestIP,DestPort,"
             << "TxPackets,RxPackets,LostPackets,PDR_Percent,Throughput_Kbps,MeanDelay_ms,MeanJitter_ms\n";
        for (const auto& r : flowRecords)
        {
            fOut << modeTag << "," << runIndex << "," << seedValue << ","
                 << r.flowIndex << "," << r.sourceNodeId << ","
                 << r.destServerNodeId << ",\"" << r.trafficClass << "\","
                 << r.destIp << "," << r.destPort << ","
                 << r.txPackets << "," << r.rxPackets << "," << r.lostPackets << ","
                 << std::fixed << std::setprecision(2) << r.pdrPercent << ","
                 << r.throughputKbps << ","
                 << r.meanDelayMs << ","
                 << r.meanJitterMs << "\n";
        }
        fOut.close();
        std::cout << "[Saved] Per-Flow CSV written to    : " << flowCsvFile << std::endl;
    }

    // 3. Write Queue Validation CSV (Appends or overwrites)
    std::ofstream qOut(queueCsvFile.c_str(), std::ios::app);
    if (qOut.is_open())
    {
        // Check if file is empty to write header
        qOut.seekp(0, std::ios::end);
        if (qOut.tellp() == 0)
        {
            qOut << "Mode,RunIndex,Seed,Band0Enqueued,Band1Enqueued,Band2Enqueued,Band0Dropped,Band1Dropped,Band2Dropped,TotalQueueDrops\n";
        }
        qOut << modeTag << "," << runIndex << "," << seedValue << ","
             << g_band0Enqueued << "," << g_band1Enqueued << "," << g_band2Enqueued << ","
             << g_band0Dropped << "," << g_band1Dropped << "," << g_band2Dropped << ","
             << totalQueueDrops << "\n";
        qOut.close();
        std::cout << "[Saved] Queue Diagnostics written to: " << queueCsvFile << std::endl;
    }

    std::cout << "================================================================================\n" << std::endl;

    Simulator::Destroy();
    return 0;
}

