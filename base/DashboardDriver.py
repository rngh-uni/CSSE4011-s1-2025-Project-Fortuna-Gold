import serial
import requests
import time
import json
import paho.mqtt.client as mqtt
import ssl
from threading import Lock, Thread, current_thread, Event
from web3 import Web3

TAGOIO_DEVICE_TOKEN = 'f30ee999-d492-45df-9802-ea9c8af0d51f'
TAGOIO_API_URL = 'https://api.tago.io/data'
HEADERS = {
    'Content-Type': 'application/json',
    'device-token': 'f30ee999-d492-45df-9802-ea9c8af0d51f'
}

# MQTT Configuration
MQTT_BROKER_HOST = 'mqtt.tago.io'
MQTT_BROKER_PORT = 8883 # Use 8883 for SSL/TLS for secure communication
MQTT_USERNAME = 'token'
MQTT_PASSWORD = TAGOIO_DEVICE_TOKEN # Password is typically empty for device tokens
MQTT_CLIENT_ID = TAGOIO_DEVICE_TOKEN
MQTT_PUBLISH_TOPIC = 'temperature'
MQTT_SUBSCRIBE_TOPIC = 'tago/device/commands' # Topic to receive commands from TagoIO

# --- Global MQTT Client Instance ---
mqtt_client = None
command_queue = [] # To store commands received from MQTT for main loop processing
queue_lock = Lock() # To make access to command_queue thread-safe

# --global vars --
temp_active = False
hum_active = False
c02_active = False
tvoc_active = False
num_of_mqtt = 0
mode_change = False
cur_mode = 'd'

GANACHE_URL = 'http://127.0.0.1:7545'
w3 = Web3(Web3.HTTPProvider(GANACHE_URL))




CONTRACT_ADDRESS = '0xF56b2c06ac8E092a33E435f96a2592A8d21B3383'
CONTRACT_ABI = json.loads('''
[
	{
		"anonymous": false,
		"inputs": [
			{
				"indexed": true,
				"internalType": "uint256",
				"name": "timestamp",
				"type": "uint256"
			},
			{
				"indexed": false,
				"internalType": "uint256",
				"name": "temperature",
				"type": "uint256"
			},
			{
				"indexed": false,
				"internalType": "uint256",
				"name": "humidity",
				"type": "uint256"
			},
			{
				"indexed": false,
				"internalType": "uint256",
				"name": "C02",
				"type": "uint256"
			},
			{
				"indexed": false,
				"internalType": "uint256",
				"name": "TVOC",
				"type": "uint256"
			}
		],
		"name": "DataStored",
		"type": "event"
	},
	{
		"inputs": [
			{
				"internalType": "uint256",
				"name": "_temperature",
				"type": "uint256"
			},
			{
				"internalType": "uint256",
				"name": "_humidity",
				"type": "uint256"
			},
			{
				"internalType": "uint256",
				"name": "_C02",
				"type": "uint256"
			},
			{
				"internalType": "uint256",
				"name": "_TVOC",
				"type": "uint256"
			}
		],
		"name": "storeSensorData",
		"outputs": [],
		"stateMutability": "nonpayable",
		"type": "function"
	},
	{
		"inputs": [
			{
				"internalType": "uint256",
				"name": "_index",
				"type": "uint256"
			}
		],
		"name": "getReading",
		"outputs": [
			{
				"internalType": "uint256",
				"name": "timestamp",
				"type": "uint256"
			},
			{
				"internalType": "uint256",
				"name": "temperature",
				"type": "uint256"
			},
			{
				"internalType": "uint256",
				"name": "humidity",
				"type": "uint256"
			},
			{
				"internalType": "uint256",
				"name": "C02",
				"type": "uint256"
			},
			{
				"internalType": "uint256",
				"name": "TVOC",
				"type": "uint256"
			}
		],
		"stateMutability": "view",
		"type": "function"
	},
	{
		"inputs": [],
		"name": "getReadingsCount",
		"outputs": [
			{
				"internalType": "uint256",
				"name": "",
				"type": "uint256"
			}
		],
		"stateMutability": "view",
		"type": "function"
	},
	{
		"inputs": [
			{
				"internalType": "uint256",
				"name": "",
				"type": "uint256"
			}
		],
		"name": "sensorReadings",
		"outputs": [
			{
				"internalType": "uint256",
				"name": "timestamp",
				"type": "uint256"
			},
			{
				"internalType": "uint256",
				"name": "temperature",
				"type": "uint256"
			},
			{
				"internalType": "uint256",
				"name": "humidity",
				"type": "uint256"
			},
			{
				"internalType": "uint256",
				"name": "C02",
				"type": "uint256"
			},
			{
				"internalType": "uint256",
				"name": "TVOC",
				"type": "uint256"
			}
		],
		"stateMutability": "view",
		"type": "function"
	}
]
''')

ACCOUNT_ADDRESS = w3.eth.accounts[0]

contract = w3.eth.contract(address=CONTRACT_ADDRESS, abi=CONTRACT_ABI)

def send_data_to_blockchain(temperature, humidity, c02, tvoc):
    try:
        temp_scaled = int(temperature * 1000)
        hum_scaled = int(humidity*1000)
        c02_scaled = int(c02 * 1000)
        tvoc_scaled = int(tvoc*1000)
        
        transaction = contract.functions.storeSensorData(temp_scaled, hum_scaled, c02_scaled, tvoc_scaled).build_transaction({
            'from': ACCOUNT_ADDRESS,
            'nonce': w3.eth.get_transaction_count(ACCOUNT_ADDRESS),
            'gas': 200000,
            'gasPrice': w3.eth.gas_price
        })

        tx_hash = w3.eth.send_transaction(transaction)
        print(f"Transaction sent! Tx_hash: {tx_hash.hex()}")
        w3.eth.wait_for_transaction_receipt(tx_hash)
        print("Transaction Confirmed!")
    except Exception as e:
        print(f"ERROR: {e}")

#send_data_to_blockchain(32.45, 65.22, 345.5, 8645)


# --- MQTT Callbacks ---
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("Connected to MQTT Broker!")
        # Subscribe to the command topic after successful connection
        client.subscribe(MQTT_SUBSCRIBE_TOPIC)
        print(f"Subscribed to topic: {MQTT_SUBSCRIBE_TOPIC}")
    else:
        print(f"Failed to connect, return code {rc}\n")

def on_message(client, userdata, msg):
    global temp_active, hum_active, c02_active, tvoc_active, num_of_mqtt, mode_change, cur_mode
    print(f"Received MQTT message on topic: {msg.topic}")
    try:
        # Decode the payload. TagoIO sends command as JSON
        message = msg.payload.decode('utf-8')
        print(f"Received command: {message}")
        splitMsg = message.split(": ")
        if splitMsg[0] == "temperature_active":
            num_of_mqtt += 1
            if splitMsg[1] == "true":
                temp_active = True
            else:
                temp_active = False
        elif splitMsg[0] == "humidity_active":
            num_of_mqtt += 1
            if splitMsg[1] == "true":
                hum_active = True
            else:
                hum_active = False
        elif splitMsg[0] == "c02_active":
            num_of_mqtt += 1
            if splitMsg[1] == "true":
                c02_active = True
            else:
                c02_active = False
        elif splitMsg[0] == "tvoc_active":
            num_of_mqtt += 1
            if splitMsg[1] == "true":
                tvoc_active = True
            else:
                tvoc_active = False
        elif splitMsg[0] == "mode":
            mode_change = True
            cur_mode = splitMsg[1]

    except Exception as e:
        print(f"Error processing MQTT message: {e}")

def read_serial_data(serial_port, baud_rate, ser):
        while True:
            
            line = ser.readline().decode('utf-8').rstrip()
            if line:
                valid = True
                #print(f"Received: {line}")
                if"JSON:" in line:
                    try:
                        json_start_index = line.find("JSON:")
                        json_string = line[json_start_index + len("JSON:"):]
                        data = json.loads(json_string)
                        payload = []
                        if 'temperature' in data:
                            payload.append({'variable': f'temperature', 'value': data['temperature']})
                        if 'humidity' in data:
                            payload.append({'variable': f'humidity', 'value': data['humidity']})
                        if 'C02' in data:
                            payload.append({'variable': f'c02', 'value': data['C02']})
                        if 'TVOC' in data:
                            payload.append({'variable': f'tvoc', 'value': data['TVOC']})
                        if payload and mqtt_client:
                            try:
                                mqtt_payload = json.dumps(payload)
                                mqtt_client.publish(MQTT_PUBLISH_TOPIC, mqtt_payload)
                                print(f"Data sent to TagoIO via MQTT successfully: {mqtt_payload}")
                                temp = float(data['temperature'])
                                hum = float(data['humidity'])
                                c02 = float(data['C02'])
                                tvoc = float(data['TVOC'])
                                print('GOT THE FOLLOWING DATA: Temp:'+str(temp)+', Hum:'+str(hum)+', C02:'+str(c02)+', TVOC:'+str(tvoc)+'\n')
                                send_data_to_blockchain(temp, hum, c02, tvoc)
                                time.sleep(1.4)
                            except Exception as mqtt_err:
                                print(f"Error publishing data to MQTT: {mqtt_err}")
                        elif not mqtt_client:
                            print("MQTT client not connected, skipping data send.")
                        else:
                            print("Payload was empty, not sending data.")
                    except json.JSONDecodeError:
                        print("Error decoding JSON data")
                    except requests.exceptions.RequestException as e:
                        print(f"Network error: {e}")
                #else:
                    #print("NOT VALID JSON")
                    #print("got: " + line.split()[0])

    

def send_serial_data(serial_port, baud_rate, ser):
        global num_of_mqtt, cur_mode
        while 1:
            if num_of_mqtt >= 4:
                message = "cmd s "
                val = 0
                if temp_active:
                    val += 1
                if hum_active:
                    val += 2
                if c02_active:
                    val += 4
                if tvoc_active:
                    val += 8
                message += str(val)+ "\n"
                ser.write(message.encode())
                print("got here, message is: " + message)
                num_of_mqtt = 0
            elif mode_change:
                message = "cmd m " + cur_mode +"\n"
                ser.write(message.encode())
            time.sleep(1)
        


if __name__ == "__main__":
    serial_port = 'COM12'
    baud_rate = 115200
    # Initialize MQTT client
    mqtt_client = mqtt.Client(client_id=MQTT_CLIENT_ID)
    mqtt_client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    mqtt_client.on_connect = on_connect
    mqtt_client.on_message = on_message
    if not w3.is_connected():
        print("Failed to connenct to Ethereum node.")
        exit()
    else:
        print(f"Connected to Ethereun node: {GANACHE_URL}")
    try:
        ser = serial.Serial(serial_port, baud_rate, timeout=5)
        print(f"Connected to {serial_port} at {baud_rate} baud")
        cmd_Thread = Thread(target=send_serial_data, args=(serial_port, baud_rate, ser))
        cmd_Thread.start()
        # Configure SSL/TLS
        # Use TLSv1.2 or higher for best security
        # You might need to provide a CA cert if you encounter SSL verification issues
        mqtt_client.tls_set(tls_version=ssl.PROTOCOL_TLSv1_2)

        # Start MQTT client in a separate thread to handle network loop
        mqtt_thread = Thread(target=lambda: mqtt_client.connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT, 60))
        mqtt_thread.daemon = True # Allows the main program to exit even if this thread is running
        mqtt_thread.start()

        # Loop to process MQTT network events in the background
        mqtt_client.loop_start()
        # Start reading serial data in the main thread
        read_serial_data(serial_port, baud_rate, ser)

        # Clean up MQTT client when main loop exits (optional, as daemon thread will exit)
        mqtt_client.loop_stop()
        mqtt_client.disconnect()
    except serial.SerialException as e:
        print(f"Error: Serial port {serial_port} not found or inaccessible: {e}")
    except UnicodeDecodeError as e:
        print(f"Error decoding serial data: {e}.  Check encoding.")
    except Exception as e:
        print(f"An unexpected error occurred: {e}")
    # finally:
    #     if 'ser' in locals() and ser.is_open:
    #         ser.close()
    #         print(f"Disconnected from {serial_port}")

    

    #read_serial_data(serial_port, baud_rate)
    #send_serial_data(serial_port, baud_rate)
