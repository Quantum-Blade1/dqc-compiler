// DQC dialect IR tests

// EPR allocation test
func.func @test_epr_alloc() {
  // Allocate entangled pair
  %epr = dqc.epr_alloc 0, 1 : i32, i32 -> !dqc.epr_handle
  return
}

// TeleGate operation test
func.func @test_telegate() {
  return
}

// Partition metadata test
func.func @test_partition_info() {
  return
}
