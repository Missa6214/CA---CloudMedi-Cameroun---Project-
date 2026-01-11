/* 
 * CloudMedi-Cameroun - Compatible NS-3.46
 * Système DISTRIBUÉ avec BD locale par hôpital
 * Architecture: Chaque hôpital possède sa propre base de données
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/netanim-module.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("CloudMediCameroun");

int main(int argc, char* argv[])
{
    // ========== PARAMÈTRES DE SIMULATION ==========
    uint32_t nHospitals = 4;           // Nombre d'hôpitaux
    uint32_t nDoctorsPerHospital = 2;  // Médecins par hôpital
    double simTime = 30.0;             // Durée en secondes
    std::string topology = "mesh";      // "star" ou "mesh"
    bool verbose = true;

    CommandLine cmd(__FILE__);
    cmd.AddValue("nHospitals", "Nombre d'hôpitaux", nHospitals);
    cmd.AddValue("nDoctors", "Médecins par hôpital", nDoctorsPerHospital);
    cmd.AddValue("simTime", "Durée simulation (s)", simTime);
    cmd.AddValue("topology", "Topologie (star/mesh)", topology);
    cmd.AddValue("verbose", "Mode verbeux", verbose);
    cmd.Parse(argc, argv);

    if (verbose)
    {
        LogComponentEnable("CloudMediCameroun", LOG_LEVEL_INFO);
    }

    NS_LOG_INFO("╔════════════════════════════════════════════════════════╗");
    NS_LOG_INFO("║   CloudMedi-Cameroun - Architecture Distribuée         ║");
    NS_LOG_INFO("╚════════════════════════════════════════════════════════╝");
    NS_LOG_INFO("");
    NS_LOG_INFO("Configuration:");
    NS_LOG_INFO("  • Topologie: " << topology);
    NS_LOG_INFO("  • Hôpitaux: " << nHospitals);
    NS_LOG_INFO("  • Médecins/hôpital: " << nDoctorsPerHospital);
    NS_LOG_INFO("  • Durée: " << simTime << "s");
    NS_LOG_INFO("");

    // ========== NOMS DES HÔPITAUX ==========
    std::vector<std::string> hospitalNames = {
        "CHU_Yaounde",
        "CHU_Douala",
        "HR_Garoua",
        "HR_Bamenda",
        "HR_Bafoussam",
        "HD_Maroua",
        "HD_Buea",
        "HD_Ngaoundere"
    };

    // ========== CRÉATION DES NŒUDS ==========
    
    // Serveurs de BD (un par hôpital)
    NodeContainer hospitalServers;
    hospitalServers.Create(nHospitals);

    // Routeur central (pour topologie étoile)
    NodeContainer centralRouter;
    if (topology == "star")
    {
        centralRouter.Create(1);
    }

    // Réseaux locaux de chaque hôpital (serveur + médecins)
    std::vector<NodeContainer> hospitalNetworks;
    for (uint32_t i = 0; i < nHospitals; ++i)
    {
        NodeContainer net;
        net.Add(hospitalServers.Get(i));
        net.Create(nDoctorsPerHospital);
        hospitalNetworks.push_back(net);
    }

    // ========== INSTALLATION PILE INTERNET ==========
    InternetStackHelper stack;
    stack.Install(hospitalServers);

    if (topology == "star")
    {
        stack.Install(centralRouter);
    }

    for (auto& net : hospitalNetworks)
    {
        for (uint32_t i = 1; i < net.GetN(); ++i)
        {
            stack.Install(net.Get(i));
        }
    }

    // ========== CONFIGURATION RÉSEAU WAN ==========
    PointToPointHelper p2pWan;
    p2pWan.SetDeviceAttribute("DataRate", StringValue("100Mbps"));

    Ipv4AddressHelper address;
    std::vector<Ipv4InterfaceContainer> wanInterfaces;

    if (topology == "star")
    {
        NS_LOG_INFO("═══ Configuration TOPOLOGIE ÉTOILE ═══");
        
        for (uint32_t i = 0; i < nHospitals; ++i)
        {
            uint32_t latency = 10 + (i * 15);
            p2pWan.SetChannelAttribute("Delay", TimeValue(MilliSeconds(latency)));

            NetDeviceContainer link = p2pWan.Install(centralRouter.Get(0),
                                                      hospitalServers.Get(i));

            std::ostringstream subnet;
            subnet << "10.1." << i << ".0";
            address.SetBase(subnet.str().c_str(), "255.255.255.0");

            Ipv4InterfaceContainer iface = address.Assign(link);
            wanInterfaces.push_back(iface);

            NS_LOG_INFO("  ✓ " << hospitalNames[i] 
                        << " connecté (latence: " << latency << "ms)");
        }
    }
    else // mesh
    {
        NS_LOG_INFO("═══ Configuration TOPOLOGIE MAILLÉE ═══");
        
        uint32_t linkIndex = 0;
        for (uint32_t i = 0; i < nHospitals; ++i)
        {
            for (uint32_t j = i + 1; j < nHospitals; ++j)
            {
                uint32_t latency = 15 + std::abs((int)i - (int)j) * 20;
                p2pWan.SetChannelAttribute("Delay", TimeValue(MilliSeconds(latency)));

                NetDeviceContainer link = p2pWan.Install(hospitalServers.Get(i),
                                                          hospitalServers.Get(j));

                std::ostringstream subnet;
                subnet << "10." << (linkIndex / 254 + 1) << "." 
                       << (linkIndex % 254) << ".0";
                address.SetBase(subnet.str().c_str(), "255.255.255.252");
                linkIndex++;

                Ipv4InterfaceContainer iface = address.Assign(link);
                wanInterfaces.push_back(iface);

                NS_LOG_INFO("  ✓ " << hospitalNames[i] << " ↔ " << hospitalNames[j]
                            << " (latence: " << latency << "ms)");
            }
        }
    }

    // ========== RÉSEAUX LOCAUX (LAN) ==========
    CsmaHelper csma;
    csma.SetChannelAttribute("DataRate", StringValue("1Gbps"));
    csma.SetChannelAttribute("Delay", TimeValue(MicroSeconds(100)));

    std::vector<Ipv4InterfaceContainer> lanInterfaces;

    NS_LOG_INFO("");
    NS_LOG_INFO("═══ Configuration RÉSEAUX LOCAUX ═══");
    for (uint32_t i = 0; i < nHospitals; ++i)
    {
        NetDeviceContainer lanDevices = csma.Install(hospitalNetworks[i]);

        std::ostringstream subnet;
        subnet << "192.168." << i << ".0";
        address.SetBase(subnet.str().c_str(), "255.255.255.0");

        Ipv4InterfaceContainer lanIface = address.Assign(lanDevices);
        lanInterfaces.push_back(lanIface);

        NS_LOG_INFO("  ✓ LAN " << hospitalNames[i] << ": " << subnet.str());
    }

    // Routage global
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    // ========== APPLICATIONS ==========
    uint16_t port = 9;

    NS_LOG_INFO("");
    NS_LOG_INFO("═══ Installation APPLICATIONS ═══");

    // Serveur UDP Echo sur chaque hôpital
    for (uint32_t i = 0; i < nHospitals; ++i)
    {
        UdpEchoServerHelper echoServer(port);
        ApplicationContainer serverApp = echoServer.Install(hospitalServers.Get(i));
        serverApp.Start(Seconds(1.0));
        serverApp.Stop(Seconds(simTime));

        NS_LOG_INFO("  ✓ Serveur BD " << hospitalNames[i] 
                    << " sur " << lanInterfaces[i].GetAddress(0));
    }

    // Scénario 1: Transfert patient Yaoundé → Douala (T=5s)
    if (nHospitals >= 2)
    {
        NS_LOG_INFO("");
        NS_LOG_INFO("═══ SCÉNARIO: Transfert patient ═══");
        NS_LOG_INFO("  T=5s: Yaoundé → Douala (PATIENT_001)");

        UdpEchoClientHelper client1(lanInterfaces[0].GetAddress(0), port);
        client1.SetAttribute("MaxPackets", UintegerValue(5));
        client1.SetAttribute("Interval", TimeValue(Seconds(1.0)));
        client1.SetAttribute("PacketSize", UintegerValue(1024));

        ApplicationContainer clientApp1 = client1.Install(hospitalNetworks[1].Get(1));
        clientApp1.Start(Seconds(5.0));
        clientApp1.Stop(Seconds(10.0));
    }

    // Scénario 2: Garoua interroge Douala (T=12s)
    if (nHospitals >= 3)
    {
        NS_LOG_INFO("  T=12s: Douala → Garoua (PATIENT_002)");

        UdpEchoClientHelper client2(lanInterfaces[1].GetAddress(0), port);
        client2.SetAttribute("MaxPackets", UintegerValue(5));
        client2.SetAttribute("Interval", TimeValue(Seconds(1.0)));
        client2.SetAttribute("PacketSize", UintegerValue(2048));

        ApplicationContainer clientApp2 = client2.Install(hospitalNetworks[2].Get(1));
        clientApp2.Start(Seconds(12.0));
        clientApp2.Stop(Seconds(17.0));
    }

    // ========== MÉTRIQUES - FLOWMONITOR ==========
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    // ========== TRACES PCAP ==========
    p2pWan.EnablePcapAll("cloudmedi-wan");
    csma.EnablePcapAll("cloudmedi-lan");

    // ========== ANIMATION NETANIM ==========
    AnimationInterface anim("cloudmedi-animation.xml");

    // Configuration visuelle
    double radius = 150;
    double centerX = 300;
    double centerY = 300;

    // Positionnement des hôpitaux en cercle
    for (uint32_t i = 0; i < nHospitals; ++i)
    {
        double angle = (2 * M_PI * i) / nHospitals;
        double x = centerX + radius * std::cos(angle);
        double y = centerY + radius * std::sin(angle);

        // Serveur BD
        anim.SetConstantPosition(hospitalServers.Get(i), x, y);
        anim.UpdateNodeDescription(hospitalServers.Get(i),
                                   hospitalNames[i] + "_BD");
        anim.UpdateNodeColor(hospitalServers.Get(i), 0, 0, 255); // Bleu

        // Postes médecins autour
        for (uint32_t d = 1; d <= nDoctorsPerHospital; ++d)
        {
            double angleOffset = (2 * M_PI * d) / nDoctorsPerHospital;
            double dx = x + 40 * std::cos(angleOffset);
            double dy = y + 40 * std::sin(angleOffset);

            anim.SetConstantPosition(hospitalNetworks[i].Get(d), dx, dy);
            anim.UpdateNodeDescription(hospitalNetworks[i].Get(d),
                                      "Dr_" + std::to_string(d));
            anim.UpdateNodeColor(hospitalNetworks[i].Get(d), 0, 255, 0); // Vert
        }
    }

    // Routeur central (si étoile)
    if (topology == "star")
    {
        anim.SetConstantPosition(centralRouter.Get(0), centerX, centerY);
        anim.UpdateNodeDescription(centralRouter.Get(0), "Routeur_WAN");
        anim.UpdateNodeColor(centralRouter.Get(0), 255, 0, 0); // Rouge
    }

    // Options animation
    anim.EnablePacketMetadata(true);

    // ========== EXÉCUTION ==========
    NS_LOG_INFO("");
    NS_LOG_INFO("═══════════════════════════════════════════════════════");
    NS_LOG_INFO("    🚀 DÉMARRAGE DE LA SIMULATION");
    NS_LOG_INFO("═══════════════════════════════════════════════════════");
    NS_LOG_INFO("");

    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    // ========== STATISTIQUES FINALES ==========
    NS_LOG_INFO("");
    NS_LOG_INFO("═══════════════════════════════════════════════════════");
    NS_LOG_INFO("    📊 STATISTIQUES FLOWMONITOR");
    NS_LOG_INFO("═══════════════════════════════════════════════════════");

    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = 
        DynamicCast<Ipv4FlowClassifier>(flowmon.GetClassifier());
    
    FlowMonitor::FlowStatsContainer stats = monitor->GetFlowStats();

    double totalThroughput = 0.0;
    double totalDelay = 0.0;
    uint32_t flowCount = 0;

    for (auto iter = stats.begin(); iter != stats.end(); ++iter)
    {
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(iter->first);

        if (iter->second.rxPackets > 0)
        {
            double throughput = iter->second.rxBytes * 8.0 / simTime / 1e6;
            double avgDelay = iter->second.delaySum.GetMilliSeconds() / 
                             iter->second.rxPackets;

            totalThroughput += throughput;
            totalDelay += avgDelay;
            flowCount++;

            NS_LOG_INFO("Flux " << iter->first);
            NS_LOG_INFO("  Source: " << t.sourceAddress 
                        << " → Dest: " << t.destinationAddress);
            NS_LOG_INFO("  Paquets TX/RX: " << iter->second.txPackets 
                        << "/" << iter->second.rxPackets);
            NS_LOG_INFO("  Débit: " << throughput << " Mbps");
            NS_LOG_INFO("  Latence moyenne: " << avgDelay << " ms");
            NS_LOG_INFO("  Perte: " << iter->second.lostPackets << " paquets");
            NS_LOG_INFO("");
        }
    }

    if (flowCount > 0)
    {
        NS_LOG_INFO("═══ RÉSUMÉ GLOBAL ═══");
        NS_LOG_INFO("  Débit total: " << totalThroughput << " Mbps");
        NS_LOG_INFO("  Débit moyen: " << totalThroughput / flowCount << " Mbps");
        NS_LOG_INFO("  Latence moyenne: " << totalDelay / flowCount << " ms");
    }

    Simulator::Destroy();

    // ========== MESSAGES FINAUX ==========
    std::cout << "\n";
    std::cout << "╔════════════════════════════════════════════════════════╗\n";
    std::cout << "║          ✅ SIMULATION TERMINÉE AVEC SUCCÈS            ║\n";
    std::cout << "╚════════════════════════════════════════════════════════╝\n";
    std::cout << "\n";
    std::cout << "📂 Fichiers générés:\n";
    std::cout << "  • cloudmedi-animation.xml  (NetAnim)\n";
    std::cout << "  • cloudmedi-wan-*.pcap     (Wireshark)\n";
    std::cout << "  • cloudmedi-lan-*.pcap     (Wireshark)\n";
    std::cout << "\n";
    std::cout << "📊 Visualisation:\n";
    std::cout << "  NetAnim:   netanim cloudmedi-animation.xml\n";
    std::cout << "  Wireshark: wireshark cloudmedi-wan-0-0.pcap\n";
    std::cout << "\n";
    std::cout << "🔄 Relancer avec:\n";
    std::cout << "  ./ns3 run 'cloudmedi-distributed --topology=mesh'\n";
    std::cout << "  ./ns3 run 'cloudmedi-distributed --topology=star'\n";
    std::cout << "\n";

    return 0;
}