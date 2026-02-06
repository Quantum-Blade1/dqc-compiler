// DQC MLIR example: Distributed entanglement with EPR allocation
func.func @stage0() {
  // Allocate EPR pair between ranks 0 and 1
  %0 = dqc.epr_alloc 0, 1 : !dqc.epr_handle
  // Allocate EPR pair between ranks 1 and 2
  %1 = dqc.epr_alloc 1, 2 : !dqc.epr_handle
  // Consume EPR pairs
  dqc.epr_consume %0 : !dqc.epr_handle
  dqc.epr_consume %1 : !dqc.epr_handle
  return
}
