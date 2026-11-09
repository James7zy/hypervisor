# 提交信息规范

本仓库的每一次提交都按本文件写。规则由
[`check-commit-message`](check-commit-message) 强制检查，装了本地钩子后 `git commit`
当场报错，不用等 CI。

## 格式

```
[领域][专项] 组件: 摘要
```

```
[kernel][fix] sched: initialize running_vcpu before the first switch
[arch_virt][refactor] stage2: split the walk from the permission check
[scripts][feat] commit: add commit message validator
```

- **领域**（第一个方括号）必须取自下面的领域表，纯英文小写。
- **专项**（第二个方括号）自由取值，建议 `feat` `fix` `refactor` `perf` `test` `docs`
  `build`。检查器不查这一格，但同义写法（`fix` / `bugfix` / `修复`）只能留一个，
  否则这一格没有信息量。
- **组件**写在标签之后、冒号之前，如 `sched:`、`common/config:`、`stage2:`。
  只允许英文、数字和 `.` `_` `/` `-`。
- **摘要**中英文都行，整行 ≤ 72 字符，祈使句，句尾不加句号。方括号内只能是英文。

## 领域表

| 领域 | 覆盖范围 |
| --- | --- |
| `kernel` | `hypervisor/common/`：sched、vm、mmu、io 等共用内核 |
| `arch` | `hypervisor/arch/arm64`、`arch/x86`：启动、异常、CPU online、寄存器 |
| `arch_virt` | Stage-2、EL2 虚拟化扩展、SVM / VT-x |
| `hypercall` | hypercall 分发与参数校验 |
| `device_virt` | `hypervisor/dm/`、`devicemodel/`：vuart、console、虚拟设备 |
| `base_libs` | `hypervisor/lib/`、`hypervisor/include/`：print、string、fdt |
| `hypervisor` | hypervisor 树的主干改动：Makefile、Kconfig、链接脚本 |
| `build` | 顶层 Makefile、编译脚本、工具链 |
| `ci` | gate 脚本与 CI 配置（含 `make test` 的聚合项） |
| `scripts` | `scripts/` 下的运行、检查脚本 |
| `docs` | `docs/`、README、AGENTS.md |
| `tests` | `tests/`：host 测试与 QEMU 场景 |
| `guest` | `guest/`：VM 镜像配置、DTS |
| `tools` | `tools/` |
| `treewide` | 跨六七个以上领域的机械改动（重命名、错误码统一等） |

## 一次改了多个领域

主题行只写主要的那个领域，其余列在标签块里：第 3 行起，遇空行为止。

```
[hypercall][fix] dispatch: reject HVC from a vCPU that does not own the VM

[arch][fix] arm64: mask the trap before touching the handler table
[base_libs][refactor] string: drop the unused strnlen
```

- 次要领域只是跟着改了个调用点 → 不用列。
- 跨了六七个领域 → 主题行用 `[treewide]`，也不用列。
- 块内以 `[` 开头的行同样受上面的格式约束；描述行别以 `[` 开头，缩进一格即可
  （不以 `[` 开头的行不检查）。

空行之后的正文完全不检查，随便写。但请写清楚**为什么**这么改——半年后有用的是这个。
正文按 72 列换行，尾部标签沿用本仓库的 `Test:` / `Bug:` / `Fixed:`（见
[`docs/reference/commit-conventions.md`](../../docs/reference/commit-conventions.md)）。

## 不检查的提交

`Merge`、`Revert`、`fixup!`、`squash!`、`amend!` 开头的提交跳过检查。
`Revert` 沿用 git 默认写法，正文写 `This reverts commit <hash>.` 和回滚原因。

## 安装本地钩子

```sh
./scripts/commit-message/install-git-hooks.sh
```

## 直接检查

```sh
scripts/commit-message/check-commit-message .git/COMMIT_EDITMSG   # 某个消息文件
scripts/commit-message/check-commit-message                       # 当前 HEAD 的提交信息
```
