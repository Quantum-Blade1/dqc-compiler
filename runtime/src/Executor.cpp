#include "Executor.h"
#include "QPUSimulator.h"
#include "MPILayer.h"
#include <iostream>
#include <fstream>
#include <thread>
#include <vector>
#include <sstream>

Executor::Executor(const std::string &filename, int numRanks)
    : filename(filename), numRanks(numRanks) {}

static std::string readFile(const std::string &path) {
  std::ifstream in(path);
  if (!in) return std::string();
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  return content;
}

int Executor::run(bool viz) {
  auto text = readFile(filename);
  if (text.empty()) {
    std::cerr << "Failed to read file: " << filename << "\n";
    return 2;
  }

  std::cout << "Loaded .dqc (text) file, size=" << text.size() << " bytes\n";
  std::cout << "Parsing rank-scoped commands...\n";

  // Simple .dqc format for runtime demo: lines like
  // rank: JSON_COMMAND
  // Example:
  // 0: {"op":"APPLY","gate":"H","args":[0]}

  std::vector<std::vector<QPUSimulator::Command>> rankCommands(numRanks);
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty()) continue;
    // trim
    auto pos = line.find(':');
    if (pos == std::string::npos) continue;
    int r = std::stoi(line.substr(0, pos));
    if (r < 0 || r >= numRanks) continue;
    std::string cmd = line.substr(pos + 1);
    // trim leading spaces
    size_t start = cmd.find_first_not_of(" \t");
    if (start != std::string::npos) cmd = cmd.substr(start);
    // parse minimal JSON-like command into Command struct
    QPUSimulator::Command C;
    // op
    auto findStr = [&](const std::string &key)->std::string{
      auto k = std::string("\"") + key + "\"";
      auto p = cmd.find(k);
      if (p == std::string::npos) return std::string();
      auto colon = cmd.find(':', p + k.size());
      if (colon == std::string::npos) return std::string();
      auto startv = cmd.find_first_not_of(" \t\"", colon+1);
      if (startv == std::string::npos) return std::string();
      auto endv = cmd.find_first_of(",}\n", startv);
      if (endv == std::string::npos) endv = cmd.size();
      return cmd.substr(startv, endv - startv);
    };
    std::string opv = findStr("op");
    if (!opv.empty()) {
      // strip quotes and spaces
      if (opv.front()=='\"') opv.erase(0, 1);
      if (!opv.empty() && opv.back()=='\"') opv.erase(opv.size()-1);
      C.op = opv;
    }
    std::string gatev = findStr("gate");
    if (!gatev.empty()) {
      if (gatev.front()=='\"') gatev.erase(0, 1);
      if (!gatev.empty() && gatev.back()=='\"') gatev.erase(gatev.size()-1);
      C.gate = gatev;
    }
    // parse args array
    auto argspos = cmd.find("\"args\"");
    if (argspos != std::string::npos) {
      auto b = cmd.find('[', argspos);
      auto e = cmd.find(']', b);
      if (b!=std::string::npos && e!=std::string::npos) {
        auto body = cmd.substr(b+1, e-b-1);
        std::istringstream as(body);
        std::string tok;
        while (std::getline(as, tok, ',')) {
          try { C.args.push_back(std::stoi(tok)); } catch(...) {}
        }
      }
    }
    // parse qubit field
    auto qp = findStr("qubit");
    if (!qp.empty()) {
      try { C.qubit = std::stoi(qp); } catch(...) { C.qubit = -1; }
    }
    // parse ranks array (for EPR ops)
    auto rankspos = cmd.find("\"ranks\"");
    if (rankspos != std::string::npos) {
      auto b = cmd.find('[', rankspos);
      auto e = cmd.find(']', b);
      if (b!=std::string::npos && e!=std::string::npos) {
        auto body = cmd.substr(b+1, e-b-1);
        std::istringstream rs(body);
        std::string tok;
        while (std::getline(rs, tok, ',')) {
          try { C.ranks.push_back(std::stoi(tok)); } catch(...) {}
        }
      }
    }
    rankCommands[r].push_back(C);
  }

  MPILayer mpi(numRanks);
  std::vector<std::thread> threads;
  std::vector<QPUSimulator> sims;

  for (int r = 0; r < numRanks; ++r) {
    sims.emplace_back(r, rankCommands[r], &mpi, 8);
    std::cerr << "[exec] rank " << r << " cmds=" << rankCommands[r].size() << "\n";
  }

  // Send a start sync message to each rank so threads start in a coordinated way.
  for (int r = 0; r < numRanks; ++r) mpi.send(0, r, "start");

  for (int r = 0; r < numRanks; ++r) {
    threads.emplace_back([r, &sims, &mpi]() {
      // Each rank waits for a start message from the coordinator (rank 0).
      MPILayer::Message m;
      mpi.recv(r, m);
      std::cerr << "[exec] rank " << r << " received start-msg='" << m << "'\n";

      // Run local simulator (may be CPU intensive).
      sims[r].run();

      // Simulator can optionally send messages; here we send a done notice.
      mpi.send(r, 0, "rank-" + std::to_string(r) + "-done");
      // increment per-simulator counters for bytes/messages sent
      // (QPUSimulator may also send during run; those increments should be done there if implemented)
    });
  }

  for (auto &t : threads) t.join();

  std::cout << "All ranks finished. Results:\n";
  for (int r = 0; r < numRanks; ++r) {
    std::cout << "rank " << r << ": " << sims[r].getResult() << "\n";
  }

  // Build JSON output with per-rank results and global statistics.
  int totalEbits = 0;
  for (int r = 0; r < numRanks; ++r) totalEbits += sims[r].getEntanglingOps();

  std::ostringstream out;
  out << "{\n  \"ranks\": [\n";
  for (int r = 0; r < numRanks; ++r) {
    out << "    { \"rank\": " << r << ", \"result\": \"";
    // escape simple quotes/newlines in result
    std::string res = sims[r].getResult();
    for (char c : res) {
      if (c == '\\') out << "\\\\";
      else if (c == '"') out << "\\\"";
      else if (c == '\n') out << "\\n";
      else out << c;
    }
    out << "\", \"entangling_ops\": " << sims[r].getEntanglingOps()
      << ", \"total_gates\": " << sims[r].getTotalGates()
      << ", \"messages_sent\": " << sims[r].getMessagesSent()
      << ", \"messages_received\": " << sims[r].getMessagesReceived()
      << ", \"bytes_sent\": " << sims[r].getBytesSent()
      << ", \"run_ms\": " << sims[r].getRunMs() << " }";
    if (r + 1 < numRanks) out << ",\n";
    else out << "\n";
  }
  out << "  ],\n  \"stats\": {\n";
  out << "    \"total_ebits\": " << totalEbits << ",\n";
  out << "    \"messages_sent\": " << mpi.getTotalMessagesSent() << ",\n";
  out << "    \"bytes_sent\": " << mpi.getTotalBytesSent() << "\n";
  out << "  }\n}\n";

  std::cout << out.str();

  if (viz) {
    // Simple ASCII visualization: messages received per-rank and e-bit bars.
    std::cout << "\nVisualization:\n";
    std::cout << "Messages received (to rank):\n";
    for (int r = 0; r < numRanks; ++r) {
      int m = mpi.getMessagesSentTo(r);
      std::cout << " rank " << r << ": " << m << " ";
      for (int i = 0; i < std::min(m, 40); ++i) std::cout << '#';
      std::cout << "\n";
    }
    std::cout << "E-bits (entangling ops) per rank:\n";
    for (int r = 0; r < numRanks; ++r) {
      int e = sims[r].getEntanglingOps();
      std::cout << " rank " << r << ": " << e << " ";
      for (int i = 0; i < std::min(e, 40); ++i) std::cout << '*';
      std::cout << "\n";
    }
  }

  return 0;
}
