CC = riscv64-unknown-elf-gcc
LD = riscv64-unknown-elf-ld
OBJCOPY = riscv64-unknown-elf-objcopy

CFLAGS = -nostdlib -ffreestanding -O2 -Wall -Wextra -I. -march=rv64imac -mabi=lp64 -mcmodel=medany
LDFLAGS = -T kernel/linker.ld -nostdlib -static

KERNEL_SRCS = kernel/entry.S kernel/main.c kernel/print.c \
	kernel/trap.S kernel/syscall.c \
	drivers/virtio_blk.c fs/fat32.c kernel/elf_loader.c
KERNEL_OBJS = $(KERNEL_SRCS:.c=.o)
KERNEL_OBJS := $(KERNEL_OBJS:.S=.o)

all: kernel-qemu

# 占位目标：依赖 QEMU 内置 OpenSBI，故不生成独立 sbi-qemu
sbi-qemu:
	@echo "Using default OpenSBI (-bios default); no sbi-qemu built."

kernel-qemu: $(KERNEL_OBJS) kernel/linker.ld
	$(LD) $(LDFLAGS) -o kernel.elf $(KERNEL_OBJS)
	$(OBJCOPY) -O binary kernel.elf kernel.bin
	cp kernel.elf kernel-qemu
	@echo "✅ Build successful! Kernel saved as 'kernel-qemu'"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(KERNEL_OBJS) kernel.elf kernel.bin kernel-qemu

run: kernel-qemu
	@echo "Starting QEMU..."
	@echo "If it doesn't exit automatically, press Ctrl+A then X to quit."
	qemu-system-riscv64 -machine virt -kernel kernel-qemu \
		-m 128M -nographic -bios default

.PHONY: all clean run
