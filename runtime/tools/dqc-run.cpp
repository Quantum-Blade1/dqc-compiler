#include "Executor.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <unistd.h>
#include <sstream>

static std::string writeTemp(const std::vector<std::string> &lines) {
  char tmpl[] = "/tmp/dqc_prog_XXXXXX";
  int fd = mkstemp(tmpl);
  if (fd == -1) return std::string();
  std::string path = std::string(tmpl) + ".dqc";
  std::ofstream out(path);
  for (size_t i = 0; i < lines.size(); ++i) {
    out << lines[i];
    if (i + 1 < lines.size()) out << "\n";
  }
  out.close();
  close(fd);
  return path;
}

static bool isMlirContent(const std::vector<std::string> &lines) {
  // Check if content looks like MLIR (has func.func, dqc.*, or type annotations)
  for (const auto &l : lines) {
    if (l.find("func.func") != std::string::npos ||
        l.find("dqc.") != std::string::npos ||
        l.find("!dqc.") != std::string::npos) {
      return true;
    }
  }
  return false;
}

static std::string compileMlirToTemp(const std::vector<std::string> &lines) {
  // Write MLIR to temp, compile it, return .dqc path
  char tmpl[] = "/tmp/dqc_mlir_XXXXXX";
  int fd = mkstemp(tmpl);
  if (fd == -1) return std::string();
  std::string mlirPath = std::string(tmpl) + ".mlir";
  std::ofstream out(mlirPath);
  for (const auto &l : lines) out << l << "\n";
  out.close();
  close(fd);
  
  // Compile MLIR to .dqc using full path to dqc-compile
  std::string dqcPath = mlirPath + ".dqc";
  std::string compileCmd = "/workspaces/dqc-compiler/runtime/build/dqc-compile " + mlirPath + " " + dqcPath;
  std::cerr << "[debug] Running: " << compileCmd << "\n";
  int ret = system(compileCmd.c_str());
  if (ret != 0) {
    std::cerr << "[error] MLIR compilation failed (return code: " << ret << ")\n";
    return std::string();
  }
  return dqcPath;
}

static int getMaxRankFromDQC(const std::string &dqcFile) {
  // Parse .dqc file to find maximum rank number
  std::ifstream in(dqcFile);
  std::string line;
  int maxRank = 0;
  while (std::getline(in, line)) {
    // Lines are formatted as "rank: {...}"
    size_t colon = line.find(':');
    if (colon != std::string::npos) {
      try {
        int rank = std::stoi(line.substr(0, colon));
        if (rank > maxRank) maxRank = rank;
      } catch (...) {}
    }
  }
  return maxRank + 1;  // Return number of ranks (0-indexed to count)
}


static void showProgram(const std::vector<std::string> &lines) {
  std::cout << "\n=== Program ===\n";
  for (size_t i = 0; i < lines.size(); ++i) {
    std::cout << (i + 1) << ": " << lines[i] << "\n";
  }
  std::cout << "===============\n\n";
}

static std::vector<std::string> chainCreateProgram() {
  std::cout << "\n=== DQC Program Creator ===\n";
  std::cout << "Enter your .dqc program lines (one per line).\n";
  std::cout << "Type ':done' when finished.\n\n";
  
  std::vector<std::string> lines;
  std::string line;
  int lineNum = 1;
  while (true) {
    std::cout << lineNum << "> ";
    if (!std::getline(std::cin, line)) break;
    if (line == ":done") break;
    lines.push_back(line);
    lineNum++;
  }
  return lines;
}

static void chainDebug(const std::vector<std::string> &lines) {
  std::cout << "\n=== Debug Mode ===\n";
  showProgram(lines);
  std::cout << "Program analysis:\n";
  std::cout << " - Total lines: " << lines.size() << "\n";
  int rankLines = 0;
  for (const auto &l : lines) {
    if (l.find(':') != std::string::npos) rankLines++;
  }
  std::cout << " - Rank-scoped commands: " << rankLines << "\n";
  std::cout << "Type 'edit' to re-edit, 'back' to return to menu.\n";
  std::string cmd;
  while (true) {
    std::cout << "debug> ";
    if (!std::getline(std::cin, cmd)) break;
    if (cmd == "back") break;
    if (cmd == "edit") {
      std::cout << "Re-entering edit mode...\n";
      break;
    }
    if (cmd == "show") {
      showProgram(lines);
      continue;
    }
    std::cout << "Unknown command. Try 'edit', 'show', or 'back'.\n";
  }
}

static int chainRun(const std::vector<std::string> &lines, int ranks, bool viz) {
  std::cout << "\n=== Running Program ===\n";
  
  // Auto-detect and compile MLIR if necessary
  std::string toExecute;
  if (isMlirContent(lines)) {
    std::cout << "[Detected MLIR input - compiling...]\n";
    toExecute = compileMlirToTemp(lines);
    if (toExecute.empty()) {
      std::cerr << "Compilation failed\n";
      return 1;
    }
  } else {
    toExecute = writeTemp(lines);
    if (toExecute.empty()) {
      std::cerr << "Failed to create temp file\n";
      return 1;
    }
  }
  
  std::cout << "Program file: " << toExecute << "\n";
  Executor exec(toExecute, ranks);
  int ret = exec.run(viz);
  std::cout << "\n=== Execution Complete ===\n";
  return ret;
}

static void chainMenu(std::vector<std::string> &lines, int &ranks, bool &viz) {
  std::cout << "\n=== Action Menu ===\n";
  std::cout << "1. Edit program\n";
  std::cout << "2. Run program\n";
  std::cout << "3. Debug program\n";
  std::cout << "4. Set num-ranks (current: " << ranks << ")\n";
  std::cout << "5. Toggle viz (current: " << (viz ? "ON" : "OFF") << ")\n";
  std::cout << "6. Exit\n";
  std::cout << "Choose: ";
  
  std::string choice;
  if (!std::getline(std::cin, choice)) return;
  
  if (choice == "1") {
    auto newLines = chainCreateProgram();
    if (!newLines.empty()) lines = newLines;
  } else if (choice == "2") {
    showProgram(lines);
    std::cout << "Proceeding with execution...\n";
    chainRun(lines, ranks, viz);
  } else if (choice == "3") {
    chainDebug(lines);
  } else if (choice == "4") {
    std::cout << "Enter num-ranks: ";
    std::string r;
    if (std::getline(std::cin, r)) {
      try { ranks = std::stoi(r); } catch (...) {}
    }
  } else if (choice == "5") {
    viz = !viz;
    std::cout << "Visualization is now " << (viz ? "ON" : "OFF") << "\n";
  }
}

int main(int argc, char **argv) {
  bool fileMode = false;
  bool viz = false;
  int ranks = 1;
  std::string file;

  // Parse arguments: if a file is given, use file mode; else use chain mode
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--viz") viz = true;
    else if (file.empty() && a[0] != '-') {
      file = a;
      fileMode = true;
    } else if (fileMode && ranks == 1) {
      try { ranks = std::stoi(a); } catch (...) {}
    }
  }

  if (fileMode) {
    // Check if input is .mlir; if so, compile it first
    std::string toExecute = file;
    if (file.size() > 5 && file.substr(file.size() - 5) == ".mlir") {
      std::cout << "[dqc-run] Detected MLIR input: " << file << "\n";
      std::cout << "[dqc-run] Invoking dqc-compile...\n";
      
      // Build the compile command
      char tmpTemplate[] = "/tmp/dqc_compiled_XXXXXX";
      int fd = mkstemp(tmpTemplate);
      if (fd == -1) {
        std::cerr << "[error] Failed to create temp file for compilation\n";
        return 1;
      }
      toExecute = std::string(tmpTemplate) + ".dqc";
      close(fd);
      
      // Find dqc-compile in the same directory as dqc-run
      std::string runPath = argv[0];
      std::string binDir = runPath.substr(0, runPath.rfind('/'));
      std::string compileCmd = binDir + "/dqc-compile " + file + " " + toExecute;
      
      int ret = system(compileCmd.c_str());
      if (ret != 0) {
        std::cerr << "[error] Compilation failed\n";
        return ret;
      }
      std::cout << "[dqc-run] Compilation successful. Executing...\n";
    }
    
    // Auto-detect number of ranks if not specified
    if (ranks == 1) {
      ranks = getMaxRankFromDQC(toExecute);
      if (ranks < 1) ranks = 1;  // Default to 1 if detection fails
      std::cout << "[dqc-run] Auto-detected " << ranks << " rank(s)\n";
    }
    
    Executor exec(toExecute, ranks);
    return exec.run(viz);
  }

  // Chain mode: guided wizard
  std::cout << "\n╔════════════════════════════════╗\n";
  std::cout << "║   DQC Quantum Compiler Runtime   ║\n";
  std::cout << "║   Interactive CLI Wizard         ║\n";
  std::cout << "╚════════════════════════════════╝\n";
  
  std::vector<std::string> program;
  
  // Initial program creation
  program = chainCreateProgram();
  if (program.empty()) {
    std::cout << "No program entered. Exiting.\n";
    return 0;
  }

  // Main menu loop
  while (true) {
    showProgram(program);
    chainMenu(program, ranks, viz);
    
    std::string continueChoice;
    std::cout << "Continue? (y/n): ";
    if (!std::getline(std::cin, continueChoice)) break;
    if (continueChoice != "y" && continueChoice != "yes") break;
  }

  std::cout << "Exiting DQC CLI. Goodbye!\n";
  return 0;
}
