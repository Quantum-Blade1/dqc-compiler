#pragma once

#include <string>
#include <vector>

class QPUSimulator {
public:
  struct Command {
    std::string op; // APPLY, MEASURE, SNAPSHOT, EPR_ALLOC, EPR_CONSUME
    std::string gate; // H, X, CX
    std::vector<int> args; // gate args or qubit index
    std::vector<int> ranks; // for EPR ops: participant ranks
    int qubit = -1;
  };

  QPUSimulator(int rankId, int numQubits = 8);
  QPUSimulator(int rankId, const std::vector<Command> &commands, int numQubits = 8);
  QPUSimulator(int rankId, const std::vector<Command> &commands, class MPILayer *mpi, int numQubits = 8);
  void run();
  std::string getResult() const;
  // Messaging and timing stats
  int getMessagesSent() const;
  int getMessagesReceived() const;
  int getBytesSent() const;
  double getRunMs() const;
  // Statistics
  int getEntanglingOps() const;
  int getTotalGates() const;
private:
  int rankId;
  int numQubits;
  std::string result;
  std::vector<Command> commands;
  int entanglingOps = 0;
  int totalGates = 0;
  // Messaging/timing
  MPILayer *mpi = nullptr;
  int messagesSent = 0;
  int messagesReceived = 0;
  int bytesSent = 0;
  double runMs = 0.0;
};
