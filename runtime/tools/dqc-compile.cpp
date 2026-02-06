#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <regex>

// Simple MLIR → .dqc lowering: parse DQC ops and distribute by rank
class MLIRParser {
public:
  struct Op {
    int rank = 0;
    std::string op;      // APPLY, MEASURE, SNAPSHOT, EPR_ALLOC, EPR_CONSUME
    std::string gate;    // H, X, CX, RZ, ...
    std::vector<int> args;
    std::vector<int> ranks; // for EPR ops: participant ranks
    int qubit = -1; // for MEASURE ops
  };

  // Extract integer from strings like "%q0", "%0", etc.
  static int extractQubitId(const std::string &s) {
    size_t start = 0;
    if (s[0] == '%') start = 1;
    std::string numStr;
    while (start < s.size() && std::isdigit(s[start])) {
      numStr += s[start];
      start++;
    }
    if (!numStr.empty()) return std::stoi(numStr);
    return -1;
  }

  // Split string by delimiter
  static std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> result;
    std::string token;
    std::istringstream iss(s);
    while (std::getline(iss, token, delim)) {
      // trim spaces
      size_t start = token.find_first_not_of(" \t");
      if (start != std::string::npos) {
        size_t end = token.find_last_not_of(" \t");
        result.push_back(token.substr(start, end - start + 1));
      }
    }
    return result;
  }

  static std::vector<Op> parse(const std::string &mlir) {
    std::vector<Op> ops;
    std::istringstream in(mlir);
    std::string line;
    
    while (std::getline(in, line)) {
      // Trim spaces
      size_t start = line.find_first_not_of(" \t");
      if (start == std::string::npos || line[start] == '/' || line[start] == '}' || line[start] == '{') continue;
      
      std::string trimmed = line.substr(start);
      
      // Skip return and closing braces
      if (trimmed.find("return") == 0) continue;
      
      // Match gate operations: "dqc.apply_h"(%q0)
      std::regex gateOp("\"dqc\\.(apply_h|apply_x|apply_cx|apply_rz|measure)\"\\s*\\(([^)]*)\\)");
      std::smatch m;
      
      if (std::regex_search(trimmed, m, gateOp)) {
        std::string opType = m[1];
        std::string argsStr = m[2];
        
        // Extract qubit IDs from arguments
        std::vector<int> qubits;
        std::regex qPattern(R"(%q(\d+)|%(\d+))");
        std::smatch qm;
        std::string::const_iterator searchStart(argsStr.cbegin());
        while (std::regex_search(searchStart, argsStr.cend(), qm, qPattern)) {
          int qid = -1;
          if (qm[1].matched) qid = std::stoi(qm[1]);
          else if (qm[2].matched) qid = std::stoi(qm[2]);
          if (qid >= 0) qubits.push_back(qid);
          searchStart = qm.suffix().first;
        }
        
        Op op;
        // Assign rank based on first qubit (can be extended for smarter distribution)
        op.rank = qubits.empty() ? 0 : qubits[0];
        op.op = "APPLY";
        
        if (opType == "apply_h" || opType == "apply_x") {
          op.gate = (opType == "apply_h") ? "H" : "X";
          if (!qubits.empty()) {
            op.args.push_back(qubits[0]);
          }
        } else if (opType == "apply_cx") {
          op.gate = "CX";
          if (qubits.size() >= 2) {
            op.args.push_back(qubits[0]);
            op.args.push_back(qubits[1]);
            // CX operates on two qubits; assign to rank of first qubit
            op.rank = qubits[0];
          }
        } else if (opType == "measure") {
          op.op = "MEASURE";
          if (!qubits.empty()) {
            op.args.push_back(qubits[0]);
            op.qubit = qubits[0];
            op.rank = qubits[0];
          }
        }
        ops.push_back(op);
      } else {
        // Try EPR operations
        std::regex eprAlloc(R"(dqc\.epr_alloc\s+(\d+)\s*,\s*(\d+))");
        std::regex eprConsume(R"(dqc\.epr_consume)");
        
        if (std::regex_search(trimmed, m, eprAlloc)) {
          int r0 = std::stoi(m[1]);
          int r1 = std::stoi(m[2]);
          
          // Generate EPR_ALLOC for both ranks
          Op op0, op1;
          op0.op = op1.op = "EPR_ALLOC";
          op0.ranks = op1.ranks = {r0, r1};
          
          op0.rank = r0;
          op1.rank = r1;
          
          ops.push_back(op0);
          if (r0 != r1) ops.push_back(op1);  // Only add if ranks differ
        } else if (std::regex_search(trimmed, eprConsume)) {
          Op op;
          op.op = "EPR_CONSUME";
          op.rank = 0;  // Coordinator
          ops.push_back(op);
        } else if (std::regex_search(trimmed, std::regex(R"(dqc\.snapshot)"))) {
          Op op;
          op.op = "SNAPSHOT";
          op.rank = 0;
          ops.push_back(op);
        }
      }
    }
    
    return ops;
  }

  static std::string toLower(const std::vector<Op> &ops) {
    std::ostringstream out;
    for (const auto &op : ops) {
      out << op.rank << ": {\"op\":\"" << op.op << "\"";
      if (!op.gate.empty()) out << ",\"gate\":\"" << op.gate << "\"";
      if (!op.args.empty()) {
        out << ",\"args\":[";
        for (size_t i = 0; i < op.args.size(); ++i) {
          if (i > 0) out << ",";
          out << op.args[i];
        }
        out << "]";
      }
      if (!op.ranks.empty()) {
        out << ",\"ranks\":[";
        for (size_t i = 0; i < op.ranks.size(); ++i) {
          if (i > 0) out << ",";
          out << op.ranks[i];
        }
        out << "]";
      }
      if (op.op == "MEASURE" && op.qubit >= 0) {
        out << ",\"qubit\":" << op.qubit;
      }
      out << "}\n";
    }
    return out.str();
  }
};

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cout << "Usage: dqc-compile <input.mlir> [output.dqc]\n";
    std::cout << "Compile DQC MLIR to .dqc executable format.\n";
    return 1;
  }

  std::string inFile = argv[1];
  std::string outFile = (argc >= 3) ? argv[2] : inFile + ".dqc";

  // Read MLIR
  std::ifstream in(inFile);
  if (!in) {
    std::cerr << "Error: cannot open " << inFile << "\n";
    return 1;
  }
  std::string mlir((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();

  std::cout << "[dqc-compile] Parsing MLIR from: " << inFile << "\n";
  
  // Parse and lower
  auto ops = MLIRParser::parse(mlir);
  std::cout << "[dqc-compile] Found " << ops.size() << " operations\n";

  std::string dqc = MLIRParser::toLower(ops);

  // Write output
  std::ofstream out(outFile);
  if (!out) {
    std::cerr << "Error: cannot write to " << outFile << "\n";
    return 1;
  }
  out << dqc;
  out.close();

  std::cout << "[dqc-compile] Lowered to .dqc: " << outFile << "\n";
  std::cout << "[dqc-compile] Output size: " << dqc.size() << " bytes\n";

  return 0;
}
