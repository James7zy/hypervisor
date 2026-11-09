#!/usr/bin/env python3
# SPDX-License-Identifier: TBD
"""check_s2perm_output 的正/反日志自检，不启动 QEMU。"""
from check_s2perm_output import check

checks = 0
for probe in (0, 1):
    kind = "RO" if probe == 0 else "XN"
    far = "0x40210000" if probe == 0 else "0x40211000"
    elr = "0x40212000" if probe == 0 else "0x40211000"
    fault = (f"[hv] s2 fault: vCPU0 ipa=0x0 fsc=0xf ec={'0x24' if probe == 0 else '0x20'} "
             f"class=permission vm=0 vcpu=0 pcpu=0 ipa_valid=0 far_valid=1 far={far} "
             f"elr={elr} esr=0x0 wnr_valid={1 if probe == 0 else 0} wnr=1\n")
    progress = "SVM: s2perm survivor vm=1 vcpu=0 pcpu=0 progress="
    pre = progress + "00000001\n"
    post = progress + "00000002\n"
    good = pre + f"SVM: s2perm {kind} read ok\n" + fault + post
    assert check(good, probe)
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
    for bad in bad_logs:
        assert not check(bad, probe), bad
        checks += 1
print(f"check_s2perm_output PASS ({checks} checks)")
