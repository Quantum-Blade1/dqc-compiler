DQC Runtime (phase 1)

This is a minimal runtime demo that simulates multiple QPUs locally.

Build (standalone):

```bash
mkdir -p runtime/build && cd runtime/build
cmake ..
cmake --build . -- -j$(nproc)

# Run: (use any text file as .dqc placeholder)
./dqc-run ../test_stub.dqc 4
```
