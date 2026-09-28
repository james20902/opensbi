#
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2019 Western Digital Corporation or its affiliates.
#

# Compiler pre-processor flags
platform-cppflags-y =

# C Compiler and assembler flags.
platform-cflags-y =
platform-asflags-y =

#
# On a no-FP ISA every "fp = true" assignment in lib/sbi/sbi_trap_ldst.c is
# compiled out by #ifdef __riscv_flen, but clang's -Wsometimes-uninitialized
# does not follow that far and reports the store emulation path as leaving
# 'val' uninitialized. GCC stays quiet. Keep it a warning rather than an
# error so the upstream false positive does not stop this platform building.
#
ifeq ($(CC_IS_CLANG),y)
platform-cflags-y += -Wno-error=sometimes-uninitialized
endif

# Linker flags: additional libraries and object files that the platform
# code needs can be added here
platform-ldflags-y =

#
# Command for platform specific "make run"
# Useful for development and debugging on plaftform simulator (such as QEMU)
#
# platform-runcmd = your_platform_run.sh

#
# Platform RISC-V XLEN, ABI, ISA and Code Model configuration.
#
# No floating point: the machine instantiates a plain rv32imac CPU, so pin
# the ISA rather than letting OpenSBI fall back to its rv32imafdc default.
# Keep this in step with the "riscv,isa" property in zmachine.dts and with
# the CPU the QEMU machine instantiates.
#
PLATFORM_RISCV_XLEN = 32
PLATFORM_RISCV_ABI = ilp32
PLATFORM_RISCV_ISA = rv32imac_zicsr_zifencei
PLATFORM_RISCV_CODE_MODEL = medany

# Space separated list of object file names to be compiled for the platform
platform-objs-y += platform.o

#
# The machine description lives in zmachine.dts. It is compiled to a DTB and
# embedded in the firmware (see FW_FDT_PATH below), so OpenSBI always has a
# device tree to probe the zdevice from and to hand to the next boot stage,
# even when the previous stage passes none.
#
FW_FDT_PATH = $(platform_build_dir)/zmachine.dtb

# Room for the /reserved-memory node that final_init adds to the DTB
FW_FDT_PADDING = 1024

#
# Dynamic firmware configuration.
#
FW_DYNAMIC=n

#
# Jump firmware configuration.
#
# OpenSBI does not carry the kernel: something else loads it to FW_JUMP_ADDR
# and OpenSBI just jumps there in S-mode once it is done initializing. The
# kernel lives 48 KiB into the payload RAM at 0xa0000000, not in main RAM.
# Keep FW_JUMP_ADDR equal to the link address in kernel.ld and to
# ZMACHINE_KERNEL_BASE in the QEMU machine.
#
# The device tree is relocated to FW_JUMP_FDT_ADDR before the jump and its
# address arrives in a1. That stays in main RAM, so the blob cannot land on
# top of the kernel however large the kernel grows.
#
FW_JUMP=y
FW_JUMP_ADDR=0xa000c000
FW_JUMP_FDT_ADDR=0x81000000

#
# Firmware with payload configuration.
#
FW_PAYLOAD=n
