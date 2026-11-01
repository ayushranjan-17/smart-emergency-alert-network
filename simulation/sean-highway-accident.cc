/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * SEAN add-on: Highway accident warning (vehicle-to-vehicle).
 *
 * A vehicle crashes on a straight highway at time T. It broadcasts a warning.
 * Vehicles behind the accident receive it (directly or by multi-hop relay),
 * and slow down so they can stop safely before reaching the crash site.
 *
 * Run from the ns-3 root (after copying this file to scratch/):
 *   ./ns3 run "sean-highway-accident --vehicles=30 --priority=1"
 *   ./ns3 run "sean-highway-accident --vehicles=30 --priority=0 --bgRateKbps=800"
 *
 * Output: highway-accident-vehicles.csv (one row per vehicle behind the crash)
 *         and a summary on stdout.
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/applications-module.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("SeanHighwayAccident");

namespace
{

const uint16_t WARNING_PORT = 9900;
const uint16_t BG_PORT = 9901;

// Warning message exchanged over UDP broadcast (fixed 10 bytes, same process).
struct WarningMsg
{
    uint32_t accidentId;
    int32_t xCm;   // accident position along the road, in centimetres
    uint8_t ttl;   // remaining relay hops
    uint8_t hops;  // hops travelled so far
};

// Global simulation parameters (set from command line).
struct Params
{
    uint32_t vehicles = 30;      // vehicles behind the accident
    double spacingM = 40.0;      // gap between vehicles (m)
    double speedMps = 20.0;      // cruising speed (~90 km/h)
    double decelMps2 = 8.0;      // comfortable braking deceleration
    double standoffM = 10.0;     // desired stopping distance before crash
    double accidentTimeS = 10.0; // when the crash happens
    double simTimeS = 40.0;
    uint8_t maxTtl = 40;         // max relay hops
    uint8_t warnTos = 0xB8;      // EF (DSCP 46) when priority=1, 0 otherwise
    uint32_t bgRateKbps = 0;     // civilian background broadcast load per vehicle
    bool priority = true;
    std::string csvFile = "highway-accident-vehicles.csv";
} P;

struct VehicleState
{
    Ptr<Node> node;
    Ptr<ConstantVelocityMobilityModel> mob;
    Ptr<Socket> rx;     // receives warnings
    Ptr<Socket> tx;     // sends warnings / background traffic
    bool warned = false;
    bool relayed = false;
    double warnTime = -1.0;
    double warnDistance = -1.0;  // distance to accident when first warned
    uint8_t hops = 0;
    double minSpeed = 1e9;
    double closestApproach = 1e9;  // min gap to accident (negative = collision)
};

std::vector<VehicleState> g_vehicles;  // index 0 = crashed vehicle
Ipv4Address g_bcast("10.1.1.255");
double g_accidentX = 0.0;
bool g_accidentActive = false;
uint32_t g_accidentId = 1;
std::map<uint32_t, bool> g_seen;  // per-vehicle dedupe is in VehicleState; this is unused fallback

void SendWarning(uint32_t idx, uint8_t ttl, uint8_t hops)
{
    WarningMsg m{g_accidentId, (int32_t)std::lround(g_accidentX * 100.0), ttl, hops};
    Ptr<Packet> p = Create<Packet>((uint8_t*)&m, sizeof(m));
    g_vehicles[idx].tx->SendTo(p, 0, InetSocketAddress(g_bcast, WARNING_PORT));
}

void SetVelocity(uint32_t idx, double v)
{
    g_vehicles[idx].mob->SetVelocity(Vector(v, 0.0, 0.0));
}

// Accident vehicle announces the crash, repeated for reliability.
void AccidentBroadcast(uint32_t repeat)
{
    SendWarning(0, P.maxTtl, 0);
    if (repeat > 0)
    {
        Simulator::Schedule(Seconds(0.5), &AccidentBroadcast, repeat - 1);
    }
}

void CrashEvent()
{
    VehicleState& crash = g_vehicles[0];
    g_accidentX = crash.mob->GetPosition().x;
    g_accidentActive = true;
    SetVelocity(0, 0.0);  // crashed vehicle stops here
    NS_LOG_UNCOND("t=" << Simulator::Now().GetSeconds() << "s accident at x=" << g_accidentX << " m");
    AccidentBroadcast(2);
}

// Receive handler: warnings from the crashed car or relays.
void OnWarning(uint32_t idx, Ptr<Socket> s)
{
    Address from;
    Ptr<Packet> p;
    while ((p = s->RecvFrom(from)))
    {
        if (p->GetSize() < sizeof(WarningMsg))
        {
            continue;
        }
        WarningMsg m;
        p->CopyData((uint8_t*)&m, sizeof(m));
        VehicleState& v = g_vehicles[idx];
        if (idx == 0 || v.warned)
        {
            continue;  // the crashed car ignores its own echo; dedupe repeats
        }
        double x = v.mob->GetPosition().x;
        if (x >= m.xCm / 100.0)
        {
            continue;  // vehicles already past the accident don't need to slow
        }

        v.warned = true;
        v.warnTime = Simulator::Now().GetSeconds();
        v.warnDistance = m.xCm / 100.0 - x;
        v.hops = m.hops + 1;

        // Relay once (with a small random jitter to reduce collisions).
        if (m.ttl > 0 && !v.relayed)
        {
            v.relayed = true;
            double jitter = 0.005 + 0.02 * (double)(idx % 7) / 7.0;
            Simulator::Schedule(Seconds(jitter), &SendWarning, idx, (uint8_t)(m.ttl - 1), (uint8_t)(m.hops + 1));
        }
    }
}

// Speed controller: each vehicle brakes so it can stop standoffM before the crash.
void ControlStep(double dt)
{
    if (g_accidentActive)
    {
        for (uint32_t i = 1; i < g_vehicles.size(); ++i)
        {
            VehicleState& v = g_vehicles[i];
            double x = v.mob->GetPosition().x;
            double cur = v.mob->GetVelocity().x;
            double gap = g_accidentX - x;
            if (gap < v.closestApproach)
            {
                v.closestApproach = gap;
            }
            if (v.warned && gap > 0.0)
            {
                // Speed from which we can still stop within the available distance.
                double usable = std::max(gap - P.standoffM, 0.0);
                double target = std::min(P.speedMps, std::sqrt(2.0 * P.decelMps2 * usable));
                double next = std::max(target, cur - P.decelMps2 * dt);  // limited braking
                next = std::min(next, cur);                              // never speed up
                SetVelocity(i, next);
                cur = next;
            }
            if (cur < v.minSpeed)
            {
                v.minSpeed = cur;
            }
        }
    }
    Simulator::Schedule(Seconds(dt), &ControlStep, dt);
}

// Optional civilian background broadcast (priority=0 vs 1 comparison).
void BackgroundSend(uint32_t idx, uint32_t bytes, double interval)
{
    if (P.bgRateKbps > 0)
    {
        std::vector<uint8_t> buf(bytes, 0);
        Ptr<Packet> p = Create<Packet>(buf.data(), bytes);
        g_vehicles[idx].tx->SendTo(p, 0, InetSocketAddress(g_bcast, BG_PORT));
    }
    Simulator::Schedule(Seconds(interval), &BackgroundSend, idx, bytes, interval);
}

} // namespace

int main(int argc, char* argv[])
{
    CommandLine cmd(__FILE__);
    cmd.AddValue("vehicles", "Vehicles behind the accident", P.vehicles);
    cmd.AddValue("spacing", "Gap between vehicles (m)", P.spacingM);
    cmd.AddValue("speed", "Cruising speed (m/s)", P.speedMps);
    cmd.AddValue("decel", "Braking deceleration (m/s^2)", P.decelMps2);
    cmd.AddValue("standoff", "Stopping distance before crash (m)", P.standoffM);
    cmd.AddValue("accidentTime", "Time of the crash (s)", P.accidentTimeS);
    cmd.AddValue("simTime", "Simulation time (s)", P.simTimeS);
    cmd.AddValue("maxTtl", "Max relay hops", P.maxTtl);
    cmd.AddValue("priority", "1 = EF/QoS for warnings, 0 = best effort", P.priority);
    cmd.AddValue("bgRateKbps", "Civilian background broadcast per vehicle (kbps)", P.bgRateKbps);
    cmd.AddValue("csv", "Output CSV file", P.csvFile);
    cmd.Parse(argc, argv);
    P.warnTos = P.priority ? 0xB8 : 0x00;

    // Vehicles: index 0 is the crashed car at the front; 1..N are behind it.
    // All move along +x at the same speed. The crash car sits at the front of the
    // platoon, so following cars are behind it.
    uint32_t total = P.vehicles + 1;
    NodeContainer nodes;
    nodes.Create(total);

    Ptr<ListPositionAllocator> pos = CreateObject<ListPositionAllocator>();
    for (uint32_t i = 0; i < total; ++i)
    {
        // Crash car at x = 0 + gap, followers spaced behind it.
        double x = (i == 0) ? 0.0 : -(double)i * P.spacingM;
        pos->Add(Vector(x, 0.0, 0.0));
    }
    MobilityHelper mob;
    mob.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mob.SetPositionAllocator(pos);
    mob.Install(nodes);

    // 802.11g ad-hoc network. QoS enables TOS -> access category mapping (EF -> voice).
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211g);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("ErpOfdmRate6Mbps"));
    YansWifiPhyHelper phy;
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    phy.SetChannel(channel.Create());
    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac", "QosSupported", BooleanValue(P.priority));
    NetDeviceContainer devs = wifi.Install(phy, mac, nodes);

    InternetStackHelper stack;
    stack.Install(nodes);
    Ipv4AddressHelper addr;
    addr.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer ifs = addr.Assign(devs);

    // Per-vehicle sockets.
    g_vehicles.resize(total);
    TypeId udp = TypeId::LookupByName("ns3::UdpSocketFactory");
    for (uint32_t i = 0; i < total; ++i)
    {
        VehicleState& v = g_vehicles[i];
        v.node = nodes.Get(i);
        v.mob = v.node->GetObject<ConstantVelocityMobilityModel>();
        v.mob->SetVelocity(Vector(P.speedMps, 0.0, 0.0));

        v.rx = Socket::CreateSocket(v.node, udp);
        v.rx->SetAllowBroadcast(true);
        v.rx->Bind(InetSocketAddress(Ipv4Address::GetAny(), WARNING_PORT));
        v.rx->SetRecvCallback(MakeBoundCallback(&OnWarning, i));

        v.tx = Socket::CreateSocket(v.node, udp);
        v.tx->SetAllowBroadcast(true);
        v.tx->Bind();
        v.tx->SetIpTos(P.warnTos);
    }

    // Crash at accidentTime; control loop every 100 ms.
    Simulator::Schedule(Seconds(P.accidentTimeS), &CrashEvent);
    Simulator::Schedule(Seconds(0.1), &ControlStep, 0.1);

    // Background civilian traffic (tos 0) to test priority under load.
    if (P.bgRateKbps > 0)
    {
        double interval = (512.0 * 8.0) / (P.bgRateKbps * 1000.0);
        for (uint32_t i = 0; i < total; ++i)
        {
            g_vehicles[i].tx->SetIpTos(0x00);
            Simulator::Schedule(Seconds(1.0 + 0.001 * i), &BackgroundSend, i, 512u, interval);
        }
    }

    Simulator::Stop(Seconds(P.simTimeS));
    Simulator::Run();

    // Results.
    std::ofstream csv(P.csvFile);
    csv << "vehicle,warned,warn_time_s,hops,distance_at_warning_m,min_speed_mps,gap_at_closest_m,collision\n";
    uint32_t behind = 0, warned = 0, collisions = 0;
    double sumDelay = 0.0;
    uint32_t maxHops = 0;
    for (uint32_t i = 1; i < g_vehicles.size(); ++i)
    {
        VehicleState& v = g_vehicles[i];
        ++behind;
        bool collided = v.closestApproach < 0.0;
        if (collided)
        {
            ++collisions;
        }
        if (v.warned)
        {
            ++warned;
            maxHops = std::max<uint32_t>(maxHops, v.hops);
            sumDelay += v.warnTime - P.accidentTimeS;
        }
        csv << i << "," << (v.warned ? 1 : 0) << "," << v.warnTime << ","
            << (unsigned)v.hops << "," << v.warnDistance << ","
            << v.minSpeed << "," << v.closestApproach << "," << (collided ? 1 : 0) << "\n";
    }
    csv.close();

    std::cout << "=== SEAN highway accident warning ===\n"
              << "Mode:                       " << (P.priority ? "priority (EF/QoS)" : "best effort") << "\n"
              << "Vehicles behind accident:   " << behind << "\n"
              << "Warned:                     " << warned << " (" << (behind ? 100.0 * warned / behind : 0) << "%)\n"
              << "Max relay hops used:        " << maxHops << "\n"
              << "Mean warning delay:         " << (warned ? sumDelay / warned : 0.0) << " s after crash\n"
              << "Collisions with crash site: " << collisions << "\n"
              << "Details written to:         " << P.csvFile << "\n";

    Simulator::Destroy();
    return 0;
}
