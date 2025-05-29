/*
 * Copyright (c) 2018 Jan Van Winkel <jan.van_winkel@dxplore.eu>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

 #include <zephyr/shell/shell.h>
 #include <zephyr/version.h>
 #include <zephyr/logging/log.h>
 #include <zephyr/drivers/uart.h>
 #include <zephyr/usb/usb_device.h>
 #include <ctype.h>
 #include <zephyr/types.h>
 #include <stddef.h>
 #include <zephyr/sys/printk.h>
 #include <zephyr/sys/__assert.h>
 #include <zephyr/sys/slist.h>

 #include <zephyr/bluetooth/bluetooth.h>
 #include <zephyr/bluetooth/hci.h>
 #include <zephyr/bluetooth/conn.h>
 #include <zephyr/bluetooth/uuid.h>
 #include <zephyr/bluetooth/gatt.h>
 #include <stdio.h>
 #include <stdint.h>
 #include <stdlib.h>
 #include <math.h>
 #include <assert.h>
 #include <zephyr/data/json.h>
 
 #include <zephyr/device.h>
 #include <zephyr/devicetree.h>
 #include <zephyr/drivers/display.h>
 #include <zephyr/drivers/gpio.h>
 #include <lvgl.h>
 #include <stdio.h>
 #include <string.h>
 #include <zephyr/kernel.h>
 #include <lvgl_input_device.h>
 #include <zephyr/sys/util.h>
 

LOG_MODULE_REGISTER(viewer);
 static uint8_t mfg_data[] = { 0xff, 0xff, 0x00 };
 uint16_t x_coord;
 uint16_t y_coord;
 uint16_t coords[2];
 int bluetooth_read(void);

 double sensorVals[4] = {0, 0, 0, 0}; //temp, Hum, CO2, tvoc

 static const struct bt_data ad[] = {
     BT_DATA(BT_DATA_MANUFACTURER_DATA, mfg_data, 3),
 };
 
static const uint8_t expected_uuid_mobile[10] = {0xca, 0xb1, 0xeb, 0x1a, 0xde, 0xca, 0x5c, 0xad, 0xe0, 0xaf};

double scale_value(double value, int sensor){
    double returnVal = 0;
    if(sensor == 0){
        returnVal = (value/40)*100;
    } else if(sensor == 1) {
        returnVal = value;
    } else if(sensor == 2) {
        returnVal = value;
    } else {
        returnVal = (value/1000)*100;
    }
    return returnVal;
}

static void parse_ultrasonic_data(struct net_buf_simple *ad) {
	while (ad->len > 1) {
		uint8_t len = net_buf_simple_pull_u8(ad);
		if (len == 0 || len > ad->len) break;

		uint8_t type = net_buf_simple_pull_u8(ad);
		if (type == BT_DATA_MANUFACTURER_DATA && len >= 25) {
			uint8_t company_id_0 = net_buf_simple_pull_u8(ad);
			uint8_t company_id_1 = net_buf_simple_pull_u8(ad);
			uint8_t beacon_type = net_buf_simple_pull_u8(ad);
			uint8_t beacon_len  = net_buf_simple_pull_u8(ad);

			if (company_id_0 == 0x4C && company_id_1 == 0x00 && beacon_type == 0x02 && beacon_len == 0x15) {
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

				uint16_t major = (net_buf_simple_pull_u8(ad) << 8) | net_buf_simple_pull_u8(ad);
				uint16_t minor = (net_buf_simple_pull_u8(ad) << 8) | net_buf_simple_pull_u8(ad);
                uint8_t tempVals[8] = {minor & 0xFF, (minor >> 8) & 0xFF, major & 0xFF, (major >> 8) & 0xFF, uuid[15], uuid[14], uuid[13], uuid[12] };
				double convertedVal;
				memcpy(&convertedVal, tempVals, sizeof(convertedVal));
				uint8_t sensor = uuid[10];
				printf("sensor is: %d\n", sensor);
				printf("Converted to double: %lf\n", convertedVal);
				if (sensor == 1) { //change this to 1
                    sensorVals[0] = scale_value(convertedVal, 0);
				} else if (sensor == 2) {
					sensorVals[1] = scale_value(convertedVal, 1);
				} else if (sensor == 4) {
					sensorVals[2] = scale_value(convertedVal, 2);
				} else if (sensor == 8) {
					sensorVals[3] = scale_value(convertedVal, 3);
				}
			} else {
				net_buf_simple_pull(ad, len - 4);
			}
		} else {
			net_buf_simple_pull(ad, len - 1);
		}
	}
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi,
                    uint8_t type, struct net_buf_simple *ad)
{
    for (size_t i = 0; i < ad->len;) {
        uint8_t len = ad->data[i++];
        if (len == 0 || (i + len) > ad->len) break;

        uint8_t type = ad->data[i++];
        if (type == BT_DATA_MANUFACTURER_DATA && len >= 25) {
            const uint8_t *data = &ad->data[i];

            if (data[0] == 0x4C && data[1] == 0x00 &&  // Apple Company ID
                data[2] == 0x02 && data[3] == 0x15) {  // iBeacon type + length
                const uint8_t *uuid = &data[4];
                if (memcmp(uuid, expected_uuid_mobile, 10) == 0) {
                    char addr_str[BT_ADDR_LE_STR_LEN];
                    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
                    // printk("Found target iBeacon with UUID at %s, RSSI: %d dBm\n", addr_str, rssi);
					parse_ultrasonic_data(ad);
                    printf("temp: %f, hum:%f, CO2: %f, tvoc: %f\n", ((double)sensorVals[0]), ((double)sensorVals[1]), ((double)sensorVals[2]), ((double)sensorVals[3]));
                }
            }
        }
        i += (len - 1);
    }
	
}
 
int bluetooth_read(void)
{
    struct bt_le_scan_param scan_param = {
        .type       = BT_LE_SCAN_TYPE_PASSIVE,
        .options    = BT_LE_SCAN_OPT_NONE,
        .interval   = 0x0010,
        .window     = 0x0010,
    };

    printk("Starting Scanner/Advertiser Demo\n");

    int err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return 0;
    }

    printk("Bluetooth initialized\n");

    err = bt_le_scan_start(&scan_param, scan_cb);
    if (err) {
        printk("Starting scanning failed (err %d)\n", err);
        return 0;
    }

    err = bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        printk("Advertising failed to start (err %d)\n", err);
        return 0;
    }

    return 0;
}

int main(void)
{
    const struct device *display_dev;
    lv_obj_t *chart;
    lv_chart_series_t *series;

    display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
    if (!device_is_ready(display_dev)) {
        LOG_ERR("Display device not ready");
        return 0;
    }

    // Bar chart setup (FULL SCREEN)
    chart = lv_chart_create(lv_screen_active());
    lv_obj_set_size(chart, LV_HOR_RES, LV_VER_RES);
    lv_obj_align(chart, LV_ALIGN_CENTER, 0, 0);
    lv_chart_set_type(chart, LV_CHART_TYPE_BAR);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);  // Max range
    lv_chart_set_point_count(chart, 4);  // 4 sensor bars

    series = lv_chart_add_series(chart, lv_palette_main(LV_PALETTE_BLUE), LV_CHART_AXIS_PRIMARY_Y);

    // Add labels above each bar (adjust positions based on full-screen size)
    static const char *labels[] = {"40°C", "100%", "1000ppm", "100ppb"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *lbl = lv_label_create(lv_screen_active());
        lv_label_set_text(lbl, labels[i]);
        int x_offset = (LV_HOR_RES / 4) * i + (LV_HOR_RES / 8) - 10;
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, x_offset, 10);
    }

    lv_timer_handler();
    display_blanking_off(display_dev);

    bluetooth_read();  // Start BLE scanning

    while (1) {
        // Fill bar values: X in first two, Y in last two
        lv_chart_set_value_by_id(chart, series, 0, (int)sensorVals[0]);
        lv_chart_set_value_by_id(chart, series, 1, (int)sensorVals[1]);
        lv_chart_set_value_by_id(chart, series, 2, (int)sensorVals[2]);
        lv_chart_set_value_by_id(chart, series, 3, (int)sensorVals[3]);
        lv_chart_refresh(chart);

        lv_timer_handler();
        k_sleep(K_MSEC(100));
    }

    return 0;
}
