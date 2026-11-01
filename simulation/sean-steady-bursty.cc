/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Smart Emergency Alert and Priority Messaging Network (SEAN)
 * Controlled Experiment: Steady Ordinary Traffic vs. Bursty Ordinary Traffic
 *
 * Simulator    : Network Simulator 3 (ns-3.49)
 * Topology     : 50-Node Multi-Tier Point-to-Point Network (49 Links)
 * Objective    : Evaluate emergency message delivery when civilian cross-traffic
 *                transitions from steady CBR to repeated ON/OFF burst traffic,
 *                comparing FIFO (FifoQueueDisc) against Priority (PrioQueueDisc).
 *
 * Author       : SEAN Research Team / Academic Examiner
 * Date         : October 2026
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

NS_LOG_COMPONENT_DEFINE("SeanSteadyBurstySimulation");

// =============================================================================
// GLOBAL DIAGNOSTIC DROP & ENQUEUE COUNTERS
// =============================================================================
static uint32_t g_band0Enqueued = 0;
static uint32_t g_band1Enqueued = 0;
static uint32_t g_band2Enqueued = 0;
static uint32_t g_band0Dropped  = 0;
static uint32_t g_band1Dropped  = 0;
static uint32_t g_band2Dropped  = 0;
static uint32_t g_fifoEnqueued  = 0;
static uint32_t g_fifoDropped   = 0;

static void TraceBand0Enqueue(Ptr<const QueueDiscItem> item) { g_band0Enqueued++; }
static void TraceBand1Enqueue(Ptr<const QueueDiscItem> item) { g_band1Enqueued++; }
static void TraceBand2Enqueue(Ptr<const QueueDiscItem> item) { g_band2Enqueued++; }
static void TraceBand0Drop(Ptr<const QueueDiscItem> item)    { g_band0Dropped++; }
static void TraceBand1Drop(Ptr<const QueueDiscItem> item)    { g_band1Dropped++; }
static void TraceBand2Drop(Ptr<const QueueDiscItem> item)    { g_band2Dropped++; }
static void TraceFifoEnqueue(Ptr<const QueueDiscItem> item)  { g_fifoEnqueued++; }
static void TraceFifoDrop(Ptr<const QueueDiscItem> item)     { g_fifoDropped++; }

// =============================================================================
// FLOW CONFIGURATION & RESULT STRUCTURES
// =============================================================================
struct FlowConfig
{
    uint32_t sourceNodeId;
    uint32_t serverIndex;
    uint32_t destServerNodeId;
    std::string trafficClass;
    Ipv4Address destIp;
    uint16_t destPort;
    uint8_t tos;
    DataRate steadyRate;
    DataRate burstRate;
    bool isBursty;
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
// STEADY & BURSTY TRAFFIC GENERATOR APPLICATION
// =============================================================================
class SteadyBurstyApp : public Application
{
public:
    SteadyBurstyApp()
        : m_socket(nullptr),
          m_peer(),
          m_packetSize(512),
          m_steadyRate("40kb/s"),
          m_burstRate("80kb/s"),
          m_tos(0x00),
          m_isBursty(false),
          m_onTime(0.5),
          m_offTime(0.5),
          m_jitterRatio(0.20),
          m_running(false),
          m_isOn(true),
          m_packetsSent(0),
          m_currentInterval(0.0),
          m_lastScheduleTime(Seconds(0))
    {
    }

    ~SteadyBurstyApp() override
    {
        m_socket = nullptr;
    }

    void Setup(Address peer,
               uint32_t packetSize,
               DataRate steadyRate,
               DataRate burstRate,
               uint8_t tos,
               bool isBursty = false,
               double onTime = 0.5,
               double offTime = 0.5,
               double jitterRatio = 0.20)
    {
        m_peer = peer;
        m_packetSize = packetSize;
        m_steadyRate = steadyRate;
        m_burstRate = burstRate;
        m_tos = tos;
        m_isBursty = isBursty;
        m_onTime = onTime;
        m_offTime = offTime;
        m_jitterRatio = jitterRatio;
    }

private:
    void StartApplication() override
    {
        m_running = true;
        m_isOn = true;
        m_packetsSent = 0;

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

        m_uniformVar = CreateObject<UniformRandomVariable>();

        if (m_isBursty)
        {
            // Start in ON state; schedule transition to OFF after onTime
            m_cycleEvent = Simulator::Schedule(Seconds(m_onTime), &SteadyBurstyApp::TransitionToOff, this);
        }

        ScheduleNextTx(true);
    }

    void StopApplication() override
    {
        m_running = false;
        m_isOn = false;
        Simulator::Cancel(m_sendEvent);
        Simulator::Cancel(m_cycleEvent);
        if (m_socket)
        {
            m_socket->Close();
        }
    }

    void TransitionToOff()
    {
        if (!m_running) return;

        m_isOn = false;

        // Compute elapsed time into current interval and preserve residual time
        Time now = Simulator::Now();
        Time elapsed = now - m_lastScheduleTime;
        if (elapsed < Seconds(m_currentInterval))
        {
            m_remainingInterval = m_currentInterval - elapsed.GetSeconds();
        }
        else
        {
            m_remainingInterval = 0.0;
        }

        // Cancel pending transmission during silent OFF period
        Simulator::Cancel(m_sendEvent);

        // Schedule next transition to ON state
        m_cycleEvent = Simulator::Schedule(Seconds(m_offTime), &SteadyBurstyApp::TransitionToOn, this);
    }

    void TransitionToOn()
    {
        if (!m_running) return;

        m_isOn = true;

        // Schedule next transition to OFF state
        m_cycleEvent = Simulator::Schedule(Seconds(m_onTime), &SteadyBurstyApp::TransitionToOff, this);

        // Resume transmission using preserved residual interval for exact renewal continuity
        double nextDt = m_remainingInterval;
        if (nextDt <= 0.0)
        {
            nextDt = ComputeInterval();
        }
        m_currentInterval = nextDt;
        m_lastScheduleTime = Simulator::Now();
        m_sendEvent = Simulator::Schedule(Seconds(nextDt), &SteadyBurstyApp::SendPacket, this);
    }

    void SendPacket()
    {
        if (!m_running) return;
        if (m_isBursty && !m_isOn) return;

        Ptr<Packet> packet = Create<Packet>(m_packetSize);
        m_socket->Send(packet);
        m_packetsSent++;

        ScheduleNextTx(false);
    }

    double ComputeInterval()
    {
        double rateBps = m_isBursty ? m_burstRate.GetBitRate() : m_steadyRate.GetBitRate();
        double nominalInterval = static_cast<double>(m_packetSize * 8) / rateBps;
        double nextInterval = nominalInterval;

        if (m_jitterRatio > 0.0)
        {
            double low = nominalInterval * (1.0 - m_jitterRatio);
            double high = nominalInterval * (1.0 + m_jitterRatio);
            m_uniformVar->SetAttribute("Min", DoubleValue(low));
            m_uniformVar->SetAttribute("Max", DoubleValue(high));
            nextInterval = m_uniformVar->GetValue();
        }
        return nextInterval;
    }

    void ScheduleNextTx(bool isFirst)
    {
        if (!m_running) return;
        if (m_isBursty && !m_isOn) return;

        double nextInterval = ComputeInterval();
        m_currentInterval = nextInterval;
        m_lastScheduleTime = Simulator::Now();

        m_sendEvent = Simulator::Schedule(Seconds(nextInterval), &SteadyBurstyApp::SendPacket, this);
    }

    Ptr<Socket> m_socket;
    Address m_peer;
    uint32_t m_packetSize;
    DataRate m_steadyRate;
    DataRate m_burstRate;
    uint8_t m_tos;
    bool m_isBursty;
    double m_onTime;
    double m_offTime;
    double m_jitterRatio;
    bool m_running;
    bool m_isOn;
    uint32_t m_packetsSent;

    double m_currentInterval;
    double m_remainingInterval;
    Time m_lastScheduleTime;

    EventId m_sendEvent;
    EventId m_cycleEvent;
    Ptr<UniformRandomVariable> m_uniformVar;
};

// =============================================================================
// MAIN SIMULATION ENTRY POINT
// =============================================================================
int
main(int argc, char* argv[])
{
    // Simulation Parameters
    bool priorityMode = false;
    bool burstyTraffic = false;  // false = STEADY, true = BURSTY
    double onTime = 0.5;         // Burst ON duration in seconds
    double offTime = 0.5;        // Burst OFF duration in seconds
    double simTime = 30.0;
    uint32_t packetSize = 512;
    std::string bottleneckBandwidth = "1Mbps";
    std::string bottleneckDelay = "10ms";
    std::string accessBandwidth = "10Mbps";
    std::string accessDelay = "2ms";
    std::string aggBandwidth = "10Mbps";
    std::string aggDelay = "2ms";
    std::string serverBandwidth = "10Mbps";
    std::string serverDelay = "2ms";
    uint32_t queueDiscSize = 50;
    uint32_t runIndex = 1;
    uint32_t seedValue = 1;

    std::string customSummaryCsv = "";
    std::string customFlowCsv = "";
    std::string customQueueCsv = "";
    std::string customXmlFile = "";

    CommandLine cmd(__FILE__);
    cmd.AddValue("priority", "Enable priority queueing (1 = PrioQueueDisc, 0 = FifoQueueDisc)", priorityMode);
    cmd.AddValue("bursty", "Enable bursty ordinary traffic (1 = BURSTY ON/OFF, 0 = STEADY CBR)", burstyTraffic);
    cmd.AddValue("onTime", "Burst ON period duration in seconds (default 0.5s)", onTime);
    cmd.AddValue("offTime", "Burst OFF period duration in seconds (default 0.5s)", offTime);
    cmd.AddValue("simTime", "Total active simulation duration in seconds (default 30.0s)", simTime);
    cmd.AddValue("packetSize", "UDP payload size in bytes (default 512)", packetSize);
    cmd.AddValue("queueSize", "Queue disc capacity in packets (default 50)", queueDiscSize);
    cmd.AddValue("run", "Independent simulation replication number (sets SetRun)", runIndex);
    cmd.AddValue("seed", "Base random number generator seed (sets SetSeed)", seedValue);
    cmd.AddValue("summaryCsv", "Path for per-class summary CSV output", customSummaryCsv);
    cmd.AddValue("flowCsv", "Path for per-flow CSV output", customFlowCsv);
    cmd.AddValue("queueCsv", "Path for queue diagnostics CSV output", customQueueCsv);
    cmd.AddValue("xmlFile", "Path for FlowMonitor XML output with delay histograms", customXmlFile);
    cmd.Parse(argc, argv);

    // -------------------------------------------------------------------------
    // MATHEMATICAL RATE & DUTY CYCLE CALCULATION
    // -------------------------------------------------------------------------
    // Emergency Traffic: 10 sources @ 20 kbps = 200 kbps (always steady)
    // Ordinary Traffic:  30 sources @ 40 kbps nominal average = 1,200 kbps
    // Total Average Offered Payload = 1,400 kbps (140% of 1 Mbps bottleneck)
    double dutyCycle = onTime / (onTime + offTime);
    double ordinarySteadyBps = 40000.0; // 40 kbps
    double ordinaryBurstBps = ordinarySteadyBps / dutyCycle; // 80 kbps at 50% duty cycle

    DataRate emergencyRate("20kb/s");
    DataRate ordinarySteadyRate(std::to_string(static_cast<uint64_t>(ordinarySteadyBps)) + "bps");
    DataRate ordinaryBurstRate(std::to_string(static_cast<uint64_t>(ordinaryBurstBps)) + "bps");

    // Configure RNG Seeds
    RngSeedManager::SetSeed(seedValue);
    RngSeedManager::SetRun(runIndex);

    // Reset Diagnostic Counters
    g_band0Enqueued = 0; g_band1Enqueued = 0; g_band2Enqueued = 0;
    g_band0Dropped  = 0; g_band1Dropped  = 0; g_band2Dropped  = 0;
    g_fifoEnqueued  = 0; g_fifoDropped   = 0;

    std::string scenarioLabel = burstyTraffic ? "BURSTY" : "STEADY";
    std::string modeLabel = priorityMode ? "Priority" : "Baseline";

    std::cout << "\n================================================================================" << std::endl;
    std::cout << "  SEAN Steady vs. Bursty Experiment: 50-Node Topology (ns-3.49)                 " << std::endl;
    std::cout << "  Traffic Scenario : " << scenarioLabel << (burstyTraffic ? " (Repeated ON/OFF Periods)" : " (Continuous Jittered CBR)") << std::endl;
    std::cout << "  Operating Mode   : " << (priorityMode ? "PRIORITY (PrioQueueDisc)" : "BASELINE (FifoQueueDisc)") << std::endl;
    std::cout << "  Replication Run  : " << runIndex << " (Seed: " << seedValue << ")" << std::endl;
    std::cout << "  Emergency Rate   : 200 Kbps total (10 sources x 20 kbps steady)" << std::endl;
    if (burstyTraffic)
    {
        std::cout << "  Ordinary Pattern : BURSTY (ON=" << onTime << "s @ " << (ordinaryBurstBps / 1000.0)
                  << " kbps, OFF=" << offTime << "s @ 0 kbps, Duty=" << (dutyCycle * 100.0) << "%)" << std::endl;
        std::cout << "  Ordinary Peak    : " << (30.0 * ordinaryBurstBps / 1000.0) << " Kbps during ON (240% overload alone)" << std::endl;
        std::cout << "  Ordinary Average : 1200 Kbps long-run equivalent (30 sources x 40 kbps)" << std::endl;
    }
    else
    {
        std::cout << "  Ordinary Pattern : STEADY (Continuous 40 kbps per source x 30 = 1200 Kbps total)" << std::endl;
    }
    std::cout << "  Bottleneck Link  : 1 Mbps, 10 ms, QueueDisc Capacity: " << queueDiscSize << " packets" << std::endl;
    std::cout << "================================================================================" << std::endl;

    // -------------------------------------------------------------------------
    // 1. NODE CREATION (EXACTLY 50 NODES)
    // -------------------------------------------------------------------------
    NodeContainer nodes;
    nodes.Create(50);

    NodeContainer emergencySources;
    for (uint32_t i = 0; i < 10; ++i) emergencySources.Add(nodes.Get(i));

    NodeContainer ordinarySources;
    for (uint32_t i = 10; i < 40; ++i) ordinarySources.Add(nodes.Get(i));

    Ptr<Node> r1 = nodes.Get(40);
    Ptr<Node> r2 = nodes.Get(41);
    Ptr<Node> r3 = nodes.Get(42);

    Ptr<Node> gwIn = nodes.Get(43);
    Ptr<Node> gwEg = nodes.Get(44);

    NodeContainer servers;
    for (uint32_t i = 45; i < 50; ++i) servers.Add(nodes.Get(i));

    // -------------------------------------------------------------------------
    // 2. TOPOLOGY & LINK CREATION (EXACTLY 49 POINT-TO-POINT LINKS)
    // -------------------------------------------------------------------------
    PointToPointHelper accessLink;
    accessLink.SetDeviceAttribute("DataRate", StringValue(accessBandwidth));
    accessLink.SetChannelAttribute("Delay", StringValue(accessDelay));
    accessLink.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("100p"));

    std::vector<NetDeviceContainer> accessDevices;
    accessDevices.reserve(40);

    // Zone A -> R1 (Nodes 0..2 emergency, Nodes 10..19 ordinary)
    for (uint32_t i = 0; i <= 2; ++i)   accessDevices.push_back(accessLink.Install(nodes.Get(i), r1));
    for (uint32_t i = 10; i <= 19; ++i) accessDevices.push_back(accessLink.Install(nodes.Get(i), r1));

    // Zone B -> R2 (Nodes 3..5 emergency, Nodes 20..29 ordinary)
    for (uint32_t i = 3; i <= 5; ++i)   accessDevices.push_back(accessLink.Install(nodes.Get(i), r2));
    for (uint32_t i = 20; i <= 29; ++i) accessDevices.push_back(accessLink.Install(nodes.Get(i), r2));

    // Zone C -> R3 (Nodes 6..9 emergency, Nodes 30..39 ordinary)
    for (uint32_t i = 6; i <= 9; ++i)   accessDevices.push_back(accessLink.Install(nodes.Get(i), r3));
    for (uint32_t i = 30; i <= 39; ++i) accessDevices.push_back(accessLink.Install(nodes.Get(i), r3));

    // Regional Trunks -> GW_in
    PointToPointHelper trunkLink;
    trunkLink.SetDeviceAttribute("DataRate", StringValue(aggBandwidth));
    trunkLink.SetChannelAttribute("Delay", StringValue(aggDelay));
    trunkLink.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("100p"));

    NetDeviceContainer trunkR1 = trunkLink.Install(r1, gwIn);
    NetDeviceContainer trunkR2 = trunkLink.Install(r2, gwIn);
    NetDeviceContainer trunkR3 = trunkLink.Install(r3, gwIn);

    // Core Shared Bottleneck Link: GW_in -> GW_eg
    PointToPointHelper bottleneckLink;
    bottleneckLink.SetDeviceAttribute("DataRate", StringValue(bottleneckBandwidth));
    bottleneckLink.SetChannelAttribute("Delay", StringValue(bottleneckDelay));
    bottleneckLink.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("1p"));

    NetDeviceContainer devBottleneckContainer = bottleneckLink.Install(gwIn, gwEg);

    // Server Distribution Links: GW_eg -> Servers
    PointToPointHelper serverLink;
    serverLink.SetDeviceAttribute("DataRate", StringValue(serverBandwidth));
    serverLink.SetChannelAttribute("Delay", StringValue(serverDelay));
    serverLink.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("100p"));

    std::vector<NetDeviceContainer> serverDevices;
    serverDevices.reserve(5);
    for (uint32_t i = 0; i < 5; ++i)
    {
        serverDevices.push_back(serverLink.Install(gwEg, servers.Get(i)));
    }

    // -------------------------------------------------------------------------
    // 3. INTERNET STACK & IP ADDRESSING
    // -------------------------------------------------------------------------
    InternetStackHelper internet;
    internet.Install(nodes);

    Ipv4AddressHelper ipv4;
    std::vector<Ipv4InterfaceContainer> accessInterfaces;
    accessInterfaces.reserve(40);

    for (uint32_t i = 0; i < 40; ++i)
    {
        std::stringstream ss;
        ss << "10.1." << (i + 1) << ".0";
        ipv4.SetBase(ss.str().c_str(), "255.255.255.0");
        accessInterfaces.push_back(ipv4.Assign(accessDevices[i]));
    }

    ipv4.SetBase("10.2.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifTrunkR1 = ipv4.Assign(trunkR1);

    ipv4.SetBase("10.2.2.0", "255.255.255.0");
    Ipv4InterfaceContainer ifTrunkR2 = ipv4.Assign(trunkR2);

    ipv4.SetBase("10.2.3.0", "255.255.255.0");
    Ipv4InterfaceContainer ifTrunkR3 = ipv4.Assign(trunkR3);

    ipv4.SetBase("10.3.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifBottleneck = ipv4.Assign(devBottleneckContainer);

    std::vector<Ipv4InterfaceContainer> serverInterfaces;
    serverInterfaces.reserve(5);
    for (uint32_t i = 0; i < 5; ++i)
    {
        std::stringstream ss;
        ss << "10.4." << (i + 1) << ".0";
        ipv4.SetBase(ss.str().c_str(), "255.255.255.0");
        serverInterfaces.push_back(ipv4.Assign(serverDevices[i]));
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // -------------------------------------------------------------------------
    // 4. QUEUE CONTROL AT BOTTLENECK GATEWAY (NODE 43 EGRESS)
    // -------------------------------------------------------------------------
    Ptr<NetDevice> devBottleneck = devBottleneckContainer.Get(0);
    TrafficControlHelper tch;
    tch.Uninstall(devBottleneck);

    Ptr<QueueDisc> rootQdisc = nullptr;
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
    // 5. PACKET SINKS (SERVERS S0..S4)
    // -------------------------------------------------------------------------
    const uint16_t basePort = 9000;
    ApplicationContainer sinkApps;

    for (uint32_t s = 0; s < 5; ++s)
    {
        for (uint16_t portOffset = 0; portOffset < 8; ++portOffset)
        {
            uint16_t port = basePort + portOffset;
            PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                        InetSocketAddress(Ipv4Address::GetAny(), port));
            sinkApps.Add(sinkHelper.Install(servers.Get(s)));
        }
    }
    sinkApps.Start(Seconds(0.5));
    sinkApps.Stop(Seconds(simTime + 1.0));

    // -------------------------------------------------------------------------
    // 6. TRAFFIC APPLICATIONS (40 FLOWS: 10 EMERGENCY + 30 ORDINARY)
    // -------------------------------------------------------------------------
    std::vector<FlowConfig> flowConfigs;
    flowConfigs.reserve(40);
    ApplicationContainer clientApps;

    // 10 Emergency Sources (Nodes 0..9) -> Always Steady CBR (TOS 0x10 in Priority, 0x00 in Baseline)
    for (uint32_t i = 0; i < 10; ++i)
    {
        uint32_t sIdx = i % 5;
        FlowConfig fc;
        fc.sourceNodeId = i;
        fc.serverIndex = sIdx;
        fc.destServerNodeId = 45 + sIdx;
        fc.trafficClass = "Emergency Alert";
        fc.destIp = serverInterfaces[sIdx].GetAddress(1);
        fc.destPort = basePort + static_cast<uint16_t>(i / 5);
        fc.tos = priorityMode ? 0x10 : 0x00;
        fc.steadyRate = emergencyRate;
        fc.burstRate = emergencyRate;
        fc.isBursty = false; // Emergency alerts are never bursty
        flowConfigs.push_back(fc);

        Ptr<SteadyBurstyApp> app = CreateObject<SteadyBurstyApp>();
        app->Setup(InetSocketAddress(fc.destIp, fc.destPort),
                   packetSize, fc.steadyRate, fc.burstRate, fc.tos,
                   false, onTime, offTime, 0.20);
        nodes.Get(i)->AddApplication(app);
        clientApps.Add(app);
    }

    // 30 Ordinary Sources (Nodes 10..39) -> Steady CBR or Bursty ON/OFF
    for (uint32_t i = 10; i < 40; ++i)
    {
        uint32_t ordIdx = i - 10;
        uint32_t sIdx = ordIdx % 5;
        FlowConfig fc;
        fc.sourceNodeId = i;
        fc.serverIndex = sIdx;
        fc.destServerNodeId = 45 + sIdx;
        fc.trafficClass = "Ordinary Msg";
        fc.destIp = serverInterfaces[sIdx].GetAddress(1);
        fc.destPort = basePort + 2 + static_cast<uint16_t>(ordIdx / 5);
        fc.tos = 0x00;
        fc.steadyRate = ordinarySteadyRate;
        fc.burstRate = ordinaryBurstRate;
        fc.isBursty = burstyTraffic;
        flowConfigs.push_back(fc);

        Ptr<SteadyBurstyApp> app = CreateObject<SteadyBurstyApp>();
        app->Setup(InetSocketAddress(fc.destIp, fc.destPort),
                   packetSize, fc.steadyRate, fc.burstRate, fc.tos,
                   burstyTraffic, onTime, offTime, 0.20);
        nodes.Get(i)->AddApplication(app);
        clientApps.Add(app);
    }

    clientApps.Start(Seconds(1.0));
    clientApps.Stop(Seconds(simTime));

    // -------------------------------------------------------------------------
    // 7. FLOWMONITOR TELEMETRY & SIMULATION EXECUTION
    // -------------------------------------------------------------------------
    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> monitor = flowmonHelper.InstallAll();

    Simulator::Stop(Seconds(simTime + 1.0));
    Simulator::Run();

    monitor->CheckForLostPackets();
    if (!customXmlFile.empty())
    {
        monitor->SerializeToXmlFile(customXmlFile, true, true);
    }
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    // Map IP Flows to FlowConfig
    std::vector<FlowResultRecord> flowResults;
    flowResults.reserve(40);

    for (const auto& fc : flowConfigs)
    {
        FlowResultRecord fr;
        fr.flowIndex = flowResults.size() + 1;
        fr.sourceNodeId = fc.sourceNodeId;
        fr.destServerNodeId = fc.destServerNodeId;
        fr.trafficClass = fc.trafficClass;
        fr.destIp = fc.destIp;
        fr.destPort = fc.destPort;
        fr.txPackets = 0;
        fr.rxPackets = 0;
        fr.lostPackets = 0;
        fr.pdrPercent = 0.0;
        fr.throughputKbps = 0.0;
        fr.meanDelayMs = 0.0;
        fr.meanJitterMs = 0.0;

        for (const auto& entry : stats)
        {
            Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(entry.first);
            if (tuple.destinationAddress == fc.destIp && tuple.destinationPort == fc.destPort)
            {
                const auto& fs = entry.second;
                fr.txPackets = fs.txPackets;
                fr.rxPackets = fs.rxPackets;
                fr.lostPackets = fs.lostPackets;
                if (fs.txPackets > 0)
                {
                    fr.pdrPercent = (100.0 * fs.rxPackets) / fs.txPackets;
                }
                double activeTime = (simTime - 1.0); // 29.0s
                if (activeTime > 0)
                {
                    fr.throughputKbps = (fs.rxBytes * 8.0) / (activeTime * 1000.0);
                }
                if (fs.rxPackets > 0)
                {
                    fr.meanDelayMs = (fs.delaySum.GetSeconds() * 1000.0) / fs.rxPackets;
                }
                if (fs.rxPackets > 1)
                {
                    fr.meanJitterMs = (fs.jitterSum.GetSeconds() * 1000.0) / (fs.rxPackets - 1);
                }
                break;
            }
        }
        flowResults.push_back(fr);
    }

    // -------------------------------------------------------------------------
    // 8. CLASS-LEVEL AGGREGATION
    // -------------------------------------------------------------------------
    std::map<std::string, ClassSummaryMetrics> summary;
    for (const auto& tc : {"Emergency Alert", "Ordinary Msg"})
    {
        ClassSummaryMetrics cm;
        cm.trafficClass = tc;
        cm.numFlows = 0;
        cm.totalTx = 0;
        cm.totalRx = 0;
        cm.totalLost = 0;
        cm.aggregatePdr = 0.0;
        cm.aggregateThroughputKbps = 0.0;
        cm.weightedMeanDelayMs = 0.0;
        cm.meanJitterMs = 0.0;
        cm.queueDrops = 0;
        summary[tc] = cm;
    }

    double emDelayWeightSum = 0.0;
    double ordDelayWeightSum = 0.0;
    double emJitterSum = 0.0;
    double ordJitterSum = 0.0;

    for (const auto& fr : flowResults)
    {
        auto& cm = summary[fr.trafficClass];
        cm.numFlows++;
        cm.totalTx += fr.txPackets;
        cm.totalRx += fr.rxPackets;
        cm.totalLost += fr.lostPackets;
        cm.aggregateThroughputKbps += fr.throughputKbps;

        if (fr.trafficClass == "Emergency Alert")
        {
            emDelayWeightSum += fr.meanDelayMs * fr.rxPackets;
            emJitterSum += fr.meanJitterMs;
        }
        else
        {
            ordDelayWeightSum += fr.meanDelayMs * fr.rxPackets;
            ordJitterSum += fr.meanJitterMs;
        }
    }

    // Compute aggregate PDR and weighted delays
    for (auto& pair : summary)
    {
        auto& cm = pair.second;
        if (cm.totalTx > 0)
        {
            cm.aggregatePdr = (100.0 * cm.totalRx) / cm.totalTx;
        }
        if (cm.trafficClass == "Emergency Alert")
        {
            cm.weightedMeanDelayMs = (cm.totalRx > 0) ? (emDelayWeightSum / cm.totalRx) : 0.0;
            cm.meanJitterMs = (cm.numFlows > 0) ? (emJitterSum / cm.numFlows) : 0.0;
            cm.queueDrops = priorityMode ? g_band0Dropped : cm.totalLost;
        }
        else
        {
            cm.weightedMeanDelayMs = (cm.totalRx > 0) ? (ordDelayWeightSum / cm.totalRx) : 0.0;
            cm.meanJitterMs = (cm.numFlows > 0) ? (ordJitterSum / cm.numFlows) : 0.0;
            cm.queueDrops = priorityMode ? g_band1Dropped : cm.totalLost;
        }
    }

    uint32_t totalQueueDrops = rootQdisc ? rootQdisc->GetStats().nTotalDroppedPackets : 0;

    // -------------------------------------------------------------------------
    // 9. CONSOLE REPORT
    // -------------------------------------------------------------------------
    std::cout << "\n========================================================================================================" << std::endl;
    std::cout << "                 50-NODE SIMULATION RESULTS SUMMARY (Scenario: " << scenarioLabel << ", Mode: " << modeLabel << ")            " << std::endl;
    std::cout << "========================================================================================================" << std::endl;
    std::cout << std::left  << std::setw(18) << "Traffic Class"
              << std::right << std::setw(8)  << "Flows"
              << std::setw(12) << "Tx (pkts)"
              << std::setw(12) << "Rx (pkts)"
              << std::setw(12) << "Lost (pkts)"
              << std::setw(12) << "PDR (%)"
              << std::setw(18) << "Tput (Kbps)"
              << std::setw(18) << "Mean Delay (ms)"
              << std::setw(14) << "Queue Drops"
              << std::endl;
    std::cout << std::string(104, '-') << std::endl;

    for (const auto& tc : {"Emergency Alert", "Ordinary Msg"})
    {
        const auto& cm = summary[tc];
        std::cout << std::left  << std::setw(18) << cm.trafficClass
                  << std::right << std::setw(8)  << cm.numFlows
                  << std::setw(12) << cm.totalTx
                  << std::setw(12) << cm.totalRx
                  << std::setw(12) << cm.totalLost
                  << std::fixed << std::setprecision(2)
                  << std::setw(12) << cm.aggregatePdr
                  << std::setw(18) << cm.aggregateThroughputKbps
                  << std::setw(18) << cm.weightedMeanDelayMs
                  << std::setw(14) << cm.queueDrops
                  << std::endl;
    }
    std::cout << std::string(104, '-') << std::endl;
    std::cout << "  Core Bottleneck Queue Disc Total Drops : " << totalQueueDrops << " packets" << std::endl;
    if (priorityMode)
    {
        std::cout << "  Band 0 (High Priority / TOS 0x10) Drops : " << g_band0Dropped << " packets (Enqueued: " << g_band0Enqueued << ")" << std::endl;
        std::cout << "  Band 1 (Normal / Ordinary TOS 0x00) Drops: " << g_band1Dropped << " packets (Enqueued: " << g_band1Enqueued << ")" << std::endl;
    }
    std::cout << "========================================================================================================\n" << std::endl;

    // -------------------------------------------------------------------------
    // 10. FILE EXPORT
    // -------------------------------------------------------------------------
    std::string summaryFile = customSummaryCsv.empty()
        ? ("data/steady_bursty/results-summary-" + std::string(burstyTraffic ? "bursty" : "steady") + "-" + (priorityMode ? "priority" : "baseline") + "-run-" + std::to_string(runIndex) + ".csv")
        : customSummaryCsv;

    std::string flowFile = customFlowCsv.empty()
        ? ("data/steady_bursty/flow-results-" + std::string(burstyTraffic ? "bursty" : "steady") + "-" + (priorityMode ? "priority" : "baseline") + "-run-" + std::to_string(runIndex) + ".csv")
        : customFlowCsv;

    std::string queueFile = customQueueCsv.empty()
        ? ("data/steady_bursty/queue-validation-" + std::string(burstyTraffic ? "bursty" : "steady") + "-" + (priorityMode ? "priority" : "baseline") + "-run-" + std::to_string(runIndex) + ".csv")
        : customQueueCsv;

    // Export Class Summary CSV
    std::ofstream sFile(summaryFile.c_str());
    if (sFile.is_open())
    {
        sFile << "Scenario,Mode,Run,Seed,TrafficClass,FlowCount,TxPackets,RxPackets,LostPackets,PDR_Percent,Throughput_Kbps,MeanDelay_ms,MeanJitter_ms,QueueDrops\n";
        for (const auto& tc : {"Emergency Alert", "Ordinary Msg"})
        {
            const auto& cm = summary[tc];
            sFile << scenarioLabel << ","
                  << (priorityMode ? "priority" : "baseline") << ","
                  << runIndex << ","
                  << seedValue << ","
                  << "\"" << cm.trafficClass << "\","
                  << cm.numFlows << ","
                  << cm.totalTx << ","
                  << cm.totalRx << ","
                  << cm.totalLost << ","
                  << std::fixed << std::setprecision(2) << cm.aggregatePdr << ","
                  << std::fixed << std::setprecision(2) << cm.aggregateThroughputKbps << ","
                  << std::fixed << std::setprecision(3) << cm.weightedMeanDelayMs << ","
                  << std::fixed << std::setprecision(3) << cm.meanJitterMs << ","
                  << cm.queueDrops << "\n";
        }
        sFile.close();
        std::cout << "[Saved] Summary CSV written to: " << summaryFile << std::endl;
    }

    // Export Flow Results CSV
    std::ofstream fFile(flowFile.c_str());
    if (fFile.is_open())
    {
        fFile << "Scenario,Mode,Run,FlowIndex,SourceNode,DestServer,TrafficClass,TxPackets,RxPackets,LostPackets,PDR_Percent,Throughput_Kbps,MeanDelay_ms,MeanJitter_ms\n";
        for (const auto& fr : flowResults)
        {
            fFile << scenarioLabel << ","
                  << (priorityMode ? "priority" : "baseline") << ","
                  << runIndex << ","
                  << fr.flowIndex << ","
                  << fr.sourceNodeId << ","
                  << fr.destServerNodeId << ","
                  << "\"" << fr.trafficClass << "\","
                  << fr.txPackets << ","
                  << fr.rxPackets << ","
                  << fr.lostPackets << ","
                  << std::fixed << std::setprecision(2) << fr.pdrPercent << ","
                  << std::fixed << std::setprecision(2) << fr.throughputKbps << ","
                  << std::fixed << std::setprecision(3) << fr.meanDelayMs << ","
                  << std::fixed << std::setprecision(3) << fr.meanJitterMs << "\n";
        }
        fFile.close();
    }

    // Export Queue Diagnostics CSV
    std::ofstream qFile(queueFile.c_str());
    if (qFile.is_open())
    {
        qFile << "Scenario,Mode,Run,TotalDrops,Band0Enqueued,Band0Dropped,Band1Enqueued,Band1Dropped,Band2Enqueued,Band2Dropped,FifoEnqueued,FifoDropped\n";
        qFile << scenarioLabel << ","
              << (priorityMode ? "priority" : "baseline") << ","
              << runIndex << ","
              << totalQueueDrops << ","
              << g_band0Enqueued << ","
              << g_band0Dropped << ","
              << g_band1Enqueued << ","
              << g_band1Dropped << ","
              << g_band2Enqueued << ","
              << g_band2Dropped << ","
              << g_fifoEnqueued << ","
              << g_fifoDropped << "\n";
        qFile.close();
    }

    Simulator::Destroy();
    return 0;
}

