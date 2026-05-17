/**
 * @file wifi_sta.c
 * @brief Wi-Fi STA 模式及 TCP 客户端模块 — 功能实现
 *
 * 模块工作流程：
 *   1. wifista_init() 初始化 Wi-Fi 并等待连接
 *   2. 事件回调（wifista_event_handler）监听 Wi-Fi/IP 状态变化
 *   3. 连接成功后设置事件组标志位，通知等待的初始化函数
 *   4. 断开后自动调用 esp_wifi_connect() 重连
 *
 * @note 当前 TCP 客户端任务代码已注释保留，后续可按需启用。
 *
 * @author hhlong
 * @date   2026-05-10
 */

#include "wifi_sta.h"
#include "esp_netif.h"        /* esp_netif_init / esp_netif_create_default_wifi_sta */

/* ========================================================================== */
/*                         静态变量与全局变量                                    */
/* ========================================================================== */

/**
 * @brief 事件组句柄
 *
 * 用于同步 Wi-Fi 初始化函数（wifi_sta_init）和事件回调函数之间的连接状态。
 * 当 IP_EVENT_STA_GOT_IP 事件发生时，回调函数通过事件组通知等待的初始化函数。
 */
static EventGroupHandle_t wifi_event_group;

/** @brief 事件组中表示"Wi-Fi 连接成功"的位 */
const int WIFI_CONNECTED_BIT = BIT0;

/** @brief 日志输出标签 */
static const char *TAG = "wifi_sta";

/**
 * @brief Wi-Fi 连接状态标志
 *
 * - true:  已获取 IP 地址，网络就绪
 * - false: 未连接或已断开
 *
 * 该标志主要由事件回调更新，供 TCP 任务（当前未启用）检查网络状态。
 */
static bool wifi_connected = false;

/* ========================================================================== */
/*                         事件回调函数（静态）                                  */
/* ========================================================================== */

/**
 * @brief Wi-Fi 和 IP 事件统一回调函数
 *
 * 注册为 WIFI_EVENT 和 IP_EVENT 两类事件的监听器。
 *
 * 处理的事件及响应：
 *   ------------------------------------------------------------------
 *   事件                                      | 响应动作
 *   ------------------------------------------------------------------
 *   WIFI_EVENT_STA_START                      | 发起连接 esp_wifi_connect()
 *   WIFI_EVENT_STA_CONNECTED                  | 仅打印日志
 *   WIFI_EVENT_STA_DISCONNECTED               | 打印原因 + 自动重连
 *   IP_EVENT_STA_GOT_IP                       | 设置事件组 + 更新标志
 *   ------------------------------------------------------------------
 *
 * @param[in] event_handler_arg 用户注册时传入的上下文参数（未使用）
 * @param[in] event_base        事件基类：WIFI_EVENT 或 IP_EVENT
 * @param[in] event_id          具体事件 ID
 * @param[in] event_data        事件数据指针，可转换为对应结构体
 */
void wifista_event_handler(void* event_handler_arg, esp_event_base_t event_base,
                           int32_t event_id, void* event_data)
{
    /* ======================== Wi-Fi 事件处理 ======================== */
    if (event_base == WIFI_EVENT) {
        switch (event_id) {

        case WIFI_EVENT_STA_START:
            /* Wi-Fi 驱动初始化完成，发起连接 */
            ESP_LOGI(TAG, "Wi-Fi 驱动已就绪，正在连接热点...");
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_CONNECTED:
            /* 物理层连接成功（尚未分配 IP） */
            ESP_LOGI(TAG, "已连接到 Wi-Fi 热点（等待 IP）");
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            /* 连接断开，记录原因并自动重连 */
            wifi_event_sta_disconnected_t *disconn =
                (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "Wi-Fi 断开连接，原因码：%d，正在重连...",
                     disconn->reason);
            wifi_connected = false;
            esp_wifi_connect();     /* ESP-IDF 驱动自动重连机制 */
            break;
        }

        default:
            /* 其他 Wi-Fi 事件暂不处理 */
            break;
        }
    }
    /* ======================== IP 事件处理 ======================== */
    else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            /* 获取到 IP 地址，网络完全就绪 */
            ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
            ESP_LOGI(TAG, "Wi-Fi 连接成功！IP 地址："
                     IPSTR, IP2STR(&event->ip_info.ip));
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
            wifi_connected = true;
        }
    }
}

/* ========================================================================== */
/*                         公有函数实现                                        */
/* ========================================================================== */

/**
 * @brief 初始化 Wi-Fi STA 模式并连接热点
 *
 * 完整的执行顺序（按步骤展开）：
 *   1. 创建事件组（xEventGroupCreate）
 *   2. NVS 初始化（nvs_flash_init），失败时擦除重试
 *   3. TCP/IP 协议栈初始化（esp_netif_init）
 *   4. 创建默认事件循环（esp_event_loop_create_default）
 *   5. 注册 Wi-Fi 和 IP 事件回调
 *   6. 创建 Wi-Fi STA 网络接口（esp_netif_create_default_wifi_sta）
 *   7. 初始化 Wi-Fi 驱动（esp_wifi_init）
 *   8. 设置为 STA 模式，关闭省电模式
 *   9. 配置 SSID、密码、认证方式
 *   10. 启动 Wi-Fi（esp_wifi_start）
 *   11. 设置发射功率 18 dBm
 *   12. 阻塞等待连接结果（最长 10 秒）
 *
 * @return
 *   - ESP_OK:   连接成功
 *   - ESP_FAIL: 10 秒超时未连接
 *   - 其他:     NVS/Wi-Fi 初始化失败的错误码
 */
esp_err_t wifista_init(void)
{
    /* ---- 1. 创建事件组 ---- */
    wifi_event_group = xEventGroupCreate();
    if (wifi_event_group == NULL) {
        ESP_LOGE(TAG, "事件组创建失败");
        return ESP_FAIL;
    }

    /* ---- 2. 初始化 NVS（非易失性存储） ---- */
    /* NVS 用于存储 Wi-Fi 配置和凭证。
     * 如果 NVS 分区损坏（无空闲页/版本不匹配），则擦除后重试。*/
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 分区需要擦除，正在擦除...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS 初始化完成");

    /* ---- 3~6. 网络栈和事件系统初始化 ---- */
    esp_netif_init();                           /* TCP/IP 协议栈 */
    esp_event_loop_create_default();            /* 默认事件循环 */

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                               &wifista_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                               &wifista_event_handler, NULL);
    ESP_LOGI(TAG, "事件回调注册完成");

    esp_netif_create_default_wifi_sta();         /* STA 网络接口 */

    /* ---- 7~8. Wi-Fi 驱动初始化与模式配置 ---- */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));        /* 初始化 WiFi 驱动 */

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));   /* STA 模式 */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));      /* 关闭省电，避免被踢下线 */
    ESP_LOGI(TAG, "Wi-Fi 驱动初始化完成");

    /* ---- 9. 配置 SSID / 密码 / 认证方式 ---- */
    wifi_config_t wifista_config = {
        .sta = {
            .ssid = DEFAULT_SSID,
            .password = DEFAULT_PWD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifista_config));
    ESP_LOGI(TAG, "Wi-Fi 配置已设置（SSID：%s）", DEFAULT_SSID);

    /* ---- 10~11. 启动 Wi-Fi 并设置发射功率 ---- */
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(18));   /* 18 dBm */
    ESP_LOGI(TAG, "Wi-Fi 已启动，发射功率 18 dBm，正在连接...");

    /* ---- 12. 等待连接结果（最长 10 秒） ---- */
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "=== Wi-Fi 连接成功 ===");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Wi-Fi 连接超时（10 秒），将继续运行（可能无网络）");
        return ESP_FAIL;   /* 返回失败但不阻塞系统继续运行 */
    }

    /* ---- TCP 客户端任务（暂未启用） ---- */
    /* 连接成功后，可在此创建 TCP 客户端任务：
     *   xTaskCreate(tcp_client_task, "tcp_client", 4096, NULL, 5, NULL);
     * 该任务负责向远端服务器（如 192.168.4.1:8888）定时发送传感器数据。*/
}
