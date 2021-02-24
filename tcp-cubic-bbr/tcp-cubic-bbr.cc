#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/point-to-point-dumbbell.h"
#include "ns3/applications-module.h"

// Default Network Topology
//
//  H1 -----.                .----- H7
//          |                |
//  H2 ----.|                |.---- H8
//         ||                ||
//  H3 ---.||                ||.--- H9
//         R0 -------------- R1
//  H4 ---'|| point-to-point ||'--- H10
//         ||                ||
//  H5 ----'|                |'---- H11
//          |                |
//  H6 -----'                '----- H12
//

using namespace std;
using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("TCPCubicBBRFlowExperiment");

int main(int argc, char** argv) {
  // bool verbose = false;
  // bool tracing = false;

  uint32_t nBBR = 3;
  uint32_t nCubic = 3;

  string pointToPointBandwidth = "10Mbps";
  string pointToPointDelay = "1ms";

  string bottleNeckBandwith = "1Mbps";
  string bottleNeckDelay = "50ms";

  // int port = 42069;

  CommandLine cmd;

  cmd.AddValue("nBBR", "Number of Clients", nBBR);
  cmd.AddValue("nCubic", "Number of Clients", nCubic);

  uint32_t nClients = nBBR + nCubic;

  // Create links 
  PointToPointHelper pointToPointLink;
  pointToPointLink.SetDeviceAttribute("DataRate", StringValue(pointToPointBandwidth));
  pointToPointLink.SetChannelAttribute("Delay",StringValue(pointToPointDelay));

  PointToPointHelper bottleNeckLink;
  bottleNeckLink.SetDeviceAttribute("DataRate", StringValue(bottleNeckBandwith));
  bottleNeckLink.SetChannelAttribute("Delay",StringValue(bottleNeckDelay));

  // Create nodes
  NodeContainer routers, leftNodes, rightNodes;
  routers.Create(2);
  leftNodes.Create(nClients);
  rightNodes.Create(nClients);

  // Install links between routers
  NetDeviceContainer routerDevices = bottleNeckLink.Install(routers);

  // Install links onto nodes
  NetDeviceContainer leftRouterDevices;
  NetDeviceContainer leftNodeDevices;
  NetDeviceContainer rightRouterDevices;
  NetDeviceContainer rightNodeDevices;

  for (uint32_t i = 0; i < nClients; i++) {
    // Install the links between the left nodes and left router
    NetDeviceContainer leftDevice = pointToPointLink.Install(routers.Get(0), leftNodes.Get(i));
    leftRouterDevices.Add(leftDevice.Get(0));
    leftNodeDevices.Add(leftDevice.Get(1));

    // Install the links between the right nodes and right router
    NetDeviceContainer rightDevice = pointToPointLink.Install(routers.Get(0), rightNodes.Get(i));
    rightRouterDevices.Add(rightDevice.Get(0));
    rightNodeDevices.Add(rightDevice.Get(1));
  }

  // Install internet stack onto all nodes
  InternetStackHelper stack;
  stack.Install(routers);
  stack.Install(leftNodes);
  stack.Install(rightNodes);

  // Create IPv4 addresses
  Ipv4AddressHelper routerIPs = Ipv4AddressHelper("10.3.0.0", "255.255.255.0");
  Ipv4AddressHelper leftIPs = Ipv4AddressHelper("10.1.0.0", "255.255.255.0");
  Ipv4AddressHelper rightIPs = Ipv4AddressHelper("10.2.0.0", "255.255.255.0");

  // Assign IPv4 addresses to connection between router devices
  Ipv4InterfaceContainer routerInterfaces = routerIPs.Assign(routerDevices);
  
  // Assign IPv4 address to connection between node and router devices
  Ipv4InterfaceContainer leftNodeInterfaces;
  Ipv4InterfaceContainer leftRouterInterfaces;
  Ipv4InterfaceContainer rightNodeInterfaces;
  Ipv4InterfaceContainer rightRouterInterfaces;

  for (uint32_t i = 0; i < nClients; i++) {
    // Assign IPv4 address to connection between left nodes and router devices
    NetDeviceContainer leftDevices;
    leftDevices.Add(leftNodeDevices.Get(i));
    leftDevices.Add(leftRouterDevices.Get(i));
    Ipv4InterfaceContainer leftInterfaces = leftIPs.Assign(leftDevices);
    leftNodeInterfaces.Add(leftInterfaces.Get(0));
    leftRouterInterfaces.Add(leftInterfaces.Get(1));
    leftIPs.NewNetwork();

    // Assign IPv4 address to connection between right nodes and router devices
    NetDeviceContainer rightDevices;
    rightDevices.Add(rightNodeDevices.Get(i));
    rightDevices.Add(rightRouterDevices.Get(i));
    Ipv4InterfaceContainer rightInterfaces = rightIPs.Assign(rightDevices);
    rightNodeInterfaces.Add(rightInterfaces.Get(0));
    rightRouterInterfaces.Add(rightInterfaces.Get(1));
    rightIPs.NewNetwork();
  }
}
