/*
 * SPDX-License-Identifier: MIT
 *
 * Compile-time BD_ADDR for nRF5340DK hci_uart dongle.
 *
 * Lab-only: this DK's FICR DEVICEADDR is unprogrammed (all zeros),
 * so the controller would report 00:00:00:00:00:00 without an
 * explicit bt_ctlr_set_public_addr() call before bt_enable_raw().
 *
 * This address is NOT a production-assigned OUI. It is a static
 * lab identity so the dongle can scan and connect without the
 * runtime btmgmt static-addr workaround.
 */

#ifndef DONGLE_HCI_IDENTITY_H_
#define DONGLE_HCI_IDENTITY_H_

#include <stdint.h>

/* Public BD_ADDR C0:AA:BB:CC:DD:EE.
 *
 * bt_ctlr_set_public_addr() takes the array in on-air byte order
 * (little-endian — LAP first). This is the REVERSE of the display
 * order shown by btmgmt/hcitool.
 *
 * Empirical verification: array {0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0xC0}
 * produces btmgmt "addr C0:AA:BB:CC:DD:EE".
 */
static const uint8_t dongle_bd_addr[6] = {
	0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0xC0,
};

#endif /* DONGLE_HCI_IDENTITY_H_ */
