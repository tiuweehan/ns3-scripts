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
#include "ns3/traffic-control-module.h"
#include "ns3/tcp-socket-factory-impl.h"
#include "ns3/net-device-queue-interface.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/gnuplot.h"
#include "unistd.h"

typedef uint32_t uint;

using namespace std;
using namespace ns3;

#define TCP_BBR "TcpBbr"
#define TCP_CUBIC "TcpCubic"

#define ERROR 0.000001

NS_LOG_COMPONENT_DEFINE ("TcpBbrCubic");

typedef map<string, vector<int>> AlgoRTTs;

class ParetoConf {
  public:
        string RESULT_FOLDER_PATH = "results-2dumps/";
        string PCAPS_FOLDER_PATH = "pcaps-2dumps/" ;
        string HYDRA = "10.0.0.1";
        string NEMO = "10.0.0.2";
        string NEMO_INTERFACE = "enp2s0";
        string GALACTICA = "5.5.5.2";
        string CAPRICA = "5.5.5.1";
        string GALACTICA_INTERFACE = "enp7s4";
        string CAPRICA_INTERFACE = "eth1";
        string RECEIVER_IP = CAPRICA;
        string RECEIVER_BASE_PORT = "10086"  ;
        string RECEIVER_SCRIPT = "./start_test.sh";
        string ISOLATED_RECEIVER_SCRIPT = "./start_isolated_test.sh";
        string RECEIVER_INTERFACE = CAPRICA_INTERFACE;
        string SENDER_IP = GALACTICA;
        string CLEANUP_PCAPS_SCRIPT = "cleanup_pcaps_and_csvs.py";
        string CLEANUP_PCAPS_SCRIPT_PATH = CLEANUP_PCAPS_SCRIPT;
        string SENDER_SCRIPT = "spawn_senders.sh";
        string SENDER_SCRIPT_PATH = SENDER_SCRIPT;
        string TC_CONFIG_SCRIPT = "config.sh";
        string TC_CONFIG_SCRIPT_PATH = TC_CONFIG_SCRIPT;
        string PCAP2CSV_SCRIPT = "pcap2csv.sh";
        string PCAP2CSV_SCRIPT_PATH = PCAP2CSV_SCRIPT;
        vector<string> PROCESS_CSV_SCRIPTS  { "plot_unfairness.py", "get_sending_rates.py" };
        vector<string> PROCESS_CSV_SCRIPT_PATHS = PROCESS_CSV_SCRIPTS;
        double BUFF_SIZE = 1  ;
        int NUM_TOTAL_FLOWS = 10;
        int BANDWIDTH = 50  ;
        vector<int> RTTS { 20, 50, 100 };
        int DURATION = 60  ;
        double WINDOW_TIME_LENGTH = 0.5  ;
        int MOVING_STRIDE = 1  ;
        string FIGURE_FILETYPE = "png";
        string UNFAIRNESS_FIGURE_TITLE = "BBR\"s Throughput Unfairness Ratio vs. Share of BBR on Various Bandwidths";
        string CSV_FOLDER_PATH = PCAPS_FOLDER_PATH;
        int FIGURE_DPI = 100;

        vector<string> BIT_PATTERNS;
        vector<AlgoRTTs> RUNNING_ALGO_RTTS;
        vector<string> ALGOS { TCP_BBR, TCP_CUBIC };
};

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
  //   ScheduleTx();
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
    //  cout << Simulator::Now ().GetSeconds () << "\t" << mPacketsSent << endl;
  }
}

vector<uint64_t> mapPacketsReceivedIPV4;
vector<vector<Time>> mapRTT;  

void ReceivedPacket(string context, Ptr<const Packet> p, const Address& addr){
}

void ReceivedPacketIPV4(uint key, string context, Ptr<const Packet> p, Ptr<Ipv4> ipv4, uint interface) {
  mapPacketsReceivedIPV4[key]++;
}

void
RttTracer (uint key, string context, Time oldval, Time newval)
{
  mapRTT[key].push_back(newval);
}

Ptr<Socket> uniFlow(Address sinkAddress, 
          uint sinkPort, 
          string tcpVariant, 
          Ptr<Node> hostNode, 
          Ptr<Node> sinkNode, 
          double startTime, 
          double stopTime,
          uint packetSize,
          uint numPackets,
          string dataRate,
          double appStartTime,
          double appStopTime,
          uint buff_size) {

  // if(tcpVariant.compare(TCP_BBR) == 0) {
  //   Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpBbr::GetTypeId()));
  // } else if(tcpVariant.compare(TCP_CUBIC) == 0) {
  //   Config::SetDefault("ns3::TcpL4Protocol::SocketType", TypeIdValue(TcpCubic::GetTypeId()));
  // } else {
  //   fprintf(stderr, "Invalid TCP version\n");
  //   exit(EXIT_FAILURE);
  // }
  PacketSinkHelper packetSinkHelper("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
  ApplicationContainer sinkApps = packetSinkHelper.Install(sinkNode);
  sinkApps.Start(Seconds(startTime));
  sinkApps.Stop(Seconds(stopTime));

  // Ptr<Socket> ns3TcpSocket = Socket::CreateSocket(hostNode, TcpSocketFactory::GetTypeId()); // this doesn't work, it always creates TCP new reno :(
  // Hack src/internet/wscript to include tcp-socket-factory-impl and Hack src/internet/model/tcp-socket-factory-impl.h to make m_tcp public 
  Ptr<TcpSocketFactoryImpl> socketFactory = hostNode->GetObject<TcpSocketFactoryImpl>(TcpSocketFactory::GetTypeId());
  
  Ptr<Socket> ns3TcpSocket;
  if (tcpVariant.compare(TCP_BBR) == 0) {
    // BBR not compatible with ns-3.33
    ns3TcpSocket = socketFactory->m_tcp->CreateSocket(TcpBbr::GetTypeId());
  } else if (tcpVariant.compare(TCP_CUBIC) == 0) {
    // ns-3.27 does not have TcpCubic yet - implementation is stolen from ns-3.33
    ns3TcpSocket = socketFactory->m_tcp->CreateSocket(TcpCubic::GetTypeId());
  } else {
     fprintf(stderr, "Invalid TCP version\n");
    exit(EXIT_FAILURE); 
  }

  ns3TcpSocket->SetAttribute("SegmentSize", UintegerValue(packetSize)); // Do not fragment packets
  ns3TcpSocket->SetAttribute("SndBufSize", UintegerValue(buff_size)); // Transmit buffer size
  ns3TcpSocket->SetAttribute("RcvBufSize", UintegerValue(buff_size)); // Receiver buffer size

  Ptr<ClientApp> app = CreateObject<ClientApp>();
  app->Setup(ns3TcpSocket, sinkAddress, packetSize, numPackets, DataRate(dataRate));
  hostNode->AddApplication(app);
  app->SetStartTime(Seconds(appStartTime));
  app->SetStopTime(Seconds(appStopTime));

  return ns3TcpSocket;
}

void start(AlgoRTTs running_algo_rtts, string bit_pattern, vector<string> algos, vector<int> rtts, double buff_bdp, int bandwidth) {
  uint nBbr = running_algo_rtts[TCP_BBR].size();
  uint nCubic = running_algo_rtts[TCP_CUBIC].size();
  uint numSender = nBbr + nCubic;

  // map of RTT to count of nBBr and nCubic for each RTT
  map<int, map<string, uint>> rttCounts;
  for (string algo : algos) {
    for (int rtt : running_algo_rtts[algo]) {
      rttCounts[rtt][algo]++;
    }
  }

  string rateHR = "10000Mbps"; // infinitely fast
  // string latencyHR = "20ms";

  stringstream rateRRstream;
  rateRRstream << bandwidth << "Mbps";
  string rateRR = rateRRstream.str();
  string latencyRR = "50ms";
  // double errorP = ERROR;

  string transferSpeed = "5Mbps";

  double flowStart = 0;
  double durationGap = 120;

  uint basePort = 10086; // Base port
  uint numPackets = 10000000;

  uint packetSize = 1446;    // 1446 + 54 = 1500 Bytes
  // uint queueSizeHRBytes = 20 * 100000;

  double maxRTT = *max_element(rtts.begin(), rtts.end());
  uint buff_size = buff_bdp * maxRTT * bandwidth * 1000 / 8;
  // uint queueSizeRRBytes = buff_size * maxRTT * bandwidth * 1000 / 8;

  // uint queueSizeHR = queueSizeHRBytes / packetSize;
  // uint queueSizeRR = queueSizeRRBytes / packetSize;

  // cout <<
  //   "Number of Flows:" << endl <<
  //   "   BBR Flows: " << nBbr << " | Cubic Flows: " << nCubic << " | Total Flows: " << numSender << endl <<
  //   "Application:" << endl <<
  //   "   Packet Size: " << packetSize << " bytes | Send Rate: " << transferSpeed << " | Duration: " << durationGap << " seconds" << endl <<
  //   "Host <-> Router:" << endl <<
  //   "   Data Rate: " << rateHR << " | Queue Size: " << queueSizeHR << " packets (" << queueSizeHRBytes << " bytes)" << endl <<
  //   "Router <-> Router:" << endl <<
  //   "   Data Rate: " << rateRR << " | Latency: " << latencyRR << " | Queue Size: " << queueSizeRR << " packets (" << queueSizeRRBytes << " bytes) | Error Rate: " << errorP << endl;

  // Config::SetDefault("ns3::QueueBase::Mode", StringValue("QUEUE_MODE_PACKETS"));


  // Create RTTs file
  stringstream rtts_filename_stream;
  rtts_filename_stream << bit_pattern << ".rtt.csv";
  string rtts_filename = rtts_filename_stream.str();
  ofstream rtts_file;
  rtts_file.open(rtts_filename);
  rtts_file << "srcipaddr destipaddr portnum rtt algo"<< endl;
  {
     uint nodeIndex = 0;
     for (int rtt : rtts) {
       for (string algo : algos) {
         uint algosCount = rttCounts[rtt][algo];
         for (uint i = 0; i < algosCount; i++, nodeIndex++) {
           int portNum = basePort + nodeIndex;
           rtts_file << "10.1." << nodeIndex << ".1" << " "; // Source IP
           rtts_file << "10.2." << nodeIndex << ".1" << " "; // Destination IP
           rtts_file << portNum << " " << rtt << " " << (algo == TCP_BBR ? "BBR" : "CUBIC" ) << endl;
         }
       }
     } 
  }
  rtts_file.close();

  // Create Nodes
  NodeContainer routers, senders, receivers;
  routers.Create(2);
  senders.Create(numSender);
  receivers.Create(numSender);

  // Create Router <-> Router links
  PointToPointHelper p2pRR;
  p2pRR.SetDeviceAttribute("DataRate", StringValue(rateRR));
  p2pRR.SetChannelAttribute("Delay", StringValue(latencyRR));
  // p2pRR.SetQueue("ns3::DropTailQueue", "MaxPackets", UintegerValue(queueSizeRR));

  NetDeviceContainer routerDevices = p2pRR.Install(routers);

  // Enable PCAP on sending Router        
  stringstream pcap_filename_stream_all;
  pcap_filename_stream_all << bit_pattern << "-nemo.pcap";
  string pcap_filename_all = pcap_filename_stream_all.str();
  p2pRR.EnablePcap(pcap_filename_all, routerDevices.Get(0), true, true);

  NetDeviceContainer leftRouterDevices, rightRouterDevices, senderDevices, receiverDevices;
  {
    uint nodeIndex = 0;
    for (int rtt : rtts) {
      // Create link with RTT delay
      PointToPointHelper p2pHR;
      p2pHR.SetDeviceAttribute("DataRate", StringValue(rateHR));
      stringstream latencyHRstream;
      latencyHRstream << (rtt / 4) << "ms"; // Divide by 4 as each packet goes through 4 links between router and host
      string latencyHR = latencyHRstream.str();
      p2pHR.SetChannelAttribute("Delay", StringValue(latencyHR));
      // p2pHR.SetQueue("ns3::DropTailQueue", "MaxPackets", UintegerValue(queueSizeHR));
      
      for (string algo : algos) {
        uint algoCount = rttCounts[rtt][algo];
        // Create links for given RTT
        for (uint i = 0; i < algoCount; i++, nodeIndex++) {
          // Install links between sender and sending router
          NetDeviceContainer cleft = p2pHR.Install(routers.Get(0), senders.Get(nodeIndex));
          leftRouterDevices.Add(cleft.Get(0));
          senderDevices.Add(cleft.Get(1));

          // Install links between receiver and receiving router
          NetDeviceContainer cright = p2pHR.Install(routers.Get(1), receivers.Get(nodeIndex));
          rightRouterDevices.Add(cright.Get(0));
          receiverDevices.Add(cright.Get(1));

          // PCAP tracing on sender
          stringstream pcap_filename_stream;

          // For RTT
          // pcap_filename_stream << bit_pattern << "-" << rtt << "-mm.pcap";
          pcap_filename_stream << bit_pattern << "-" << rtt << "-mm-" << nodeIndex << ".pcap";
          string pcap_filename = pcap_filename_stream.str();
          p2pHR.EnablePcap(pcap_filename, cleft.Get(1), true, true);
        }
      }
    }
  }

  //Install Internet Stack
  cout << "Install Internet Stack" << endl;;
  InternetStackHelper stack;
  stack.Install(routers);
  stack.Install(senders);
  stack.Install(receivers);

  //Adding IP addresses
  cout << "Adding IP addresses" << endl;;
  Ipv4AddressHelper routerIP = Ipv4AddressHelper("10.3.0.0", "255.255.255.0");
  Ipv4AddressHelper senderIP = Ipv4AddressHelper("10.1.0.0", "255.255.255.0");
  Ipv4AddressHelper receiverIP = Ipv4AddressHelper("10.2.0.0", "255.255.255.0");

  Ipv4InterfaceContainer routerIFC, senderIFCs, receiverIFCs, leftRouterIFCs, rightRouterIFCs;

  routerIFC = routerIP.Assign(routerDevices);

  {
    uint nodeIndex = 0;
    for (int rtt : rtts) {
      for (string algo : algos) {
        uint algoCount = rttCounts[rtt][algo];
        for (uint i = 0; i < algoCount; i++, nodeIndex++) {
          NetDeviceContainer senderDevice;
          senderDevice.Add(senderDevices.Get(nodeIndex));
          senderDevice.Add(leftRouterDevices.Get(nodeIndex));
          Ipv4InterfaceContainer senderIFC = senderIP.Assign(senderDevice);
          senderIFCs.Add(senderIFC.Get(0));
          leftRouterIFCs.Add(senderIFC.Get(1));
          senderIP.NewNetwork();

          NetDeviceContainer receiverDevice;
          receiverDevice.Add(receiverDevices.Get(nodeIndex));
          receiverDevice.Add(rightRouterDevices.Get(nodeIndex));
          Ipv4InterfaceContainer receiverIFC = receiverIP.Assign(receiverDevice);
          receiverIFCs.Add(receiverIFC.Get(0));
          rightRouterIFCs.Add(receiverIFC.Get(1));
          receiverIP.NewNetwork();
        }
      }
    }
  }

  // Create applications
  {
    uint nodeIndex = 0;
    for (int rtt : rtts) {
      for (string algo : algos) {
        uint algoCount = rttCounts[rtt][algo];
        for (uint i = 0; i < algoCount; i++, nodeIndex++) {
          int port = basePort + nodeIndex;
          uniFlow(InetSocketAddress(receiverIFCs.GetAddress(nodeIndex), port), port, algo, senders.Get(nodeIndex), receivers.Get(nodeIndex), flowStart, flowStart+durationGap, packetSize, numPackets, transferSpeed, flowStart, flowStart+durationGap, buff_size);
        }
      }
    }
  }

  //Turning on Static Global Routing
  cout << "Turning on Static Global Routing" << endl;
  Ipv4GlobalRoutingHelper::PopulateRoutingTables();

  cout << "Simulation started" << endl;
  Simulator::Stop(Seconds(durationGap+flowStart));
  Simulator::Run();
 
  // for (uint i = 0; i < numSender; i++) {
  //   cout << (i < nBbr ? TCP_BBR : TCP_CUBIC) << " #" << (i < nBbr ? i + 1: i % nBbr + 1) << endl; 
    
  //   double totalBytes = static_cast<double>(mapPacketsReceivedIPV4[i] * packetSize);
  //   cout << "Total Bytes Sent: " << totalBytes << endl;;
    
  //   // Throughput
  //   double totalBits = static_cast<double>(totalBytes * 8);
  //   double throughput = totalBits / durationGap;
  //   throughput /= (1024 * 1024); // Convert to Mbps
  //   cout << "Throughput: " << throughput << "Mbps" << endl;

  //   // Delay    
  //   double delay = 0.0;
  //   for (Time t : mapRTT[i]) {
  //     delay += t.GetNanoSeconds();
  //   }
  //   delay /= mapRTT[i].size();
  //   delay /= 1e6; // Convert to miliseconds
  //   cout << "Delay: " << delay << "ms" << endl;
  // }

  cout << "Simulation finished" << endl;
  Simulator::Destroy();
}

void run_this_throughput(ParetoConf config) {
  for (uint i = 0; i < config.BIT_PATTERNS.size(); i++) {
    AlgoRTTs running_algo_rtts = config.RUNNING_ALGO_RTTS[i];
    string bit_pattern = config.BIT_PATTERNS[i];

    sort(config.RTTS.begin(), config.RTTS.end());

    start(running_algo_rtts, bit_pattern, config.ALGOS, config.RTTS, config.BUFF_SIZE, config.BANDWIDTH);

    // Merge pcap files
    for (int rtt : config.RTTS) {
      stringstream merge_pcap_command;
      // mergecap -w BBBBBB-40-mm.pcap BBBBBB-40-mm-*.pcap
      merge_pcap_command << "mergecap -w " << bit_pattern << "-" << rtt << "-mm.pcap " << bit_pattern << "-" << rtt << "-mm-*.pcap";
      system(merge_pcap_command.str().c_str());

      // Remove original files
      // stringstream remove_pcap_files_command;
      // remove_pcap_files_command << "rm -rf " << bit_pattern << "-" << rtt << "-mm-*.pcap";
      // system(remove_pcap_files_command.str().c_str());
    }

    // Convert to CSV TODO

    // Remove PCAP files TODO
  }
}

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
  // return vector<string> { "BCBCCB" };
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

vector<AlgoRTTs> get_running_algo_rtts(vector<string> bit_patterns, vector<int> partition, vector<int> rtts) {
  vector<AlgoRTTs> running_algo_rtts;

  for (string bit_pattern : bit_patterns) {
    AlgoRTTs running_algo_rtt;
    running_algo_rtt[TCP_BBR] = vector<int>();
    running_algo_rtt[TCP_CUBIC] = vector<int>();
    int start = 0;
    for (uint i = 0; i < partition.size(); i++) {
      int p = partition[i];

      string sub_p = bit_pattern.substr(start, p);
      uint num_b = 0, num_c = 0;
      for (char c : sub_p) {
        if (c == 'B') num_b++;
        if (c == 'C') num_c++;
      }
      for (uint j = 0; j < num_b; j++) running_algo_rtt[TCP_BBR].push_back(rtts[i]);
      for (uint j = 0; j < num_c; j++) running_algo_rtt[TCP_CUBIC].push_back(rtts[i]);

      start += p;
    }
    running_algo_rtts.push_back(running_algo_rtt);
  }
  return running_algo_rtts;
}

void run_single_experiment(
  string experiment_name,
  vector<int> bandwidths,
  vector<double> buffer_sizes,
  vector<int> rtts,
  vector<string> algos,
  int flow_duration,
  int num_total_flows,
  vector<int> partition
) {
  ParetoConf ex_conf;
  ex_conf.RTTS = rtts;
  ex_conf.DURATION = flow_duration;
  ex_conf.NUM_TOTAL_FLOWS = num_total_flows;

  vector<string> bit_patterns = get_bit_patterns(partition);
  vector<AlgoRTTs> running_algo_rtts = get_running_algo_rtts(bit_patterns, partition, rtts);
  ex_conf.BIT_PATTERNS = bit_patterns;
  ex_conf.RUNNING_ALGO_RTTS = running_algo_rtts;

  int i = 0;
  for (int bandwidth : bandwidths) {
    for (double buffer_size : buffer_sizes) {
      i++;

      // chdir to run folder
      stringstream run_folder_name_stream;
      run_folder_name_stream << bandwidth << "Mbps-" << buffer_size << "BDP-" << num_total_flows << "flows";
      string run_folder_name = run_folder_name_stream.str();
      stringstream mkdir_command_stream;
      mkdir_command_stream << "mkdir -p " << run_folder_name;
      string mkdir_command = mkdir_command_stream.str();
      system(mkdir_command.c_str());
      chdir(run_folder_name.c_str());

      ex_conf.BANDWIDTH = bandwidth;
      ex_conf.BUFF_SIZE = buffer_size;
      ex_conf.ALGOS = algos;

      run_this_throughput(ex_conf);

      // chdir to parent folder
      chdir("..");
    }
  }
}


void run_multi_experiment(
  string experiment_name,
  int num_runs,
  vector<int> bandwidths,
  vector<double> buffer_sizes,
  vector<int> rtts,
  vector<string> algos,
  int flow_duration,
  int num_total_flows,
  int start_run_num,
  vector<AlgoRTTs> bit_patterns,
  vector<int> partition
) {
  int i_run = start_run_num - 1;
  while (num_runs > 0) {
    num_runs--;
    i_run++;

    // chdir to run folder
    stringstream run_folder_name_stream;
    run_folder_name_stream << "./run" << i_run;
    string run_folder_name = run_folder_name_stream.str();
    stringstream mkdir_command_stream;
    mkdir_command_stream << "mkdir -p " << run_folder_name;
    string mkdir_command = mkdir_command_stream.str();
    system(mkdir_command.c_str());
    chdir(run_folder_name.c_str());

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

    // chdir to parent folder
    chdir("..");
  }
}

void run_experiment(
  string experiment_name,
  int num_runs,
  vector<int> bandwidths,
  vector<double> buffer_sizes,
  vector<int> rtts,
  vector<string> algos,
  bool debug = false,
  int flow_duration = 60,
  int num_total_flows = 6,
  int start_run_num = 1,
  vector<AlgoRTTs> bit_patterns = vector<AlgoRTTs>(),
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
  string experimentName = "pareto_multi_rtt_6flow_20mbps";
  int numRuns = 1;
  vector<string> algos { TCP_BBR, TCP_CUBIC };
  vector<int> RTTs { 40, 80, 120 };
  vector<int> bandWidths { 20 };
  vector<double> bufferSizes { 0.5, 1, 3, 5, 10 };
  int flowDuration = 60 * 2;
  run_experiment(experimentName, numRuns, bandWidths, bufferSizes, RTTs, algos, false, flowDuration);
}

int main(int argc, char **argv) {
  run_pareto_multi_rtt_6flow();
}
