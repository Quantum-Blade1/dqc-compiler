// DQC MLIR example: Bell state + measurement
module @bell {
  func.func @main() {
    %q0 = "dqc.qubit"() : () -> i64
    %q1 = "dqc.qubit"() : () -> i64
    
    // Apply H gate to q0
    %h0 = "dqc.apply_h"(%q0) : (i64) -> i64
    
    // Apply CX gate (q0 control, q1 target)
    %cx = "dqc.apply_cx"(%h0, %q1) : (i64, i64) -> i64
    
    // Measure both qubits
    %m0 = "dqc.measure"(%h0) : (i64) -> i64
    %m1 = "dqc.measure"(%q1) : (i64) -> i64
    
    // Snapshot state
    %snap = "dqc.snapshot"() : () -> none
    
    return
  }
}
