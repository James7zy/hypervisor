#!/usr/bin/env python3
# SPDX-License-Identifier: TBD
"""check_s2perm_output 的正/反日志自检，不启动 QEMU。"""
from check_s2perm_output import check

checks = 0
failures = []
for probe in (0, 1):
    kind = "RO" if probe == 0 else "XN"
    far = "0x40210000" if probe == 0 else "0x40211000"
    elr = "0x40212000" if probe == 0 else "0x40211000"
    esr = 0x93c1804f if probe == 0 else 0x8200000f
    fault = (f"[hv] s2 fault: vCPU0 ipa=0x0 fsc=0xf ec={'0x24' if probe == 0 else '0x20'} "
             f"class=permission vm=0 vcpu=0 pcpu=0 ipa_valid=0 far_valid=1 far={far} "
             f"elr={elr} esr={esr:#x} wnr_valid={1 if probe == 0 else 0} wnr={1 if probe == 0 else 0}\n")
    progress = "SVM: s2perm survivor vm=1 vcpu=0 pcpu=0 progress="
    pre = progress + "00000001\n"
    post = progress + "00000002\n"
    good = pre + f"SVM: s2perm {kind} read ok\n" + fault + post
    for positive in (good, good.replace("\n", "\r\n"), good.removeprefix(pre),
                     good.rstrip("\n")):
        assert check(positive, probe), positive
        checks += 1
    bad_logs = [
        good.replace(fault, ""), good.replace("fsc=0xf", "fsc=0x7"),
        good + "SVM: s2perm RO STORE SURVIVED\n",
        good + "SVM: s2perm XN ENTERED\n", good + "SVM: s2perm XN RETURNED\n",
        good.replace(post, ""), good.replace(post, pre),
        good.replace("class=permission vm=0", "class=permission vm=1"),
        good.replace("vcpu=0 pcpu=0 ipa", "vcpu=1 pcpu=0 ipa"),
        good.replace("pcpu=0 ipa", "pcpu=1 ipa"),
        good.replace("far=" + far, "far=0xdead"),
        good.replace("elr=" + elr, "elr=0xdead"),
        good.replace(f"SVM: s2perm {kind} read ok\n", ""),
        # 两条错记录不能拼成一条合法身份/地址记录。
        good.replace(fault, fault.replace("vm=0", "vm=1") + fault.replace("far=" + far, "far=0xdead")),
    ]
    for case, bad in enumerate(bad_logs):
        if check(bad, probe):
            failures.append(f"{kind}: existing negative {case}")
        checks += 1

    # 每种事件必须是原始行，不能从引用文本中提取正向证据。
    read = f"SVM: s2perm {kind} read ok\n"
    regressions = {
        "quoted progress": good.replace(post, "DIAGNOSTIC REPLAY: " + post),
        "quoted read": good.replace(read, "DIAGNOSTIC REPLAY: " + read),
        "quoted fault": good.replace(fault, "DIAGNOSTIC REPLAY: " + fault),
        "progress trailing quote": good.replace(post, post.rstrip("\n") + "'\n"),
        "read trailing quote": good.replace(read, read.rstrip("\n") + "'\n"),
        "duplicate identity": good.replace("class=permission vm=0", "class=permission vm=1 vm=0"),
        "duplicate same value": good.replace("class=permission vm=0", "class=permission vm=0 vm=0"),
        "duplicate ESR": good.replace(f"esr={esr:#x}", f"esr=0x0 esr={esr:#x}"),
        "duplicate FAR": good.replace("far=" + far, "far=0xdead far=" + far),
        "zero ESR": good.replace(f"esr={esr:#x}", "esr=0x0"),
        "wrong raw EC": good.replace(f"esr={esr:#x}", f"esr={esr ^ (4 << 26):#x}"),
        "wrong printed EC": good.replace("ec=0x24" if probe == 0 else "ec=0x20", "ec=0x25"),
        "wrong raw FSC": good.replace(f"esr={esr:#x}", f"esr={esr ^ 8:#x}"),
        "S1PTW": good.replace(f"esr={esr:#x}", f"esr={esr | (1 << 7):#x}"),
        "missing ESR": good.replace(f"esr={esr:#x} ", ""),
        "invalid ESR": good.replace(f"esr={esr:#x}", "esr=invalid"),
        "oversize ESR": good.replace(f"esr={esr:#x}", f"esr={esr | (1 << 32):#x}"),
        "negative ESR": good.replace(f"esr={esr:#x}", f"esr={esr - (1 << 32):#x}"),
        "not greater than all prefault progress": pre.replace("00000001", "00000003") + good,
    }
    if probe == 0:
        regressions.update({
            "wrong raw WnR": good.replace(f"esr={esr:#x}", f"esr={esr & ~(1 << 6):#x}"),
            "wrong printed WnR": good.replace("wnr=1", "wnr=0"),
            "CM": good.replace(f"esr={esr:#x}", f"esr={esr | (1 << 8):#x}"),
        })
    for name, bad in regressions.items():
        if check(bad, probe):
            failures.append(f"{kind}: {name}")
        checks += 1
print(f"check_s2perm_output: {checks} checks, {len(failures)} failures")
assert not failures, "\n".join(failures)
print("check_s2perm_output PASS")
