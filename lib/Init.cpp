// Initialize DQC compiler and register dialects

#include "dqc/DQCDialect.h"
#include "dqc/DQCOps.h"
#include "dqc/Passes.h"
#include "mlir/IR/Dialect.h"

using namespace mlir;
using namespace dqc;

/// Initialize the DQC compiler by registering dialects and passes
void dqc::initDQCCompiler(mlir::MLIRContext *context) {
  context->loadDialect<DQCDialect>();
  dqc::registerDQCPasses();
}
