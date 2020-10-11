/**
 * main.c
 */

#define ACTUAL_MODE 0
#define USE_BLE 1

#include "tt_sec.h"
#include "tt_bme280.h"

#include "bme280.h"


/**
 * STATIC VARIABLES
 */
#if !USE_BLE
static char s_last_js[128] = "";
#endif
RTC_DATA_ATTR static int ntp_cycle_cnt = 0;
static xQueueHandle s_pubAlarmQueue;
static TaskHandle_t s_taskHandle;
static time_t waked_up_time = -1;
static struct gatts_profile_inst gl_profile_tab[NUM_OF_GATT_PROFILE] = {
    [TT_BLE_BME280_APP_ID] = {
        .gatts_cb = ble_gatts_profile_event_handler,
        .gatts_if = ESP_GATT_IF_NONE,       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    },
};


/**
 * EVENT HANDLERS
 */

static void ble_gatts_profile_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t* param)
{
    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_LOGI(TAG_BLE, "PROFILE EVT: ESP_GATTS_REG_EVT, status %d, app_id %d\n", param->reg.status, param->reg.app_id);
        gl_profile_tab[TT_BLE_BME280_APP_ID].service_id.is_primary = true;
        gl_profile_tab[TT_BLE_BME280_APP_ID].service_id.id.inst_id = 0x00;
        gl_profile_tab[TT_BLE_BME280_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_128;
        memcpy((void*)gl_profile_tab[TT_BLE_BME280_APP_ID].service_id.id.uuid.uuid.uuid128, (void*)GATTS_SERVICE_UUID_TT_BME280, sizeof(GATTS_SERVICE_UUID_TT_BME280));
        //gl_profile_tab[TT_BLE_BME280_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;
        //gl_profile_tab[TT_BLE_BME280_APP_ID].service_id.id.uuid.uuid.uuid16 = GATTS_SERVICE_UUID_TEST_A;

        esp_err_t set_dev_name_ret = esp_ble_gap_set_device_name(TT_BLE_DEVNAME);
        if (set_dev_name_ret){
            ESP_LOGE(TAG_BLE, "set device name failed, error code = %x", set_dev_name_ret);
        }

        // CONFIG ADV DATA
        esp_err_t adv_ret = esp_ble_gap_config_adv_data(&s_adv_data);
        //esp_err_t raw_adv_ret = esp_ble_gap_config_adv_data_raw(raw_adv_data, sizeof(raw_adv_data));
        if (adv_ret){
            ESP_LOGE(TAG_BLE, "config adv data failed, error code = %x ", adv_ret);
        }
        s_adv_config_done |= adv_config_flag;

        // CONFIG SCAN RESPONSE DATA
        esp_err_t scan_ret = esp_ble_gap_config_adv_data(&s_scan_rsp_data);
        if (scan_ret){
            ESP_LOGE(TAG_BLE, "config scan response data failed, error code = %x", scan_ret);
        }
        s_adv_config_done |= scan_rsp_config_flag;
        
        esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[TT_BLE_BME280_APP_ID].service_id, 2);
        break;
    case ESP_GATTS_READ_EVT:
        ESP_LOGI(TAG_BLE, "PROFILE EVT: ESP_GATTS_READ_EVT");
        /*ESP_LOGI(GATTS_TAG, "GATT_READ_EVT, conn_id %d, trans_id %d, handle %d\n", param->read.conn_id, param->read.trans_id, param->read.handle);
        esp_gatt_rsp_t rsp;
        memset(&rsp, 0, sizeof(esp_gatt_rsp_t));
        rsp.attr_value.handle = param->read.handle;
        rsp.attr_value.len = 4;
        rsp.attr_value.value[0] = 0xde;
        rsp.attr_value.value[1] = 0xed;
        rsp.attr_value.value[2] = 0xbe;
        rsp.attr_value.value[3] = 0xef;
        esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id,
                                    ESP_GATT_OK, &rsp);
        */break;
    case ESP_GATTS_WRITE_EVT:
        ESP_LOGI(TAG_BLE, "PROFILE EVT: ESP_GATTS_WRITE_EVT");
        /*ESP_LOGI(GATTS_TAG, "GATT_WRITE_EVT, conn_id %d, trans_id %d, handle %d", param->write.conn_id, param->write.trans_id, param->write.handle);
        if (!param->write.is_prep){
            ESP_LOGI(GATTS_TAG, "GATT_WRITE_EVT, value len %d, value :", param->write.len);
            esp_log_buffer_hex(GATTS_TAG, param->write.value, param->write.len);
            if (gl_profile_tab[PROFILE_A_APP_ID].descr_handle == param->write.handle && param->write.len == 2){
                uint16_t descr_value = param->write.value[1]<<8 | param->write.value[0];
                if (descr_value == 0x0001){
                    if (a_property & ESP_GATT_CHAR_PROP_BIT_NOTIFY){
                        ESP_LOGI(GATTS_TAG, "notify enable");
                        uint8_t notify_data[15];
                        for (int i = 0; i < sizeof(notify_data); ++i)
                        {
                            notify_data[i] = i%0xff;
                        }
                        //the size of notify_data[] need less than MTU size
                        esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_profile_tab[PROFILE_A_APP_ID].char_handle,
                                                sizeof(notify_data), notify_data, false);
                    }
                }else if (descr_value == 0x0002){
                    if (a_property & ESP_GATT_CHAR_PROP_BIT_INDICATE){
                        ESP_LOGI(GATTS_TAG, "indicate enable");
                        uint8_t indicate_data[15];
                        for (int i = 0; i < sizeof(indicate_data); ++i)
                        {
                            indicate_data[i] = i%0xff;
                        }
                        //the size of indicate_data[] need less than MTU size
                        esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_profile_tab[PROFILE_A_APP_ID].char_handle,
                                                sizeof(indicate_data), indicate_data, true);
                    }
                }
                else if (descr_value == 0x0000){
                    ESP_LOGI(GATTS_TAG, "notify/indicate disable ");
                }else{
                    ESP_LOGE(GATTS_TAG, "unknown descr value");
                    esp_log_buffer_hex(GATTS_TAG, param->write.value, param->write.len);
                }

            }
        }
        example_write_event_env(gatts_if, &a_prepare_write_env, param);
        */break;
    case ESP_GATTS_EXEC_WRITE_EVT:
        ESP_LOGI(TAG_BLE, "PROFILE EVT: ESP_GATTS_EXEC_WRITE_EVT");
        /*esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
        example_exec_write_event_env(&a_prepare_write_env, param);
        */break;
    case ESP_GATTS_MTU_EVT:
        ESP_LOGI(TAG_BLE, "PROFILE EVT: ESP_GATTS_MTU_EVT, MTU %d", param->mtu.mtu);
        break;
    case ESP_GATTS_UNREG_EVT:
        ESP_LOGI(TAG_BLE, "PROFILE EVT: ESP_GATTS_UNREG_EVT");
        break;
    case ESP_GATTS_CREATE_EVT:
        ESP_LOGI(TAG_BLE, "PROFILE EVT: CREATE_SERVICE_EVT, status %d,  service_handle %d\n", param->create.status, param->create.service_handle);
        gl_profile_tab[TT_BLE_BME280_APP_ID].service_handle = param->create.service_handle;
        gl_profile_tab[TT_BLE_BME280_APP_ID].char_uuid.len = ESP_UUID_LEN_128;
        memcpy((void*)gl_profile_tab[TT_BLE_BME280_APP_ID].char_uuid.uuid.uuid128, (void*)GATTS_CHARACTERISTIC_UUID_TT_BME280, sizeof(GATTS_CHARACTERISTIC_UUID_TT_BME280));
        //gl_profile_tab[TT_BLE_BME280_APP_ID].char_uuid.len = ESP_UUID_LEN_16;
        //gl_profile_tab[TT_BLE_BME280_APP_ID].char_uuid.uuid.uuid16 = GATTS_CHAR_UUID_TEST_A;

        esp_ble_gatts_start_service(gl_profile_tab[TT_BLE_BME280_APP_ID].service_handle);
        /*esp_gatt_char_prop_t bme280_property = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
        esp_err_t add_char_ret = esp_ble_gatts_add_char(gl_profile_tab[TT_BLE_BME280_APP_ID].service_handle, &gl_profile_tab[TT_BLE_BME280_APP_ID].char_uuid,
                                                        ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                                                        bme280_property,
                                                        &s_gatts_demo_char1_val, NULL);
        if (add_char_ret){
            ESP_LOGE(TAG_BLE, "add char failed, error code =%x", add_char_ret);
        }*/
        break;
    case ESP_GATTS_ADD_INCL_SRVC_EVT:
        ESP_LOGI(TAG_BLE, "PROFILE EVT: ESP_GATTS_ADD_INCL_SRVC_EVT");
        break;
    case ESP_GATTS_ADD_CHAR_EVT: {
        ESP_LOGI(TAG_BLE, "PROFILE EVT: ESP_GATTS_ADD_CHAR_EVT");
        /*uint16_t length = 0;
        const uint8_t *prf_char;

        ESP_LOGI(GATTS_TAG, "ADD_CHAR_EVT, status %d,  attr_handle %d, service_handle %d\n",
                param->add_char.status, param->add_char.attr_handle, param->add_char.service_handle);
        gl_profile_tab[PROFILE_A_APP_ID].char_handle = param->add_char.attr_handle;
        gl_profile_tab[PROFILE_A_APP_ID].descr_uuid.len = ESP_UUID_LEN_16;
        gl_profile_tab[PROFILE_A_APP_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
        esp_err_t get_attr_ret = esp_ble_gatts_get_attr_value(param->add_char.attr_handle,  &length, &prf_char);
        if (get_attr_ret == ESP_FAIL){
            ESP_LOGE(GATTS_TAG, "ILLEGAL HANDLE");
        }

        ESP_LOGI(GATTS_TAG, "the gatts demo char length = %x\n", length);
        for(int i = 0; i < length; i++){
            ESP_LOGI(GATTS_TAG, "prf_char[%x] =%x\n",i,prf_char[i]);
        }
        esp_err_t add_descr_ret = esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_A_APP_ID].service_handle, &gl_profile_tab[PROFILE_A_APP_ID].descr_uuid,
                                                                ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE, NULL, NULL);
        if (add_descr_ret){
            ESP_LOGE(GATTS_TAG, "add char descr failed, error code =%x", add_descr_ret);
        }
        */break;
    }
    case ESP_GATTS_ADD_CHAR_DESCR_EVT:
        ESP_LOGI(TAG_BLE, "PROFILE EVT: ESP_GATTS_ADD_CHAR_DESCR_EVT");
        /*gl_profile_tab[PROFILE_A_APP_ID].descr_handle = param->add_char_descr.attr_handle;
        ESP_LOGI(GATTS_TAG, "ADD_DESCR_EVT, status %d, attr_handle %d, service_handle %d\n",
                 param->add_char_descr.status, param->add_char_descr.attr_handle, param->add_char_descr.service_handle);
        */break;
    case ESP_GATTS_DELETE_EVT:
        ESP_LOGI(TAG_BLE, "PROFILE EVT: ESP_GATTS_DELETE_EVT");
        break;
    case ESP_GATTS_START_EVT:
        ESP_LOGI(TAG_BLE, "PROFILE EVT: ESP_GATTS_START_EVT, status %d, service_handle %d\n",
                 param->start.status, param->start.service_handle);
        break;
    case ESP_GATTS_STOP_EVT:
        ESP_LOGI(TAG_BLE, "PROFILE EVT: ESP_GATTS_STOP_EVT");
        break;
    case ESP_GATTS_CONNECT_EVT: {
        ESP_LOGI(TAG_BLE, "PROFILE EVT: ESP_GATTS_CONNECT_EVT");
        /*esp_ble_conn_update_params_t conn_params = {0};
        memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
        *//* For the IOS system, please reference the apple official documents about the ble connection parameters restrictions. */
        /*conn_params.latency = 0;
        conn_params.max_int = 0x20;    // max_int = 0x20*1.25ms = 40ms
        conn_params.min_int = 0x10;    // min_int = 0x10*1.25ms = 20ms
        conn_params.timeout = 400;    // timeout = 400*10ms = 4000ms
        ESP_LOGI(GATTS_TAG, "ESP_GATTS_CONNECT_EVT, conn_id %d, remote %02x:%02x:%02x:%02x:%02x:%02x:",
                 param->connect.conn_id,
                 param->connect.remote_bda[0], param->connect.remote_bda[1], param->connect.remote_bda[2],
                 param->connect.remote_bda[3], param->connect.remote_bda[4], param->connect.remote_bda[5]);
        gl_profile_tab[PROFILE_A_APP_ID].conn_id = param->connect.conn_id;
        //start sent the update connection parameters to the peer device.
        esp_ble_gap_update_conn_params(&conn_params);
        */break;
    }
    case ESP_GATTS_DISCONNECT_EVT:
        ESP_LOGI(TAG_BLE, "PROFILE EVT: ESP_GATTS_DISCONNECT_EVT, disconnect reason 0x%x", param->disconnect.reason);
        /*esp_ble_gap_start_advertising(&adv_params);
        */break;
    case ESP_GATTS_CONF_EVT:
        ESP_LOGI(TAG_BLE, "PROFILE EVT: ESP_GATTS_CONF_EVT, status %d attr_handle %d", param->conf.status, param->conf.handle);
        /*if (param->conf.status != ESP_GATT_OK){
            esp_log_buffer_hex(GATTS_TAG, param->conf.value, param->conf.len);
        }*/
        break;
    case ESP_GATTS_OPEN_EVT:
    case ESP_GATTS_CANCEL_OPEN_EVT:
    case ESP_GATTS_CLOSE_EVT:
    case ESP_GATTS_LISTEN_EVT:
    case ESP_GATTS_CONGEST_EVT:
    default:
        break;
    }

}

static void ble_gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param)
{
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG_BLE, "GAP EVT: ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT");
        s_adv_config_done &= (~adv_config_flag);
        if (s_adv_config_done == 0){
            esp_ble_gap_start_advertising(&s_adv_params);
        }
        break;
    case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
        ESP_LOGI(TAG_BLE, "GAP EVT: ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT");
        s_adv_config_done &= (~scan_rsp_config_flag);
        if (s_adv_config_done == 0){
            esp_ble_gap_start_advertising(&s_adv_params);
        }
        break;
    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        ESP_LOGI(TAG_BLE, "GAP EVT: ESP_GAP_BLE_ADV_START_COMPLETE_EVT");
        //advertising start complete event to indicate advertising start successfully or failed
        if (param->adv_start_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG_BLE, "Advertising start failed\n");
        }
        break;
    case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        ESP_LOGI(TAG_BLE, "GAP EVT: ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT");
        if (param->adv_stop_cmpl.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG_BLE, "Advertising stop failed\n");
        } else {
            ESP_LOGI(TAG_BLE, "Stop advertising successfully\n");
        }
        break;
    case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
        ESP_LOGI(TAG_BLE, "GAP EVT: ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT");
        ESP_LOGI(TAG_BLE, "update connection params status = %d, min_int = %d, max_int = %d,conn_int = %d,latency = %d, timeout = %d",
                  param->update_conn_params.status,
                  param->update_conn_params.min_int,
                  param->update_conn_params.max_int,
                  param->update_conn_params.conn_int,
                  param->update_conn_params.latency,
                  param->update_conn_params.timeout);
        break;
    default:
        break;
    }
}

static void ble_gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    /* If event is register event, store the gatts_if for each profile */
    if (event == ESP_GATTS_REG_EVT) {
        ESP_LOGI(TAG_BLE, "GATT EV: ESP_GATTS_REG_EVT");

        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[param->reg.app_id].gatts_if = gatts_if;
        } else {
            ESP_LOGI(TAG_BLE, "reg app failed, app_id %04x, status %d",
                    param->reg.app_id,
                    param->reg.status);
            return;
        }
    }

    /* If the gatts_if equal to profile A, call profile A cb handler,
     * so here call each profile's callback */
    do {
        int idx;
        for (idx = 0; idx < NUM_OF_GATT_PROFILE; idx++) {
            if (gatts_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                    gatts_if == gl_profile_tab[idx].gatts_if) {
                if (gl_profile_tab[idx].gatts_cb) {
                    gl_profile_tab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
}

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

void init_ble(void)
{
    esp_bt_controller_config_t btCfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_bt_controller_init(&btCfg);
    if (ret) {
        ESP_LOGE(TAG_BLE, "Initialize controller failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) {
        ESP_LOGE(TAG_BLE, "Enable controller failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_init();
    if (ret) {
        ESP_LOGE(TAG_BLE, "Init bluetooth failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = esp_bluedroid_enable();
    if (ret) {
        ESP_LOGE(TAG_BLE, "Enable bluetooth failed: %s", esp_err_to_name(ret));
        return;
    }

    esp_ble_gatts_register_callback(ble_gatts_event_handler);  // gatts module
    esp_ble_gap_register_callback(ble_gap_event_handler);      // gap module

    // MOVE TO BME280_TASK_START() => esp_ble_gatts_app_register(TT_BLE_BME280_APP_ID);

    esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(500);
    if (local_mtu_ret){
        ESP_LOGE(TAG_BLE, "set local  MTU failed, error code = %x", local_mtu_ret);
    }
}

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

void init_esp_timer()
{
    const esp_timer_create_args_t timerArgs = {
        .callback = &onTimer,
        .name = "esp_timer"
    };
    esp_timer_handle_t timerHandle;
    ESP_ERROR_CHECK(esp_timer_create(&timerArgs, &timerHandle));

    ESP_ERROR_CHECK(esp_timer_start_once(timerHandle, 300000000));  // 5min
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
    s_wifi_initialized = true;

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

void bme280_convert_advertisement_data(uint8_t* out, struct bme280_data* comp_data)
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
    time(&now);
    setenv("TZ", "JST-9", 1);
    tzset();
    localtime_r(&now, &timeinfo);

    create_advertisement_data(out, &timeinfo, temp, press, hum, TT_BLE_DEVNAME);
}

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

#if !USE_BLE
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
#else
int8_t bme280_task_start(uint8_t* out)
{
    int8_t rslt = BME280_OK;

    struct bme280_dev bme280;
    struct bme280_data comp_data;

    rslt = bme280_ready(&bme280);
    if (rslt == BME280_OK) {
        rslt = bme280_stream_sensor_data_forced_mode(&comp_data, &bme280);

        if (rslt == BME280_OK) {
            bme280_convert_advertisement_data(out, &comp_data);
            decode_advertisement_data(out);     // check the advertisement raw data
        }       
    }

    return rslt;
}
#endif

void create_advertisement_data(uint8_t* dest, struct tm* timeinfo, float temp, float press, float humid, const char* dsrc)
{
    char tm_buff[16];
    strftime(tm_buff, sizeof(tm_buff), "+%Y%m%d%H%M", timeinfo);

    char cv_buff[9];

    // yyyymmdd
    strncpy(cv_buff, tm_buff + 1, 8);
    *(cv_buff + 8) = '\0';
    int32_t ymd32 = atoi(cv_buff);
    *dest = ymd32 & 255; *(dest + 1) = (ymd32 >> 8) & 255; *(dest + 2) = (ymd32 >> 16) & 255; *(dest + 3) = (ymd32 >> 24) & 255; *(dest + 4) = '\0';

    // hhmm
    strncpy(cv_buff, tm_buff + 9, 4);
    *(cv_buff + 4) = '\0';
    int16_t hm16 = atoi(cv_buff);
    *(dest + 4) = hm16 & 255; *(dest + 5) = (hm16 >> 8) & 255; *(dest + 6) = '\0';

    // temperature
    int16_t t16 = (int)(temp * 100);
    *(dest + 6) = t16 & 255; *(dest + 7) = (t16 >> 8) & 255; *(dest + 8) = '\0';

    // atmosphere pressure
    int32_t p32 = (int)(press * 100);
    *(dest + 8) = p32 & 255; *(dest + 9) = (p32 >> 8) & 255; *(dest + 10) = (p32 >> 16) & 255; *(dest + 11) = '\0';

    // humidity
    int16_t h16 = (int)(humid * 100);
    *(dest + 11) = h16 & 255; *(dest + 12) = (h16 >> 8) & 255; *(dest + 13) = '\0';

    // device name
    /*size_t len = (strlen(dsrc) <= 17) ? strlen(dsrc) : 17;
    *(dest + 13) = len & 255;
    strncpy((char*)(dest + 14), dsrc, 17);*/
}

void decode_advertisement_data(uint8_t* src)
{
    char dc_buff[19];

    /*for (int i = 0; i < 31; i++) {
        ESP_LOGI(TAG_TASK, "Raw advertisement data on test[%d] => %u", i, src[i]);
    }*/

    // yyyymmdd
    int32_t ymd32 = ((int32_t)*src) + ((int32_t)*(src + 1) << 8) + ((int32_t)*(src + 2) << 16) + ((int32_t)*(src + 3) << 24);

    // hhmm
    int32_t hm32 = (int32_t)0 + ((int32_t)*(src + 4)) + ((int32_t)*(src + 5) << 8);

    sprintf(dc_buff, "+%d%04d", ymd32, hm32);
    ESP_LOGI(TAG_BLE, "Datetime => %s", dc_buff);

    // temperature
    int32_t t32 = (int32_t)0 + ((int32_t)*(src + 6)) + ((int32_t)*(src + 7) << 8);
    sprintf(dc_buff, "%2.2f", (float)((float)t32 / 100.00));
    ESP_LOGI(TAG_BLE, "Temp => %s", dc_buff);

    // atmosphere pressure
    int32_t p32 = (int32_t)0 + ((int32_t)*(src + 8)) + ((int32_t)*(src + 9) << 8) + ((int32_t)*(src + 10) << 16);
    sprintf(dc_buff, "%4.2f", (float)((float)p32 / 100.00));
    ESP_LOGI(TAG_BLE, "Press => %s", dc_buff);

    // humidity
    int32_t h32 = (int32_t)0 + ((int32_t)*(src + 11)) + ((int32_t)*(src + 12) << 8);
    sprintf(dc_buff, "%3.2f", (float)((float)h32 / 100.00));
    ESP_LOGI(TAG_BLE, "Humid => %s", dc_buff);

    // device name
    size_t len = ((int32_t)*(src + 13) <= 17) ? (int32_t)*(src + 13) : 17;
    strncpy(dc_buff, (char*)(src + 14), len);
    *(dc_buff + len) = '\0';
    ESP_LOGI(TAG_BLE, "Device Name => %s (Length:%d)", dc_buff, len);
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

#if USE_BLE
    // Initialize Bluetooth Low Energy
    init_ble();
#else
    // Initialize Wi-Fi
    init_wifi();
#endif

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
        // sometimes synchronize the system time
        if (24 <= ntp_cycle_cnt) {
#if USE_BLE
            init_wifi();        // Initialize Wi-Fi
#endif
            init_datetime();
            ntp_cycle_cnt = 0;
        } else {
            ntp_cycle_cnt++;
        }

    } else {
#if USE_BLE
            init_wifi();        // Initialize Wi-Fi
#endif

        // Initialize local datetime
        init_datetime();

    }

    // Initialize I2C
    init_i2c();

    // Initialize Timer
    //init_timer();
    init_esp_timer();
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

    // for HW Tiemr
    /*BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(s_pubAlarmQueue, &cause, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }*/

    // for SW Timer
    xQueueSend(s_pubAlarmQueue, &cause, 0);
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

#if !ACTUAL_MODE    // TEST MODE

#if !USE_BLE
    rslt = bme280_task_start(s_last_js);
    if (rslt == BME280_OK) {
        ESP_LOGI(TAG_TASK, "JSON Data on test => %s", s_last_js);
        mqtt_client = mqtt_task_start(s_last_js);       
    }
#else
    rslt = bme280_task_start(s_raw_manufacturer_specific_data);
    if (rslt == BME280_OK) {
        s_adv_data.p_manufacturer_data = s_raw_manufacturer_specific_data;
        s_scan_rsp_data.p_manufacturer_data = s_raw_manufacturer_specific_data;
            char msdata[66], tmpx[6];
            for (int i = 0; i < 13; i++) {
                sprintf(tmpx, "0x%02x ", s_raw_manufacturer_specific_data[i]);
                strcpy(msdata + (5 * i), tmpx);
            }
            ESP_LOGI(TAG_TASK, "MANUFACTURER SPECIFIC DATA => %s", msdata);
        s_adv_data.manufacturer_len = 14;
        s_scan_rsp_data.manufacturer_len = 14;

        esp_ble_gatts_app_register(TT_BLE_BME280_APP_ID);
    }
#endif

#else               // ACTUAL MODE

    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
#if !USE_BLE
        rslt = bme280_task_start(s_last_js);
        if (rslt == BME280_OK) {
            ESP_LOGI(TAG_TASK, "JSON Data => %s", s_last_js);
            mqtt_client = mqtt_task_start(s_last_js);
        }
#else
        rslt = bme280_task_start(s_raw_manufacturer_specific_data);
        if (rslt == BME280_OK) {
            s_adv_data.p_manufacturer_data = s_raw_manufacturer_specific_data;
            s_scan_rsp_data.p_manufacturer_data = s_raw_manufacturer_specific_data;
                char msdata[66], tmpx[6];
                for (int i = 0; i < 13; i++) {
                    sprintf(tmpx, "0x%02x ", s_raw_manufacturer_specific_data[i]);
                    strcpy(msdata + (5 * i), tmpx);
                }
                ESP_LOGI(TAG_TASK, "MANUFACTURER SPECIFIC DATA => %s", msdata);
            s_adv_data.manufacturer_len = 14;
            s_scan_rsp_data.manufacturer_len = 14;

            esp_ble_gatts_app_register(TT_BLE_BME280_APP_ID);
        }
#endif

    } else {
        sleep_deeply();
    }

#endif

    while (1) {
        int recData = 0;

        xQueueReceive(s_pubAlarmQueue, &recData, portMAX_DELAY);
        /*BaseType_t xTaskWokenByReceive = pdFALSE;
        while (!xQueueReceiveFromISR(s_pubAlarmQueue, &recData, &xTaskWokenByReceive)) {
            vTaskDelay(1000);
        }
        if (xTaskWokenByReceive) {
            taskYIELD();
        }*/

        ESP_LOGI(TAG_TASK, "received the timer alarm: CAUSE(%d)", recData);

        if (mqtt_client != NULL) {
            esp_mqtt_client_stop(mqtt_client);
            sleep_deeply();     // fall into deep sleep.

        } else {
#if !USE_BLE
            ESP_LOGE(TAG_TASK, "MQTT Error occurred. Falling into deep sleep...");
#endif
            sleep_deeply();     // fall into deep sleep.
        }

        vTaskDelay(portMAX_DELAY);
    }
}

void sleep_deeply()
{
    if (s_wifi_initialized) {
        ESP_ERROR_CHECK(esp_wifi_stop());
    }

    int sleep_sec = get_sec_for_alarm_00();
    ESP_LOGI(TAG_TASK, "Falling into a deep sleep...%ds", sleep_sec);

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_timer_wakeup((uint64_t)sleep_sec * 1000000);

    esp_deep_sleep_start();
}

int substr(char* dest, const char* src, size_t pos, size_t len, size_t dest_len)
{
    if (/*pos < 0 || len < 0 ||*/strlen(src) < len || dest_len < len + 1)
        return -1;

    if (pos < strlen(src)) {
        strncpy(dest, src + pos, len);
        *(dest + len) = '\0';
    } else {
        *dest = '\0';
    }

    return 0;
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

    s_pubAlarmQueue = xQueueCreate(1, sizeof(int));

    xTaskCreatePinnedToCore(&run_task, "run_task", 4096, NULL, 10, &s_taskHandle, APP_CPU_NUM);

    // L Chika
    //gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
    //run_lchika();
}
