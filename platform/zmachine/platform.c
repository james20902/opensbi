/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * zmachine - a bare minimum RISC-V machine (one hart, RAM, CLINT and one
 * 8250 UART) plus the custom "zdevice" XOR/shift scratchpad peripheral.
 *
 * There is deliberately no interrupt controller here: nothing on this
 * machine raises an external interrupt. Add a PLIC to the device tree, to
 * the Kconfig (select IRQCHIP_PLIC) and to platform_irqchip_init() if that
 * ever changes.
 *
 * The addresses below must agree with zmachine.dts and with the QEMU
 * machine model.
 */

#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_const.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_platform.h>

#include <sbi_utils/fdt/fdt_fixup.h>
#include <sbi_utils/fdt/fdt_helper.h>
#include <sbi_utils/ipi/aclint_mswi.h>
#include <sbi_utils/timer/aclint_mtimer.h>
#include <sbi_utils/serial/uart8250.h>
#include <sbi_utils/zdevice/fdt_zdevice.h>

#define PLATFORM_HART_COUNT		1

#define PLATFORM_CLINT_ADDR		0x02000000
#define PLATFORM_ACLINT_MTIMER_FREQ	10000000
#define PLATFORM_ACLINT_MSWI_ADDR	(PLATFORM_CLINT_ADDR + \
					 CLINT_MSWI_OFFSET)
#define PLATFORM_ACLINT_MTIMER_ADDR	(PLATFORM_CLINT_ADDR + \
					 CLINT_MTIMER_OFFSET)

#define PLATFORM_UART_ADDR		0x10000000
#define PLATFORM_UART_INPUT_FREQ	3686400
#define PLATFORM_UART_BAUDRATE		115200

static struct aclint_mswi_data mswi = {
	.addr = PLATFORM_ACLINT_MSWI_ADDR,
	.size = ACLINT_MSWI_SIZE,
	.first_hartid = 0,
	.hart_count = PLATFORM_HART_COUNT,
};

static struct aclint_mtimer_data mtimer = {
	.mtime_freq = PLATFORM_ACLINT_MTIMER_FREQ,
	.mtime_addr = PLATFORM_ACLINT_MTIMER_ADDR +
		      ACLINT_DEFAULT_MTIME_OFFSET,
	.mtime_size = ACLINT_DEFAULT_MTIME_SIZE,
	.mtimecmp_addr = PLATFORM_ACLINT_MTIMER_ADDR +
			 ACLINT_DEFAULT_MTIMECMP_OFFSET,
	.mtimecmp_size = ACLINT_DEFAULT_MTIMECMP_SIZE,
	.first_hartid = 0,
	.hart_count = PLATFORM_HART_COUNT,
	.has_64bit_mmio = true,
};

/*
 * Bring up the zdevice from its device tree node.
 *
 * The device tree is the single source of truth for where the peripheral
 * lives, so a machine without a zdevice node simply boots without it
 * rather than poking at an address that may not be backed by anything.
 *
 * This has to run during early init: it hands the MMIO window to the root
 * domain, and the domain is finalized before final_init gets a chance.
 */
static void platform_zdevice_init(void)
{
	int rc;

	/*
	 * The scratchpad is not needed to boot, so a machine that does not
	 * have one, or that describes one badly, still comes up. Note that
	 * fdt_driver_init_one() has already reported the details of a real
	 * probe failure by the time we get here.
	 */
	rc = fdt_zdevice_init(fdt_get_address());
	if (rc && rc != SBI_ENODEV)
		sbi_printf("zdevice: probe failed (error %d)\n", rc);
}

/*
 * Describe the zdevice on the console, and optionally prove that it does
 * what the specification says. Split out from platform_zdevice_init() only
 * so that it lands after the OpenSBI banner rather than ahead of it.
 */
static void platform_zdevice_report(void)
{
	struct zdevice *zd = zdevice_get();

	if (!zd) {
		sbi_printf("zdevice: not present\n");
		return;
	}

	sbi_printf("zdevice: 0x%lx-0x%lx (key @ +0x%x, shift @ +0x%x, "
		   "data @ +0x%x)\n",
		   zd->addr, zd->addr + zd->size - 1,
		   ZDEVICE_REG_KEY, ZDEVICE_REG_SHIFT, ZDEVICE_DATA_BASE);

	/* Diagnostics only: a failing device should not stop the boot */
	zdevice_selftest(zd);
}

/*
 * Platform early initialization.
 */
static int platform_early_init(bool cold_boot)
{
	int rc;

	if (!cold_boot)
		return 0;

	rc = uart8250_init(PLATFORM_UART_ADDR, PLATFORM_UART_INPUT_FREQ,
			   PLATFORM_UART_BAUDRATE, 0, 1, 0, 0);
	if (rc)
		return rc;

	rc = aclint_mswi_cold_init(&mswi);
	if (rc)
		return rc;

	platform_zdevice_init();

	return 0;
}

/*
 * Platform final initialization.
 */
static int platform_final_init(bool cold_boot)
{
	void *fdt;

	if (!cold_boot)
		return 0;

	/*
	 * Publish the firmware's own memory regions as /reserved-memory so
	 * the next boot stage does not allocate over OpenSBI.
	 */
	fdt = fdt_get_address_rw();
	if (fdt)
		fdt_fixups(fdt);

	platform_zdevice_report();

	return 0;
}

/*
 * Initialize platform timer during cold boot.
 */
static int platform_timer_init(void)
{
	return aclint_mtimer_cold_init(&mtimer, NULL);
}

/*
 * Platform descriptor.
 */
const struct sbi_platform_operations platform_ops = {
	.early_init		= platform_early_init,
	.final_init		= platform_final_init,
	.timer_init		= platform_timer_init
};

const struct sbi_platform platform = {
	.opensbi_version	= OPENSBI_VERSION,
	.platform_version	= SBI_PLATFORM_VERSION(0x0, 0x01),
	.name			= "zmachine",
	.features		= SBI_PLATFORM_DEFAULT_FEATURES,
	.hart_count		= PLATFORM_HART_COUNT,
	.hart_stack_size	= SBI_PLATFORM_DEFAULT_HART_STACK_SIZE,
	.heap_size		= SBI_PLATFORM_DEFAULT_HEAP_SIZE(
						PLATFORM_HART_COUNT),
	.platform_ops_addr	= (unsigned long)&platform_ops
};
