/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * zdevice - XOR/shift scratchpad peripheral of the "zmachine" platform.
 */

#ifndef __ZDEVICE_H__
#define __ZDEVICE_H__

#include <sbi/sbi_types.h>

/*
 * MMIO layout, as byte offsets from the device base address:
 *
 *   0x0000 .. 0x003f	reserved
 *   0x0040		ZDEVICE_REG_KEY   (16-bit) XOR key
 *   0x0042		ZDEVICE_REG_SHIFT (16-bit) left shift toggle
 *   0x0044 .. 0x00ff	reserved
 *   0x0100 .. 0xffff	data scratchpad
 *
 * The device transforms every halfword written to the data scratchpad
 * before storing it:
 *
 *	stored = (written ^ KEY) << (SHIFT ? ZDEVICE_SHIFT_BITS : 0)
 *
 * A read of the data scratchpad returns the stored halfword as-is, so the
 * transform is never applied a second time. The two control registers are
 * plain read/write storage and are not transformed.
 */

/* Size of the MMIO window */
#define ZDEVICE_SIZE		0x10000

/* Control register block */
#define ZDEVICE_CTRL_BASE	0x0040
#define ZDEVICE_REG_KEY		(ZDEVICE_CTRL_BASE + 0x0)
#define ZDEVICE_REG_SHIFT	(ZDEVICE_CTRL_BASE + 0x2)

/* Data scratchpad */
#define ZDEVICE_DATA_BASE	0x0100

/* Bits the device shifts left by when the shift toggle is active */
#define ZDEVICE_SHIFT_BITS	2

/* Values accepted by ZDEVICE_REG_SHIFT */
#define ZDEVICE_SHIFT_OFF	0
#define ZDEVICE_SHIFT_ON	1

struct zdevice {
	/* Base address of the MMIO window */
	unsigned long addr;
	/* Size of the MMIO window, at least ZDEVICE_DATA_BASE + 2 */
	unsigned long size;
};

/**
 * Bring up the one and only zdevice instance
 *
 * Records the MMIO window, resets the control registers to the identity
 * transform (key 0, shift off) and grants S/U-mode access to the window
 * in the root domain.
 *
 * @param addr base address of the MMIO window
 * @param size size of the MMIO window
 *
 * @return 0 on success or a negative error code on failure
 */
int zdevice_init(unsigned long addr, unsigned long size);

/**
 * Get the zdevice instance
 *
 * @return the instance, or NULL if zdevice_init() has not run yet
 */
struct zdevice *zdevice_get(void);

/** Usable size of the data scratchpad, in bytes */
static inline unsigned long zdevice_data_size(struct zdevice *zd)
{
	return zd->size - ZDEVICE_DATA_BASE;
}

/** Read the XOR key applied to data scratchpad writes */
u16 zdevice_get_key(struct zdevice *zd);

/** Set the XOR key applied to data scratchpad writes */
void zdevice_set_key(struct zdevice *zd, u16 key);

/** Check whether the post-XOR left shift is enabled */
bool zdevice_get_shift(struct zdevice *zd);

/** Enable or disable the post-XOR left shift */
void zdevice_set_shift(struct zdevice *zd, bool enable);

/**
 * Write a halfword to the data scratchpad
 *
 * The device applies the XOR/shift transform, so the value read back from
 * @off afterwards is zdevice_transform(val, key, shift).
 *
 * @param zd zdevice instance
 * @param off halfword-aligned byte offset within the data scratchpad
 * @param val value to write
 *
 * @return 0 on success or a negative error code on failure
 */
int zdevice_write(struct zdevice *zd, unsigned long off, u16 val);

/**
 * Read a halfword back from the data scratchpad
 *
 * @param zd zdevice instance
 * @param off halfword-aligned byte offset within the data scratchpad
 * @param val where to store the value read
 *
 * @return 0 on success or a negative error code on failure
 */
int zdevice_read(struct zdevice *zd, unsigned long off, u16 *val);

/**
 * Software model of the transform the device applies on a scratchpad write
 *
 * Lets a caller predict what the device will store without reading it back.
 */
static inline u16 zdevice_transform(u16 val, u16 key, bool shift)
{
	u16 out = val ^ key;

	if (shift)
		out <<= ZDEVICE_SHIFT_BITS;

	return out;
}

#ifdef CONFIG_ZDEVICE_SELFTEST
/**
 * Exercise the device and check it against zdevice_transform()
 *
 * Clobbers the control registers and the first few halfwords of the data
 * scratchpad, so run it before handing the device to the next boot stage.
 *
 * @return 0 if the device behaved as specified, SBI_EIO otherwise
 */
int zdevice_selftest(struct zdevice *zd);
#else
static inline int zdevice_selftest(struct zdevice *zd) { return 0; }
#endif

#endif /* __ZDEVICE_H__ */
