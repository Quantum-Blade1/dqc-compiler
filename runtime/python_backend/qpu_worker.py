#!/usr/bin/env python3
import sys
import json
import math
import random

# Minimal pure-Python statevector simulator for small qubit counts.

class QPU:
    def __init__(self, rank, n_qubits=8):
        self.rank = rank
        self.n = n_qubits
        self.N = 1 << n_qubits
        self.state = [0+0j] * self.N
        self.state[0] = 1+0j

    def apply_h(self, q):
        sqrt2 = math.sqrt(2)
        new = [0+0j] * self.N
        for i in range(self.N):
            bit = (i >> q) & 1
            j = i ^ (1 << q)
            if bit == 0:
                a = self.state[i]
                b = self.state[j]
                new[i] += (a + b) / sqrt2
                new[j] += (a - b) / sqrt2
        self.state = new

    def apply_x(self, q):
        new = [0+0j] * self.N
        for i in range(self.N):
            j = i ^ (1 << q)
            new[j] = self.state[i]
        self.state = new

    def apply_cx(self, ctrl, tgt):
        new = self.state.copy()
        for i in range(self.N):
            if ((i >> ctrl) & 1) == 1:
                j = i ^ (1 << tgt)
                new[j] = self.state[i]
                new[i] = 0+0j
        self.state = new

    def measure(self, q):
        p0 = 0.0
        for i in range(self.N):
            if ((i >> q) & 1) == 0:
                p0 += abs(self.state[i])**2
        r = random.random()
        outcome = 0 if r < p0 else 1
        if outcome == 0:
            for i in range(self.N):
                if ((i >> q) & 1) == 1:
                    self.state[i] = 0+0j
            if p0 > 0:
                norm = math.sqrt(p0)
                self.state = [amp / norm for amp in self.state]
        else:
            p1 = 1.0 - p0
            for i in range(self.N):
                if ((i >> q) & 1) == 0:
                    self.state[i] = 0+0j
            if p1 > 0:
                norm = math.sqrt(p1)
                self.state = [amp / norm for amp in self.state]
        return int(outcome)

    def state_snapshot(self):
        probs = [abs(a)**2 for a in self.state]
        return probs[:min(16, len(probs))]


def main():
    if len(sys.argv) < 3:
        print(json.dumps({"error":"usage: qpu_worker.py <rank> <n_qubits>"}))
        return 2
    rank = int(sys.argv[1])
    n_qubits = int(sys.argv[2])
    qpu = QPU(rank, n_qubits)

    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            cmd = json.loads(line)
        except Exception as e:
            print(json.dumps({"error":"invalid-json","msg":str(e)}))
            sys.stdout.flush()
            continue
        op = cmd.get("op")
        if op == "APPLY":
            gate = cmd.get("gate")
            args = cmd.get("args", [])
            if gate == "H":
                qpu.apply_h(int(args[0]))
                print(json.dumps({"ok":True}))
            elif gate == "X":
                qpu.apply_x(int(args[0]))
                print(json.dumps({"ok":True}))
            elif gate == "CX":
                qpu.apply_cx(int(args[0]), int(args[1]))
                print(json.dumps({"ok":True}))
            else:
                print(json.dumps({"error":"unknown-gate"}))
            sys.stdout.flush()
        elif op == "MEASURE":
            q = int(cmd.get("qubit"))
            out = qpu.measure(q)
            print(json.dumps({"result":out}))
            sys.stdout.flush()
        elif op == "SNAPSHOT":
            print(json.dumps({"probs":qpu.state_snapshot()}))
            sys.stdout.flush()
        elif op == "EXIT":
            print(json.dumps({"ok":True}))
            sys.stdout.flush()
            break
        else:
            print(json.dumps({"error":"unknown-op"}))
            sys.stdout.flush()


if __name__ == '__main__':
    main()
