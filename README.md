# OSKernel2025 最小内核说明

## 项目简介
- 平台：RISC-V virt（QEMU 7.0.0 测试）
- 目标：生成可在 QEMU 下启动的最小内核，串口输出启动信息后通过 SBI 正常关机。
- 默认使用 QEMU 自带 OpenSBI（`-bios default`），不单独构建 `sbi-qemu`。

## 目录结构
- kernel/：入口、链接脚本、打印与主函数
- drivers/, fs/, lib/, tests/：后续阶段预留

## 构建与运行
```bash
make clean
make all          # 生成 kernel-qemu (ELF) 与 kernel.bin
make run          # 启动 QEMU，看到串口输出后自动关机
```

QEMU 运行参数（比赛评测示例）：
```
qemu-system-riscv64 -machine virt -kernel kernel-qemu \
	-m 128M -nographic -smp 2 -bios default \
	-drive file=sdcard.img,if=none,format=raw,id=x0 \
	-device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0 \
	-device virtio-net-device,netdev=net -netdev user,id=net
```

## 现在的内核行为
1. 汇编入口设置栈并跳转 `main`
2. 串口输出启动横幅与团队信息
3. 调用 SBI 关机

## 后续计划（简要）
- 第2阶段：物理内存管理
- 第3阶段：中断/时钟
- 第4阶段：virtio-blk + FAT32 扫描 SD 卡测试点
- 第5阶段：ELF 加载与用户态执行
- 第6阶段：系统调用/进程调度
