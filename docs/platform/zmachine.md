zmachine Platform
=================

The **zmachine** platform is a bare minimum RV32 machine — one hart, RAM, a
CLINT and a single 8250 UART — plus one custom peripheral, the **zdevice**.
There is deliberately no interrupt controller: nothing on this machine raises
an external interrupt.

It is meant to be paired with a matching QEMU machine model. OpenSBI is the
M-mode firmware and hands off to a separately loaded S-mode kernel with
*FW_JUMP*.

Memory map
----------

| Base         | Size    | Device                                |
| ------------ | ------- | ------------------------------------- |
| `0x02000000` | 64 KiB  | CLINT (MSWI at +0x0, MTIMER at +0x4000) |
| `0x10000000` | 256 B   | 8250/16550 UART, 3686400 Hz input clock |
| `0x60000000` | 64 KiB  | zdevice                               |
| `0x80000000` | 128 MiB | RAM                                   |

The CPU is `rv32imac_zicsr_zifencei` with Sv32. There is no floating point.

These addresses appear in three places that must agree with each other: the
QEMU machine model, `platform/zmachine/zmachine.dts`, and the constants at
the top of `platform/zmachine/platform.c`.

The zdevice
-----------

The zdevice is a 64 KiB MMIO window holding two control registers and a data
scratchpad:

| Offset            | Width   | Name                                     |
| ----------------- | ------- | ---------------------------------------- |
| `0x0040`          | 16 bits | XOR key                                  |
| `0x0042`          | 16 bits | Left shift toggle (0 = off, non-zero = on) |
| `0x0100`–`0xffff` | —       | Data scratchpad                          |

Everything below `0x0100` that is not one of the two control registers is
reserved.

A halfword written to the data scratchpad is transformed before it is stored:

```
stored = (written ^ key) << (shift ? 2 : 0)
```

Reading the scratchpad returns the stored — already transformed — halfword,
so a value is never transformed twice. The control registers themselves are
plain read/write storage and are not transformed.

Because the key is two bytes wide, the halfword is the natural access size
for the scratchpad. The QEMU model should declare `.impl.min_access_size = 2`
and `.impl.max_access_size = 2` (or handle narrower accesses explicitly) so
that a byte access cannot see half a transformed value.

The shift drops the top two bits: with the toggle on, `0xffff ^ 0x0000` is
stored as `0xfffc`, not as a 18-bit value.

### Driver

The driver lives in `lib/utils/zdevice/` and is built for any platform that
enables it, not just this one:

| File                                  | Purpose                          |
| ------------------------------------- | -------------------------------- |
| `include/sbi_utils/zdevice/zdevice.h` | Register map and API             |
| `lib/utils/zdevice/zdevice.c`         | Register access, self test       |
| `lib/utils/zdevice/fdt_zdevice.c`     | Probing from a device tree node  |

Three Kconfig symbols control it:

* `CONFIG_ZDEVICE` — build the driver.
* `CONFIG_FDT_ZDEVICE` — probe the device from a `zmachine,zdevice` DT node
  instead of a compiled-in address. Selected by this platform.
* `CONFIG_ZDEVICE_SELFTEST` — after probing, write a handful of values
  through the device and check the result against the specification above.
  Enabled in this platform's `configs/defconfig`; it is the quickest way to
  tell whether the QEMU model is behaving.

The device tree is the single source of truth for where the peripheral lives.
A device tree with no zdevice node just boots without one rather than poking
at an address that may not be backed by anything, and a probe failure is
reported but never fatal.

`zdevice_init()` registers the MMIO window with the root domain as
`SBI_DOMAIN_MEMREGION_MMIO | SBI_DOMAIN_MEMREGION_SHARED_SURW_MRW`, so the
S-mode payload can drive the device directly with ordinary loads and stores —
it shows up in the boot log as one of the `Domain0 Region` lines.

Building
--------

```
make PLATFORM=zmachine \
     LLVM=/opt/homebrew/opt/llvm/bin/ LD=$(which ld.lld)
```

This is a *FW_JUMP* firmware: it does not carry the kernel. OpenSBI brings the
machine up and then jumps to `FW_JUMP_ADDR` in S-mode, leaving whoever loaded
the firmware to place the kernel there as well. `FW_JUMP_ADDR` is `0x80200000`
and has to stay equal to the link address in `kernel.ld`.

The device tree is built from `zmachine.dts`, embedded in the firmware via
`FW_FDT_PATH`, and relocated to `FW_JUMP_FDT_ADDR` (`0x81000000`) before the
jump. The kernel is entered with the hart id in `a0` and that address in `a1`.
`0x81000000` is 14 MiB clear of the kernel entry, so the blob cannot land on
top of a kernel that grows; move it further out if the kernel ever gets that
large.

### Toolchain notes

OpenSBI links itself as a PIE. A bare-metal GNU toolchain configured without
shared library support (Homebrew's `riscv64-elf-binutils`, for instance)
reports `-pie not supported` and cannot build OpenSBI at all, so use clang
with `ld.lld` as shown above.

On a no-FP ISA, clang's `-Wsometimes-uninitialized` misfires on
`lib/sbi/sbi_trap_ldst.c`: every `fp = true` assignment there sits behind
`#ifdef __riscv_flen` and is compiled out, but the analysis does not follow
that far and reports the store emulation path as leaving `val` uninitialized.
GCC stays quiet. `objects.mk` demotes that one diagnostic from an error back
to a warning for clang builds; nothing else is relaxed.

Running
-------

```
qemu-system-riscv32 -machine zmachine -smp 1 -nographic \
                    -serial mon:stdio --no-reboot \
                    -bios build/platform/zmachine/firmware/fw_jump.bin \
                    -kernel kernel.elf
```

`-kernel` takes the ELF as it is — QEMU loads it at its own link address, so
there is no objcopy step and nothing about the kernel is baked into the
firmware. Rebuilding the kernel alone is enough; the firmware only has to be
rebuilt when the platform changes.

The boot log reports the device and, with the self test enabled, whether it
matches the specification:

```
zdevice: 0x60000000-0x6000ffff (key @ +0x40, shift @ +0x42, data @ +0x100)
zdevice: selftest passed
```

A model that is not wired up yet shows every read coming back as `0xffff`:

```
zdevice: selftest: key=0x0000 shift=0 wrote=0x1234 want=0x1234 got=0xffff
zdevice: selftest FAILED (11 mismatches)
```

Using the device from S-mode
----------------------------

The payload finds the device through the device tree node and then uses
plain MMIO:

```c
#define ZDEV_BASE	0x60000000
#define ZDEV_KEY	(ZDEV_BASE + 0x40)
#define ZDEV_SHIFT	(ZDEV_BASE + 0x42)
#define ZDEV_DATA	(ZDEV_BASE + 0x100)

*(volatile uint16_t *)ZDEV_KEY   = 0xa5a5;
*(volatile uint16_t *)ZDEV_SHIFT = 1;
*(volatile uint16_t *)ZDEV_DATA  = 0x1234;

/* reads back (0x1234 ^ 0xa5a5) << 2 == 0xde44 */
uint16_t v = *(volatile uint16_t *)ZDEV_DATA;
```

If the payload enables Sv32 paging it has to map the window itself; the
firmware only guarantees that PMP permits the access.

Adding an interrupt controller
------------------------------

If the machine ever grows a device that raises interrupts, add a PLIC node to
`zmachine.dts`, `select IRQCHIP_PLIC` in `platform/zmachine/Kconfig`, and give
`platform_ops` an `irqchip_init` that calls `plic_cold_irqchip_init()`.
