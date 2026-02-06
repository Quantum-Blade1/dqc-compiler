#pragma once

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>

#include <atomic>

// Very small local MPI-like messaging layer for simulation.
// Thread-safe local IPC: per-rank queues protected by mutex+cv.
class MPILayer {
public:
  using Message = std::string;
  explicit MPILayer(int ranks);

  // Send a message from `fromRank` to `toRank` (thread-safe).
  void send(int fromRank, int toRank, const Message &msg);

  // Non-blocking poll: return true and pop message if available.
  bool poll(int rank, Message &out);

  // Blocking receive: waits until a message is available and pops it.
  void recv(int rank, Message &out);

  // Return approximate queue size for a rank.
  int queued(int rank) const;

  // Global statistics
  int getTotalMessagesSent() const;
  int getTotalBytesSent() const;
  int getMessagesSentTo(int rank) const;

private:
  int ranks;
  std::vector<std::deque<Message>> queues;
  mutable std::vector<std::mutex> mtxs;
  std::vector<std::condition_variable> cvs;
  std::atomic<int> totalMessagesSent{0};
  std::atomic<int> totalBytesSent{0};
  std::vector<std::atomic<int>> messagesSentTo;
};
