/* Code for Mobile Node */

#include <zephyr/types.h>
#include <stddef.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/ccs811.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/gap.h>

#define STACKSIZE 4096 //2048
#define PRIORITY 7

#include <zephyr/drivers/gpio.h>
#define LED_RED_NODE DT_ALIAS(led0)
#define LED_GREEN_NODE DT_ALIAS(led1)
#define LED_BLUE_NODE DT_ALIAS(led2)
static const struct gpio_dt_spec led_red = GPIO_DT_SPEC_GET(LED_RED_NODE, gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
static const struct gpio_dt_spec led_blue = GPIO_DT_SPEC_GET(LED_BLUE_NODE, gpios);

#define DEFAULT_DELAY 1000
#define DEFAULT_BROADCAST_TIME 250

#define BROADCAST_UUID  0xca, 0x11, 0xed, 0xba, 0x1d, \
                        0xfa, 0xca, 0xde, 0x7a, 0x1e

#define PACKET_PREAMBLE 0x4c, 0x00,\
                        0x02, 0x15, \
                        BROADCAST_UUID

#define DEFAULT_SENSOR_VALUES   0xff, 0xff, 0xff, 0xff, \
                                0xff, 0xff, 0xff, 0xff

#define MOBILE_RSSI 0xc8

#define SENSOR_DATA_OFFSET 16

K_SEM_DEFINE(sensor_update_sem, 0, 1);
K_SEM_DEFINE(data_broadcast_sem, 0, 1);

struct sensor_values_struct {
    struct sensor_value temp;
    struct sensor_value humidity;
    struct sensor_value co2;
    struct sensor_value tvoc;
};
struct sensor_values_struct sensor_values;

/*
 * Bit | 7 | 6 | 5 |      4      |   3  |  2  |     1    |  0
 * Flag| - | - | - | continuous? | tvoc | co2 | humidity | temp
 */
uint8_t mobile_flags = 0;

//static struct bt_le_ext_adv *adv[CONFIG_BT_EXT_ADV_MAX_ADV_SET];

static const uint8_t expected_uuid_base[16] = {
    0x1a, 0xbb, 0xe1, 0xed, 0xde, 0xad, 0xfa, 0x11,
    0xba, 0xff, 0x1e, 0xdb, 0xee, 0x5f, 0x10, 0x55
};

// Create default ads
volatile uint8_t data_temp[25] = {
    PACKET_PREAMBLE,
    0x01,
    DEFAULT_SENSOR_VALUES,
    MOBILE_RSSI
};
volatile uint8_t data_humidity[25] = {
    PACKET_PREAMBLE,
    0x02,
    DEFAULT_SENSOR_VALUES,
    MOBILE_RSSI
};
volatile uint8_t data_co2[25] = {
    PACKET_PREAMBLE,
    0x04,
    DEFAULT_SENSOR_VALUES,
    MOBILE_RSSI
};
volatile uint8_t data_tvoc[25] = {
    PACKET_PREAMBLE,
    0x08,
    DEFAULT_SENSOR_VALUES,
    MOBILE_RSSI
};

const struct bt_data ad_temp[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
    BT_DATA(BT_DATA_MANUFACTURER_DATA, data_temp, 25)
};
const struct bt_data ad_humidity[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
    BT_DATA(BT_DATA_MANUFACTURER_DATA, data_humidity, 25)
};
const struct bt_data ad_co2[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
    BT_DATA(BT_DATA_MANUFACTURER_DATA, data_co2, 25)
};
const struct bt_data ad_tvoc[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
    BT_DATA(BT_DATA_MANUFACTURER_DATA, data_tvoc, 25)
};

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

            if (company_id_0 == 0x4C && company_id_1 == 0x00 && beacon_type == 0x02 && beacon_len == 0x15) {
                uint8_t uuid[16];
                for (int i = 0; i < 16; i++) {
                    uuid[i] = net_buf_simple_pull_u8(ad);
                }

                //uint16_t major = (net_buf_simple_pull_u8(ad) << 8 | net_buf_simple_pull_u8(ad));
                //uint16_t minor = (net_buf_simple_pull_u8(ad) << 8) | net_buf_simple_pull_u8(ad);
				//int8_t tx_power = (int8_t)net_buf_simple_pull_u8(ad);

                /* Read in the packet bytes */
                uint8_t command_byte = net_buf_simple_pull_u8(ad);
                uint8_t sensor_flag_byte = net_buf_simple_pull_u8(ad);
                uint8_t mode_select_byte = net_buf_simple_pull_u8(ad);
                uint8_t minor_reserved = net_buf_simple_pull_u8(ad);
                int8_t tx_power = (int8_t)net_buf_simple_pull_u8(ad);

                /* Check commands */
                if (command_byte & 0x01) {
                    // sensor req
                    // set flags for sensors to be requested
                    (sensor_flag_byte & (1 << 0)) ? (mobile_flags |= (1 << 0)) : (mobile_flags &= ~(1 << 0));
                    (sensor_flag_byte & (1 << 1)) ? (mobile_flags |= (1 << 1)) : (mobile_flags &= ~(1 << 1));
                    (sensor_flag_byte & (1 << 2)) ? (mobile_flags |= (1 << 2)) : (mobile_flags &= ~(1 << 2));
                    (sensor_flag_byte & (1 << 3)) ? (mobile_flags |= (1 << 3)) : (mobile_flags &= ~(1 << 3));

                    // Let sensors update
                    k_sem_give(&sensor_update_sem);
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

static void change_data(volatile uint8_t* target_ad, double new_data) {
    // Doing this instead of using a union because of endianess
    int64_t *double_as_int = (int64_t *)&new_data;

    for (int i = 0; i < 8; i++) {
        target_ad[i + SENSOR_DATA_OFFSET] = (*double_as_int >> (64 - 8*(i+1)));
    }
}

static void take_temp(const struct device *dev) {
    struct sensor_value temp;
    if (sensor_sample_fetch(dev) < 0) {
        printf("Sensor sample update error\n");
		return;
    }

    if (sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &temp) < 0) {
        printk("Cannot read HTS221 temperature channel\n");
        return;
    }

    //sensor_values.temp = temp;
    change_data(data_temp, sensor_value_to_double(&temp));
    //bt_le_ext_adv_set_data(adv[0], ad_temp, ARRAY_SIZE(ad_temp), NULL, 0);
    printk("Temp: %lf\n", sensor_value_to_double(&temp));

}
static void take_humidity(const struct device *dev) {
    struct sensor_value humidity;
    if (sensor_sample_fetch(dev) < 0) {
        printk("Sensor sample update error.\n");
        return;
    }

    if (sensor_channel_get(dev, SENSOR_CHAN_HUMIDITY, &humidity) < 0) {
        printk("Cannot read HTS221 humidity channel\n");
        return;
    }

    //sensor_values.humidity = humidity;
    change_data(data_humidity, sensor_value_to_double(&humidity));
    //bt_le_ext_adv_set_data(adv[1], ad_humidity, ARRAY_SIZE(ad_humidity), NULL, 0);
    printk("Humidity: %lf\n", sensor_value_to_double(&humidity));

}
static void take_co2(const struct device *dev) {
    struct sensor_value co2;

    if (sensor_sample_fetch(dev) < 0) {
        printk("Sensor sample update error.\n");
        return;
    }

    if (sensor_channel_get(dev, SENSOR_CHAN_CO2, &co2) < 0) {
        printk("Cannot read CCS811 CO2 channel\n");
        return;
    }

    //sensor_values.co2 = co2;
    change_data(data_co2, sensor_value_to_double(&co2));
    //bt_le_ext_adv_set_data(adv[2], ad_co2, ARRAY_SIZE(ad_co2), NULL, 0);
    printk("CO2: %lf\n", sensor_value_to_double(&co2));

}
static void take_tvoc(const struct device *dev) {
    struct sensor_value tvoc;
    
    if (sensor_sample_fetch(dev) < 0) {
        printk("Sensor sample update error.\n");
        return;
    }

    if (sensor_channel_get(dev, SENSOR_CHAN_VOC, &tvoc) < 0) {
        printk("Cannot read CCS811 TVOC channel\n");
        return;
    }

    //sensor_values.tvoc = tvoc;
    change_data(data_tvoc, sensor_value_to_double(&tvoc));
    //bt_le_ext_adv_set_data(adv[3], ad_tvoc, ARRAY_SIZE(ad_tvoc), NULL, 0);
    printk("TVOC: %lf\n", sensor_value_to_double(&tvoc));

}

void update_sensors_entry_point() {
    const struct device *const temp_humidity_sensor = DEVICE_DT_GET_ONE(st_hts221);
    const struct device *const co2_tvoc_sensor = DEVICE_DT_GET_ONE(ams_ccs811);

    while (1) {
        // take semaphore
        k_sem_take(&sensor_update_sem, K_FOREVER);

        gpio_pin_set_dt(&led_blue, 1);

        // check flags & update
        if (mobile_flags & (1 << 0)) take_temp(temp_humidity_sensor);
        if (mobile_flags & (1 << 1)) take_humidity(temp_humidity_sensor);
        if (mobile_flags & (1 << 2)) take_co2(co2_tvoc_sensor);
        if (mobile_flags & (1 << 3)) take_tvoc(co2_tvoc_sensor);

        gpio_pin_set_dt(&led_blue, 0);

        // if continuous broadcasting, give sensor_update_sem
        if (mobile_flags & (1 << 4)) {
            k_sem_give(&sensor_update_sem);
        }

        // give data_broadcast_sem
        k_sem_give(&data_broadcast_sem);

        // delay
        k_msleep(DEFAULT_DELAY);

    }    
}
void broadcast_sensors_entry_point() {

    //k_msleep(500);

    //struct bt_le_adv_param adv_param = {
    //    .id = BT_ID_DEFAULT,
    //    .sid = 0U,
    //    .secondary_max_skip = 0U,
    //    .options = BT_LE_ADV_OPT_EXT_ADV,
	//	.interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
	//	.interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
	//	.peer = NULL,
    //};

    //int err;

    // WARNING: DOESNT WORK DUE TO PDU SEMAPHORE TIMEOUT ON SETUP
    // Create advertising set
    //0 - temp | 1 - humidity | 2 - co2 | 3 - tvoc 
    //for (int i = 0; i < CONFIG_BT_EXT_ADV_MAX_ADV_SET; i++) {
    //    adv_param.sid = i;
    //    
    //    err = bt_le_ext_adv_create(&adv_param, NULL, &adv[i]);
    //    if (err) {
	//		printk("Failed to create advertising set %d (err %d)\n",
	//		       i, err);
	//		return err;
	//	}
    //}
    // Set initial data
    //bt_le_ext_adv_set_data(adv[0], ad_temp, ARRAY_SIZE(ad_temp), NULL, 0);
    //bt_le_ext_adv_set_data(adv[1], ad_humidity, ARRAY_SIZE(ad_humidity), NULL, 0);
    //bt_le_ext_adv_set_data(adv[2], ad_co2, ARRAY_SIZE(ad_co2), NULL, 0);
    //bt_le_ext_adv_set_data(adv[3], ad_tvoc, ARRAY_SIZE(ad_tvoc), NULL, 0);
    

    while (1) {
        k_sem_take(&data_broadcast_sem, K_FOREVER);
        // broadcast depending on flags
        //if (mobile_flags & (1 << 0)) bt_le_ext_adv_start(adv[0], BT_LE_EXT_ADV_START_DEFAULT);
        //if (mobile_flags & (1 << 1)) bt_le_ext_adv_start(adv[1], BT_LE_EXT_ADV_START_DEFAULT);
        //if (mobile_flags & (1 << 2)) bt_le_ext_adv_start(adv[2], BT_LE_EXT_ADV_START_DEFAULT);
        //if (mobile_flags & (1 << 3)) bt_le_ext_adv_start(adv[3], BT_LE_EXT_ADV_START_DEFAULT);

        gpio_pin_set_dt(&led_green, 1);
        gpio_pin_set_dt(&led_red, 0);

        if (mobile_flags & (1 << 0)) {
            bt_le_adv_start(BT_LE_ADV_NCONN, ad_temp, ARRAY_SIZE(ad_temp), NULL, 0);
            k_msleep(DEFAULT_BROADCAST_TIME);
            bt_le_adv_stop();
        }
        if (mobile_flags & (1 << 1)) {
            bt_le_adv_start(BT_LE_ADV_NCONN, ad_humidity, ARRAY_SIZE(ad_humidity), NULL, 0);
            k_msleep(DEFAULT_BROADCAST_TIME);
            bt_le_adv_stop();
        }
        if (mobile_flags & (1 << 2)) {
            bt_le_adv_start(BT_LE_ADV_NCONN, ad_co2, ARRAY_SIZE(ad_co2), NULL, 0);
            k_msleep(DEFAULT_BROADCAST_TIME);
            bt_le_adv_stop();
        }
        if (mobile_flags & (1 << 3)) {
            bt_le_adv_start(BT_LE_ADV_NCONN, ad_tvoc, ARRAY_SIZE(ad_tvoc), NULL, 0);
            k_msleep(DEFAULT_BROADCAST_TIME);
            bt_le_adv_stop();
        }

        //k_msleep(DEFAULT_DELAY);

        // if not continuous, don't give semaphore back
        if (!(mobile_flags & (1 << 4))) continue;
        
        gpio_pin_set_dt(&led_green, 0);
        gpio_pin_set_dt(&led_red, 1);
        k_sem_give(&data_broadcast_sem);
        
        // just suppress errors, everything will be stopped
        //bt_le_ext_adv_stop(adv[0]);
        //bt_le_ext_adv_stop(adv[1]);
        //bt_le_ext_adv_stop(adv[2]);
        //bt_le_ext_adv_stop(adv[3]);
    }

}

int bt_scanner_entry_point() {
    struct bt_le_scan_param scan_param = {
        .type       = BT_LE_SCAN_TYPE_PASSIVE,
		.options    = BT_LE_SCAN_OPT_NONE,
		.interval   = BT_GAP_SCAN_FAST_INTERVAL,
		.window     = BT_GAP_SCAN_FAST_WINDOW
    };
    int err = 0;

    err = bt_enable(NULL);
    if (err) {
        printk("Bluetooth init failed (err %d)\n", err);
        return err;
    }

    err = bt_le_scan_start(&scan_param, scan_cb);
    if (err) {
        printk("Starting scanning failed (err %d)\n", err);
        return err;
    }
    return err;
}

K_THREAD_DEFINE(bt_scanner, STACKSIZE, bt_scanner_entry_point, NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(update_sensors, STACKSIZE, update_sensors_entry_point, NULL, NULL, NULL, PRIORITY, 0, 0);
K_THREAD_DEFINE(broadcast, STACKSIZE, broadcast_sensors_entry_point, NULL, NULL, NULL, PRIORITY, 0, 0);

int main(void) {
    printk("Mobile Node\n");

    /*
    RED: IDLE
    GREEN: BROADCASTING
    BLUE: UPDATING DATA
    */
    gpio_pin_configure_dt(&led_red, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_ACTIVE);
    gpio_pin_configure_dt(&led_blue, GPIO_OUTPUT_ACTIVE);

    gpio_pin_set_dt(&led_red, 1);
    gpio_pin_set_dt(&led_green, 0);
    gpio_pin_set_dt(&led_blue, 0);

    return 0;
}