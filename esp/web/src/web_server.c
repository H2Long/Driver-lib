/**
 * @file web_server.c
 * @brief HTTP Web 服务器模块 — 功能实现
 *
 * 本模块基于 ESP-IDF 的 esp_http_server 组件实现一个轻量级 HTTP 服务器，
 * 为外部浏览器客户端提供 Web 控制页面。
 *
 * 当前功能：
 *   - 监听端口 80（HTTP 默认端口）
 *   - 处理 GET / 请求，返回设备状态页面（HTML）
 *
 * 设计说明：
 *   - HTTP 服务器的配置使用 HTTPD_DEFAULT_CONFIG() 默认值
 *   - 启用 LRU purge，在内存不足时自动清理最久未使用的连接
 *   - URI 匹配使用通配符方式，便于扩展动态路由
 *
 * @author hhlong
 * @date   2026-05-10
 */

#include "web_server.h"

/** @brief 日志输出标签 */
static const char *TAG = "web_server";

/* ========================================================================== */
/*                          路由处理器实现                                     */
/* ========================================================================== */

/**
 * @brief 根路由 "/" 的 HTTP 请求处理函数
 *
 * 返回一个完整的 HTML 页面，具有现代化深色主题 UI：
 *   - 显示设备在线状态（⚡ ESP32 在线）
 *   - 提供 "查看实时画面" 按钮（指向 /stream 路由，待实现）
 *
 * @param[in] req HTTP 请求对象
 * @return
 *   - ESP_OK: 正常响应
 */
esp_err_t root_handler(httpd_req_t *req)
{
    /* ---- 构造 HTML 响应内容 ---- */
    /* 深色主题卡片式布局，适配移动端 viewport */
    const char* html =
        "<!DOCTYPE html>\n"
        "<html lang='zh-CN'>\n"
        "<head>\n"
        "    <meta charset='UTF-8'>\n"
        "    <meta name='viewport' content='width=device-width, initial-scale=1.0'>\n"
        "    <title>我的 ESP32</title>\n"
        "    <style>\n"
        "        * { margin: 0; padding: 0; box-sizing: border-box; }\n"
        "        body {\n"
        "            font-family: system-ui, -apple-system, 'Segoe UI', Roboto, sans-serif;\n"
        "            background: #111827;\n"
        "            color: #f3f4f6;\n"
        "            min-height: 100vh;\n"
        "            display: flex;\n"
        "            justify-content: center;\n"
        "            align-items: center;\n"
        "            padding: 20px;\n"
        "        }\n"
        "        .card {\n"
        "            background: #1f2937;\n"
        "            border-radius: 24px;\n"
        "            padding: 32px 28px;\n"
        "            max-width: 420px;\n"
        "            width: 100%;\n"
        "            box-shadow: 0 10px 30px rgba(0,0,0,0.5);\n"
        "            text-align: center;\n"
        "        }\n"
        "        .icon { font-size: 48px; margin-bottom: 16px; }\n"
        "        h1 { font-size: 1.8em; margin-bottom: 10px; letter-spacing: -0.5px; }\n"
        "        p { color: #9ca3af; line-height: 1.6; margin-bottom: 24px; }\n"
        "        .button {\n"
        "            display: inline-block;\n"
        "            background: #2563eb;\n"
        "            color: white;\n"
        "            text-decoration: none;\n"
        "            padding: 12px 28px;\n"
        "            border-radius: 40px;\n"
        "            font-weight: 500;\n"
        "            transition: all 0.2s;\n"
        "            border: none;\n"
        "            cursor: pointer;\n"
        "            font-size: 0.95em;\n"
        "        }\n"
        "        .button:hover {\n"
        "            background: #1d4ed8;\n"
        "            transform: translateY(-1px);\n"
        "            box-shadow: 0 6px 15px rgba(37,99,235,0.4);\n"
        "        }\n"
        "        .footer { margin-top: 20px; font-size: 0.75em; color: #4b5563; }\n"
        "    </style>\n"
        "</head>\n"
        "<body>\n"
        "    <div class='card'>\n"
        "        <div class='icon'>⚡</div>\n"
        "        <h1>ESP32 在线</h1>\n"
        "        <p>设备运行正常，你可以通过下方按钮查看实时数据或进行控制。</p>\n"
        "        <a href='/stream' class='button'>📷 查看实时画面</a>\n"
        "        <div class='footer'>ESP-IDF · FreeRTOS</div>\n"
        "    </div>\n"
        "</body>\n"
        "</html>";

    /* 发送 HTTP 响应
     * ──────────────────────────────────────────────────
     * httpd_resp_send() 做了以下工作：
     *   1. 自动设置 Content-Type 为 text/html（未调用 httpd_resp_set_type 时）
     *   2. 构建并发送 HTTP 响应头（状态行 + 头字段）
     *   3. 通过 socket 发送响应体数据
     *   4. 关闭连接（若 Connection: keep-alive 未设置）
     * ────────────────────────────────────────────────── */
    httpd_resp_send(req, html, strlen(html));

    return ESP_OK;
}

/* ========================================================================== */
/*                          服务器生命周期管理                                  */
/* ========================================================================== */

/**
 * @brief 启动 HTTP 服务器
 *
 * 完整执行流程：
 *   1. 分配 httpd_handle_t 句柄
 *   2. 获取默认服务器配置（HTTPD_DEFAULT_CONFIG()）
 *      - 默认监听端口：80
 *      - 默认任务栈大小：4096
 *      - 默认任务优先级：5
 *   3. 启用 LRU purge（低内存时自动断开最久未活动的客户端）
 *   4. 设置 URI 匹配函数为通配符匹配（支持 * 通配符）
 *   5. 调用 httpd_start() → 内部执行：
 *      ├── 创建 TCP socket
 *      ├── bind() 绑定端口 80
 *      ├── listen() 开始监听
 *      ├── 创建 HTTPD 主任务（accept 新连接）
 *      ├── 创建控制任务（内部通信）
 *      ├── 创建 Tx 任务（数据发送）
 *      └── 进入主循环 → accept → 查找路由 → 调用处理器
 *   6. 注册根路由 "/"
 *
 * @return
 *   - ESP_OK: 启动成功
 *   - 其他:   失败，具体原因记录在日志中
 */
esp_err_t start_http_server(void)
{
    httpd_handle_t server = NULL;

    /* ---- 获取默认配置并自定义 ---- */
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;         /* 启用 LRU 缓存清理 */
    config.uri_match_fn = httpd_uri_match_wildcard;  /* 支持通配符匹配 */

    /* ---- 启动 HTTP 服务器 ---- */
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 服务器启动失败：%s", esp_err_to_name(ret));
        return ret;
    }

    /* ---- 注册根路由 "/"（HTTP GET 方法） ---- */
    /* httpd_register_uri_handler() 内部流程：
     *   1. 检查是否已有相同 uri + method 的路由
     *      → 重复注册返回 ESP_ERR_HTTPD_HANDLER_EXISTS
     *   2. 在路由表中分配一个空闲 slot
     *   3. 复制 uri_handler 到路由表
     *   4. 返回 ESP_OK */
    httpd_uri_t root_uri = {
        .uri       = "/",                  /* 路由路径 */
        .method    = HTTP_GET,             /* 支持的 HTTP 方法 */
        .handler   = root_handler,         /* 请求处理函数指针 */
        .user_ctx  = NULL                  /* 用户上下文（暂未使用） */
    };
    httpd_register_uri_handler(server, &root_uri);

    ESP_LOGI(TAG, "HTTP 服务器启动成功！监听端口 %d", config.server_port);
    return ESP_OK;
}
