/* Code for Mobile Node */

#include <zephyr/types.h>
#include <stddef.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/gap.h>

#define STACKSIZE 1024
#define PRIORITY 7

//struct mobile_flags {
//    bool continuous;
//    bool temperature;
//    bool humidity;
//    bool c02;
//    bool tvoc;
//}

/*
 * Bit | 7 | 6 | 5 |      4      |   3  |  2  |     1    |  0
 * Flag| - | - | - | continuous? | tvoc | c02 | humidity | temp
 */
uint8_t mobile_flags = 0;

static void parse_packet_data(struct net_buf_simple *ad) {
    while (ad->len > 1) {
        uint8_t len = net_buf_simple_pull_u8(ad);
        if (len == 0 || len > ad->len) break;

        uint8_t type = net_buf_simple_pull_u8(ad);

        if (type == BT_DATA_MANUFACTURER_DATA && len >= 25) {
            uint8_t company_id_0 = net_buf_simple_pull_u8(ad);
			uint8_t company_id_1 = net_buf_simple_pull_u8(ad);
			uint8_t beacon_type = net_buf_simple_pull_u8(ad);
			uint8_t beacon_len  = net_buf_simple_pull_u8(ad);

            if (company_id_0 == 0x4C && company_id_1 == 0x00 && beacon_type == 0x02 && beacon_len = 0x15) {
                char uuid_str[37];
                uint8_t uuid[16];
                for (int i = 0; i < 16; i++) {
                    uuid[i] = net_buf_simple_pull_u8(ad);
                }

                snprintf(uuid_str, sizeof(uuid_str),
						  "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
						  uuid[0], uuid[1], uuid[2], uuid[3],
						  uuid[4], uuid[5],
						  uuid[6], uuid[7],
						  uuid[8], uuid[9],
						  uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
                
                uint16_t major = (net_buf_simple_pull_u8(ad) << 8 | net_buf_simple_pull_u8(ad));
                uint16_t minor = (net_buf_simple_pull_u8(ad) << 8) | net_buf_simple_pull_u8(ad);
				int8_t tx_power = (int8_t)net_buf_simple_pull_u8(ad);

                /* Read in the packet bytes */
                uint8_t command_byte = net_buf_simple_pull_u8(ad);
                uint8_t sensor_flag_byte = net_buf_simple_pull_u8(ad);
                uint8_t mode_select_byte = net_buf_simple_pull_u8(ad);

                /* Switch case for commands */
                if (command_byte & 0x01) {
                    // sensor req
                    // set flags for sensors to be requested
                    (sensor_flag_byte & 0x01) ? (mobile_flags |= (1 << 0)) : (mobile_flags &= ~(1 << 0));
                    (sensor_flag_byte & 0x02) ? (mobile_flags |= (1 << 1)) : (mobile_flags &= ~(1 << 1));
                    (sensor_flag_byte & 0x04) ? (mobile_flags |= (1 << 2)) : (mobile_flags &= ~(1 << 2));
                    (sensor_flag_byte & 0x08) ? (mobile_flags |= (1 << 3)) : (mobile_flags &= ~(1 << 3));

                    // TODO: send sensor data
                }
                
                // mode select
                if (command_byte & 0x02) {
                    // toggle
                    if (mode_select_byte & 0x01) {
                        mobile_flags ^= (1 << 4);
                        continue;
                    }
                    // discrete
                    if (mode_select_byte & 0x02) {
                        mobile_flags &= ~(1 << 4);
                        continue;
                    }
                    // continuous
                    if (mode_select_byte & 0x04) {
                        mobile_flags |= (1 << 4);
                        continue;
                    }
                }
            }
        }
    }
    
}

static const uint8_t expected_uuid_base[16] = {
    0x1a, 0xbb, 0xe1, 0xed, 0xde, 0xad, 0xfa, 0x11,
    0xba, 0xff, 0x1e, 0xdb, 0xee, 0x5f, 0x10, 0x55
};

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi,
                    uint8_t type, struct net_buf_simple *ad) {
    for (size_t i = 0; i < ad->len;) {
        uint8_t len = ad->data[i++];
        if (len == 0 || (i + len) > ad->len) break;

        uint8_t type = ad->data[i++];
        if (type == BT_DATA_MANUFACTURER_DATA && len >= 25) {
            const uint8_t *data = &ad->data[i];

            if (data[0] == 0x4C && data[1] == 0x00 &&  // Apple Company ID)
                    data[2] == 0x02 && data[3] == 0x15) {  // iBeacon type + length
                const uint8_t *uuid = &data[4];

                if (memcmp(uuid, expected_uuid_base, 16) == 0) {
                    char addr_str[BT_ADDR_LE_STR_LEN];
                    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
                    parse_packet_data(ad);
                }
            }
        }
        i += (len - 1);
    }

}