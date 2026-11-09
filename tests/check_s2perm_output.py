#!/usr/bin/env python3
# SPDX-License-Identifier: TBD
"""检查同一 fault 记录的身份/地址，以及故障后同核 survivor 的新进展。"""
import re
import sys


def check(text, probe):
    expected = {
        "fsc": "0xf", "ec": "0x24" if probe == 0 else "0x20",
        "class": "permission", "vm": "0", "vcpu": "0", "pcpu": "0",
        "ipa_valid": "0", "far_valid": "1",
        "far": "0x40210000" if probe == 0 else "0x40211000",
        "elr": "0x40212000" if probe == 0 else "0x40211000",
        "wnr_valid": "1" if probe == 0 else "0",
    }
    if probe == 0:
        expected["wnr"] = "1"
    faults = list(re.finditer(r"^\[hv\] s2 fault: vCPU(\d+) ([^\r\n]*)\r?$", text, re.M))
    if len(faults) != 1 or faults[0].group(1) != "0":
        return False
    fault = faults[0]
    fields = {}
    for token in fault.group(2).split():
        key, separator, value = token.partition("=")
        if not separator or key in fields:
            return False
        fields[key] = value
    if any(fields.get(k) != v for k, v in expected.items()):
        return False
    try:
        esr = int(fields.get("esr", ""), 0)
    except ValueError:
        return False
    if (not 0 <= esr < (1 << 32) or (esr >> 26) != int(fields["ec"], 16) or
            (esr & 0x3f) != int(fields["fsc"], 16)):
        return False
    # 两个探针都是直接访存；RO 的 WnR 必须与原始 ISS 一致，不能是 CM。
    if esr & (1 << 7) or (probe == 0 and
                         (esr & (1 << 8) or ((esr >> 6) & 1) != int(fields["wnr"]))):
        return False
    marker = "SVM: s2perm " + ("RO" if probe == 0 else "XN") + " read ok"
    if not re.search("^" + re.escape(marker) + r"\r?$", text[:fault.start()], re.M):
        return False
    if any(marker in text for marker in ("STORE SURVIVED", "XN ENTERED", "XN RETURNED")):
        return False
    before = 0
    progressed = False
    for match in re.finditer(r"^SVM: s2perm survivor vm=1 vcpu=0 pcpu=0 progress=([0-9a-f]{8})\r?$", text, re.M):
        value = int(match.group(1), 16)
        if match.start() < fault.start():
            before = max(before, value)
        elif value > before:
            progressed = True
    return progressed


if __name__ == "__main__":
    probe = int(sys.argv[1])
    if probe not in (0, 1):
        sys.exit("invalid probe")
    with open(sys.argv[2], encoding="utf-8", errors="replace") as log:
        passed = check(log.read(), probe)
    print("s2perm ordered check: " + ("PASS" if passed else "FAIL"))
    sys.exit(0 if passed else 1)
