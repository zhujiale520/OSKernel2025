# Minimal build + run environment for OSKernel2025
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Base packages and cross toolchain
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    git \
    wget \
    python3 \
    python3-pip \
    ninja-build \
    qemu-system-misc \
    qemu-user-static \
    gcc-riscv64-unknown-elf \
    binutils-riscv64-unknown-elf \
    && rm -rf /var/lib/apt/lists/*

# Optional: add a non-root user (comment out if not needed)
#RUN useradd -m -s /bin/bash dev && echo "dev ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers
#USER dev
#WORKDIR /home/dev

# Workspace location inside container
WORKDIR /workspace/oskernel2025
COPY . /workspace/oskernel2025

# Build kernel by default (adjust if you prefer a pure build image)
RUN make

CMD ["/bin/bash"]
