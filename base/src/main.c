/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

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

double total_distance_travelled = 0;
double average_velocity = 0;
int8_t MeasuredPower = -55;

bool send_cmd_to_mobile = false;
int cmd_to_mobile = 0;
int modeInput = 0;
int sensorInput = 0;

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
//tempSensor.sensor = 1;
//tempSensor.readyToTransmit = false;
//tempSensor.value = 0;

struct sensor_data humSensor = {2, false, 0};
struct sensor_data C02Sensor = {4, false, 0};
struct sensor_data TVOCSensor = {8, false, 0};

//tempSensor.sensor = 1;

struct iBeaconNode {
	const char* Name;
	const char* macAddr;
	const char* major;
	const char* minor;
	const char* Xcoord;
	const char* Ycoord;
	const char* leftNeighbour;
	const char* rightNeighbour;
	sys_snode_t next;
};

sys_slist_t ibeaconNodes;
int8_t RSSIs[8];
double distances[8];
bool newRSSIData;
bool newUltraData;
uint16_t ultra_distance;
double ultrasonic_sensor_pos[2] = {1.5, 0};

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
 0x1a, 0xbb, 0xe1, 0xed,\
 0xde, 0xad, 0xfa, 0x11,\
 0x00, 0x00, 0x00, 0x00,\
 0x00, 0x00, 0x00, 0x00

 #define DEFAULT_SENSOR 0xFF, 0xFF

 #define PACKET_PREAMBLE_VIEWER 0x4c, 0x00,\
 0xca, 0xb1, 0xeb, 0x1a,\
 0xde, 0x00, 0x00, 0x00,\
 0x00, 0x00, 0x00, 0x00,\
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

				 uint8_t tempVals[8] = {uuid[12], uuid[13], uuid[14], uuid[15], (major >> 8) & 0xFF, major & 0xFF, (minor >> 8) & 0xFF, minor & 0xFF };
				 double convertedVal;
				 memcpy(&convertedVal, tempVals, sizeof(convertedVal));
				 //printf("Original Bytes: ");
				 //for (int i = 0; i < 8; i++) {
					//printf("0x%02X ", tempVals[i]);
				 //}
				 //printf("\n");
				 uint8_t sensor = uuid[11];
				 printf("sensor is: %d\n", sensor);
				 printf("Converted to double: %lf\n", convertedVal);
				 if (sensor == 0) { //change this to 1
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
				 //  for (int i = 0; i < 8; i++) {
				// 	RSSIs[i] = tempRSSIs[i];
				// }
				// bool valid = false;
				// double tempDistances[8];
				//  for (int i = 0; i < 8; i++) {
				// 	//tempDistances[i] = rssi_to_distance_cm(RSSIs[i]);
				// 	if (tempDistances[i] >= -55) {
				// 		valid = true;
				// 	}
				//  } 
				// if (valid = true) {
				// 	for (int i = 0; i < 8; i++) {
				// 		distances[i] = tempDistances[i];
				// 	}
				// 	newRSSIData = true;
				// }
				 
			 } else {
				 // Not an iBeacon
				 net_buf_simple_pull(ad, len - 4);
			 }
		 } else {
			 net_buf_simple_pull(ad, len - 1);
		 }
	 }
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
				int8_t tx_power = (int8_t)net_buf_simple_pull_u8(ad);

				// printk("iBeacon UUID: %s\n", uuid_str);
				// printk("Major: %x, Minor: %x, TX Power: %d dBm\n", major, minor, tx_power);
				if (minor < 500) {
					ultra_distance = minor;
					newUltraData = true;
					
				} //else {
					//printk("AHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH");
				//}
			} else {
				net_buf_simple_pull(ad, len - 4);
			}
		} else {
			net_buf_simple_pull(ad, len - 1);
		}
	}
}

static const uint8_t expected_uuid_mobile[5] = {
    0xCA, 0x11, 0xED, 0xBA, 0x1D
};

// static const uint8_t expected_uuid_ultra[16] = {
//     0xDE, 0xAD, 0xCE, 0x11,
// 	0xCA, 0xFE, 0xBA, 0xBE,
// 	0xDA, 0xF7, 0xDE, 0xC0,
// 	0xDE, 0x5A, 0xD1, 0xAD
// };

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
                if (memcmp(uuid, expected_uuid_mobile, 5) == 0) {
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

void add_ibeacon(char* name, char* mac, char* major, char* minor, char* X, char* Y, char* left, char* right) {
	struct iBeaconNode *ibeaconA = malloc(sizeof(struct iBeaconNode));
	if (ibeaconA != NULL) {
		ibeaconA-> Name = strdup(name);
		ibeaconA->macAddr = strdup(mac);
		ibeaconA->major = strdup(major);
		ibeaconA->minor = strdup(minor);
		ibeaconA->Xcoord = strdup(X);
		ibeaconA->Ycoord = strdup(Y);
		ibeaconA->leftNeighbour = strdup(left);
		ibeaconA->rightNeighbour = strdup(right);
		sys_slist_append(&ibeaconNodes, &ibeaconA->next);
		
	}
}

void add_ibeacons() {
	add_ibeacon("4011-A", "F5:75:FE:85:34:67", "2753", "32998", "0", "0", "4011-H", "4011-B");
	add_ibeacon("4011-B", "E5:73:87:06:1E:86", "32975", "20959", "0", "0", "4011-A", "4011-C");
	add_ibeacon("4011-C", "CA:99:9E:FD:98:B1", "26679", "40363", "0", "0", "4011-B", "4011-D");
	add_ibeacon("4011-D", "CB:1B:89:82:FF:FE", "41747", "38800", "0", "0", "4011-C", "4011-E");
	add_ibeacon("4011-E", "D4:D2:A0:A4:5C:AC", "30679", "51963", "0", "0", "4011-D", "4011-F");
	add_ibeacon("4011-F", "C1:13:27:E9:B7:7C", "6195", "18394", "0", "0", "4011-E", "4011-G");
	add_ibeacon("4011-G", "F1:04:48:06:39:A0", "30525", "30544", "0", "0", "4011-F", "4011-H");
	add_ibeacon("4011-H", "CA:0C:E0:DB:CE:60", "57395", "28931", "0", "0", "4011-G", "4011-A");
}

struct iBeaconNode* get_ibeacon_node(sys_snode_t* node_ptr) {
    if (node_ptr == NULL) {
        return NULL;
    }
    return (struct iBeaconNode*)((char*)node_ptr - offsetof(struct iBeaconNode, next));
}

int remove_node(char* targetName) {
    sys_snode_t *current_snode = sys_slist_peek_head(&ibeaconNodes);
	k_sleep(K_MSEC(1000));
    sys_snode_t *prev_snode = NULL;
    struct iBeaconNode *current_node;

    while (current_snode != NULL) {
        current_node = get_ibeacon_node(current_snode);
        if (current_node->Name != NULL && strcmp(current_node->Name, targetName) == 0) {
            sys_slist_remove(&ibeaconNodes, prev_snode, current_snode);
            return 0;
        }
        prev_snode = current_snode;
        current_snode = sys_slist_peek_next(current_snode);
    }

    return -ENOENT;
}

void sheller(void){
	#if DT_NODE_HAS_COMPAT(DT_CHOSEN(zephyr_shell_uart), zephyr_cdc_acm_uart)
		 const struct device *dev;
		 uint32_t dtr = 0;
	
		 dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));
		while (!dtr) {
			uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
			k_sleep(K_MSEC(100));
		}
	#endif
	sys_slist_init(&ibeaconNodes);
	add_ibeacons();
}

void cmd_iBeacon(const struct shell *sh, size_t argc, char **argv) {
	if (strcmp(argv[1], "list") == 0 && argc == 3) {
		if (strcmp(argv[2], "all") == 0) {
			sys_snode_t *current;
			SYS_SLIST_FOR_EACH_NODE(&ibeaconNodes, current) {
				struct iBeaconNode *item = CONTAINER_OF(current, struct iBeaconNode, next);
				printk("Name: %s\n", item->Name);
				printk("MAC Address: %s\n", item->macAddr);
				printk("Major Value: %s\n", item->major);
				printk("Minor Value: %s\n", item->minor);
				printk("X Coordinate: %s\n", item->Xcoord);
				printk("Y Coordinate: %s\n", item->Ycoord);
				printk("Left Neighbour: %s\n", item->leftNeighbour);
				printk("Right Neighbour: %s\n", item->rightNeighbour);
				printk("\n");
			}
		} else {
			sys_snode_t *current;
			SYS_SLIST_FOR_EACH_NODE(&ibeaconNodes, current) {
				struct iBeaconNode *item = CONTAINER_OF(current, struct iBeaconNode, next);
				if (strcmp(item->Name, argv[2]) == 0){
					printk("Name: %s\n", item->Name);
					printk("MAC Address: %s\n", item->macAddr);
					printk("Major Value: %s\n", item->major);
					printk("Minor Value: %s\n", item->minor);
					printk("X Coordinate: %s\n", item->Xcoord);
					printk("Y Coordinate: %s\n", item->Ycoord);
					printk("Left Neighbour: %s\n", item->leftNeighbour);
					printk("Right Neighbour: %s\n", item->rightNeighbour);
					printk("\n");
				}
			}
		}
	} else if (strcmp(argv[1], "add") == 0 && argc == 10) {
		add_ibeacon(argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], argv[8], argv[9]);
	//} else if (strcmp(argv[1], "test") == 0) {
		//construct_linear_system_v3();
    	//solve_least_squares();
	} else if (strcmp(argv[1], "remove") == 0 && argc == 3) {
		remove_node(argv[2]);
	}
}
 
SHELL_CMD_ARG_REGISTER(iBeacon, NULL, "iBeacon", cmd_iBeacon, 2, 9);
 
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
    k_msleep(400);
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
    			uint8_t high_byte_Y = (uint8_t)modeInput;
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
        		k_msleep(1000);

				err = bt_le_adv_stop();
        		if (err) {
					printk("Advertising failed to stop (err %d)\n", err);
					return;
				}
        		printk("Ending broadcast to mobile\n");
				send_cmd_to_mobile = false;
		//} else if (tempSensor.readyToTransmit && humSensor.readyToTransmit && C02Sensor.readyToTransmit && TVOCSensor.readyToTransmit) {
		} else if (tempSensor.readyToTransmit) {
			printf("SENDING temp: %f\n", tempSensor.value);
			tempSensor.readyToTransmit = false;
			printf("SENDING humidity: %f\n", humSensor.value);
			humSensor.readyToTransmit = false;
			printf("SENDING C02: %f\n", C02Sensor.value);
			C02Sensor.readyToTransmit = false;
			printf("SENDING TVOC: %f\n", TVOCSensor.value);
			TVOCSensor.readyToTransmit = false;

			//DO THE DASHBOARD STUFF HERE
			create_sensor_data_json();
			
			uint8_t eight_byte_array[8] = {0,0,0,0,0,0,0,0};
			memcpy(eight_byte_array, &tempSensor.value, sizeof(tempSensor.value));
			// printf("Converted to Bytes (System Endianness): ");
    		// for (int i = 0; i < 8; i++) {
    		//     printf("0x%02X ", eight_byte_array[i]);
    		// }
    		// printf("\n");
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
			k_msleep(500);
		}		
	}
}

/* change this to any other UART peripheral if desired */
#define UART_DEVICE_NODE DT_CHOSEN(zephyr_shell_uart)

#define MSG_SIZE 32

/* queue to store up to 10 messages (aligned to 4-byte boundary) */
K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 10, 4);

static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

/* receive buffer used in UART ISR callback */
static char rx_buf[MSG_SIZE];
static int rx_buf_pos;

/*
 * Read characters from UART until line end is detected. Afterwards push the
 * data to the message queue.
 */
void serial_cb(const struct device *dev, void *user_data)
{
	uint8_t c;

	if (!uart_irq_update(uart_dev)) {
		return;
	}

	if (!uart_irq_rx_ready(uart_dev)) {
		return;
	}

	/* read until FIFO empty */
	while (uart_fifo_read(uart_dev, &c, 1) == 1) {
		if ((c == '\n' || c == '\r') && rx_buf_pos > 0) {
			/* terminate string */
			rx_buf[rx_buf_pos] = '\0';

			/* if queue is full, message is silently dropped */
			k_msgq_put(&uart_msgq, &rx_buf, K_NO_WAIT);

			/* reset the buffer (it was copied to the msgq) */
			rx_buf_pos = 0;
		} else if (rx_buf_pos < (sizeof(rx_buf) - 1)) {
			rx_buf[rx_buf_pos++] = c;
		}
		/* else: characters beyond buffer size are dropped */
	}
}

/*
 * Print a null-terminated string character by character to the UART interface
 */
void print_uart(char *buf)
{
	int msg_len = strlen(buf);
	printk("%s", buf);
	// for (int i = 0; i < msg_len; i++) {
	// 	uart_poll_out(uart_dev, buf[i]);
	// }
}

void serialInput_driver(void) {
	char tx_buf[MSG_SIZE];
	if (!device_is_ready(uart_dev)) {
		printk("UART device not found!");
	}

	/* configure interrupt and callback to receive data */
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

	/* indefinitely wait for input from the user */
	while (k_msgq_get(&uart_msgq, &tx_buf, K_FOREVER) == 0) {
		//print_uart("Echo: ");
		//print_uart(tx_buf);
		//print_uart("\r\n");
		printk("%s\n", tx_buf);
		//do the strcmp stuff
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

    			// Convert the string to an integer
    			// Iterate through each character of the string
    			for (int i = 0; i < length; i++) {
    			    // Check if the character is a digit
    			    if (num_str[i] >= '0' && num_str[i] <= '9') {
    			        // For each digit, multiply the current integer_value by 10
    			        // and add the numeric value of the current digit.
    			        // Subtract '0' (ASCII 48) to get the actual integer value of the digit.
    			        integer_value = integer_value * 10 + (num_str[i] - '0');
    			    } else {
    			        printf("Invalid input. Please enter only digits.\n");
    			    }
    			}

    			// Print the converted integer value
    			//printf("The integer value is: %d\n", integer_value);
				printk("RECEIVED A COMMAND\n");
				sensorInput = integer_value;
				send_cmd_to_mobile = true;
				modeInput = 0;

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
				
				// d for discrete
				} else if (strcmp(modeSelect, "d") == 0) {
					modeInput = 2;
					printk("RECEIVED A COMMAND\n");
					send_cmd_to_mobile = true;
					
				// c for continuous
				} else if (strcmp(modeSelect, "c") == 0) {
					modeInput = 3;
					printk("RECEIVED A COMMAND\n");
					send_cmd_to_mobile = true;
				}
			}
			
		} else {
			printk("%s\n", firstThree);
		}
	}
}
 
K_THREAD_DEFINE(sheller_id, STACKSIZE, sheller, NULL, NULL, NULL,
	PRIORITY, 0, 0);
K_THREAD_DEFINE(bluetooth_id, STACKSIZE, bluetooth_driver, NULL, NULL, NULL,
	PRIORITY, 0, 0);
K_THREAD_DEFINE(dashboard_id, STACKSIZE, dashboard_driver, NULL, NULL, NULL,
	PRIORITY, 0, 0);
K_THREAD_DEFINE(serialInput_id, STACKSIZE, serialInput_driver, NULL, NULL, NULL,
	PRIORITY, 0, 0);
