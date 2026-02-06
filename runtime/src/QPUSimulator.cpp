#include "QPUSimulator.h"
#include "MPILayer.h"
#include <thread>
#include <chrono>
#include <sstream>
#include <iostream>
#include <complex>
#include <random>

using Complex = std::complex<double>;

QPUSimulator::QPUSimulator(int rankId, int numQubits)
    : rankId(rankId), numQubits(numQubits) {}

QPUSimulator::QPUSimulator(int rankId, const std::vector<Command> &commands, MPILayer *mpi, int numQubits)
  : rankId(rankId), numQubits(numQubits), result(), commands(commands), mpi(mpi) {}

static int64_t vecSize(int nQubits) { return int64_t(1) << nQubits; }

void QPUSimulator::run() {
  if (commands.empty()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100 + (rankId * 10)));
    std::ostringstream out;
    out << "simulated(" << rankId << ",qubits=" << numQubits << ")";
    result = out.str();
    return;
  }

  // Build statevector
  int n = numQubits;
  int64_t N = vecSize(n);
  std::vector<Complex> state(N, Complex(0,0));
  state[0] = Complex(1,0);

  std::mt19937 rng(rankId + 12345);
  std::uniform_real_distribution<double> urd(0.0, 1.0);

  // Start timing
  auto t0 = std::chrono::steady_clock::now();

  // Process any incoming messages before executing commands
  if (mpi) {
    MPILayer::Message msg;
    while (mpi->poll(rankId, msg)) {
      messagesReceived++;
      // For now, just append incoming messages to result log
      result += "{\"inmsg\":\"" + msg + "\"}";
    }
  }

  auto apply_h = [&](int q) {
    std::vector<Complex> newst(N, Complex(0,0));
    double invs = 1.0 / std::sqrt(2.0);
    for (int64_t i = 0; i < N; ++i) {
      if (((i >> q) & 1) == 0) {
        int64_t j = i | (1LL << q);
        Complex a = state[i];
        Complex b = state[j];
        newst[i] += (a + b) * invs;
        newst[j] += (a - b) * invs;
      }
    }
    state.swap(newst);
  };

  auto apply_x = [&](int q) {
    std::vector<Complex> newst(N, Complex(0,0));
    for (int64_t i = 0; i < N; ++i) {
      int64_t j = i ^ (1LL << q);
      newst[j] = state[i];
    }
    state.swap(newst);
  };

  auto apply_cx = [&](int ctrl, int tgt) {
    std::vector<Complex> newst = state;
    for (int64_t i = 0; i < N; ++i) {
      if (((i >> ctrl) & 1) == 1) {
        int64_t j = i ^ (1LL << tgt);
        newst[j] = state[i];
        newst[i] = Complex(0,0);
      }
    }
    state.swap(newst);
  };

  auto measure = [&](int q) -> int {
    double p0 = 0.0;
    for (int64_t i = 0; i < N; ++i) if (((i >> q) & 1) == 0) p0 += std::norm(state[i]);
    double r = urd(rng);
    int outcome = (r < p0) ? 0 : 1;
    if (outcome == 0) {
      for (int64_t i = 0; i < N; ++i) if (((i >> q) & 1) == 1) state[i] = Complex(0,0);
      double norm = std::sqrt(p0);
      if (norm > 0) for (auto &c : state) c /= norm;
    } else {
      double p1 = 1.0 - p0;
      for (int64_t i = 0; i < N; ++i) if (((i >> q) & 1) == 0) state[i] = Complex(0,0);
      double norm = std::sqrt(p1);
      if (norm > 0) for (auto &c : state) c /= norm;
    }
    return outcome;
  };

  // Execute commands
  for (auto &c : commands) {
    // between commands, poll for new incoming messages
    if (mpi) {
      MPILayer::Message im;
      while (mpi->poll(rankId, im)) {
        messagesReceived++;
        result += "{\"inmsg\":\"" + im + "\"}";
      }
    }
    if (c.op == "APPLY") {
      if (c.gate == "H") { apply_h(c.args[0]); totalGates++; }
      else if (c.gate == "X") { apply_x(c.args[0]); totalGates++; }
      else if (c.gate == "CX") { apply_cx(c.args[0], c.args[1]); totalGates++; entanglingOps++; }
      // append ack to result
      result += "{\"ok\":true}";
    } else if (c.op == "MEASURE") {
      int out = measure(c.qubit);
      totalGates++;
      result += "{\"result\":" + std::to_string(out) + "}";
    } else if (c.op == "SNAPSHOT") {
      // append small prob snapshot
      std::ostringstream oss;
      oss << "{\"probs\":[";
      for (int i = 0; i < std::min<int64_t>(16, N); ++i) {
        if (i) oss << ",";
        oss << std::norm(state[i]);
      }
      oss << "]}";
      result += oss.str();
    } else if (c.op == "EPR_ALLOC") {
      // EPR allocation: send message to participant ranks
      if (mpi && !c.ranks.empty()) {
        std::string eprMsg = "{\"epr\":\"alloc\",\"from\":" + std::to_string(rankId) + "}";
        for (int r : c.ranks) {
          if (r != rankId) {
            mpi->send(rankId, r, eprMsg);
            messagesSent++;
            bytesSent += eprMsg.size();
          }
        }
      }
      result += "{\"epr_alloc\":true}";
    } else if (c.op == "EPR_CONSUME") {
      // EPR consume: log the consumption
      result += "{\"epr_consume\":true}";
    }
  }

  // End timing
  auto t1 = std::chrono::steady_clock::now();
  runMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
}

std::string QPUSimulator::getResult() const { return result; }

int QPUSimulator::getEntanglingOps() const { return entanglingOps; }
int QPUSimulator::getTotalGates() const { return totalGates; }

int QPUSimulator::getMessagesSent() const { return messagesSent; }
int QPUSimulator::getMessagesReceived() const { return messagesReceived; }
int QPUSimulator::getBytesSent() const { return bytesSent; }
double QPUSimulator::getRunMs() const { return runMs; }
