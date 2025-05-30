#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/version.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>
#include <ctype.h>
#include <zephyr/types.h>
#include <stddef.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/__assert.h>
#include <string.h>
#include <zephyr/sys/slist.h>
 
#include <zephyr/bluetooth/bluetooth.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <zephyr/data/json.h>

//double total_distance_travelled = 0;
//double average_velocity = 0;
//int8_t MeasuredPower = -55;

bool send_cmd_to_mobile = false;
int cmd_to_mobile = 0;
int modeInput = 0;
int sensorInput = 0;
int curMode = 1; //1 for descite, 2 for continousous

#define STACKSIZE 4096
#define PRIORITY 7

struct json_sensor_data {
	const char* temperature;
	const char* humidity;
	const char* C02;
	const char* TVOC;
};

struct sensor_data {
	const uint8_t sensor; //1 for temp, 2 for humidity, 4 for C02, 8 for TVOC
	bool readyToTransmit;
	double value;
};

struct sensor_data tempSensor = {1, false, 0};

struct sensor_data humSensor = {2, false, 0};
struct sensor_data C02Sensor = {4, false, 0};
struct sensor_data TVOCSensor = {8, false, 0};

// struct iBeaconNode {
// 	const char* Name;
// 	const char* macAddr;
// 	const char* major;
// 	const char* minor;
// 	const char* Xcoord;
// 	const char* Ycoord;
// 	const char* leftNeighbour;
// 	const char* rightNeighbour;
// 	sys_snode_t next;
// };

// sys_slist_t ibeaconNodes;
// int8_t RSSIs[8];
// double distances[8];
// bool newRSSIData;
// bool newUltraData;
// uint16_t ultra_distance;
// double ultrasonic_sensor_pos[2] = {1.5, 0};

static const struct json_obj_descr sensor_data_descr[] = {
	JSON_OBJ_DESCR_PRIM(struct json_sensor_data, temperature, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct json_sensor_data, humidity, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct json_sensor_data, C02, JSON_TOK_STRING),
	JSON_OBJ_DESCR_PRIM(struct json_sensor_data, TVOC, JSON_TOK_STRING)
};

void create_sensor_data_json() {
	char tempbuffer[20];
	char humbuffer[20];
	char c02buffer[20];
	char tvocbuffer[20];
	snprintf(tempbuffer, sizeof(tempbuffer), "%f", tempSensor.value);
	snprintf(humbuffer, sizeof(humbuffer), "%f", humSensor.value);
	snprintf(c02buffer, sizeof(tempbuffer), "%f", C02Sensor.value);
	snprintf(tvocbuffer, sizeof(humbuffer), "%f", TVOCSensor.value);

    // // //printf("The RSSI string is: \"%s\"\n", strRSSI);
    // // //printf("The distance string is: \"%s\"\n", strDistance);

	struct json_sensor_data data = {
		.temperature = tempbuffer,
		.humidity = humbuffer,
		.C02 = c02buffer,
		.TVOC = tvocbuffer
	};
	ssize_t len = json_calc_encoded_len(sensor_data_descr,
		ARRAY_SIZE(sensor_data_descr),
		&data);
	char json_buf[len+1];
	json_obj_encode_buf(sensor_data_descr, ARRAY_SIZE(sensor_data_descr),
								&data, json_buf, sizeof(json_buf));
	
	printk("JSON: %s\n", json_buf);
}

#define MOBILE_RSSI 0xC8
#define DEFAULT_CMD 0x00, 0x00, 0x00, 0x00

#define PACKET_PREAMBLE_MOBILE 0x4c, 0x00,\
 0x02, 0x15, \
 0x1a, 0xbb, 0xe1, 0xed,\
 0xde, 0xad, 0xfa, 0x11,\
 0xba, 0xff, 0x1e, 0xdb,\
 0xee, 0x5f, 0x10, 0x55

 #define DEFAULT_SENSOR 0xFF, 0xFF

 #define PACKET_PREAMBLE_VIEWER 0x4c, 0x00,\
 0x02, 0x15, \
 0xca, 0xb1, 0xeb, 0x1a,\
 0xde, 0xca, 0x5c, 0xad,\
 0xe0, 0xaf, 0x00, 0x00,\
 0x00, 0x00, 0x00

static uint8_t packet_data_mobile[25] = { 
	PACKET_PREAMBLE_MOBILE,
	DEFAULT_CMD,
	MOBILE_RSSI
};

static uint8_t packet_data_viewer[25] = { 
	PACKET_PREAMBLE_VIEWER,
	DEFAULT_SENSOR,
	MOBILE_RSSI
};

static const struct bt_data ad_mobile[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, packet_data_mobile, 25),
};

static const struct bt_data ad_viewer[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, packet_data_viewer, 25),
};

 static const bt_addr_le_t target_addr = {
	.type = BT_ADDR_LE_RANDOM,
	.a.val = {0x97, 0xae, 0x9b, 0x90, 0x97, 0x3d}
 };

 static void parse_ibeacon_data(struct net_buf_simple *ad)
 {
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
				 uint8_t tx_power = (uint8_t)net_buf_simple_pull_u8(ad);
 
				 //printk("iBeacon UUID: %s\n", uuid_str);
				 //printk("Major: %x, Minor: %x, TX Power: %d dBm\n", major, minor, tx_power);

				 //uint8_t tempVals[8] = {uuid[12], uuid[13], uuid[14], uuid[15], (major >> 8) & 0xFF, major & 0xFF, (minor >> 8) & 0xFF, minor & 0xFF };
				 uint8_t tempVals[8] = {minor & 0xFF, (minor >> 8) & 0xFF, major & 0xFF, (major >> 8) & 0xFF, uuid[15], uuid[14], uuid[13], uuid[12] };
				 double convertedVal;
				 memcpy(&convertedVal, tempVals, sizeof(convertedVal));
				 uint8_t sensor = uuid[10];
				 printf("sensor is: %d\n", sensor);
				 printf("Converted to double: %lf\n", convertedVal);
				 if (isnan(convertedVal)){
					convertedVal = 0;
				 }
				 if (sensor == 1) { //change this to 1
					tempSensor.value = convertedVal;
					tempSensor.readyToTransmit = true;
				 } else if (sensor == 2) {
					humSensor.value = convertedVal;
					humSensor.readyToTransmit = true;
				 } else if (sensor == 4) {
					C02Sensor.value = convertedVal;
					C02Sensor.readyToTransmit = true;
				 } else if (sensor == 8) {
					TVOCSensor.value = convertedVal;
					TVOCSensor.readyToTransmit = true;
				 }
				 
			 } else {
				 // Not an iBeacon
				 net_buf_simple_pull(ad, len - 4);
			 }
		 } else {
			 net_buf_simple_pull(ad, len - 1);
		 }
	 }
}

static const uint8_t expected_uuid_mobile[10] = {
    0xCA, 0x11, 0xED, 0xBA, 0x1D, 0xfa, 0xca, 0xde, 0x7a, 0x1e
};

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
                    //printk("✅ Found target iBeacon with UUID at %s, RSSI: %d dBm\n", addr_str, rssi);
					parse_ibeacon_data(ad);
                }
            }
        }
        i += (len - 1);
    }
	
}
 
 int bluetooth_driver(void)
 {
	 struct bt_le_scan_param scan_param = {
		 .type       = BT_LE_SCAN_TYPE_PASSIVE,
		 .options    = BT_LE_SCAN_OPT_NONE,
		 .interval   = BT_GAP_SCAN_FAST_INTERVAL,
		 .window     = BT_GAP_SCAN_FAST_WINDOW,
	 };
	 int err;


	 err = bt_enable(NULL);
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
	 return 0;
 }

 void transmit_to_viewer(void) {
	printk("Starting broadcast to viewer\n");
    int err = bt_le_adv_start(BT_LE_ADV_NCONN, ad_viewer, ARRAY_SIZE(ad_viewer), NULL, 0);
    if (err) {
		printk("Advertising failed to start (err %d)\n", err);
	}
    k_msleep(500);
	err = bt_le_adv_stop();
    if (err) {
		printk("Advertising failed to stop (err %d)\n", err);
	}
    printk("Ending broadcast to viewer\n");
 }

 void dashboard_driver(void) {
	while (1) {
		if (send_cmd_to_mobile == true) {
				uint8_t low_byte_X = (uint8_t)sensorInput;
    			uint8_t high_byte_X = (uint8_t)cmd_to_mobile;
				uint8_t low_byte_Y = 0;
    			uint8_t high_byte_Y = (uint8_t)curMode;
				packet_data_mobile[20] = high_byte_X;
				packet_data_mobile[21] = low_byte_X;
				packet_data_mobile[22] = high_byte_Y;
				packet_data_mobile[23] = low_byte_Y;
				//create_iBeacon_data_json(numElements);
				printk("Starting broadcast to mobile\n");
        		int err = bt_le_adv_start(BT_LE_ADV_NCONN, ad_mobile, ARRAY_SIZE(ad_mobile), NULL, 0);
        		if (err) {
					printk("Advertising failed to start (err %d)\n", err);
					return;
				}
        		k_msleep(400);

				err = bt_le_adv_stop();
        		if (err) {
					printk("Advertising failed to stop (err %d)\n", err);
					return;
				}
        		printk("Ending broadcast to mobile\n");
				send_cmd_to_mobile = false;
		} else if (tempSensor.readyToTransmit && humSensor.readyToTransmit && C02Sensor.readyToTransmit && TVOCSensor.readyToTransmit) {
		//} else if (tempSensor.readyToTransmit) {
			printf("SENDING temp: %f\n", tempSensor.value);
			tempSensor.readyToTransmit = false;
			printf("SENDING humidity: %f\n", humSensor.value);
			humSensor.readyToTransmit = false;
			printf("SENDING C02: %f\n", C02Sensor.value);
			C02Sensor.readyToTransmit = false;
			printf("SENDING TVOC: %f\n", TVOCSensor.value);
			TVOCSensor.readyToTransmit = false;

			create_sensor_data_json();
			
			uint8_t eight_byte_array[8] = {0,0,0,0,0,0,0,0};
			memcpy(eight_byte_array, &tempSensor.value, sizeof(tempSensor.value));
			packet_data_viewer[14] = (uint8_t)tempSensor.sensor;
			packet_data_viewer[15] = 0;
			packet_data_viewer[16] = eight_byte_array[0];
			packet_data_viewer[17] = eight_byte_array[1];
			packet_data_viewer[18] = eight_byte_array[2];
			packet_data_viewer[19] = eight_byte_array[3];
			packet_data_viewer[20] = eight_byte_array[4];
			packet_data_viewer[21] = eight_byte_array[5];
			packet_data_viewer[22] = eight_byte_array[6];
			packet_data_viewer[23] = eight_byte_array[7];
			transmit_to_viewer();
			k_msleep(200);

			memcpy(eight_byte_array, &humSensor.value, sizeof(humSensor.value));
			packet_data_viewer[14] = (uint8_t)humSensor.sensor;
			packet_data_viewer[15] = 0;
			packet_data_viewer[16] = eight_byte_array[0];
			packet_data_viewer[17] = eight_byte_array[1];
			packet_data_viewer[18] = eight_byte_array[2];
			packet_data_viewer[19] = eight_byte_array[3];
			packet_data_viewer[20] = eight_byte_array[4];
			packet_data_viewer[21] = eight_byte_array[5];
			packet_data_viewer[22] = eight_byte_array[6];
			packet_data_viewer[23] = eight_byte_array[7];
			transmit_to_viewer();
			k_msleep(200);

			memcpy(eight_byte_array, &C02Sensor.value, sizeof(C02Sensor.value));
			packet_data_viewer[14] = (uint8_t)C02Sensor.sensor;
			packet_data_viewer[15] = 0;
			packet_data_viewer[16] = eight_byte_array[0];
			packet_data_viewer[17] = eight_byte_array[1];
			packet_data_viewer[18] = eight_byte_array[2];
			packet_data_viewer[19] = eight_byte_array[3];
			packet_data_viewer[20] = eight_byte_array[4];
			packet_data_viewer[21] = eight_byte_array[5];
			packet_data_viewer[22] = eight_byte_array[6];
			packet_data_viewer[23] = eight_byte_array[7];
			transmit_to_viewer();
			k_msleep(200);

			memcpy(eight_byte_array, &TVOCSensor.value, sizeof(TVOCSensor.value));
			packet_data_viewer[14] = (uint8_t)TVOCSensor.sensor;
			packet_data_viewer[15] = 0;
			packet_data_viewer[16] = eight_byte_array[0];
			packet_data_viewer[17] = eight_byte_array[1];
			packet_data_viewer[18] = eight_byte_array[2];
			packet_data_viewer[19] = eight_byte_array[3];
			packet_data_viewer[20] = eight_byte_array[4];
			packet_data_viewer[21] = eight_byte_array[5];
			packet_data_viewer[22] = eight_byte_array[6];
			packet_data_viewer[23] = eight_byte_array[7];
			transmit_to_viewer();

			k_msleep(500);
		} else {
			k_msleep(100);
		}		
	}
}

#define UART_DEVICE_NODE DT_CHOSEN(zephyr_shell_uart)

#define MSG_SIZE 32

K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 10, 4);

static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

static char rx_buf[MSG_SIZE];
static int rx_buf_pos;

void serial_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	if (!uart_irq_update(uart_dev)) {
		return;
	}

	if (!uart_irq_rx_ready(uart_dev)) {
		return;
	}

	while (uart_fifo_read(uart_dev, &c, 1) == 1) {
		if ((c == '\n' || c == '\r') && rx_buf_pos > 0) {
			rx_buf[rx_buf_pos] = '\0';

			k_msgq_put(&uart_msgq, &rx_buf, K_NO_WAIT);

			rx_buf_pos = 0;
		} else if (rx_buf_pos < (sizeof(rx_buf) - 1)) {
			rx_buf[rx_buf_pos++] = c;
		}
	}
}

void print_uart(char *buf)
{
	int msg_len = strlen(buf);
	printk("%s", buf);
}

void serialInput_driver(void) {
	char tx_buf[MSG_SIZE];
	if (!device_is_ready(uart_dev)) {
		printk("UART device not found!");
	}

	int ret = uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);

	if (ret < 0) {
		if (ret == -ENOTSUP) {
			printk("Interrupt-driven UART API support not enabled\n");
		} else if (ret == -ENOSYS) {
			printk("UART device does not support interrupt-driven API\n");
		} else {
			printk("Error setting UART callback: %d\n", ret);
		}
	}
	uart_irq_rx_enable(uart_dev);

	print_uart("Hello! I'm your echo bot.\r\n");
	print_uart("Tell me something and press enter:\r\n");

	while (k_msgq_get(&uart_msgq, &tx_buf, K_FOREVER) == 0) {
		printk("sending: ");
		printk("%s\n", tx_buf);
		printk("SENT\n");
		char firstThree[4];
		strncpy(firstThree, tx_buf, 3);
		firstThree[3] = '\0';
		if (strcmp(firstThree, "cmd") == 0) {
			char cmdType[2];
			strncpy(cmdType, tx_buf+4, 1);
			cmdType[1] = '\0';
			// s for sensor
			if (strcmp(cmdType, "s") == 0) {
				cmd_to_mobile = 1;
				//sensorInput = tx_buf[6] - '0';
				char num_str[3];
				int integer_value = 0;
    			int length;
				strncpy(num_str, tx_buf+6, 2);
				num_str[2] = '\0';
				length = strlen(num_str);
				//printf("String is: %s\n", num_str);

    			// Check if the input is valid (1 or 2 digits)
    			if (length == 0 || length > 2) {
    			    printf("Invalid input. Please enter a number with 1 or 2 digits.\n");
    			}
    			for (int i = 0; i < length; i++) {
    			    if (num_str[i] >= '0' && num_str[i] <= '9') {
    			        integer_value = integer_value * 10 + (num_str[i] - '0');
    			    } else {
    			        printf("Invalid input. Please enter only digits.\n");
    			    }
    			}
    			//printf("The integer value is: %d\n", integer_value);
				printk("RECEIVED A COMMAND\n");
				sensorInput = integer_value;
				send_cmd_to_mobile = true;
				modeInput = 0;
				curMode = 0;

			// m for mode
			} else if (strcmp(cmdType, "m") == 0) {
				cmd_to_mobile = 2;
				sensorInput = 0;
				char modeSelect[2];
				strncpy(modeSelect, tx_buf+6, 1);
				modeSelect[1] = '\0';

				//t for toggle
				if (strcmp(modeSelect, "t") == 0) {
					modeInput = 1;
					printk("RECEIVED A COMMAND\n");
					send_cmd_to_mobile = true;
					if (curMode == 2) {
						curMode = 3;
					} else {
						curMode = 2;
					}
				
				// d for discrete
				} else if (strcmp(modeSelect, "d") == 0) {
					modeInput = 2;
					printk("RECEIVED A COMMAND\n");
					send_cmd_to_mobile = true;
					curMode = 2;
					
				// c for continuous
				} else if (strcmp(modeSelect, "c") == 0) {
					modeInput = 3;
					printk("RECEIVED A COMMAND\n");
					send_cmd_to_mobile = true;
					curMode = 3;
				}
			}
			
		//} else {
			//printk("%s\n", firstThree);
		}
	}
}
 
K_THREAD_DEFINE(bluetooth_id, STACKSIZE, bluetooth_driver, NULL, NULL, NULL,
	PRIORITY, 0, 0);
K_THREAD_DEFINE(dashboard_id, STACKSIZE, dashboard_driver, NULL, NULL, NULL,
	PRIORITY, 0, 0);
K_THREAD_DEFINE(serialInput_id, STACKSIZE, serialInput_driver, NULL, NULL, NULL,
	PRIORITY, 0, 0);
