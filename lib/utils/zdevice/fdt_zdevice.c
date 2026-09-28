/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Device tree probing for the zdevice peripheral.
 */

#include <sbi/sbi_error.h>
#include <sbi_utils/fdt/fdt_driver.h>
#include <sbi_utils/fdt/fdt_helper.h>
#include <sbi_utils/zdevice/fdt_zdevice.h>

static int zdevice_fdt_init(const void *fdt, int nodeoff,
			    const struct fdt_match *match)
{
	uint64_t addr, size;
	int rc;

	rc = fdt_get_node_addr_size(fdt, nodeoff, 0, &addr, &size);
	if (rc)
		return rc;

	return zdevice_init(addr, size);
}

static const struct fdt_match zdevice_match[] = {
	{ .compatible = "zmachine,zdevice" },
	{ },
};

static const struct fdt_driver fdt_zdevice = {
	.match_table = zdevice_match,
	.init = zdevice_fdt_init,
};

static const struct fdt_driver *const zdevice_drivers[] = {
	&fdt_zdevice,
	NULL
};

int fdt_zdevice_init(const void *fdt)
{
	if (!fdt)
		return SBI_EINVAL;

	return fdt_driver_init_one(fdt, zdevice_drivers);
}
