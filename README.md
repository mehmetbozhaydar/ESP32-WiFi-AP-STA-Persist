# ESP32 WiFi Access Point and Station with Persistent Configuration
## Project Purpose and General Workflow

This project aims to dynamically configure WiFi connectivity via the TCP protocol using an ESP32-based system. The system supports two main modes: **Access Point (AP)** and **Station (STA)**. Additionally, WiFi credentials are stored persistently for use during subsequent startups, enhancing the system's flexibility and user-friendliness.

---
## General Flow

* NVS Start: First NVS is started and WiFi information is checked.
* WiFi Connection: If there is previously saved WiFi information, it tries to connect to the WiFi network.
* AP Mode Start: If the connection cannot be established, AP mode is started.
* TCP Server Task: SSID and password are received from the clients.
* WiFi Connection and Saving: If the WiFi connection is successful, the information is saved.
* Connection Status: After the connection status is reported to the client, the connection is closed and new client connections are waited.

---

## Code Flow

### 1. Initialization and NVS Setup
- The system starts by initializing the **NVS (Non-Volatile Storage)** module, which is used to store WiFi SSID and password information persistently.
- If NVS fails to initialize, the system erases the current NVS partition and restarts.

### 2. WiFi Event Management
- **WiFi events** are managed using an event group. Connection status, disconnection events, and IP acquisition are monitored through this group.
- If the connection is successful, WiFi credentials are saved to NVS.

### 3. Access Point (AP) Mode
- The system can operate as an **Access Point (AP)** and start a TCP server for clients.
- The TCP server processes incoming JSON-formatted WiFi credentials from the client.
  - First, the **SSID** is received.
  - Then, the **password** is received.
- Using the received information, the system connects to the WiFi network and notifies the client of the connection status.

### 4. Station (STA) Mode
- If WiFi credentials exist in NVS, the system attempts to connect to the specified WiFi network in **Station (STA)** mode.
- Upon successful connection, an IP address is acquired, and the system is ready for further operations.

### 5. TCP Server Task
- The server accepts incoming connections and expects JSON-formatted WiFi configuration data from the client.
- A two-step process is carried out:
  - **SSID Retrieval**: Extracts the SSID from the "wifi_name" key.
  - **Password Retrieval**: Extracts the password from the "wifi_password" key.
- After validation, the system attempts to connect to the WiFi network.

---

## Detailed Function Descriptions

### `nvs_init()`
Initializes the NVS module and opens it in read/write mode. If the current NVS partition is full or incompatible, the partition is erased and restarted.

### `nvs_write_wifi_data()`
Writes WiFi SSID and password information to NVS, ensuring that the data is stored persistently.

### `nvs_read_wifi_data()`
Reads WiFi credentials from NVS and uses them for connection.

### `connect_wifi()`
Attempts to connect to the specified WiFi network using the provided SSID and password. The connection status is checked, and necessary actions are taken.

### `wifi_event_handler()`
Handles WiFi events. Depending on the event type, actions such as connecting, retrying, or obtaining an IP address are performed.

### `wifi_init_softap()`
Initializes the Access Point (AP) mode. Configures the IP address, SSID, and password for the AP.

### `tcp_server_task()`
Runs the TCP server and communicates with clients using JSON format. It validates incoming SSID and password data, connects to the WiFi network, and notifies the client of the result.

---

## Project Usage

1. When the system starts, it first checks the **NVS**.
2. If WiFi credentials are available, the system connects to the specified WiFi network in **Station (STA)** mode.
3. If WiFi credentials are unavailable or the connection fails, the system operates in **Access Point (AP)** mode and starts the TCP server.
4. A client connects to the AP network and sends WiFi configuration in JSON format to the server. Example JSON:
   ```json
   {
       "wifi_name": "MySSID",
       "wifi_password": "MyPassword"
   }
   ```
5. After receiving the credentials, the system attempts to connect and notifies the client of the result.

---

## Important Notes
- **SSID** and **password** must be entered in the correct format.
- The maximum number of connection retries is limited to 5. If exceeded, the system must be restarted.
- If the TCP connection is lost, the client must reconnect.

---

## Future Enhancements
- Implement a more secure encryption method for storing WiFi credentials.
- Increase TCP server capacity to allow multiple clients to connect simultaneously.
- Integrate OTA (Over-the-Air) updates.


