# OSKernel2025 操作系统内核

## 项目简介
本项目是一个基于 RISC-V 架构的操作系统内核，旨在参加 2025 年操作系统内核实现赛。内核运行在 QEMU virt 平台上，支持基本的进程管理、内存管理、文件系统以及系统调用，能够加载并运行用户态测试程序。

## 团队信息
- **Team**: 404notfound
- **School**: Xi'an Jiaotong University

## 功能特性
- **启动与初始化**: 支持从 OpenSBI 启动，初始化串口、中断向量表。
- **驱动支持**: 实现了 VirtIO Block 设备驱动，用于访问磁盘。
- **文件系统**: 实现了只读 FAT32 文件系统，支持文件查找、读取和遍历。
- **进程管理**:
  - 支持 ELF 格式的可执行文件加载。
  - 实现了基本的进程控制块 (PCB) 和调度器。
  - 支持用户态与内核态的切换 (Context Switch)。
  - 实现了简单的轮转调度算法。
- **系统调用**:
  - 实现了基础的系统调用框架。
  - 支持 `write`, `getpid`, `exit`, `brk`, `open`, `read`, `close` 等常用系统调用。
  - 能够自动扫描并运行 SD 卡中的测试用例。

## 目录结构
```
.
├── drivers/        # 设备驱动 (virtio_blk)
├── fs/             # 文件系统 (fat32)
├── kernel/         # 内核核心代码
│   ├── entry.S     # 内核入口汇编
│   ├── main.c      # 内核主函数
│   ├── proc.c      # 进程管理与调度
│   ├── syscall.c   # 系统调用处理
│   ├── trap.S      # 异常与中断处理汇编
│   ├── elf_loader.c# ELF 文件加载器
│   └── ...
├── Makefile        # 构建脚本
└── README.md       # 项目说明
```

## 构建与运行

### 环境要求
- RISC-V 64位交叉编译工具链 (`riscv64-unknown-elf-gcc` 等)
- QEMU 模拟器 (`qemu-system-riscv64`)

### 编译
```bash
make clean
make all
```
这将生成 `kernel-qemu` (ELF 格式) 和 `kernel.bin` (二进制格式)。

### 运行
```bash
make run
```
该命令将启动 QEMU，加载内核以及 `testsuits-for-oskernel/sdcard.img` 镜像。内核启动后会自动扫描 SD 卡中的测试程序并依次执行。

## 测试流程
1. 内核启动，打印欢迎信息。
2. 初始化 VirtIO 块设备和 FAT32 文件系统。
3. 遍历 SD 卡根目录，查找测试用例文件。
4. 加载测试用例到内存，创建用户进程并执行。
5. 测试用例执行完毕后（通过 `exit` 系统调用），内核调度器会自动加载下一个测试用例。
6. 所有测试执行完毕后，内核自动关机。

## 比赛评测命令参考
```bash
qemu-system-riscv64 -machine virt -kernel kernel-qemu \
    -m 128M -nographic -smp 2 -bios default \
    -drive file=sdcard.img,if=none,format=raw,id=x0 \
    -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
    -device virtio-net-device,netdev=net -netdev user,id=net
```
