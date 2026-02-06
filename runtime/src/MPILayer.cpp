#include "MPILayer.h"

MPILayer::MPILayer(int ranks)
  : ranks(ranks), queues(ranks), mtxs(ranks), cvs(ranks), messagesSentTo(ranks) {}

void MPILayer::send(int fromRank, int toRank, const Message &msg) {
  (void)fromRank;
  if (toRank < 0 || toRank >= ranks) return;
  {
    std::lock_guard<std::mutex> lk(mtxs[toRank]);
    queues[toRank].push_back(msg);
  }
  cvs[toRank].notify_one();
  totalMessagesSent.fetch_add(1, std::memory_order_relaxed);
  totalBytesSent.fetch_add(static_cast<int>(msg.size()), std::memory_order_relaxed);
  if ((int)messagesSentTo.size() == ranks) {
    messagesSentTo[toRank].fetch_add(1, std::memory_order_relaxed);
  }
}

bool MPILayer::poll(int rank, Message &out) {
  if (rank < 0 || rank >= ranks) return false;
  std::lock_guard<std::mutex> lk(mtxs[rank]);
  if (queues[rank].empty()) return false;
  out = std::move(queues[rank].front());
  queues[rank].pop_front();
  return true;
}

void MPILayer::recv(int rank, Message &out) {
  if (rank < 0 || rank >= ranks) return;
  std::unique_lock<std::mutex> lk(mtxs[rank]);
  cvs[rank].wait(lk, [&]{ return !queues[rank].empty(); });
  out = std::move(queues[rank].front());
  queues[rank].pop_front();
}

int MPILayer::queued(int rank) const {
  if (rank < 0 || rank >= ranks) return 0;
  std::lock_guard<std::mutex> lk(mtxs[rank]);
  return static_cast<int>(queues[rank].size());
}

int MPILayer::getTotalMessagesSent() const { return totalMessagesSent.load(std::memory_order_relaxed); }
int MPILayer::getTotalBytesSent() const { return totalBytesSent.load(std::memory_order_relaxed); }

int MPILayer::getMessagesSentTo(int rank) const {
  if (rank < 0 || rank >= ranks) return 0;
  return messagesSentTo[rank].load(std::memory_order_relaxed);
}

// Not exposed previously: initialize per-rank counters when constructed.

