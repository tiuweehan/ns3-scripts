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

#define ERROR 0.000001

NS_LOG_COMPONENT_DEFINE ("App6");

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

	if(tcpVariant.compare("TcpBbr") == 0) {
		Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpBbr::GetTypeId()));
	} else if(tcpVariant.compare("TcpCubic") == 0) {
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
	std::string rateHR = "100Mbps";
	std::string latencyHR = "20ms";
	std::string rateRR = "10Mbps";
	std::string latencyRR = "50ms";

	uint packetSize = 1.2*1024;		//1.2KB
	uint queueSizeHR = (100000*20)/packetSize;
	uint queueSizeRR = (10000*50)/packetSize;

  uint nBbr = 3;
  uint nCubic = 3;
	uint numSender = nBbr + nCubic;

	double errorP = ERROR;


	Config::SetDefault("ns3::QueueBase::Mode", StringValue("QUEUE_MODE_PACKETS"));
    /*
    Config::SetDefault("ns3::DropTailQueue::MaxPackets", UintegerValue(queuesize));
	*/

	//Creating channel without IP address
	std::cout << "Creating channel without IP address" << std::endl;
	PointToPointHelper p2pHR, p2pRR;
	p2pHR.SetDeviceAttribute("DataRate", StringValue(rateHR));
	p2pHR.SetChannelAttribute("Delay", StringValue(latencyHR));
	p2pHR.SetQueue("ns3::DropTailQueue", "MaxPackets", UintegerValue(queueSizeHR));
	p2pRR.SetDeviceAttribute("DataRate", StringValue(rateRR));
	p2pRR.SetChannelAttribute("Delay", StringValue(latencyRR));
	p2pRR.SetQueue("ns3::DropTailQueue", "MaxPackets", UintegerValue(queueSizeRR));

	//Adding some error rate
	std::cout << "Adding some error rate" << std::endl;
	Ptr<RateErrorModel> em = CreateObjectWithAttributes<RateErrorModel> ("ErrorRate", DoubleValue (errorP));

	NodeContainer routers, senders, receivers;
	routers.Create(2);
	senders.Create(numSender);
	receivers.Create(numSender);

	NetDeviceContainer routerDevices = p2pRR.Install(routers);
	NetDeviceContainer leftRouterDevices, rightRouterDevices, senderDevices, receiverDevices;

	//Adding links
	std::cout << "Adding links" << std::endl;
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
	std::cout << "Install Internet Stack" << std::endl;
	InternetStackHelper stack;
	stack.Install(routers);
	stack.Install(senders);
	stack.Install(receivers);


	//Adding IP addresses
	std::cout << "Adding IP addresses" << std::endl;
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

	double durationGap = 120;
	double flowStart = 0;
	uint port = 9000;
	uint numPackets = 10000000;
	std::string transferSpeed = "400Mbps";
	
	//TCP BBR
  std::cout << "Creating TCP BBR Sockets" << std::endl;
  for (uint i = 0; i < nBbr; i++) {
    uniFlow(InetSocketAddress(receiverIFCs.GetAddress(i), port), port, "TcpBbr", senders.Get(i), receivers.Get(i), flowStart, flowStart+durationGap, packetSize, numPackets, transferSpeed, flowStart, flowStart+durationGap);

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

	//TCP CUBIC
  std::cout << "Creating TCP CUBIC Sockets" << std::endl;
  for (uint i = nBbr; i < nBbr + nCubic; i++) {
    uniFlow(InetSocketAddress(receiverIFCs.GetAddress(i), port), port, "TcpCubic", senders.Get(i), receivers.Get(i), flowStart, flowStart+durationGap, packetSize, numPackets, transferSpeed, flowStart, flowStart+durationGap);

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

	//p2pHR.EnablePcapAll("application_6_HR_a");
	//p2pRR.EnablePcapAll("application_6_RR_a");

	//Turning on Static Global Routing
	std::cout << "Turning on Static Global Routing" << std::endl;
	Ipv4GlobalRoutingHelper::PopulateRoutingTables();

	Simulator::Stop(Seconds(durationGap+flowStart));
	Simulator::Run();
 
  for (uint i = 0; i < numSender; i++) {
    std::cout << (i < nBbr ? "BBR" : "Cubic") << " #" << (i < nBbr ? i + 1: i % nBbr + 1) << std::endl; 
    
    // Throughput
    double totalBits = static_cast<double>(mapPacketsReceivedIPV4[i] * packetSize * 8);
    double throughput = totalBits / durationGap;
    throughput /= (1024 * 1024); // Convert to Mbps
    std::cout <<  "Throughput: " << throughput << "Mbps"<< std::endl;

    // Delay    
    double delay = 0.0;
    for (Time t : mapRTT[i]) {
      delay += t.GetNanoSeconds();
    }
    delay /= mapRTT[i].size();
    delay /= 1e6; // Convert to miliseconds
    std::cout <<  "Delay: " << delay << "ms" << std::endl;
  }

	std::cout << "Simulation finished" << std::endl;
	Simulator::Destroy();
}