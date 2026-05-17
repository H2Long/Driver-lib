# 用esp搭一个轻网页
## 简单介绍
    利用esp的http服务搭建轻量级的http服务器，可用于简单的物联网交互
## demo使用说明
1.需要先修改sta库里面的wifi的账号密码
```c
/** @brief 目标 Wi-Fi 热点的 SSID 名称 */
#define DEFAULT_SSID    "dafault"
/** @brief 目标 Wi-Fi 热点的连接密码 */
#define DEFAULT_PWD     "dafault"
```
2.根据自己的需要修改根路由的html内容
```c
esp_err_t root_handler(httpd_req_t *req)
{
    const char* html = "......";
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}
```
3.目前只注册了一个路由，后续可以自己扩展
4.烧录完成后，打开idf.py monitor查看ip地址，打开浏览器输入ip即可看见http网页
