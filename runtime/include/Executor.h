#pragma once

#include <string>
#include <vector>

class Executor {
public:
  Executor(const std::string &filename, int numRanks = 1);
  int run(bool viz = false);

private:
  std::string filename;
  int numRanks;
};
