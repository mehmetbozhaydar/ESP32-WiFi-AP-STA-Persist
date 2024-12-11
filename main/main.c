#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/sockets.h"

// WiFi and network configuration constants
#define WIFI_AP_SSID     "ESP32_C6_AP"    // SSID name for Access Point mode
#define WIFI_AP_PASS     "12345678"       // Password for Access Point mode
#define MAX_CLIENTS      1                // Maximum number of clients that can connect
#define PORT            3333              // Port number for the TCP server
#define WIFI_TIMEOUT_MS 30000             // WiFi connection timeout (30 seconds)
#define RX_BUFFER_SIZE  512               // TCP receiver buffer size

// NVS (Non-Volatile Storage) configuration constants
#define NVS_NAMESPACE   "wifi_table"      // NVS namespace
#define WIFI_SSID_KEY   "wifi_ssid"       // Key to store the SSID
#define WIFI_PASS_KEY   "wifi_pass"       // Key to store the password
#define TABLE_FLAG_KEY  "table_flag"      // Key for the table flag
#define WIFI_NAME_SIZE  32                // Maximum size for SSID
#define WIFI_PASS_SIZE  64                // Maximum size for password
#define MAX_RETRY 5                       // Maximum number of connection attempts

// Global variables and definitions
static const char *TAG = "wifi_manager";                   // Logging tag
static EventGroupHandle_t wifi_event_group;                // Event group for WiFi events
static const int WIFI_CONNECTED_BIT = BIT0;               // WiFi connection status bit
static nvs_handle_t my_nvs_handle;                        // NVS operation handle
static int retry_count = 0;                               // Connection attempt counter

/**
 * @brief Initializes and opens NVS
 * @return true if successful, false if failed
 */
bool nvs_init(void) {
    // Initialize NVS flash
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // If NVS section is full or version mismatch, erase and initialize again
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Open NVS in read-write mode
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS!");
        return false;
    }

    return true;
}

/**
 * @brief Saves WiFi information to NVS
 * @param ssid WiFi network name
 * @param password WiFi password
 * @return true if successful, false if failed
 */
bool nvs_write_wifi_data(const char* ssid, const char* password) {
    esp_err_t err;

    // Save SSID
    err = nvs_set_str(my_nvs_handle, WIFI_SSID_KEY, ssid);
    if (err != ESP_OK) return false;

    // Save password
    err = nvs_set_str(my_nvs_handle, WIFI_PASS_KEY, password);
    if (err != ESP_OK) return false;

    // Commit changes to NVS
    err = nvs_commit(my_nvs_handle);
    if (err != ESP_OK) return false;

    ESP_LOGI(TAG, "WiFi information successfully saved");
    return true;
}

/**
 * @brief Reads WiFi information from NVS
 * @param ssid_out SSID output buffer
 * @param pass_out Password output buffer
 * @return true if successful, false if failed
 */
bool nvs_read_wifi_data(char* ssid_out, char* pass_out) {
    size_t ssid_size = WIFI_NAME_SIZE;
    size_t pass_size = WIFI_PASS_SIZE;

    // Read SSID
    esp_err_t err = nvs_get_str(my_nvs_handle, WIFI_SSID_KEY, ssid_out, &ssid_size);
    if (err != ESP_OK) return false;

    // Read password
    err = nvs_get_str(my_nvs_handle, WIFI_PASS_KEY, pass_out, &pass_size);
    if (err != ESP_OK) return false;

    return true;
}

/**
 * @brief WiFi event handler callback function
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    // When WiFi Station starts
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Trying to connect to WiFi...");
        esp_wifi_connect();
    } 
    // When WiFi connection is lost
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retry_count < MAX_RETRY) {
            ESP_LOGI(TAG, "WiFi connection lost. Trying to reconnect...");
            esp_wifi_connect();
            retry_count++;
        } else {
            ESP_LOGE(TAG, "WiFi connection attempts exhausted!");
            xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
        }
    } 
    // When IP address is obtained
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Successfully connected to WiFi! IP address: " IPSTR,
                 IP2STR(&event->ip_info.ip));
        
        // Get the current WiFi configuration and save to NVS
        wifi_config_t wifi_config;
        esp_wifi_get_config(WIFI_IF_STA, &wifi_config);
        
        if (nvs_write_wifi_data((char*)wifi_config.sta.ssid, (char*)wifi_config.sta.password)) {
            ESP_LOGI(TAG, "WiFi information successfully saved to NVS");
        } else {
            ESP_LOGE(TAG, "Failed to save WiFi information to NVS!");
        }
        
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        retry_count = 0;
    }
}

/**
 * @brief Connects to the specified WiFi network
 * @param ssid WiFi network name
 * @param password WiFi password
 * @return ESP_OK if successful, ESP_FAIL if failed
 */
static esp_err_t connect_wifi(const char* ssid, const char* password) {
    wifi_config_t wifi_config = {0};

    // Securely copy SSID and password
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
    // Configure and start WiFi
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Trying to connect to the %s network...", ssid);
    
    // Check connection success
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
            WIFI_CONNECTED_BIT,
            pdFALSE,
            pdFALSE,
            pdMS_TO_TICKS(WIFI_TIMEOUT_MS));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connection successful!");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Connection failed! Timeout");
    ESP_ERROR_CHECK(esp_wifi_stop());
    return ESP_FAIL;
}

/**
 * @brief Starts Access Point mode
 */
static void wifi_init_softap(void) {
    // AP mode configuration
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = 1,
            .password = WIFI_AP_PASS,
            .max_connection = MAX_CLIENTS,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
        },
    };

    // IP configuration for AP
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 1, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 1, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);

    // Configure DHCP server
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(ap_netif));
    ESP_ERROR_CHECK(esp_netif_set_ip_info(ap_netif, &ip_info));
    ESP_ERROR_CHECK(esp_netif_dhcps_start(ap_netif));

    // Start AP mode
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP mode started:");
    ESP_LOGI(TAG, "SSID: %s", WIFI_AP_SSID);
    ESP_LOGI(TAG, "Password: %s", WIFI_AP_PASS);
    ESP_LOGI(TAG, "IP Address: 192.168.1.1");
    ESP_LOGI(TAG, "Channel: 1");
}

/**
 * @brief Extracts a value from a JSON formatted string and cleans control characters
 * @param json_str The input JSON string
 * @param key The key to search for
 * @param output The output buffer to store the extracted value
 * @param output_size The size of the output buffer
 * @return true if successful, false if failed
 */
static bool validate_and_extract_value(const char *json_str, const char *key, char *output, size_t output_size) {
    // Search for the given key in the JSON string
    char *key_start = strstr(json_str, key);
    if (!key_start) return false;  // Failed if the key is not found
    
    // Find the ':' character after the key
    key_start = strchr(key_start, ':');
    if (!key_start) return false;  // Failed if the ':' character is not found
    
    // Find the starting double quote of the value
    key_start = strchr(key_start, '"');
    if (!key_start) return false;  // Failed if the starting quote is not found
    
    // Move to the character after the starting quote
    key_start++;
    
    // Find the ending double quote of the value
    char *key_end = strchr(key_start, '"');
    if (!key_end) return false;  // Failed if the ending quote is not found
    
    // Calculate the length of the value and compare with the output buffer size
    size_t key_len = key_end - key_start;
    if (key_len >= output_size) return false;  // Failed if the value doesn't fit in the output buffer
    
    // Copy the value to the output buffer
    strncpy(output, key_start, key_len);
    output[key_len] = '\0';  // Add string terminator
    
    // Clean control characters (ASCII values less than 32)
    for (size_t i = 0; i < key_len; i++) {
        if (output[i] < 32) {  // ASCII 32: space character
            output[i] = '_';   // Replace control characters with an underscore
        }
    }
    
    return true;  // Successful operation
}

/**
 * @brief TCP server task
 * @details A TCP server that receives WiFi configuration data in JSON format.
 * It performs a two-step process:
 * 1. First, it waits for the SSID information ("wifi_name" key)
 * 2. Then, it waits for the password ("wifi_password" key)
 * After receiving the information, it connects to the WiFi and saves it to NVS.
 */
static void tcp_server_task(void *pvParameters) {
    // Buffer for received data and variables for WiFi information
    char rx_buffer[RX_BUFFER_SIZE];
    char ssid[WIFI_NAME_SIZE] = {0};
    char password[WIFI_PASS_SIZE] = {0};
    bool ssid_received = false;  // Flag to check if SSID is received

    // Server socket address configuration
    struct sockaddr_in dest_addr;
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);  // Accept connections from all interfaces
    dest_addr.sin_family = AF_INET;                 // IPv4
    dest_addr.sin_port = htons(PORT);               // Port number

    // Create the TCP listening socket
    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Socket creation failed! Error: %d", errno);
        vTaskDelete(NULL);
        return;
    }

    // Set socket options (allow address reuse)
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Bind the socket and start listening
    ESP_ERROR_CHECK(bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)));
    ESP_ERROR_CHECK(listen(listen_sock, 1));
    ESP_LOGI(TAG, "TCP server started. Port: %d", PORT);

    // Main server loop
    while (1) {
        struct sockaddr_in source_addr;
        socklen_t addr_len = sizeof(source_addr);

        // Wait for new connection
        ESP_LOGI(TAG, "Waiting for connection...");
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Connection failed! Error: %d", errno);
            continue;
        }

        ESP_LOGI(TAG, "Client connected!");
        
        // Communication loop with the client
        while (1) {
            // Receive data
            int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            if (len <= 0) break;  // Connection closed or error occurred

            rx_buffer[len] = '\0';
            ESP_LOGI(TAG, "Received data: %s", rx_buffer);

            // Two-step WiFi configuration
            if (!ssid_received) {
                // First step: Receive SSID
                if (validate_and_extract_value(rx_buffer, "\"wifi_name\"", ssid, WIFI_NAME_SIZE)) {
                    ssid_received = true;
                    const char *response = "SSID received. Waiting for password...\n";
                    send(sock, response, strlen(response), 0);
                } else {
                    const char *response = "Invalid or missing SSID information!\n";
                    send(sock, response, strlen(response), 0);
                }
            } else {
                // Second step: Receive password and attempt connection
                if (validate_and_extract_value(rx_buffer, "\"wifi_password\"", password, WIFI_PASS_SIZE)) {
                    // Try to connect to the WiFi
                    if (connect_wifi(ssid, password) == ESP_OK) {
                        // Successfully connected, save information to NVS
                        if (nvs_write_wifi_data(ssid, password)) {
                            const char *response = "Connected to the network and information saved.\n";
                            send(sock, response, strlen(response), 0);
                        } else {
                            const char *response = "Connected but could not save information!\n";
                            send(sock, response, strlen(response), 0);
                        }
                    } else {
                        const char *response = "Failed to connect to the network. Please check the information.\n";
                        send(sock, response, strlen(response), 0);
                    }
                    ssid_received = false;  // Ready for new SSID
                } else {
                    const char *response = "Invalid or missing password information!\n";
                    send(sock, response, strlen(response), 0);
                }
            }
        }
        close(sock);  // Close client socket
    }
    close(listen_sock);  // Close listening socket
    vTaskDelete(NULL);   // Delete task
}

/**
 * @brief Main application startup function
 * @details Initializes system components and manages WiFi:
 * 1. Initializes NVS
 * 2. Creates event group for WiFi events
 * 3. Starts the network interface
 * 4. Configures the WiFi driver
 * 5. Checks registered WiFi information
 * 6. Switches to AP mode if necessary
 * 7. Starts the TCP server task
 */
void app_main(void) {
    // Check NVS initialization
    if (!nvs_init()) {
        ESP_LOGE(TAG, "Failed to initialize NVS!");
        return;
    }

    // Create event group for WiFi events
    wifi_event_group = xEventGroupCreate();

    // Initialize the network stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // Create WiFi AP and STA interfaces
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();
    
    // Start WiFi driver with default settings
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Register event handlers for WiFi and IP events
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    // Read registered WiFi information from NVS
    char ssid[32] = {0};
    char password[64] = {0};
    
    // Try to connect using registered information, switch to AP mode if failed
    if (nvs_read_wifi_data(ssid, password)) {
        ESP_LOGI(TAG, "Found registered WiFi information. Attempting to connect...");
        if (connect_wifi(ssid, password) == ESP_OK) {
            ESP_LOGI(TAG, "Successfully connected to the registered network");
        } else {
            ESP_LOGE(TAG, "Failed to connect to registered network, switching to AP mode");
            wifi_init_softap();
        }
    } else {
        ESP_LOGI(TAG, "No registered WiFi information found, switching to AP mode");
        wifi_init_softap();
    }

    // Start the TCP server task
    // Stack size: 4096 bytes, Priority: 5
    xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 5, NULL);
}
