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

#include <bits/stdc++.h>
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/internet-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/gnuplot.h"

typedef uint32_t uint;

using namespace std;
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

std::vector<uint64_t> mapPacketsReceivedIPV4;
std::vector<std::vector<Time>> mapRTT;  

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

int start(int argc, char **argv) {
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

  cmd.AddValue("packetSize", "Size of packets sent (in bytes)", packetSize);
  cmd.AddValue ("duration", "Duration to run the simulation (in seconds)", durationGap);
  cmd.AddValue ("transferSpeed", "Application data rate", transferSpeed);

  cmd.AddValue ("rateHR", "Data rate between hosts and router", rateHR);
  cmd.AddValue ("latencyHR", "Latency between hosts and router", latencyHR);
  cmd.AddValue ("rateRR", "Data rate between routers", rateRR);
  cmd.AddValue ("latencyRR", "Latency between routers", latencyRR);
  cmd.AddValue ("queueSizeHR", "Size of queue between hosts and router (in bytes)", queueSizeHRBytes);
  cmd.AddValue ("queueSizeRR", "Size of queue between rouers (in bytes)", queueSizeRRBytes);
  cmd.AddValue ("errorRR", "Error rate of link between routers", errorP);

  cmd.Parse (argc, argv);
  
  if (nBbr == 0) nBbr = 1;
  if (nCubic == 0) nCubic = 1;
	uint numSender = nBbr + nCubic;
  mapPacketsReceivedIPV4.resize(numSender);
  mapRTT.resize(numSender);

	uint queueSizeHR = queueSizeHRBytes / packetSize;
	uint queueSizeRR = queueSizeRRBytes / packetSize;

  std::cout <<
    "Number of Flows:" << std::endl <<
    "   BBR Flows: " << nBbr << " | Cubic Flows: " << nCubic << " | Total Flows: " << numSender << std::endl <<
    "Application:" << std::endl <<
    "   Packet Size: " << packetSize << " bytes | Send Rate: " << transferSpeed << " | Duration: " << durationGap << " seconds" << std::endl <<
    "Host <-> Router:" << std::endl <<
    "   Data Rate: " << rateHR << " | Latency: " << latencyHR << " | Queue Size: " << queueSizeHR << " packets (" << queueSizeHRBytes << " bytes)" << std::endl <<
    "Router <-> Router:" << std::endl <<
    "   Data Rate: " << rateRR << " | Latency: " << latencyRR << " | Queue Size: " << queueSizeRR << " packets (" << queueSizeRRBytes << " bytes) | Error Rate: " << errorP << std::endl;

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
	std::cout << "Install Internet Stack" << std::endl;;
	InternetStackHelper stack;
	stack.Install(routers);
	stack.Install(senders);
	stack.Install(receivers);

	//Adding IP addresses
	std::cout << "Adding IP addresses" << std::endl;;
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
  std::cout << "Creating TCP BBR Sockets" << std::endl;
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
	std::cout << "Turning on Static Global Routing" << std::endl;
	Ipv4GlobalRoutingHelper::PopulateRoutingTables();

	std::cout << "Simulation started" << std::endl;
	Simulator::Stop(Seconds(durationGap+flowStart));
	Simulator::Run();
 
  for (uint i = 0; i < numSender; i++) {
    std::cout << (i < nBbr ? "BBR" : "Cubic") << " #" << (i < nBbr ? i + 1: i % nBbr + 1) << std::endl; 
    
    double totalBytes = static_cast<double>(mapPacketsReceivedIPV4[i] * packetSize);
    std::cout << "Total Bytes Sent: " << totalBytes << std::endl;;
    
    // Throughput
    double totalBits = static_cast<double>(totalBytes * 8);
    double throughput = totalBits / durationGap;
    throughput /= (1024 * 1024); // Convert to Mbps
    std::cout << "Throughput: " << throughput << "Mbps" << std::endl;

    // Delay    
    double delay = 0.0;
    for (Time t : mapRTT[i]) {
      delay += t.GetNanoSeconds();
    }
    delay /= mapRTT[i].size();
    delay /= 1e6; // Convert to miliseconds
    std::cout << "Delay: " << delay << "ms" << std::endl;
  }

	std::cout << "Simulation finished" << std::endl;
	Simulator::Destroy();
}

typedef map<string, vector<int>> BitPattern;

class ParetoConf {
  public:
        std::string RESULT_FOLDER_PATH = "results-2dumps/";
        std::string PCAPS_FOLDER_PATH = "pcaps-2dumps/" ;
        std::string HYDRA = "10.0.0.1";
        std::string NEMO = "10.0.0.2";
        std::string NEMO_INTERFACE = "enp2s0";
        std::string GALACTICA = "5.5.5.2";
        std::string CAPRICA = "5.5.5.1";
        std::string GALACTICA_INTERFACE = "enp7s4";
        std::string CAPRICA_INTERFACE = "eth1";
        std::string RECEIVER_IP = CAPRICA;
        std::string RECEIVER_BASE_PORT = "10086"  ;
        std::string RECEIVER_SCRIPT = "./start_test.sh";
        std::string ISOLATED_RECEIVER_SCRIPT = "./start_isolated_test.sh";
        std::string RECEIVER_INTERFACE = CAPRICA_INTERFACE;
        std::string SENDER_IP = GALACTICA;
        std::string CLEANUP_PCAPS_SCRIPT = "cleanup_pcaps_and_csvs.py";
        std::string CLEANUP_PCAPS_SCRIPT_PATH = CLEANUP_PCAPS_SCRIPT;
        std::string SENDER_SCRIPT = "spawn_senders.sh";
        std::string SENDER_SCRIPT_PATH = SENDER_SCRIPT;
        std::string TC_CONFIG_SCRIPT = "config.sh";
        std::string TC_CONFIG_SCRIPT_PATH = TC_CONFIG_SCRIPT;
        std::string PCAP2CSV_SCRIPT = "pcap2csv.sh";
        std::string PCAP2CSV_SCRIPT_PATH = PCAP2CSV_SCRIPT;
        std::vector<std::string> PROCESS_CSV_SCRIPTS  { "plot_unfairness.py", "get_sending_rates.py" };
        std::vector<std::string> PROCESS_CSV_SCRIPT_PATHS = PROCESS_CSV_SCRIPTS;
        int BUFF_SIZE = 1  ;
        int NUM_TOTAL_FLOWS = 10;
        int BANDWIDTH = 50  ;
        std::vector<int> RTTS { 20, 50, 100 };
        int DURATION = 60  ;
        double WINDOW_TIME_LENGTH = 0.5  ;
        int MOVING_STRIDE = 1  ;
        std::string FIGURE_FILETYPE = "png";
        std::string UNFAIRNESS_FIGURE_TITLE = "BBR\"s Throughput Unfairness Ratio vs. Share of BBR on Various Bandwidths";
        std::string CSV_FOLDER_PATH = PCAPS_FOLDER_PATH;
        int FIGURE_DPI = 100;

        vector<string> BIT_PATTERNS;
        vector<BitPattern> RUNNING_ALGO_RTTS;
};

set<vector<char>> combination_with_repetition(string s, int p) {
  if (s == "") {
    set<vector<char>> res;
    return res;
  }

  if (p == 0) {
    set<vector<char>> res { {} };
    return res;
  }

  // All combinations without the first element
  set<vector<char>> res1 = combination_with_repetition(s.substr(1), p);
  
  set<vector<char>> temp = combination_with_repetition(s, p - 1);
  set<vector<char>> res2;
  for (vector<char> v : temp) {
    v.insert(v.begin(), s[0]);
    res2.insert(v);
  }

  res1.insert(res2.begin(), res2.end());
  return res1;
}

vector<string> get_bit_patterns(vector<int> partition) {
  vector<set<vector<char>>> all_bit_sets;

  for (uint i = 0; i < partition.size(); i++) {
    int p = partition[i];
    set<vector<char>> bit_set = combination_with_repetition("BC", p);
    all_bit_sets.push_back(bit_set);
  }

  vector<string> bit_patterns;
  for (auto i : all_bit_sets[0]) {
    for (auto j : all_bit_sets[1]) {
      for (auto k : all_bit_sets[2]) {
        stringstream ss;
        for (char c : i) ss << c;
        for (char c : j) ss << c;
        for (char c : k) ss << c;
        bit_patterns.push_back(ss.str());
      }
    }
  }
  return bit_patterns;
}

vector<BitPattern> get_running_algo_rtts(vector<string> bit_patterns, vector<int> partition, vector<int> rtts) {
  vector<BitPattern> running_algo_rtts;

  for (string bit_pattern : bit_patterns) {
    BitPattern running_algo_rtt;
    running_algo_rtt["BBR"] = vector<int>();
    running_algo_rtt["CUBIC"] = vector<int>();
    int start = 0;
    for (uint i = 0; i < partition.size(); i++) {
      int p = partition[i];

      string sub_p = bit_pattern.substr(start, p);
      uint num_b = 0, num_c = 0;
      for (char c : sub_p) {
        if (c == 'B') num_b++;
        if (c == 'C') num_c++;
      }
      for (uint j = 0; j < num_b; j++) running_algo_rtt["BBR"].push_back(rtts[i]);
      for (uint j = 0; j < num_c; j++) running_algo_rtt["CUBIC"].push_back(rtts[i]);

      start += p;
    }
    running_algo_rtts.push_back(running_algo_rtt);
  }
  return running_algo_rtts;
}

void run_single_experiment(
  std::string experiment_name,
  std::vector<int> bandwidths,
  std::vector<double> buffer_sizes,
  std::vector<int> rtts,
  std::vector<std::string> algos,
  int flow_duration,
  int num_total_flows,
  vector<int> partition
) {
  ParetoConf ex_conf;
  ex_conf.RTTS = rtts;
  ex_conf.DURATION = flow_duration;
  ex_conf.NUM_TOTAL_FLOWS = num_total_flows;

  vector<string> bit_patterns = get_bit_patterns(partition);
  vector<BitPattern> running_algo_rtts = get_running_algo_rtts(bit_patterns, partition, rtts);
  ex_conf.BIT_PATTERNS = bit_patterns;
  ex_conf.RUNNING_ALGO_RTTS = running_algo_rtts;

  int i = 0;
  for (int bandwidth : bandwidths) {
    for (double buffer_size : buffer_sizes) {
      i++;

      ex_conf.BANDWIDTH = bandwidth;
      ex_conf.BUFF_SIZE = buffer_size;
    }
  }
}


void run_multi_experiment(
  std::string experiment_name,
  int num_runs,
  std::vector<int> bandwidths,
  std::vector<double> buffer_sizes,
  std::vector<int> rtts,
  std::vector<std::string> algos,
  int flow_duration,
  int num_total_flows,
  int start_run_num,
  vector<BitPattern> bit_patterns,
  vector<int> partition
) {
  int i_run = start_run_num - 1;
  while (num_runs > 0) {
    num_runs--;
    i_run++;

    run_single_experiment(
      experiment_name,
      bandwidths,
      buffer_sizes,
      rtts,
      algos,
      flow_duration,
      num_total_flows,
      partition
    );
  }
}

void run_experiment(
  std::string experiment_name,
  int num_runs,
  std::vector<int> bandwidths,
  std::vector<double> buffer_sizes,
  std::vector<int> rtts,
  std::vector<std::string> algos,
  bool debug = false,
  int flow_duration = 60,
  int num_total_flows = 6,
  int start_run_num = 1,
  vector<BitPattern> bit_patterns = vector<BitPattern>(),
  vector<int> partition = vector<int>()
) {
  if (partition.empty()) {
    for (int i = 0; i < 3; i++) {
      partition.push_back(num_total_flows / 3);
    }
  }

  run_multi_experiment(
    experiment_name,
    num_runs,
    bandwidths,
    buffer_sizes,
    rtts,
    algos,
    flow_duration,
    num_total_flows,
    start_run_num,
    bit_patterns,
    partition
  );
}

void run_pareto_multi_rtt_6flow() {
  std::string experimentName = "pareto_multi_rtt_6flow_20mbps";
  int numRuns = 3;
  std::vector<std::string> algos { "Cubic", "Bbr" };
  std::vector<int> RTTs { 40, 80, 120 };
  std::vector<int> bandWidths { 20 };
  std::vector<double> bufferSizes { 0.5, 1, 3, 5, 10 };
  int flowDuration = 60 * 2;
  run_experiment(experimentName, numRuns, bandWidths, bufferSizes, RTTs, algos, false, flowDuration);
}

int main(int argc, char **argv) {
  run_pareto_multi_rtt_6flow();
}
