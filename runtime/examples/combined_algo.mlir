// DQC MLIR: Combined gates + distributed entanglement
func.func @distributed_algorithm() {
  // Local quantum operations on rank 0
  %q0 = "dqc.qubit"() : () -> i64
  %h = "dqc.apply_h"(%q0) : (i64) -> i64
  
  // Allocate EPR pair between rank 0 and rank 1
  %epr01 = dqc.epr_alloc 0, 1 : !dqc.epr_handle
  
  // Apply CX as entangling operation
  %q1 = "dqc.qubit"() : () -> i64
  %cx = "dqc.apply_cx"(%h, %q1) : (i64, i64) -> i64
  
  // Allocate another EPR pair between rank 1 and rank 2
  %epr12 = dqc.epr_alloc 1, 2 : !dqc.epr_handle
  
  // Measure qubits
  %m0 = "dqc.measure"(%h) : (i64) -> i64
  %m1 = "dqc.measure"(%q1) : (i64) -> i64
  
  // Consume EPR pairs
  dqc.epr_consume %epr01 : !dqc.epr_handle
  dqc.epr_consume %epr12 : !dqc.epr_handle
  
  // Snapshot final state
  %snap = "dqc.snapshot"() : () -> none
  
  return
}
