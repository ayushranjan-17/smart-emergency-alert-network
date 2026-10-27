/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Smart Emergency Alert and Priority Messaging Network
 * Network Programming (NWP) Project
 *
 * Topology:
 *   [N0: Emergency Source]  ---(10Mbps, 1ms)---\
 *   [N1: Ordinary Source 1] ---(10Mbps, 1ms)---+---> [N3: Gateway Router] ---(1Mbps, 10ms)---> [N4: Response Centre]
 *   [N2: Ordinary Source 2] ---(10Mbps, 1ms)---/
 *
 * Modes:
 *   --priorityMode=false : Baseline FIFO queueing (all traffic treated equally)
 *   --priorityMode=true  : Priority queueing with PrioQueueDisc & TOS classification
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/traffic-control-module.h"

#include <iomanip>
#include <iostream>
#include <map>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("EmergencyAlertSimulation");

int
main(int argc, char* argv[])
{
    bool priorityMode = false;

    CommandLine cmd(__FILE__);
    cmd.AddValue("priorityMode",
                 "Enable priority queueing (PrioQueueDisc) for emergency alerts",
                 priorityMode);
    cmd.Parse(argc, argv);

    // Fix random seed and run number for deterministic, reproducible comparisons
    RngSeedManager::SetSeed(1);
    RngSeedManager::SetRun(1);

    std::cout << "================================================================================"
              << std::endl;
    std::cout << "  Smart Emergency Alert and Priority Messaging Network Simulation"
              << std::endl;
    std::cout << "  Operating Mode: "
              << (priorityMode ? "PRIORITY (PrioQueueDisc)" : "BASELINE (FifoQueueDisc)")
              << std::endl;
    std::cout << "================================================================================"
              << std::endl;

    // Create 5 simulation nodes:
    // Node 0: Emergency Alert Source
    // Node 1: Ordinary Message Source 1
    // Node 2: Ordinary Message Source 2
    // Node 3: Gateway / Intermediate Router
    // Node 4: Emergency Response Centre / Server
    NodeContainer nodes;
    nodes.Create(5);

    Ptr<Node> n0Emergency = nodes.Get(0);
    Ptr<Node> n1Ordinary1 = nodes.Get(1);
    Ptr<Node> n2Ordinary2 = nodes.Get(2);
    Ptr<Node> n3Gateway   = nodes.Get(3);
    Ptr<Node> n4Server    = nodes.Get(4);

    // Access links: 10 Mbps bandwidth, 1 ms delay, high capacity to isolate bottleneck
    PointToPointHelper accessLink;
    accessLink.SetDeviceAttribute("DataRate", StringValue("10Mbps"));
    accessLink.SetChannelAttribute("Delay", StringValue("1ms"));
    accessLink.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("100p"));

    NetDeviceContainer d0d3 = accessLink.Install(n0Emergency, n3Gateway);
    NetDeviceContainer d1d3 = accessLink.Install(n1Ordinary1, n3Gateway);
    NetDeviceContainer d2d3 = accessLink.Install(n2Ordinary2, n3Gateway);

    // Bottleneck link: 1 Mbps bandwidth, 10 ms propagation delay
    // NetDevice queue is set to 1 packet so packets queue in the QueueDisc (TC layer)
    PointToPointHelper bottleneckLink;
    bottleneckLink.SetDeviceAttribute("DataRate", StringValue("1Mbps"));
    bottleneckLink.SetChannelAttribute("Delay", StringValue("10ms"));
    bottleneckLink.SetQueue("ns3::DropTailQueue", "MaxSize", StringValue("1p"));

    NetDeviceContainer d3d4 = bottleneckLink.Install(n3Gateway, n4Server);

    // Install internet stack on all nodes
    InternetStackHelper stack;
    stack.Install(nodes);

    // Assign IP addresses to each link subnet
    Ipv4AddressHelper address;

    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer if0if3 = address.Assign(d0d3);

    address.SetBase("10.1.2.0", "255.255.255.0");
    Ipv4InterfaceContainer if1if3 = address.Assign(d1d3);

    address.SetBase("10.1.3.0", "255.255.255.0");
    Ipv4InterfaceContainer if2if3 = address.Assign(d2d3);

    address.SetBase("10.1.4.0", "255.255.255.0");
    Ipv4InterfaceContainer if3if4 = address.Assign(d3d4);

    // Populate routing tables
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // Configure queue discipline on the gateway's outgoing bottleneck interface (d3d4.Get(0))
    TrafficControlHelper tch;
    tch.Uninstall(d3d4.Get(0));

    if (priorityMode)
    {
        // Priority mode: PrioQueueDisc with 3 bands and FIFO leaf queue discs.
        // Priomap maps socket priority to bands:
        // Priority 6 (Interactive, TOS 0x10) -> Band 0 (highest priority, dequeued first)
        // Priority 0 (Best Effort, TOS 0x00) -> Band 1 (lower priority)
        // Band 2 serves any other lowest priority traffic.
        uint16_t handle = tch.SetRootQueueDisc(
            "ns3::PrioQueueDisc",
            "Priomap",
            StringValue("1 2 2 2 1 2 0 0 1 1 1 1 1 1 1 1"));

        TrafficControlHelper::ClassIdList cid =
            tch.AddQueueDiscClasses(handle, 3, "ns3::QueueDiscClass");
        tch.AddChildQueueDisc(handle, cid[0], "ns3::FifoQueueDisc", "MaxSize", QueueSizeValue(QueueSize("50p")));
        tch.AddChildQueueDisc(handle, cid[1], "ns3::FifoQueueDisc", "MaxSize", QueueSizeValue(QueueSize("50p")));
        tch.AddChildQueueDisc(handle, cid[2], "ns3::FifoQueueDisc", "MaxSize", QueueSizeValue(QueueSize("50p")));

        tch.Install(d3d4.Get(0));
    }
    else
    {
        // Baseline mode: Standard single FIFO queue disc with capacity of 50 packets.
        // Packets from all sources are queued into the same FIFO queue without differentiation.
        tch.SetRootQueueDisc("ns3::FifoQueueDisc", "MaxSize", QueueSizeValue(QueueSize("50p")));
        tch.Install(d3d4.Get(0));
    }

    // Configure traffic sinks on Node 4 (Emergency Response Centre)
    const uint16_t portEmergency = 9000;
    const uint16_t portOrdinary1 = 9001;
    const uint16_t portOrdinary2 = 9002;

    PacketSinkHelper sinkHelper("ns3::UdpSocketFactory", Address());

    sinkHelper.SetAttribute("Local",
                            AddressValue(InetSocketAddress(Ipv4Address::GetAny(), portEmergency)));
    ApplicationContainer sinkApps = sinkHelper.Install(n4Server);

    sinkHelper.SetAttribute("Local",
                            AddressValue(InetSocketAddress(Ipv4Address::GetAny(), portOrdinary1)));
    sinkApps.Add(sinkHelper.Install(n4Server));

    sinkHelper.SetAttribute("Local",
                            AddressValue(InetSocketAddress(Ipv4Address::GetAny(), portOrdinary2)));
    sinkApps.Add(sinkHelper.Install(n4Server));

    sinkApps.Start(Seconds(0.5));
    sinkApps.Stop(Seconds(31.0));

    // Configure traffic generators (OnOffApplication in UDP mode)
    // Packet size: 512 bytes, Rate: 400 kbps constant
    // 3 sources * 400 kbps = 1.2 Mbps offering load > 1.0 Mbps bottleneck capacity
    Ipv4Address serverIp = if3if4.GetAddress(1);

    // 1. Ordinary Message Application 1 (Node 1)
    OnOffHelper onOffOrdinary1("ns3::UdpSocketFactory",
                              InetSocketAddress(serverIp, portOrdinary1));
    onOffOrdinary1.SetConstantRate(DataRate("400kb/s"), 512);
    onOffOrdinary1.SetAttribute("Tos", UintegerValue(0x00));
    ApplicationContainer clientApps = onOffOrdinary1.Install(n1Ordinary1);

    // 2. Ordinary Message Application 2 (Node 2)
    OnOffHelper onOffOrdinary2("ns3::UdpSocketFactory",
                              InetSocketAddress(serverIp, portOrdinary2));
    onOffOrdinary2.SetConstantRate(DataRate("400kb/s"), 512);
    onOffOrdinary2.SetAttribute("Tos", UintegerValue(0x00));
    clientApps.Add(onOffOrdinary2.Install(n2Ordinary2));

    // 3. Emergency Alert Application (Node 0)
    OnOffHelper onOffEmergency("ns3::UdpSocketFactory",
                              InetSocketAddress(serverIp, portEmergency));
    onOffEmergency.SetConstantRate(DataRate("400kb/s"), 512);
    if (priorityMode)
    {
        // TOS 0x10 (Low Delay / Interactive) translates to Socket Priority 6
        onOffEmergency.SetAttribute("Tos", UintegerValue(0x10));
    }
    else
    {
        // Default Best Effort TOS 0x00
        onOffEmergency.SetAttribute("Tos", UintegerValue(0x00));
    }
    clientApps.Add(onOffEmergency.Install(n0Emergency));

    clientApps.Start(Seconds(1.0));
    clientApps.Stop(Seconds(30.0));

    // Install FlowMonitor to record end-to-end network metrics
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    // Run simulation
    Simulator::Stop(Seconds(31.0));
    Simulator::Run();

    // Collect flow statistics
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();

    std::cout << "\n--------------------------------------------------------------------------------"
              << std::endl;
    std::cout << "                         FLOW MONITOR RESULTS SUMMARY                           "
              << std::endl;
    std::cout << "--------------------------------------------------------------------------------"
              << std::endl;
    std::cout << std::left << std::setw(20) << "Traffic Class"
              << std::right << std::setw(8) << "Port"
              << std::setw(10) << "Tx Pkts"
              << std::setw(10) << "Rx Pkts"
              << std::setw(10) << "Lost"
              << std::setw(10) << "PDR (%)"
              << std::setw(14) << "Tput (Kbps)"
              << std::setw(14) << "Delay (ms)"
              << std::setw(14) << "Jitter (ms)"
              << std::endl;
    std::cout << std::string(110, '-') << std::endl;

    for (const auto& entry : stats)
    {
        FlowId flowId = entry.first;
        FlowMonitor::FlowStats flowStats = entry.second;
        Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flowId);

        // Only inspect forward flows going to the server ports
        if (tuple.destinationAddress != serverIp)
        {
            continue;
        }

        std::string flowLabel;
        if (tuple.destinationPort == portEmergency)
        {
            flowLabel = "Emergency Alert";
        }
        else if (tuple.destinationPort == portOrdinary1)
        {
            flowLabel = "Ordinary Msg 1";
        }
        else if (tuple.destinationPort == portOrdinary2)
        {
            flowLabel = "Ordinary Msg 2";
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

        std::cout << std::left << std::setw(20) << flowLabel
                  << std::right << std::setw(8) << tuple.destinationPort
                  << std::setw(10) << flowStats.txPackets
                  << std::setw(10) << flowStats.rxPackets
                  << std::setw(10) << flowStats.lostPackets
                  << std::setw(10) << std::fixed << std::setprecision(2) << pdr
                  << std::setw(14) << std::fixed << std::setprecision(2) << throughputKbps
                  << std::setw(14) << std::fixed << std::setprecision(3) << meanDelayMs
                  << std::setw(14) << std::fixed << std::setprecision(3) << meanJitterMs
                  << std::endl;
    }
    std::cout << std::string(110, '-') << std::endl;

    // Serialize statistics to XML file for post-processing or submission
    std::string xmlFileName = priorityMode ? "emergency-priority.xml" : "emergency-baseline.xml";
    monitor->SerializeToXmlFile(xmlFileName, true, true);
    std::cout << "Detailed FlowMonitor statistics saved to: " << xmlFileName << std::endl;
    std::cout << "================================================================================\n"
              << std::endl;

    Simulator::Destroy();
    return 0;
}
