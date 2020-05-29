#include <stdio.h>
#include <sys/time.h>
#include <time.h>

#include "esp_sntp.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_tls.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs_flash.h"

#include "bme280.h"
#include "tt_sec.h"


/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define ESP_MAXIMUM_RETRY  5

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/*
 * MQTT
 */
static const char* MQTT_BROKER_HOST = TT_MQTT_B_HOST;
static const int MQTT_BROKER_PORT = TT_MQTT_B_PORT;
static const char* MQTT_BROKER_TOPIC = TT_MQTT_B_TOPIC;

/**
 * LOG TAGGING
 */
static const char* TAG_APP = "APP";
static const char* TAG_MQTT = "MQTT";
static const char* TAG_WIFI = "WI-FI";


/**
 * EVENT HANDLERS
 */

extern const uint8_t root_ca_pem_start[] asm("_binary_AmazonRootCA1_pem_start");
extern const uint8_t root_ca_pem_end[] asm("_binary_AmazonRootCA1_pem_end");
extern const uint8_t client_cert_pem_start[] asm("_binary_certificate_pem_crt_start");
extern const uint8_t client_cert_pem_end[] asm("_binary_certificate_pem_crt_end");
extern const uint8_t client_key_pem_start[] asm("_binary_private_pem_key_start");
extern const uint8_t client_key_pem_end[] asm("_binary_private_pem_key_end");
static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    // your_context_t *context = event->context;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_CONNECTED");
/*            msg_id = esp_mqtt_client_publish(client, MQTT_BROKER_TOPIC, "unko", 0, 0, 0);
            ESP_LOGI(TAG_MQTT, "sent publish successful, msg_id=%d", msg_id);*/
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DISCONNECTED");
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DATA");
            printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("DATA=%.*s\r\n", event->data_len, event->data);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_ERROR");
            break;
        default:
            ESP_LOGI(TAG_MQTT, "Other event id:%d", event->event_id);
            break;
    }
    return ESP_OK;
}

static TickType_t retryDelayMS = 0;
static const TickType_t maxRetryDelayMS = 300000;   // 5min
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (maxRetryDelayMS <= retryDelayMS) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGI(TAG_WIFI,"failed to connect to the AP.");
        } else {
            retryDelayMS += 5000;
            ESP_LOGI(TAG_WIFI, "delay for re-trying: %d", retryDelayMS);
            vTaskDelay(retryDelayMS / portTICK_PERIOD_MS);

            esp_wifi_connect();
            ESP_LOGI(TAG_WIFI, "re-trying to connect to the AP...");
        }

    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG_WIFI, "got ip:%s",
                 ip4addr_ntoa(&event->ip_info.ip));
        
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        retryDelayMS = 0;
    }
}


/**
 * INITIALIZATION FUNC
 */

void init_datetime(void)
{
    init_sntp();

    time_t now = 0;
    struct tm timeinfo = { 0 };
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET) {
        ESP_LOGI(TAG_APP, "Waiting for system time to be set...");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    time(&now);
  
    char buff[64];

    setenv("TZ", "JST-9", 1);
    tzset();
    localtime_r(&now, &timeinfo);
    strftime(buff, sizeof(buff), "%c", &timeinfo);
    ESP_LOGI(TAG_APP, "Current datetime in JST: %s", buff);
}

void init_sntp()
{
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);

    sntp_init();

    ESP_LOGI(TAG_APP, "SNTP was initialized.");
}

void init_wifi(void)
{
    ESP_LOGI(TAG_WIFI, "ESP_WIFI_MODE_STA");

    s_wifi_event_group = xEventGroupCreate();

    tcpip_adapter_init();

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = ESP_WIFI_SSID,
            .password = ESP_WIFI_PASS
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );

    vTaskDelay(1);
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG_WIFI, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG_WIFI, "connected to ap SSID: %s", ESP_WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG_WIFI, "Failed to connect to SSID: %s", ESP_WIFI_SSID);
    } else {
        ESP_LOGE(TAG_WIFI, "UNEXPECTED EVENT");
    }

    ESP_ERROR_CHECK(esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler));
    ESP_ERROR_CHECK(esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler));
    vEventGroupDelete(s_wifi_event_group);
}


/**
 * FUNCTIONS
 */

uint64_t get_usec_for_alarm()
{
    time_t now = 0;
    struct tm timeinfo = { 0 };

    time(&now);

    /*setenv("TZ", "JST-9", 1);
    tzset();*/
    localtime_r(&now, &timeinfo);

    return ((60 - timeinfo.tm_min - 1) * 60 * 1000000) + ((60 - timeinfo.tm_sec) * 1000000);
}

void initialize(void)
{
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_ULP) {
        // Initialize Wi-Fi
        init_wifi();

        // sometimes synchronize the system time


    } else {
        // Initialize NVS
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);

        // Initialize Wi-Fi
        init_wifi();

        // Initialize local datetime
        init_datetime();
    }
}

void mqtt_app_start(void)
{
    ESP_ERROR_CHECK(esp_tls_init_global_ca_store());
    ESP_ERROR_CHECK(esp_tls_set_global_ca_store(
        (const unsigned char*)root_ca_pem_start,
        root_ca_pem_end - root_ca_pem_start
    ));

    char mqtturi[96];
    sprintf(mqtturi, "mqtts://%s:%d", MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    const esp_mqtt_client_config_t mqtt_cfg = {
        .uri = mqtturi,
        .event_handle = mqtt_event_handler,
        .client_cert_pem = (const char *)client_cert_pem_start,
        .client_key_pem = (const char *)client_key_pem_start,
        .use_global_ca_store = true
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(client);
}

void run_lchika(void)
{
    int level = 0;
    while (true) {
        gpio_set_level(GPIO_NUM_4, level);
        level = !level;
        vTaskDelay(300 / portTICK_PERIOD_MS);
    }
}

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG_APP, "Notification of a time synchronization event");
}


/**
 * MAIN
 */

void app_main(void)
{
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_ULP) {
        ESP_LOGI(TAG_APP, "Wake up from deep sleep..");
    } else {
        ESP_LOGI(TAG_APP, "Startup..");
        ESP_LOGI(TAG_APP, "Free memory: %d bytes", esp_get_free_heap_size());
        ESP_LOGI(TAG_APP, "IDF version: %s", esp_get_idf_version());
    }

    initialize();

    mqtt_app_start();
    //xTaskCreate(&http_get_task, "http_get_task", 4096, NULL, 5, NULL);


    // L Chika
    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
    run_lchika();
}
