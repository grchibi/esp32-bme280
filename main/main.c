/**
 * main.c
 */

#include "tt_sec.h"
#include "tt_bme280.h"

#include "bme280.h"


/**
 * STATIC VARIABLES
 */
static char s_last_js[128] = "";
RTC_DATA_ATTR static int ntp_cycle_cnt = 0;
static xQueueHandle s_pubAlarmQueue;
static TaskHandle_t s_taskHandle;
static time_t waked_up_time = -1;


/**
 * EVENT HANDLERS
 */

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG_MQTT, "MQTT_EVENT_CONNECTED");
            
            char* my_data = (char*)event->user_context;
            msg_id = esp_mqtt_client_publish(client, MQTT_BROKER_TOPIC, my_data, 0, 0, 0);
            ESP_LOGI(TAG_MQTT, "sent publish successful, msg_id=%d, data=> %s", msg_id, my_data);

            int cause = 1;
            xQueueSend(s_pubAlarmQueue, &cause, 0);

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


static EventGroupHandle_t s_wifi_event_group;   // FreeRTOS event group to signal when we are connected
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
        ESP_LOGI(TAG_SNTP, "Waiting for system time to be set...");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }
    time(&now);
  
    char buff[64];

    setenv("TZ", "JST-9", 1);
    tzset();
    localtime_r(&now, &timeinfo);
    strftime(buff, sizeof(buff), "%c", &timeinfo);
    ESP_LOGI(TAG_SNTP, "Current datetime in JST: %s", buff);
}

void init_i2c()
{
    i2c_config_t i2c_config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = SDA_PIN,
        .scl_io_num = SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 20000   //1000000
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM_0, &i2c_config));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0));
}

void init_nvs()
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void init_sntp()
{
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);

    sntp_init();

    ESP_LOGI(TAG_SNTP, "SNTP was initialized.");
}

void init_timer(void)
{
    timer_config_t config = {
        .divider = TIMER_DIVIDER,
        .counter_dir = TIMER_COUNT_UP,
        .counter_en = TIMER_PAUSE,
        .alarm_en = TIMER_ALARM_EN
    };
    timer_init(TIMER_GROUP_0, TIMER_0, &config);

    timer_set_counter_value(TIMER_GROUP_0, TIMER_0, 0x00000000ULL);

    timer_set_alarm_value(TIMER_GROUP_0, TIMER_0, 300 * (TIMER_BASE_CLK / TIMER_DIVIDER));   // 5min
    timer_enable_intr(TIMER_GROUP_0, TIMER_0);
    timer_isr_register(TIMER_GROUP_0, TIMER_0, onTimer, NULL, ESP_INTR_FLAG_IRAM, NULL);

    timer_start(TIMER_GROUP_0, TIMER_0);
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

void bme280_print_sensor_data(char* s_out, struct bme280_data* comp_data)
{
    float temp, press, hum;

#ifdef BME280_FLOAT_ENABLE
    temp = comp_data->temperature;
    press = 0.01 * comp_data->pressure;
    hum = comp_data->humidity;
#else
#ifdef BME280_64BIT_ENABLE
    temp = 0.01f * comp_data->temperature;
    press = 0.0001f * comp_data->pressure;
    hum = 1.0f / 1024.0f * comp_data->humidity;
#else
    temp = 0.01f * comp_data->temperature;
    press = 0.01f * comp_data->pressure;
    hum = 1.0f / 1024.0f * comp_data->humidity;
#endif
#endif

    ESP_LOGI(TAG_I2C, "Measurement Data: %0.2lf C, %0.2lf hPa, %0.2lf %%", temp, press, hum);

    time_t now = 0;
    struct tm timeinfo = { 0 };
    char buff[16];
    time(&now);
    setenv("TZ", "JST-9", 1);
    tzset();
    localtime_r(&now, &timeinfo);
    strftime(buff, sizeof(buff), "+%Y%m%d%H%M", &timeinfo);
    sprintf(s_out, "{\"dsrc\": \"%s\", \"dt\": \"%s\", \"t\": %0.2lf, \"p\": %0.2lf, \"h\": %0.2lf}", TT_DSRC, buff, temp, press, hum);
}

int8_t bme280_ready(struct bme280_dev* bme280)
{
    int8_t rslt = BME280_OK;

    bme280->dev_id = BME280_I2C_ADDR_PRIM;
    bme280->intf = BME280_I2C_INTF;
    bme280->read = (void*)user_i2c_read;
    bme280->write = (void*)user_i2c_write;
    bme280->delay_ms = (void*)user_delay_ms;

    rslt = bme280_init(bme280);
    if (rslt == BME280_OK) {
        ESP_LOGI(TAG_I2C, "BME280 init result OK.");
    } else {
        ESP_LOGE(TAG_I2C, "Failed to initialize the BME280 device: %d", rslt);
    }
    
    bme280->settings.osr_h = BME280_OVERSAMPLING_4X;
    bme280->settings.osr_p = BME280_OVERSAMPLING_4X;
    bme280->settings.osr_t = BME280_OVERSAMPLING_4X;
    bme280->settings.filter = BME280_FILTER_COEFF_OFF;

    uint8_t settings_sel = BME280_OSR_PRESS_SEL | BME280_OSR_TEMP_SEL | BME280_OSR_HUM_SEL | BME280_FILTER_SEL;
    rslt = bme280_set_sensor_settings(settings_sel, bme280);
    if (rslt == BME280_OK) {
        ESP_LOGI(TAG_I2C, "BME280 settings result OK.");
    } else {
        ESP_LOGE(TAG_I2C, "Failed to set the BME280 settings: %d", rslt);
    }

    return rslt;
}

// This API reads the sensor temperature, pressure and humidity data in normal mode.
int8_t bme280_stream_sensor_data_forced_mode(struct bme280_data* comp_data, struct bme280_dev* bme280)
{
    int8_t rslt = BME280_OK;

    uint32_t req_delay = 2 * bme280_cal_meas_delay(&bme280->settings);
    ESP_LOGI(TAG_I2C, "reg_delay => %d", req_delay);

    /* get stream sensor data */
    rslt = bme280_set_sensor_mode(BME280_FORCED_MODE, bme280);
    if (rslt != BME280_OK) {
        ESP_LOGE(TAG_I2C, "Failed to set sensor mode: %d", rslt);
        return rslt;
    }

    bme280->delay_ms(req_delay);
    rslt = bme280_get_sensor_data(BME280_ALL, comp_data, bme280);
    if (rslt != BME280_OK) {
        ESP_LOGE(TAG_I2C, "Failed to get sensor data: %d", rslt);
        return rslt;
    }

    return rslt;
}

int8_t bme280_task_start(char* s_out)
{
    int8_t rslt = BME280_OK;

    struct bme280_dev bme280;
    struct bme280_data comp_data;

    rslt = bme280_ready(&bme280);
    if (rslt == BME280_OK) {
        rslt = bme280_stream_sensor_data_forced_mode(&comp_data, &bme280);

        if (rslt == BME280_OK)
            bme280_print_sensor_data(s_out, &comp_data);
    }

    return rslt;
}

int get_sec_for_alarm_00()
{
    time_t now = 0;
    struct tm timeinfo = { 0 };

    time(&now);

    /*setenv("TZ", "JST-9", 1);
    tzset();*/
    localtime_r(&now, &timeinfo);

    return ((60 - timeinfo.tm_min - 1) * 60) + (60 - timeinfo.tm_sec);
}

void initialize(void)
{
    // Initialize NVS
    init_nvs();

    // Initialize Wi-Fi
    init_wifi();

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        // sometimes synchronize the system time
        if (24 <= ntp_cycle_cnt) {
            init_datetime();
            ntp_cycle_cnt = 0;
        } else {
            ntp_cycle_cnt++;
        }

    } else {
        // Initialize local datetime
        init_datetime();

    }

    // Initialize I2C
    init_i2c();

    // Initialize Timer
    init_timer();
}

void killer_task(void* args)
{
    ESP_LOGI(TAG_TASK_K, "waiting for being received the alarm...");
    xTaskNotifyWait(0, 0, NULL, portMAX_DELAY);

    ESP_LOGI(TAG_TASK_K, "time up!");
    //s_mqtt_published = TRUE;     // for falling into deep sleep.

    vTaskDelete(NULL);
}

esp_mqtt_client_handle_t mqtt_task_start(char* out_js)
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
        .use_global_ca_store = true,
        .user_context = out_js
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(client);

    return client;
}

void IRAM_ATTR onTimer(void* args)
{
    int cause = 2;
    xQueueSendFromISR(s_pubAlarmQueue, &cause, NULL);
}

void run_lchika()
{
    int level = 0;
    while (true) {
        gpio_set_level(GPIO_NUM_4, level);
        level = !level;
        vTaskDelay(300 / portTICK_PERIOD_MS);
    }
}

void run_task(void* args)
{
    int8_t rslt = BME280_OK;
    esp_mqtt_client_handle_t mqtt_client = NULL;

    /** TEST begin **/
    /*rslt = bme280_task_start(s_last_js);
    if (rslt == BME280_OK) {
        ESP_LOGI(TAG_TASK, "JSON Data on test => %s", s_last_js);
        mqtt_client = mqtt_task_start(s_last_js);
    }*/
    /** TEST end **/

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        rslt = bme280_task_start(s_last_js);

        if (rslt == BME280_OK) {
            ESP_LOGI(TAG_TASK, "JSON Data => %s", s_last_js);
            mqtt_client = mqtt_task_start(s_last_js);
        }

    } else {
        sleep_deeply();
    }

    while (1) {
        int recData = 0;

        xQueueReceive(s_pubAlarmQueue, &recData, portMAX_DELAY);
        ESP_LOGI(TAG_TASK, "received MQTT published alarm: CAUSE(%d)", recData);

        if (mqtt_client != NULL) {
            esp_mqtt_client_stop(mqtt_client);
            sleep_deeply();     // fall into deep sleep.

        } else {
            ESP_LOGE(TAG_TASK, "MQTT Error occurred. Falling into deep sleep...");
            sleep_deeply();     // fall into deep sleep.
        }

        vTaskDelay(portMAX_DELAY);
    }
}

void sleep_deeply()
{
    ESP_ERROR_CHECK(esp_wifi_stop());

    int sleep_sec = get_sec_for_alarm_00();
    ESP_LOGI(TAG_TASK, "Falling into a deep sleep...%ds", sleep_sec);

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_timer_wakeup((uint64_t)sleep_sec * 1000000);

    esp_deep_sleep_start();
}

void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG_APP, "Notification of a time synchronization event");
}

void user_delay_ms(uint32_t ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

int8_t user_i2c_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *reg_data, uint16_t cnt)
{
    int32_t iError = 0;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_READ, true);

    if (1 < cnt) {
        i2c_master_read(cmd, reg_data, cnt - 1, 0);     // I2C_MASTER_ACK
    }
    i2c_master_read_byte(cmd, reg_data + cnt - 1, 1);   // I2C_MASTER_NACK
    i2c_master_stop(cmd);

    esp_err_t espRc = i2c_master_cmd_begin(I2C_NUM_0, cmd, 200 / portTICK_PERIOD_MS);
    if (espRc == ESP_OK) {
        iError = 0;     // SUCCESS
    } else {
        iError = -1;    // FAIL
    }

    i2c_cmd_link_delete(cmd);

    return (int8_t)iError;
}

int8_t user_i2c_write(uint8_t dev_addr, uint8_t reg_addr, uint8_t *reg_data, uint16_t cnt)
{
    int32_t iError = 0;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);

    i2c_master_write_byte(cmd, reg_addr, true);
    i2c_master_write(cmd, reg_data, cnt, true);
    i2c_master_stop(cmd);

    esp_err_t espRc = i2c_master_cmd_begin(I2C_NUM_0, cmd, 200 / portTICK_PERIOD_MS);
    if (espRc == ESP_OK) {
        iError = 0;     // SUCCESS
    } else {
        iError = -1;    // FAIL
    }

    i2c_cmd_link_delete(cmd);

    return (int8_t)iError;
}


/**
 * MAIN
 */

void app_main(void)
{
    int wakeup_cause = esp_sleep_get_wakeup_cause();

    if (wakeup_cause == ESP_SLEEP_WAKEUP_TIMER) {
        ESP_LOGI(TAG_APP, "Wake up from deep sleep..");
        time(&waked_up_time);

    } else {
        ESP_LOGI(TAG_APP, "Startup..%d", wakeup_cause);
        ESP_LOGI(TAG_APP, "Free memory: %d bytes", esp_get_free_heap_size());
        ESP_LOGI(TAG_APP, "IDF version: %s", esp_get_idf_version());
    }

    initialize();

    s_pubAlarmQueue = xQueueCreate(4, sizeof(int));

    xTaskCreatePinnedToCore(&run_task, "run_task", 4096, NULL, 1, &s_taskHandle, APP_CPU_NUM);

    // L Chika
    //gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
    //run_lchika();
}
