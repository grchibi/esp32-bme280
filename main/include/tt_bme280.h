/**
 * tt_bme280.h
 */

#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "esp_sntp.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/timer.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_tls.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define ESP_MAXIMUM_RETRY  5

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/*
 * I2C
 */
#define SDA_PIN GPIO_NUM_19
#define SCL_PIN GPIO_NUM_18

/*
 * MQTT
 */
static const char* MQTT_BROKER_HOST = TT_MQTT_B_HOST;
static const int MQTT_BROKER_PORT = TT_MQTT_B_PORT;
static const char* MQTT_BROKER_TOPIC = TT_MQTT_B_TOPIC;

/*
 * TIMER
 */
#define TIMER_DIVIDER 80

/**
 * LOG TAGGING
 */
static const char* TAG_APP = "APP";
static const char* TAG_I2C = "BME280";
static const char* TAG_SNTP = "SNTP";
static const char* TAG_TASK = "TASK";
static const char* TAG_TASK_K = "TASK_K";
static const char* TAG_MQTT = "MQTT";
static const char* TAG_WIFI = "WI-FI";

/**
 * CERTIFICATE
 */
extern const uint8_t root_ca_pem_start[] asm("_binary_AmazonRootCA1_pem_start");
extern const uint8_t root_ca_pem_end[] asm("_binary_AmazonRootCA1_pem_end");
extern const uint8_t client_cert_pem_start[] asm("_binary_certificate_pem_crt_start");
extern const uint8_t client_cert_pem_end[] asm("_binary_certificate_pem_crt_end");
extern const uint8_t client_key_pem_start[] asm("_binary_private_pem_key_start");
extern const uint8_t client_key_pem_end[] asm("_binary_private_pem_key_end");


/**
 * FUNCTION PROTOTYPE
 */

int8_t bme280_task_start(char*);
int get_sec_for_alarm_00(void);
void init_sntp(void);
void IRAM_ATTR onTimer(void*);
void sleep_deeply(void);
void time_sync_notification_cb(struct timeval*);
void user_delay_ms(uint32_t);
int8_t user_i2c_read(uint8_t, uint8_t, uint8_t*, uint16_t);
int8_t user_i2c_write(uint8_t, uint8_t, uint8_t*, uint16_t);


// end of tt_bme280.h