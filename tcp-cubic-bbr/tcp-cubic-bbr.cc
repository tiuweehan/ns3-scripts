/*
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * 
 * This code heavily borrows from ns3 itself which are copyright of their
 * respective authors and redistributable under the same conditions.
 *
 */

#include <string>
#include <fstream>
#include <cstdlib>
#include <map>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/internet-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/gnuplot.h"

typedef uint32_t uint;

using namespace ns3;

#define TCP_BBR "TcpBbr"
#define TCP_CUBIC "TcpCubic"

#define ERROR 0.000001

NS_LOG_COMPONENT_DEFINE ("TcpBbrCubic");

class ClientApp: public Application {
	private:
		virtual void StartApplication(void);
		virtual void StopApplication(void);

		void ScheduleTx(void);
		void SendPacket(void);

		Ptr<Socket>     mSocket;
		Address         mPeer;
		uint32_t        mPacketSize;
		uint32_t        mNPackets;
		DataRate        mDataRate;
		EventId         mSendEvent;
		bool            mRunning;
		uint32_t        mPacketsSent;

	public:
		ClientApp();
		virtual ~ClientApp();

		void Setup(Ptr<Socket> socket, Address address, uint packetSize, uint nPackets, DataRate dataRate);

};

ClientApp::ClientApp(): mSocket(0),
		    mPeer(),
		    mPacketSize(0),
		    mNPackets(0),
		    mDataRate(0),
		    mSendEvent(),
		    mRunning(false),
		    mPacketsSent(0) {
}

ClientApp::~ClientApp() {
	mSocket = 0;
}

void ClientApp::Setup(Ptr<Socket> socket, Address address, uint packetSize, uint nPackets, DataRate dataRate) {
	mSocket = socket;
	mPeer = address;
	mPacketSize = packetSize;
	mNPackets = nPackets;
	mDataRate = dataRate;
}

void ClientApp::StartApplication() {
	mRunning = true;
	mPacketsSent = 0;
	mSocket->Bind();
	mSocket->Connect(mPeer);
	SendPacket();
}

void ClientApp::StopApplication() {
	mRunning = false;
	if(mSendEvent.IsRunning()) {
		Simulator::Cancel(mSendEvent);
	}
	if(mSocket) {
		mSocket->Close();
	}
}

void ClientApp::SendPacket() {
	Ptr<Packet> packet = Create<Packet>(mPacketSize);
	mSocket->Send(packet);

	// if(++mPacketsSent < mNPackets) {
	// 	ScheduleTx();
	// }

  mPacketsSent++;
  ScheduleTx();
}

void ClientApp::ScheduleTx() {
	if (mRunning) {
		Time tNext(Seconds(mPacketSize*8/static_cast<double>(mDataRate.GetBitRate())));
		mSendEvent = Simulator::Schedule(tNext, &ClientApp::SendPacket, this);
		//double tVal = Simulator::Now().GetSeconds();
		//if(tVal-int(tVal) >= 0.99)
		//	std::cout << Simulator::Now ().GetSeconds () << "\t" << mPacketsSent << std::endl;
	}
}

std::map<uint, uint64_t> mapPacketsReceivedIPV4;
std::map<uint, std::vector<Time>> mapRTT;  

void ReceivedPacket(std::string context, Ptr<const Packet> p, const Address& addr){
}

void ReceivedPacketIPV4(uint key, std::string context, Ptr<const Packet> p, Ptr<Ipv4> ipv4, uint interface) {
  mapPacketsReceivedIPV4[key]++;
}

void
RttTracer (uint key, std::string context, Time oldval, Time newval)
{
  mapRTT[key].push_back(newval);
}

Ptr<Socket> uniFlow(Address sinkAddress, 
					uint sinkPort, 
					std::string tcpVariant, 
					Ptr<Node> hostNode, 
					Ptr<Node> sinkNode, 
					double startTime, 
					double stopTime,
					uint packetSize,
					uint numPackets,
					std::string dataRate,
					double appStartTime,
					double appStopTime) {

	if(tcpVariant.compare(TCP_BBR) == 0) {
		Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpBbr::GetTypeId()));
	} else if(tcpVariant.compare(TCP_CUBIC) == 0) {
		Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpCubic::GetTypeId()));
	} else {
		fprintf(stderr, "Invalid TCP version\n");
		exit(EXIT_FAILURE);
	}
	PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
	ApplicationContainer sinkApps = packetSinkHelper.Install(sinkNode);
	sinkApps.Start(Seconds(startTime));
	sinkApps.Stop(Seconds(stopTime));

	Ptr<Socket> ns3TcpSocket = Socket::CreateSocket(hostNode, TcpSocketFactory::GetTypeId());
	

	Ptr<ClientApp> app = CreateObject<ClientApp>();
	app->Setup(ns3TcpSocket, sinkAddress, packetSize, numPackets, DataRate(dataRate));
	hostNode->AddApplication(app);
	app->SetStartTime(Seconds(appStartTime));
	app->SetStopTime(Seconds(appStopTime));

	return ns3TcpSocket;
}

int main(int argc, char **argv) {
  uint nBbr = 3;
  uint nCubic = 3;

	std::string rateHR = "100Mbps";
	std::string latencyHR = "20ms";
	std::string rateRR = "10Mbps";
	std::string latencyRR = "50ms";
	double errorP = ERROR;

	std::string transferSpeed = "400Mbps";

	double flowStart = 0;
	double durationGap = 120;

	uint port = 9000;
	uint numPackets = 10000000;

	uint packetSize = 1.2 * 1024;		//1.2KB
  uint queueSizeHRBytes = 20 * 100000;
  uint queueSizeRRBytes = 10000 * 50;

  CommandLine cmd;
  cmd.AddValue ("nBbr", "Number of BBR flows", nBbr);
  cmd.AddValue ("nCubic", "Number of Cubic", nCubic);

  cmd.AddValue ("rateHR", "Data rate between hosts and router", rateHR);
  cmd.AddValue ("latencyHR", "Latency between hosts and router", latencyHR);
  cmd.AddValue ("rateRR", "Data rate between routers", rateRR);
  cmd.AddValue ("latencyRR", "Latency between routers", latencyRR);
  cmd.AddValue ("queueSizeHR", "Size of queue between hosts and router (in bytes)", queueSizeHRBytes);
  cmd.AddValue ("queueSizeRR", "Size of queue between rouers (in bytes)", queueSizeRRBytes);
  cmd.AddValue ("errorRR", "Error rate of link between routers", errorP);

  cmd.AddValue("packetSize", "Size of packets sent (in bytes)", packetSize);
  cmd.AddValue ("duration", "Duration to run the simulation (in seconds)", durationGap);
  cmd.AddValue ("transferSpeed", "Application data rate", transferSpeed);

  cmd.Parse (argc, argv);
  
  if (nBbr == 0) nBbr = 1;
  if (nCubic == 0) nCubic = 1;
	uint numSender = nBbr + nCubic;

	uint queueSizeHR = queueSizeHRBytes / packetSize;
	uint queueSizeRR = queueSizeRRBytes / packetSize;

  NS_LOG_INFO(
    "Number of Flows:" << std::endl <<
    "   BBR Flows: " << nBbr << ", Cubic Flows: " << nCubic << ", Total Flows: " << numSender << std::endl <<
    "Host <-> Router:" << std::endl <<
    "   Data Rate: " << rateHR << ", Latency: " << latencyHR << ", Queue Size: " << queueSizeHR << " packets = " << queueSizeHR << " bytes" << std::endl <<
    "Router <-> Router:" << std::endl <<
    "   Data Rate: " << rateRR << ", Latency: " << latencyRR << ", Queue Size: " << queueSizeRR << " packets = " << queueSizeHR << " bytes, Error Rate: " << errorP << std::endl <<
    "Application:" << std::endl <<
    "   Packet Size: " << packetSize << " bytes, Send Rate: " << transferSpeed << ", Duration: " << durationGap << " seconds" << std::endl
  );

	Config::SetDefault("ns3::QueueBase::Mode", StringValue("QUEUE_MODE_PACKETS"));
    /*
    Config::SetDefault("ns3::DropTailQueue::MaxPackets", UintegerValue(queuesize));
	*/

	//Creating channel without IP address
  NS_LOG_INFO("Creating channel without IP address");
	PointToPointHelper p2pHR, p2pRR;
	p2pHR.SetDeviceAttribute("DataRate", StringValue(rateHR));
	p2pHR.SetChannelAttribute("Delay", StringValue(latencyHR));
	p2pHR.SetQueue("ns3::DropTailQueue", "MaxPackets", UintegerValue(queueSizeHR));
	p2pRR.SetDeviceAttribute("DataRate", StringValue(rateRR));
	p2pRR.SetChannelAttribute("Delay", StringValue(latencyRR));
	p2pRR.SetQueue("ns3::DropTailQueue", "MaxPackets", UintegerValue(queueSizeRR));

	//Adding some error rate
	NS_LOG_INFO("Adding some error rate");
	Ptr<RateErrorModel> em = CreateObjectWithAttributes<RateErrorModel> ("ErrorRate", DoubleValue (errorP));

	NodeContainer routers, senders, receivers;
	routers.Create(2);
	senders.Create(numSender);
	receivers.Create(numSender);

	NetDeviceContainer routerDevices = p2pRR.Install(routers);
	NetDeviceContainer leftRouterDevices, rightRouterDevices, senderDevices, receiverDevices;

	//Adding links
  NS_LOG_INFO("Adding links");
	for(uint i = 0; i < numSender; ++i) {
		NetDeviceContainer cleft = p2pHR.Install(routers.Get(0), senders.Get(i));
		leftRouterDevices.Add(cleft.Get(0));
		senderDevices.Add(cleft.Get(1));
		cleft.Get(0)->SetAttribute("ReceiveErrorModel", PointerValue(em));

		NetDeviceContainer cright = p2pHR.Install(routers.Get(1), receivers.Get(i));
		rightRouterDevices.Add(cright.Get(0));
		receiverDevices.Add(cright.Get(1));
		cright.Get(0)->SetAttribute("ReceiveErrorModel", PointerValue(em));
	}

	//Install Internet Stack
	NS_LOG_INFO("Install Internet Stack");
	InternetStackHelper stack;
	stack.Install(routers);
	stack.Install(senders);
	stack.Install(receivers);

	//Adding IP addresses
	NS_LOG_INFO("Adding IP addresses");
	Ipv4AddressHelper routerIP = Ipv4AddressHelper("10.3.0.0", "255.255.255.0");
	Ipv4AddressHelper senderIP = Ipv4AddressHelper("10.1.0.0", "255.255.255.0");
	Ipv4AddressHelper receiverIP = Ipv4AddressHelper("10.2.0.0", "255.255.255.0");

	Ipv4InterfaceContainer routerIFC, senderIFCs, receiverIFCs, leftRouterIFCs, rightRouterIFCs;

	routerIFC = routerIP.Assign(routerDevices);

	for(uint i = 0; i < numSender; ++i) {
		NetDeviceContainer senderDevice;
		senderDevice.Add(senderDevices.Get(i));
		senderDevice.Add(leftRouterDevices.Get(i));
		Ipv4InterfaceContainer senderIFC = senderIP.Assign(senderDevice);
		senderIFCs.Add(senderIFC.Get(0));
		leftRouterIFCs.Add(senderIFC.Get(1));
		senderIP.NewNetwork();

		NetDeviceContainer receiverDevice;
		receiverDevice.Add(receiverDevices.Get(i));
		receiverDevice.Add(rightRouterDevices.Get(i));
		Ipv4InterfaceContainer receiverIFC = receiverIP.Assign(receiverDevice);
		receiverIFCs.Add(receiverIFC.Get(0));
		rightRouterIFCs.Add(receiverIFC.Get(1));
		receiverIP.NewNetwork();
	}

	//TCP BBR
  NS_LOG_INFO("Creating TCP BBR Sockets");
  for (uint i = 0; i < numSender; i++) {
    std::string tcpProtocol = (i < nBbr ? TCP_BBR : TCP_CUBIC);

    uniFlow(InetSocketAddress(receiverIFCs.GetAddress(i), port), port, tcpProtocol, senders.Get(i), receivers.Get(i), flowStart, flowStart+durationGap, packetSize, numPackets, transferSpeed, flowStart, flowStart+durationGap);

    std::stringstream sink;
    std::stringstream sink_;
    sink << "/NodeList/" << receivers.Get(i)->GetId() << "/ApplicationList/0/$ns3::PacketSink/Rx";
    Config::Connect(sink.str(), MakeCallback(&ReceivedPacket));
    sink_ << "/NodeList/" << receivers.Get(i)->GetId() << "/$ns3::Ipv4L3Protocol/Rx";
    Config::Connect(sink_.str(), MakeBoundCallback(&ReceivedPacketIPV4, i));

    std::stringstream rttSink;
    rttSink << "/NodeList/" << senders.Get(i)->GetId() << "/$ns3::TcpL4Protocol/SocketList/0/RTT";
    Config::Connect(rttSink.str(), MakeBoundCallback(&RttTracer, i));
  }

	//Turning on Static Global Routing
	NS_LOG_INFO("Turning on Static Global Routing");
	Ipv4GlobalRoutingHelper::PopulateRoutingTables();

	NS_LOG_INFO("Simulation started");
	Simulator::Stop(Seconds(durationGap+flowStart));
	Simulator::Run();
 
  for (uint i = 0; i < numSender; i++) {
    NS_LOG_INFO((i < nBbr ? "BBR" : "Cubic") << " #" << (i < nBbr ? i + 1: i % nBbr + 1)); 
    
    double totalBytes = static_cast<double>(mapPacketsReceivedIPV4[i] * packetSize);
    NS_LOG_INFO("Total Bytes Sent: " << totalBytes);
    
    // Throughput
    double totalBits = static_cast<double>(totalBytes * 8);
    double throughput = totalBits / durationGap;
    throughput /= (1024 * 1024); // Convert to Mbps
    NS_LOG_INFO("Throughput: " << throughput << "Mbps");

    // Delay    
    double delay = 0.0;
    for (Time t : mapRTT[i]) {
      delay += t.GetNanoSeconds();
    }
    delay /= mapRTT[i].size();
    delay /= 1e6; // Convert to miliseconds
    NS_LOG_INFO("Delay: " << delay << "ms");
  }

	NS_LOG_INFO("Simulation finished");
	Simulator::Destroy();
}
