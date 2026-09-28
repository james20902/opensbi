/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Device tree probing for the zdevice peripheral.
 */

#ifndef __FDT_ZDEVICE_H__
#define __FDT_ZDEVICE_H__

#include <sbi_utils/zdevice/zdevice.h>

/**
 * Bring up the zdevice described by the first matching DT node
 *
 * @param fdt devicetree blob
 *
 * @return 0 on success, SBI_ENODEV if the blob has no zdevice node, or
 * another negative error code on failure
 */
int fdt_zdevice_init(const void *fdt);

#endif /* __FDT_ZDEVICE_H__ */
