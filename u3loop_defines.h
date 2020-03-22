/**
 * u3loop_defines.h - Defines for the PassMark USB 3.0 loopback plug protocol
 *
 * Copyright (c) 2020 David Imhoff <dimhoff.devel@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef __U3LOOP_DEFINES_H__
#define __U3LOOP_DEFINES_H__

// Note: byte order seems to Little-Endian

/**
 * Control endpoint vendor commands
 */
#define U3LOOP_CMD_SET_LEDS             0x0001
#define U3LOOP_CMD_SET_CONFIG           0x0002
#define U3LOOP_CMD_GET_CONFIG           0x0003
#define U3LOOP_CMD_SET_DISPLAY_MODE     0x0004
#define U3LOOP_CMD_CONF_ERROR_COUNTERS  0x0005
#define U3LOOP_CMD_GET_ERROR_COUNTERS   0x0006
#define U3LOOP_CMD_GET_VOLTAGE          0x0007
#define U3LOOP_CMD_RESERVED_DONOTUSE    0x0008
#define U3LOOP_CMD_GET_MAX_SPEED        0x0009 
#define U3LOOP_CMD_RESET_ERROR_COUNTERS 0x000a
#define U3LOOP_CMD_CONF_LPM             0x000b
#define U3LOOP_CMD_GET_DEVICE_INFO 	0x0050

/**
 * U3LOOP_CMD_SET_LEDS arguments
 *
 * OR together with U3LOOP_CMD_SET_LEDS to form wValue.
 */
#define U3LOOP_LED_ALL  (U3LOOP_LED_PWR | U3LOOP_LED_TX | U3LOOP_LED_RX | U3LOOP_LED_ERR)
#define U3LOOP_LED_NONE 0

#define U3LOOP_LED_PWR 0x0100
#define U3LOOP_LED_TX  0x0400
#define U3LOOP_LED_RX  0x1000
#define U3LOOP_LED_ERR 0x4000

// NOTE: exact meaning of these bits is unknown
#define U3LOOP_LED_PWR_AUTO (U3LOOP_LED_PWR << 1)
#define U3LOOP_LED_TX_AUTO  (U3LOOP_LED_TX  << 1)
#define U3LOOP_LED_RX_AUTO  (U3LOOP_LED_RX  << 1)
#define U3LOOP_LED_ERR_AUTO (U3LOOP_LED_ERR << 1)

/**
 * U3LOOP_CMD_SET_CONFIG / U3LOOP_CMD_GET_CONFIG data
 */
#pragma pack(1)
struct u3loop_config {
	uint8_t mode; // Test mode
#define U3LOOP_MODE_LOOPBACK   0
#define U3LOOP_MODE_READ       1
#define U3LOOP_MODE_WRITE      2
#define U3LOOP_MODE_READ_WRITE 3
	uint8_t ep_type; // Type of endpoint
#define U3LOOP_EP_TYPE_CTRL 0
#define U3LOOP_EP_TYPE_ISO 1
#define U3LOOP_EP_TYPE_BULK 2
#define U3LOOP_EP_TYPE_INT 3
	uint8_t ep_in; // Input Endpoint Number
	uint8_t ep_out; // Output Endpoint Number
	uint8_t ss_burst_len; // Burst length
	uint8_t polling_interval; // Iso. pooling interval
	uint8_t hs_bulk_nak_interval;
	uint8_t iso_transactions_per_bus_interval; // Iso. Packets/PI
	uint16_t iso_bytes_per_bus_interval;
	uint8_t speed; // USB version/speed
#define U3LOOP_SPEED_FULL 1 // USB 1.x - 12 Mb/s
#define U3LOOP_SPEED_HIGH 2 // USB 2.0 - 480 Mb/s
#define U3LOOP_SPEED_SUPER 3 // USB 3.0 - 5 Gb/s
#define U3LOOP_SPEED_UNKNOWN1 4 // TODO: used by Linux BiT software
	uint8_t buffer_count;
	uint16_t buffer_size;
};
#pragma pack()

/**
 * U3LOOP_CMD_SET_DISPLAY_MODE arguments
 *
 * OR together with U3LOOP_CMD_SET_DISPLAY_MODE to form wValue.
 */
#define U3LOOP_DISPLAY_DISABLE 0
#define U3LOOP_DISPLAY_ENABLE  0x0100

/**
 * U3LOOP_CMD_CONF_ERROR_COUNTERS arguments
 */
#pragma pack(1)
struct u3loop_error_cfg {
	uint16_t phy_err_mask;
	uint16_t ll_err_mask;
};
#pragma pack()

/**
 * U3LOOP_CMD_GET_ERROR_COUNTERS arguments
 */
#pragma pack(1)
struct u3loop_errors {
	uint32_t phy_error_cnt;
	uint32_t ll_error_cnt;
	uint32_t phy_errors;
	uint32_t ll_errors;
};
#pragma pack()

// See also FAQ:
// https://www.passmark.com/support/usb3loopback_faq.php
// "The red Error LED goes on. What does this mean?"

// Physical layer errors
#define U3LOOP_ERR_PHY_DECODE    (1 << 0) // 8b/10b encoding error
#define U3LOOP_ERR_PHY_EB_OVR    (1 << 1) // Elastic Buffer Overflow
#define U3LOOP_ERR_PHY_EB_UND    (1 << 2) // Elastic Buffer Underflow
#define U3LOOP_ERR_PHY_DISPARITY (1 << 3) // Receive Disparity
#define U3LOOP_ERR_PHY_CRC5      (1 << 4) // Receive CRC-5
#define U3LOOP_ERR_PHY_CRC16     (1 << 5) // Receive CRC-16
#define U3LOOP_ERR_PHY_CRC32     (1 << 6) // Receive CRC-32
#define U3LOOP_ERR_PHY_TRAINING  (1 << 7) // Training Sequence
#define U3LOOP_ERR_PHY_LOCK_LOSS (1 << 8) // PHY Lock Loss
#define U3LOOP_ERR_PHY_UNDEFINED (~((1ul << 9) - 1))

// Link layer errors
#define U3LOOP_ERR_LL_HP_TIMEOUT_EN        (1 << 0)
#define U3LOOP_ERR_LL_RX_SEQ_NUM_ERR_EN    (1 << 1)
#define U3LOOP_ERR_LL_RX_HP_FAIL_EN        (1 << 2)
#define U3LOOP_ERR_LL_MISSING_LGOOD_EN     (1 << 3)
#define U3LOOP_ERR_LL_MISSING_LCRD_EN      (1 << 4) // LCRD x Sequence does not match what is expected
#define U3LOOP_ERR_LL_CREDIT_HP_TIMEOUT_EN (1 << 5)
#define U3LOOP_ERR_LL_PM_LC_TIMEOUT_EN     (1 << 6)
#define U3LOOP_ERR_LL_TX_SEQ_NUM_ERR_EN    (1 << 7)
#define U3LOOP_ERR_LL_HDR_ADV_TIMEOUT_EN   (1 << 8)
#define U3LOOP_ERR_LL_HDR_ADV_HP_EN        (1 << 9)
#define U3LOOP_ERR_LL_HDR_ADV_LCRD_EN      (1 << 10)
#define U3LOOP_ERR_LL_HDR_ADV_LGO_EN       (1 << 11)
#define U3LOOP_ERR_LL_UNDEFINED            (~((1ul << 12) - 1))

/**
 * U3LOOP_CMD_CONF_LPM argument
 *
 * Set USB 3.0 Link Power Management(LPM).
 * OR together with U3LOOP_CMD_SET_DISPLAY_MODE to form the wValue.
 */
#define U3LOOP_LPM_ENTRY_DISABLE 0
#define U3LOOP_LPM_ENTRY_ENABLE  0x0100

#endif // __U3LOOP_DEFINES_H__
