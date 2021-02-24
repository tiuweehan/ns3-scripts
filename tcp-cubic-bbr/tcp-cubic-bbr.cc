#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/point-to-point-dumbbell.h"
#include "ns3/applications-module.h"

#include "ns3/flow-monitor-module.h"

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

#define ERROR 0.000001

NS_LOG_COMPONENT_DEFINE ("TCPCubicBBRFlowExperiment");

class ClientApp: public Application {
  public:
    ClientApp();
    virtual ~ClientApp();

    static TypeId GetTypeId (void);
    void Setup(Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate);
  
  private:
    virtual void StartApplication(void);
    virtual void StopApplication(void);

    void ScheduleTx(void);
    void SendPacket(void);

    Ptr<Socket> mSocket;
    Address mPeer;
    uint32_t mPacketSize;
    uint32_t mNPackets;
    DataRate mDataRate;
    EventId mSendEvent;
    bool mRunning;
    uint32_t mPacketsSent;

};

ClientApp::ClientApp(void):
  mSocket(0),
  mPacketSize(0),
  mNPackets(0),
  mDataRate(0),
  mSendEvent(),
  mRunning(false),
  mPacketsSent(0) {
}

ClientApp::~ClientApp(void) {
  mSocket = 0;
}

TypeId ClientApp::GetTypeId (void) {
  static TypeId tid = TypeId ("ClientApp")
    .SetParent<Application> ()
    .AddConstructor<ClientApp> ()
    ;
  return tid;
}

void ClientApp::Setup(Ptr<Socket> socket, Address address, uint32_t packetSize, uint32_t nPackets, DataRate dataRate) {
  mSocket = socket;
  mPeer = address;
  mPacketSize = packetSize;
  mNPackets = nPackets;
  mDataRate = dataRate;
}

void ClientApp::StartApplication(void) {
  mRunning = true;
  mPacketsSent = 0;
  mSocket->Bind();
  mSocket->Connect(mPeer);
  SendPacket();
}

void ClientApp::StopApplication(void) {
  mRunning = false;
  
  if (mSendEvent.IsRunning()) {
    Simulator::Cancel(mSendEvent);
  }

  if (mSocket) {
    mSocket->Close();
  }
}

void ClientApp::SendPacket(void) {
  // cout << "Time: " << Simulator::Now().GetSeconds() << "s, packet #" << mPacketsSent << '\n';
  Ptr<Packet> packet = Create<Packet>(mPacketSize);
  mSocket->Send(packet);

  // if (++mPacketsSent < mNPackets) {
  ++mPacketsSent;
  ScheduleTx();
  // }
}

void ClientApp::ScheduleTx(void) {
  if (mRunning) {
    Time tNext(Seconds(mPacketSize * 8 / static_cast<double>(mDataRate.GetBitRate())));
    mSendEvent = Simulator::Schedule(tNext, &ClientApp::SendPacket, this);
  }
}

map<string, uint64_t> m;

void ReceivedPacketIpv4(string context, Ptr<const Packet> p, Ptr< Ipv4 > ipv4, uint32_t interface) {
  m[context]++;  
}

// Creates a TCP socket and the corresponding application on the client and server.
Ptr<Socket> uniFlow(
  Address serverAddress,
  uint32_t serverPort,
  string tcpVariant,
  Ptr<Node> clientNode,
  Ptr<Node> serverNode,
  double serverAppStartTime,
  double serverAppEndTime,
  uint32_t packetSize,
  uint32_t numPackets,
  string dataRate,
  double clientAppStartTime,
  double clientAppStopTime
) {
  // Configure type of socket
  if (tcpVariant.compare("TcpBbr") == 0) {
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpBbr::GetTypeId()));
  } else if (tcpVariant.compare("TcpCubic") == 0) {
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpCubic::GetTypeId()));
  } else {
    fprintf(stderr, "Invalid TCP version\n");
		exit(EXIT_FAILURE);
  }

  // Create server app (on the server side)
  PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), serverPort));
  ApplicationContainer serverApp = packetSinkHelper.Install(serverNode);
  serverApp.Start(Seconds(serverAppStartTime)); 
  serverApp.Start(Seconds(serverAppEndTime)); 
  
  // Create TCP socket used by the client app
  Ptr<Socket> ns3TcpSocket = Socket::CreateSocket(clientNode, TcpSocketFactory::GetTypeId());

  // Create client app (on the client side)
  Ptr<ClientApp> clientApp = CreateObject<ClientApp>();
  clientApp->Setup(ns3TcpSocket, serverAddress, packetSize, numPackets, DataRate(dataRate));
  clientNode->AddApplication(clientApp);
  clientApp->SetStartTime(Seconds(clientAppStartTime));
  clientApp->SetStopTime(Seconds(clientAppStopTime));

  return ns3TcpSocket;
}

int main(int argc, char** argv) {
  // bool verbose = false;
  bool tracing = false;
  bool flowMonitoring = true;

  uint32_t nBBR = 3;
  uint32_t nCubic = 3;

  // Link Details
  string pointToPointBandwidth = "100Mbps";
  string pointToPointDelay = "20ms";

  string bottleNeckBandwith = "10Mbps";
  string bottleNeckDelay = "50ms";

  // Application Details
  double serverAppStartTime = 1;
  double clientAppStartTime = 2;
  double AppRunningDuration = 120; // 2 minutes

  double simulationStopTime = clientAppStartTime + AppRunningDuration;
  double serverAppStopTime = simulationStopTime;
  double clientAppStopTime = simulationStopTime;
  uint32_t packetSize = 1024; // 1KB
  uint32_t nPackets = INT32_MAX; // 20 * 1Mb packets = 20Mb
  string applicationTransferSpeed = "400Mbps"; 
 
  CommandLine cmd;

  cmd.AddValue("nBBR", "Number of Clients", nBBR);
  cmd.AddValue("nCubic", "Number of Clients", nCubic);

  uint32_t nClients = nBBR + nCubic;

  // Queue
  uint queueSizeHR = (100000*20)/packetSize;
	uint queueSizeRR = (10000*50)/packetSize;
  Config::SetDefault("ns3::QueueBase::Mode", StringValue("QUEUE_MODE_PACKETS"));

  // Error
  double errorP = ERROR;
  Ptr<RateErrorModel> em = CreateObjectWithAttributes<RateErrorModel> ("ErrorRate", DoubleValue (errorP));

  // Create links 
  PointToPointHelper pointToPointLink;
  pointToPointLink.SetDeviceAttribute("DataRate", StringValue(pointToPointBandwidth));
  pointToPointLink.SetChannelAttribute("Delay",StringValue(pointToPointDelay));
	pointToPointLink.SetQueue("ns3::DropTailQueue", "MaxPackets", UintegerValue(queueSizeHR));

  PointToPointHelper bottleNeckLink;
  bottleNeckLink.SetDeviceAttribute("DataRate", StringValue(bottleNeckBandwith));
  bottleNeckLink.SetChannelAttribute("Delay",StringValue(bottleNeckDelay));
	bottleNeckLink.SetQueue("ns3::DropTailQueue", "MaxPackets", UintegerValue(queueSizeRR));

  // Create nodes
  NodeContainer routers, clientNodes, serverNodes;
  routers.Create(2);
  clientNodes.Create(nClients);
  serverNodes.Create(nClients);

  // Install links between routers
  NetDeviceContainer routerDevices = bottleNeckLink.Install(routers);

  // Install links onto nodes
  NetDeviceContainer clientRouterDevices;
  NetDeviceContainer clientNodeDevices;
  NetDeviceContainer serverRouterDevices;
  NetDeviceContainer serverNodeDevices;

  for (uint32_t i = 0; i < nClients; i++) {
    // Install the links between the client nodes and client router
    NetDeviceContainer clientDevice = pointToPointLink.Install(routers.Get(0), clientNodes.Get(i));
    clientRouterDevices.Add(clientDevice.Get(0));
    clientNodeDevices.Add(clientDevice.Get(1));
    clientDevice.Get(0)->SetAttribute("ReceiveErrorModel", PointerValue(em));

    // Install the links between the server nodes and server router
    NetDeviceContainer serverDevice = pointToPointLink.Install(routers.Get(1), serverNodes.Get(i));
    serverRouterDevices.Add(serverDevice.Get(0));
    serverNodeDevices.Add(serverDevice.Get(1));
    serverDevice.Get(0)->SetAttribute("ReceiveErrorModel", PointerValue(em));
  }

  // Install internet stack onto all nodes
  InternetStackHelper stack;
  stack.Install(routers);
  stack.Install(clientNodes);
  stack.Install(serverNodes);

  // Create IPv4 addresses
  Ipv4AddressHelper routerIPs = Ipv4AddressHelper("10.3.0.0", "255.255.255.0");
  Ipv4AddressHelper clientIPs = Ipv4AddressHelper("10.1.0.0", "255.255.255.0");
  Ipv4AddressHelper serverIPs = Ipv4AddressHelper("10.2.0.0", "255.255.255.0");

  // Assign IPv4 addresses to connection between router devices
  Ipv4InterfaceContainer routerInterfaces = routerIPs.Assign(routerDevices);
  
  // Assign IPv4 address to connection between node and router devices
  Ipv4InterfaceContainer clientNodeInterfaces;
  Ipv4InterfaceContainer clientRouterInterfaces;
  Ipv4InterfaceContainer serverNodeInterfaces;
  Ipv4InterfaceContainer serverRouterInterfaces;

  for (uint32_t i = 0; i < nClients; i++) {
    // Assign IPv4 address to connection between client nodes and router devices
    NetDeviceContainer clientDevices;
    clientDevices.Add(clientNodeDevices.Get(i));
    clientDevices.Add(clientRouterDevices.Get(i));
    Ipv4InterfaceContainer clientInterfaces = clientIPs.Assign(clientDevices);
    clientNodeInterfaces.Add(clientInterfaces.Get(0));
    clientRouterInterfaces.Add(clientInterfaces.Get(1));
    clientIPs.NewNetwork();

    // Assign IPv4 address to connection between server nodes and router devices
    NetDeviceContainer serverDevices;
    serverDevices.Add(serverNodeDevices.Get(i));
    serverDevices.Add(serverRouterDevices.Get(i));
    Ipv4InterfaceContainer serverInterfaces = serverIPs.Assign(serverDevices);
    serverNodeInterfaces.Add(serverInterfaces.Get(0));
    serverRouterInterfaces.Add(serverInterfaces.Get(1));
    serverIPs.NewNetwork();
  }

  // Server Application port
  int port = 42069;

  // TCP Cubic Flows
  for (uint32_t i = 0; i < nCubic; i++) {
    uint32_t nodeIndex = i;
    Ptr<Node> clientNode = clientNodes.Get(nodeIndex);
    Ptr<Node> serverNode = serverNodes.Get(nodeIndex);
 
    uniFlow(
      InetSocketAddress(serverNodeInterfaces.GetAddress(nodeIndex), port),
      port,
      "TcpCubic",
      clientNode,
      serverNode,
      serverAppStartTime,
      serverAppStopTime,
      packetSize,
      nPackets,
      applicationTransferSpeed,
      clientAppStartTime,
      clientAppStopTime
    );

    stringstream sink;
    sink << "/NodeList/" << serverNode->GetId() << "/$ns3::Ipv4L3Protocol/Rx";
    Config::Connect(sink.str(), MakeCallback(&ReceivedPacketIpv4));
  }

  // TCP BBR Flows
  for (uint32_t i = 0; i < nBBR; i++) {
    uint32_t nodeIndex = i + nCubic;
    Ptr<Node> clientNode = clientNodes.Get(nodeIndex);
    Ptr<Node> serverNode = serverNodes.Get(nodeIndex);
    uniFlow(
      InetSocketAddress(serverNodeInterfaces.GetAddress(nodeIndex), port),
      port,
      "TcpBbr",
      clientNode,
      serverNode,
      serverAppStartTime,
      serverAppStopTime,
      packetSize,
      nPackets,
      applicationTransferSpeed,
      clientAppStartTime,
      clientAppStopTime
    );

    stringstream sink;
    sink << "/NodeList/" << serverNode->GetId() << "/$ns3::Ipv4L3Protocol/Rx";
    Config::Connect(sink.str(), MakeCallback(&ReceivedPacketIpv4));
  }

  // Use Static Global Routing
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  // Add Pcap tracing
  if (tracing == true) {
    bottleNeckLink.EnablePcapAll("bottleneck");
  }

  // Add Flow Monitor
  if (flowMonitoring == true) {
    Ptr<FlowMonitor> flowMonitor;
    FlowMonitorHelper flowMonitorHelper;
    flowMonitor = flowMonitorHelper.InstallAll();

    // Run Simulation
    Simulator::Stop (Seconds(simulationStopTime));
    Simulator::Run ();

    flowMonitor->CheckForLostPackets();
  } else {
    // Run Simulation
    Simulator::Stop (Seconds(simulationStopTime));
    Simulator::Run ();
  }

  // Output throughput
  for (const pair<string, uint64_t>& k : m) {
    cout << k.first << ": " << k.second << '\n';
  }

  Simulator::Destroy ();

  return 0;
}
