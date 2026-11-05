#!/usr/bin/env python3
"""双 Linux 共享 pCPU 的 SYSTEM_OFF 回归；输入镜像沿用 run-qemu.sh 环境变量。"""
import argparse
import codecs
import os
from pathlib import Path
import re
import select
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def timer_counts(text):
    for line in text.splitlines():
        if "arch_timer" in line:
            m = re.fullmatch(r"\s*\d+:\s+(\d+)\s+(\d+)\s+GICv3\s+.*arch_timer\s*", line)
            if m:
                return tuple(map(int, m.groups()))
    raise AssertionError(f"missing two-CPU arch_timer row: {text!r}")


def reply_pattern(token):
    # Whole output lines only: never accept the command's terminal echo.
    return re.compile(r"^" + token + r"_BEGIN\n(.*?)^" + token + r"_END:(\d+)\n", re.M | re.S)


def self_test():
    p = reply_pattern("GATE_1")
    echo = "# printf '\\n%s_BEGIN\\n' GATE_1; echo alive; printf '%s_END:%d\\n' GATE_1 0\n"
    assert p.search(echo) is None
    assert p.search("GATE_0_BEGIN\nalive\nGATE_0_END:0\n") is None
    assert p.search("GATE_1_BEGIN\nalive\n") is None
    m = p.search(echo + "\nGATE_1_BEGIN\nalive\nGATE_1_END:0\n")
    assert m.groups() == ("alive\n", "0")
    assert p.search("GATE_1_BEGIN\nfailed\nGATE_1_END:127\n").group(2) == "127"
    assert timer_counts(" 11: 123 456 GICv3 27 Level arch_timer\n") == (123, 456)
    for bad in ("", "11: 123 GICv3 27 Level arch_timer", "grep arch_timer /proc/interrupts"):
        try:
            timer_counts(bad)
        except AssertionError:
            pass
        else:
            raise AssertionError("accepted invalid timer counters")
    decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
    raw = "up — vm1\r\n".encode()
    assert "".join(decoder.decode(bytes([b])) for b in raw).replace("\r", "") == "up — vm1\n"
    print("parser self-test PASS")


class Qemu:
    def __init__(self, log):
        self.log = open(log, "w")
        self.pending = ""
        self.transcript = ""
        self.decoder = codecs.getincrementaldecoder("utf-8")(errors="replace")
        self.seq = 0
        self.shell = False
        env = dict(os.environ, HV_NR_CPUS="4", SVM_BIN="", SVM_BIN2="")
        self.proc = subprocess.Popen([str(ROOT / "scripts/run-qemu.sh")], cwd=ROOT,
                                     env=env, stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def read(self, timeout):
        ready, _, _ = select.select([self.proc.stdout], [], [], timeout)
        if ready:
            raw = os.read(self.proc.stdout.fileno(), 65536)
            if not raw:
                raise AssertionError(f"QEMU exited: {self.proc.wait(timeout=3)}")
            data = self.decoder.decode(raw).replace("\r", "")
            self.log.write(data)
            self.log.flush()
            self.pending += data
            self.transcript += data

    def wait(self, pattern, timeout=20):
        regex = re.compile(pattern, re.M) if isinstance(pattern, str) else pattern
        end = time.monotonic() + timeout
        while True:
            m = regex.search(self.pending)
            if m:
                self.pending = self.pending[m.end():]
                return m
            if time.monotonic() >= end:
                raise AssertionError(f"timeout: {regex.pattern!r}; tail={self.pending[-300:]!r}")
            self.read(max(0, min(0.2, end - time.monotonic())))

    def send(self, text):
        # Match the existing vSPI transport: stay below the 16-byte guest FIFO.
        data = text.encode()
        for start in range(0, len(data), 4):
            self.proc.stdin.write(data[start:start + 4])
            self.proc.stdin.flush()
            time.sleep(0.003)

    def enter_shell(self):
        if not self.shell:
            self.send("\x14")
            self.wait(r"hv> ")
            self.shell = True

    def topology(self, stopped=False):
        self.enter_shell()
        self.send("vm_list\n")
        self.wait(r"^\s*0\s+0-1\s+on(?:\s+\*)?\n")
        self.wait(r"^\s*1\s+0-1\s+" + ("halted" if stopped else "on") + r"(?:\s+\*)?\n")
        self.wait(r"hv> ")

    def focus(self, vm):
        self.enter_shell()
        self.send(f"vm_console {vm}\n")
        self.wait(r"^\[hv\] console: VM" + str(vm) + r"\n")
        self.shell = False

    def command(self, command, timeout=20):
        assert not self.shell
        self.seq += 1
        token = f"GATE_{self.seq}"
        self.send(f"printf '\\n%s_BEGIN\\n' {token}; {command}; rc=$?; printf '%s_END:%d\\n' {token} $rc\n")
        m = self.wait(reply_pattern(token), timeout)
        assert m.group(2) == "0", f"guest command failed: {command!r}: {m.group(1)!r}"
        return m.group(1)

    def close(self):
        self.proc.terminate()
        try:
            self.proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        self.log.close()


def run(log):
    q = Qemu(log)
    try:
        for _ in range(2):
            q.wait(r"^=== M3\.4: busybox rootfs up — vm[01] interactive shell ===\n", 120)
        q.topology()
        for vm in (0, 1):
            q.focus(vm)
            assert f"hv.vm={vm}" in q.command("cat /proc/cmdline").split(), "wrong console focus"
            assert q.command("cat /sys/devices/system/cpu/online").strip() == "0-1"
        print("boot/topology/console: PASS", flush=True)
        # SYSTEM_OFF cannot return a guest completion marker.
        q.send("poweroff -f\n")
        q.wait(r"^\[hv\] PSCI: VM1 SYSTEM_OFF - stopping all vCPUs\n", 30)
        q.topology(stopped=True)
        q.focus(0)
        # NO_HZ_IDLE legitimately stops arch_timer on an idle CPU. Pin a timer
        # workload on EACH surviving vCPU before requiring counter progress.
        for cpu in (0, 1):
            q.command(f"taskset -c {cpu} sleep 0.1")
            q.command(f"taskset -c {cpu} sh -c 'while :; do sleep 0.1; done' >/dev/null 2>&1 & echo $!")
        before = timer_counts(q.command("grep arch_timer /proc/interrupts"))
        q.command("sleep 3")
        after = timer_counts(q.command("grep arch_timer /proc/interrupts"))
        assert all(b > a for a, b in zip(before, after)), f"timer stalled: {before} -> {after}"
        assert "hv.vm=0" in q.command("cat /proc/cmdline").split()
        assert not re.search(r"Kernel panic|Internal error:|!VEC|\[hv\] BUG", q.transcript), "panic in log"
        print(f"survivor timers: {before} -> {after}; console: PASS", flush=True)
        print("Linux XVM PASS", flush=True)
    finally:
        q.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--log", default="/tmp/linux-xvm.log")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        run(args.log)
