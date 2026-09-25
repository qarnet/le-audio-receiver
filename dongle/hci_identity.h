/*
 * SPDX-License-Identifier: MIT
 *
 * Lab BD_ADDR for the current XIAO nRF54L15 HCI controller.
 *
 * Historical nRF5340DK motivation: that DK's FICR DEVICEADDR was
 * unprogrammed (all zeros), requiring an explicit public address.
 * This is not a claim about the XIAO's FICR DEVICEADDR.
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
