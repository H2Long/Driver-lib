//
// Created by hhlong on 2026/4/26.
//

#ifndef PROJECT_BLE_H
#define PROJECT_BLE_H

#include "system.h"
/* ========================== 配置结构体 ========================== */

typedef struct {
    uint16_t service_uuid;
    uint16_t char_uuid;
    uint16_t num_handles;
    uint16_t char_properties;
    uint16_t char_permissions;
} ble_profile_config_t;

typedef struct {
    char device_name[31];
    ble_profile_config_t profile_a;
    ble_profile_config_t profile_b;
    uint16_t local_mtu;
} ble_config_t;

#define BLE_CONFIG_DEFAULT() { \
.device_name = "ESP32", \
.profile_a = { 0x00FF, 0xFF01, 4, \
ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_NOTIFY, \
ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE }, \
.local_mtu = 500 \
}

/* ========================== 连接状态 ========================== */

typedef enum {
    BLE_STATE_OFF,
    BLE_STATE_ADVERTISING,
    BLE_STATE_CONNECTED,
} ble_state_t;

/* ========================== 回调类型 ========================== */

typedef void (*ble_write_cb_t)(const uint8_t *value, uint16_t len);
typedef uint16_t (*ble_read_cb_t)(uint16_t offset, uint8_t *out, uint16_t max_len);

/* ========================== 公共函数 ========================== */

esp_err_t ble_init(const ble_config_t *config);
esp_err_t ble_notify(const uint8_t *data, uint16_t len);
ble_state_t ble_get_state(void);
void ble_set_write_callback(ble_write_cb_t cb);
void ble_set_read_callback(ble_read_cb_t cb);

#endif //PROJECT_BLE_H
