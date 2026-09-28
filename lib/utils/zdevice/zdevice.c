/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * zdevice - XOR/shift scratchpad peripheral of the "zmachine" platform.
 */

#include <sbi/riscv_io.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_domain.h>
#include <sbi/sbi_error.h>
#include <sbi_utils/zdevice/zdevice.h>

static struct zdevice zdev;

static inline volatile void *zdevice_reg(struct zdevice *zd, unsigned long off)
{
	return (volatile void *)(zd->addr + off);
}

struct zdevice *zdevice_get(void)
{
	return zdev.addr ? &zdev : NULL;
}

u16 zdevice_get_key(struct zdevice *zd)
{
	return readw(zdevice_reg(zd, ZDEVICE_REG_KEY));
}

void zdevice_set_key(struct zdevice *zd, u16 key)
{
	writew(key, zdevice_reg(zd, ZDEVICE_REG_KEY));
}

bool zdevice_get_shift(struct zdevice *zd)
{
	return readw(zdevice_reg(zd, ZDEVICE_REG_SHIFT)) != ZDEVICE_SHIFT_OFF;
}

void zdevice_set_shift(struct zdevice *zd, bool enable)
{
	writew(enable ? ZDEVICE_SHIFT_ON : ZDEVICE_SHIFT_OFF,
	       zdevice_reg(zd, ZDEVICE_REG_SHIFT));
}

/*
 * Reject anything that is not a halfword-aligned halfword inside the data
 * scratchpad. Written so that a wildly out-of-range offset cannot wrap
 * around and slip past the bounds check.
 */
static int zdevice_check_data_off(struct zdevice *zd, unsigned long off)
{
	unsigned long size = zdevice_data_size(zd);

	if (off & (sizeof(u16) - 1))
		return SBI_EINVAL;
	if (off >= size || (size - off) < sizeof(u16))
		return SBI_EBAD_RANGE;

	return 0;
}

int zdevice_write(struct zdevice *zd, unsigned long off, u16 val)
{
	int rc;

	if (!zd || !zd->addr)
		return SBI_ENODEV;

	rc = zdevice_check_data_off(zd, off);
	if (rc)
		return rc;

	writew(val, zdevice_reg(zd, ZDEVICE_DATA_BASE + off));

	return 0;
}

int zdevice_read(struct zdevice *zd, unsigned long off, u16 *val)
{
	int rc;

	if (!zd || !zd->addr)
		return SBI_ENODEV;
	if (!val)
		return SBI_EINVAL;

	rc = zdevice_check_data_off(zd, off);
	if (rc)
		return rc;

	*val = readw(zdevice_reg(zd, ZDEVICE_DATA_BASE + off));

	return 0;
}

int zdevice_init(unsigned long addr, unsigned long size)
{
	int rc;

	if (!addr || size < (ZDEVICE_DATA_BASE + sizeof(u16)))
		return SBI_EINVAL;
	if (zdev.addr)
		return SBI_EALREADY;

	zdev.addr = addr;
	zdev.size = size;

	/* Hand the next boot stage an identity transform */
	zdevice_set_key(&zdev, 0);
	zdevice_set_shift(&zdev, false);

	/*
	 * The scratchpad is meant to be driven from S-mode, so the window
	 * has to be readable and writable below M-mode.
	 */
	rc = sbi_domain_root_add_memrange(addr, size, ZDEVICE_SIZE,
					  SBI_DOMAIN_MEMREGION_MMIO |
					  SBI_DOMAIN_MEMREGION_SHARED_SURW_MRW);
	if (rc) {
		zdev.addr = 0;
		zdev.size = 0;
		return rc;
	}

	return 0;
}

#ifdef CONFIG_ZDEVICE_SELFTEST

/*
 * Walk the four combinations of "key set / key clear" and "shift on / shift
 * off" and confirm that what the device stores matches the specification
 * modelled by zdevice_transform(). This is really a test of the QEMU model,
 * so report each mismatch rather than stopping at the first one.
 */
int zdevice_selftest(struct zdevice *zd)
{
	static const u16 keys[] = { 0x0000, 0xa5a5 };
	static const u16 vals[] = { 0x0000, 0x1234, 0xffff };
	int rc, failures = 0;
	unsigned int i, j, s;
	u16 want, got;

	if (!zd || !zd->addr)
		return SBI_ENODEV;

	for (i = 0; i < array_size(keys); i++) {
		zdevice_set_key(zd, keys[i]);

		for (s = 0; s < 2; s++) {
			zdevice_set_shift(zd, s ? true : false);

			for (j = 0; j < array_size(vals); j++) {
				rc = zdevice_write(zd, j * sizeof(u16),
						   vals[j]);
				if (rc)
					return rc;

				rc = zdevice_read(zd, j * sizeof(u16), &got);
				if (rc)
					return rc;

				want = zdevice_transform(vals[j], keys[i],
							 s ? true : false);
				if (got == want)
					continue;

				sbi_printf("zdevice: selftest: key=0x%04x "
					   "shift=%u wrote=0x%04x "
					   "want=0x%04x got=0x%04x\n",
					   keys[i], s, vals[j], want, got);
				failures++;
			}
		}
	}

	/* Leave the device the way zdevice_init() found it */
	zdevice_set_key(zd, 0);
	zdevice_set_shift(zd, false);

	if (failures) {
		sbi_printf("zdevice: selftest FAILED (%d mismatches)\n",
			   failures);
		return SBI_EIO;
	}

	sbi_printf("zdevice: selftest passed\n");

	return 0;
}

#endif /* CONFIG_ZDEVICE_SELFTEST */
