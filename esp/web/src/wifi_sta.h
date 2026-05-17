/**
 * @file wifi_sta.h
 * @brief Wi-Fi STA 模式及 TCP 客户端模块 — 接口声明
 *
 * 功能概述：
 *   1. 以 STA（Station）模式连接指定 SSID 的 Wi-Fi 热点
 *   2. 连接成功后自动获取 IP 地址
 *   3. 支持断线自动重连
 *   4. 预留 TCP 客户端接口（用于向远端服务器发送数据）
 *
 * 使用方式：
 *   @code{.c}
 *   #include "wifi_sta.h"
 *   esp_err_t ret = wifista_init();
 *   if (ret != ESP_OK) {
 *       ESP_LOGW(TAG, "Wi-Fi 连接超时，继续运行");
 *   }
 *   @endcode
 *
 * @author hhlong
 * @date   2026-05-10
 */

#ifndef WIFI_STA_H
#define WIFI_STA_H

#include "esp_err.h"          /* esp_err_t 类型定义 */
#include "esp_event.h"        /* 事件循环 API */
#include "esp_log.h"          /* ESP_LOGI / ESP_LOGE 日志宏 */
#include "esp_wifi.h"         /* Wi-Fi 核心 API：esp_wifi_init / esp_wifi_start 等 */
#include "nvs_flash.h"        /* NVS 非易失性存储（Wi-Fi 配置持久化） */
#include "freertos/FreeRTOS.h"/* FreeRTOS 核心 */
#include "freertos/task.h"    /* FreeRTOS 任务 API */

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                                   宏定义                                    */
/* ========================================================================== */

/** @brief 目标 Wi-Fi 热点的 SSID 名称 */
#define DEFAULT_SSID    "dafault"

/** @brief 目标 Wi-Fi 热点的连接密码 */
#define DEFAULT_PWD     "dafault"

/* ========================================================================== */
/*                                 函数声明                                    */
/* ========================================================================== */

/**
 * @brief 初始化 Wi-Fi STA 模式并连接热点
 *
 * 完整的初始化流程：
 *   1. xEventGroupCreate()          — 创建事件组用于连接状态同步
 *   2. nvs_flash_init()             — 初始化 NVS（擦除后重试）
 *   3. esp_netif_init()             — TCP/IP 协议栈初始化
 *   4. esp_event_loop_create_default() — 创建默认事件循环
 *   5. 注册事件回调                  — WiFi/IP 事件监听
 *   6. esp_netif_create_default_wifi_sta() — 创建 STA 网络接口
 *   7. wifi_init_config_t + esp_wifi_init() — Wi-Fi 驱动初始化
 *   8. esp_wifi_set_mode(WIFI_MODE_STA) — 设置为 STA 模式
 *   9. esp_wifi_set_config()        — 配置 SSID/密码/认证方式
 *   10. esp_wifi_start()            — 启动 Wi-Fi
 *   11. xEventGroupWaitBits()       — 阻塞等待连接（最长 10 秒）
 *
 * @note 连接超时后会返回 ESP_FAIL，但不会阻塞系统继续运行。
 *       调用者可根据返回值决定是否继续启动其他服务。
 *
 * @return
 *   - ESP_OK:   Wi-Fi 连接成功，已获取 IP 地址
 *   - ESP_FAIL: 连接超时（10 秒内未连接成功）
 *   - 其他:     初始化过程中的错误码
 */
esp_err_t wifista_init(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_STA_H */
