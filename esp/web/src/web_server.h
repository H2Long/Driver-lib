/**
 * @file web_server.h
 * @brief HTTP Web 服务器模块 — 接口声明
 *
 * 功能：
 *   基于 ESP-IDF HTTP Server（esp_http_server）提供的轻量级 HTTP 服务器，
 *   监听端口 80，处理来自浏览器的 HTTP 请求。
 *
 * 支持的路由：
 *   - GET / ：返回设备状态页面（HTML）
 *
 * @note 本模块目前仅注册了根路由 "/"，后续可扩展添加更多路由处理器。
 *
 * @author hhlong
 * @date   2026-05-10
 */

#ifndef ESP_WEB_WEB_H
#define ESP_WEB_WEB_H

#include "esp_err.h"          /* esp_err_t 类型定义 */
#include "esp_log.h"          /* ESP_LOGI / ESP_LOGE 日志宏 */
#include "esp_http_server.h"  /* httpd_req_t / httpd_handle_t 等 HTTPD 类型 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 根路由 "/" 的 HTTP 请求处理器
 *
 * 响应一个美观的深色主题 HTML 页面，显示设备在线状态。
 * 后续可扩展为包含实时传感器数据、摄像头画面等动态内容。
 *
 * @param[in] req HTTP 请求对象指针（由 HTTPD 框架传入）
 * @return
 *   - ESP_OK: 处理成功
 *   - 其他:   处理失败（由 httpd_resp_send 内部返回）
 */
esp_err_t root_handler(httpd_req_t *req);

/**
 * @brief 启动 HTTP 服务器
 *
 * 执行流程：
 *   1. 使用 HTTPD_DEFAULT_CONFIG() 获取默认配置
 *   2. 启用 LRU purge（缓存清理）
 *   3. 设置 URI 匹配方式为通配符匹配
 *   4. 调用 httpd_start() 启动服务器
 *   5. 注册根路由 "/" 到服务器路由表
 *
 * @return
 *   - ESP_OK: 服务器启动成功
 *   - ESP_FAIL / 其他: 启动失败（具体错误见日志）
 */
esp_err_t start_http_server(void);

#ifdef __cplusplus
}
#endif

#endif /* ESP_WEB_WEB_H */
