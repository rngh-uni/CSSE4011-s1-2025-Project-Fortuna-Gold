import serial
import requests
import time
import json

TAGOIO_DEVICE_TOKEN = '725208b5-95c9-4405-a82b-5ed4e84ebeba'
TAGOIO_API_URL = 'https://api.tago.io/data'
HEADERS = {
    'Content-Type': 'application/json',
    'device-token': '725208b5-95c9-4405-a82b-5ed4e84ebeba'
}

def read_serial_data(serial_port, baud_rate):
    try:
        ser = serial.Serial(serial_port, baud_rate, timeout=5)
        print(f"Connected to {serial_port} at {baud_rate} baud")

        while True:
            line = ser.readline().decode('utf-8').rstrip()
            if line:
                valid = True
                print(f"Received: {line}")
                if"JSON:" in line:
                    try:
                        json_start_index = line.find("JSON:")
                        json_string = line[json_start_index + len("JSON:"):]
                        data = json.loads(json_string)
                        #print("Data: " + str(data))
                        #rssiData = list(map(int, data['RSSI'].split(" ")))
                        payload = []
                        print(data['RSSI'])
                        
                        if 'RSSI' in data:
                            rssi_values = data['RSSI'].split()
                            payload = []
                            for i, rssi_value in enumerate(rssi_values):
                                payload.append({'variable': f'iBeacon{i+1}RSSI', 'value': rssi_value})
                                if rssi_value == "-1":
                                    valid = False
                            if 'distance' in data:
                                payload.append({'variable': f'distance', 'value': data['distance']})
                            if 'velocity' in data:
                                payload.append({'variable': f'velocity', 'value': data['velocity']})
                            if payload and valid:
                                response = requests.post(TAGOIO_API_URL, headers=HEADERS, json=payload)
                                if response.status_code == 200:
                                    print(f"Sent {len(payload)} RSSI values to TagoIO successfully")
                                else:
                                    print(f"Error sending RSSI data to TagoIO: {response.status_code} - {response.text}")
                    except json.JSONDecodeError:
                        print("Error decoding JSON data")
                    except requests.exceptions.RequestException as e:
                        print(f"Network error: {e}")
                else:
                    print("NOT VALID JSON")
                    print("got: " + line.split()[0])

    except serial.SerialException as e:
        print(f"Error: Serial port {serial_port} not found or inaccessible: {e}")
        return None
    except UnicodeDecodeError as e:
        print(f"Error decoding serial data: {e}.  Check encoding.")
        return None
    except Exception as e:
        print(f"An unexpected error occurred: {e}")
        return None
    finally:
        if 'ser' in locals() and ser.is_open:
            ser.close()
            print(f"Disconnected from {serial_port}")
if __name__ == "__main__":
    serial_port = 'COM12'
    baud_rate = 115200

    read_serial_data(serial_port, baud_rate)