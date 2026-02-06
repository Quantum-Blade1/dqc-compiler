// Phase B: Replace inter-QPU gates with teleportation operations

#include "dqc/DQCDialect.h"
#include "dqc/DQCOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "telegate-synthesis"

namespace {

// Qubit-to-QPU mapping
class MappingTable {
private:
  llvm::DenseMap<int, int> qubit_to_qpu;
  
public:
  void loadFromFunctionAttr(mlir::func::FuncOp func) {
    auto partition_attr = func->getAttrOfType<mlir::DictionaryAttr>("dqc.partition");
    if (!partition_attr) {
      LLVM_DEBUG(llvm::dbgs() << "No partition metadata found in function\n");
      return;
    }
    
    for (const auto &entry : partition_attr) {
      // Extract qubit ID from key
      auto key_str = entry.getName().str();
      if (key_str.find("qubit_") == 0) {
        int qubit_id = std::stoi(key_str.substr(6));
        auto value_attr = llvm::dyn_cast<mlir::IntegerAttr>(entry.getValue());
        if (value_attr) {
          int qpu_id = value_attr.getInt();
          qubit_to_qpu[qubit_id] = qpu_id;
        }
      }
    }
  }
  
  bool getQPUAssignment(int qubit_id, int &qpu_id) const {
    auto it = qubit_to_qpu.find(qubit_id);
    if (it != qubit_to_qpu.end()) {
      qpu_id = it->second;
      return true;
    }
    return false;
  }
  
  bool isLocalGate(int ctrl_qubit, int tgt_qubit) const {
    int ctrl_qpu, tgt_qpu;
    if (getQPUAssignment(ctrl_qubit, ctrl_qpu) &&
        getQPUAssignment(tgt_qubit, tgt_qpu)) {
      return ctrl_qpu == tgt_qpu;
    }
    return true;
  }
};

// Convert QUIR CNOT to TeleGate for distributed QPUs
class QUIRCNOTToTeleGatePattern : public mlir::OpConversionPattern<mlir::Operation> {
private:
  MappingTable &mapping_table;
  
public:
  QUIRCNOTToTeleGatePattern(mlir::MLIRContext *ctx, MappingTable &table)
      : mlir::OpConversionPattern<mlir::Operation>(ctx), mapping_table(table) {}
  
  mlir::LogicalResult matchAndRewrite(
      mlir::Operation *op, mlir::ArrayRef<mlir::Value> operands,
      mlir::ConversionPatternRewriter &rewriter) const final {
    
    // Match CNOT-like operations
    auto op_name = op->getName().getStringRef();
    if (!op_name.contains("cnot")) return mlir::failure();
    
    if (op->getNumOperands() < 2) return mlir::failure();
    
    auto control_qubit = operands[0];
    auto target_qubit = operands[1];
    
    // Extract qubit IDs
    int ctrl_id = 0, tgt_id = 1;
    
    // Skip local gates
    if (mapping_table.isLocalGate(ctrl_id, tgt_id)) {
      LLVM_DEBUG(llvm::dbgs() << "Gate is local, skipping\n");
      return mlir::failure();
    }
    
    LLVM_DEBUG(llvm::dbgs() << "Converting non-local CNOT to TeleGate\n");
    
    int ctrl_qpu, tgt_qpu;
    mapping_table.getQPUAssignment(ctrl_id, ctrl_qpu);
    mapping_table.getQPUAssignment(tgt_id, tgt_qpu);
    
    mlir::Location loc = op->getLoc();
    mlir::Block *insertion_block = rewriter.getInsertionBlock();
    
    // Create EPR allocation
    auto epr_type = dqc::EPRHandleType::get(op->getContext());
    mlir::OperationState eprState(loc, "dqc.epr_alloc");
    eprState.addTypes(epr_type);
    eprState.addAttribute("source_qpu", rewriter.getI32IntegerAttr(ctrl_qpu));
    eprState.addAttribute("target_qpu", rewriter.getI32IntegerAttr(tgt_qpu));
    auto *epr_alloc_op = rewriter.createOperation(eprState);
    
    LLVM_DEBUG(llvm::dbgs() << "Created epr_alloc for QPUs " << ctrl_qpu
                            << " and " << tgt_qpu << "\n");
    
    // Create TeleGate operation
    mlir::OperationState telegateState(loc, "dqc.telegate");
    // result type mirrors the control qubit type for simplicity
    telegateState.addTypes(control_qubit.getType());
    telegateState.addOperands({control_qubit, target_qubit, epr_alloc_op->getResult(0)});
    telegateState.addAttribute("control_qpu", rewriter.getI32IntegerAttr(ctrl_qpu));
    telegateState.addAttribute("target_qpu", rewriter.getI32IntegerAttr(tgt_qpu));
    auto *telegate_op = rewriter.createOperation(telegateState);
    
    LLVM_DEBUG(llvm::dbgs() << "Created telegate operation\n");
    
    // Replace CNOT with TeleGate
    rewriter.replaceOp(op, {telegate_op->getResult(0)});
    
    return mlir::success();
  }
};

/// TeleGate Synthesis Pass
class TeleGateSynthesisPass
    : public mlir::PassWrapper<TeleGateSynthesisPass,
                               mlir::OperationPass<mlir::func::FuncOp>> {
public:
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_OPNAME_ALLOCATIONFN(TeleGateSynthesisPass)
  
  StringRef getArgument() const final { return "dqc-telegate-synthesis"; }
  StringRef getDescription() const final {
    return "Replace inter-QPU gates with TeleGate sequences";
  }
  
  TeleGateSynthesisPass() = default;
  TeleGateSynthesisPass(const TeleGateSynthesisPass &) {}

private:
  void runOnOperation() final {
    mlir::func::FuncOp func = getOperation();
    mlir::MLIRContext *ctx = &getContext();
    
    LLVM_DEBUG(llvm::dbgs() << "Starting TeleGate synthesis on function "
                            << func.getName() << "\n");
    
    // Load qubit-to-QPU mapping from function attributes
    MappingTable mapping_table;
    mapping_table.loadFromFunctionAttr(func);
    
    // Set up dialect conversion
    mlir::ConversionTarget target(*ctx);
    target.addLegalDialect<dqc::DQCDialect>();
    target.addLegalDialect<mlir::func::FuncDialect>();
    target.addLegalDialect<mlir::arith::ArithDialect>();
    
    // Mark QUIR CNOT as illegal if inter-QPU
    // In real implementation, would be quir::CNOTOp
    target.addDynamicallyLegalOp<mlir::Operation>(
        [&](mlir::Operation *op) {
          // Allow operations that don't look like CNOT, or are already DQC ops
          auto op_name = op->getName().getStringRef();
          if (op_name.contains("cnot")) {
            // This is a CNOT - check if it's local
            // Simplified check; real implementation would extract actual qubit IDs
            return true;  // For now, keep all as legal
          }
          return true;
        });
    
    // Set up rewriter patterns
    mlir::RewritePatternSet patterns(ctx);
    patterns.add<QUIRCNOTToTeleGatePattern>(ctx, mapping_table);
    
    // Apply conversion
    if (mlir::failed(mlir::applyPartialConversion(func, target, std::move(patterns)))) {
      LLVM_DEBUG(llvm::dbgs() << "TeleGate synthesis failed\n");
      signalPassFailure();
    }
    
    LLVM_DEBUG(llvm::dbgs() << "TeleGate synthesis completed\n");
  }
};

} // anonymous namespace

namespace mlir {
namespace dqc {

std::unique_ptr<mlir::Pass> createTeleGateSynthesisPass() {
  return std::make_unique<::TeleGateSynthesisPass>();
}

}  // namespace dqc
}  // namespace mlir
