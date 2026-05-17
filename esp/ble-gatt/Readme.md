# esp蓝牙gatt层的示例代码
## 简单介绍
蓝牙gatt服务是蓝牙协议中最重要的一环，定义了 BLE 设备间数据交换的结构化方式，是 BLE 应用的核心。
其中包含的数据有服务（Service）、特征（Characteristic）、描述符（Descriptor）。
一个蓝牙从机可以有多个服务，每个服务代表着不同的功能；特征是服务的最小单元，由值、权限、专属uuid等组成。特征值是每个服务存储数据的地方，特征权限又表明了每个服务的具有的权限，如可读、可写、指示、通知。
## demo使用说明
### 读写回调函数
```c
static const char *TAG = "app_main";

// 写入回调：当手机 APP 向特征值写入数据时调用
static void on_ble_write(const uint8_t *data, uint16_t len) {
    ESP_LOGI(TAG, "收到 %d 字节数据: %.*s", len, len, data);
    // 这里可以处理控制指令，例如解析命令、修改设备状态等
}

// 读取回调：当手机 APP 读取特征值时调用，动态返回数据
static uint16_t on_ble_read(uint16_t offset, uint8_t *buffer, uint16_t max_len) {
    // 示例：返回设备运行时间（4字节）＋ 传感器值（2字节）
    uint32_t uptime = xTaskGetTickCount() * portTICK_PERIOD_MS;  // 毫秒
    uint16_t sensor = 1234;                                      // 模拟传感器数据

    // 总共需要 6 字节
    uint8_t raw[6];
    memcpy(raw, &uptime, 4);     // 小端
    memcpy(raw + 4, &sensor, 2);

    uint16_t copy_len = (max_len < sizeof(raw)) ? max_len : sizeof(raw);
    if (offset >= sizeof(raw)) {
        return 0;   // 偏移超出，无数据可读
    }
    if (offset + copy_len > sizeof(raw)) {
        copy_len = sizeof(raw) - offset;
    }
    memcpy(buffer, raw + offset, copy_len);
    return copy_len;
}
```
### 初始化和回调函数注册
```c
void app_main()
{
    esp_err_t ret = ble_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BLE 初始化失败: %d", ret);
        return;
    }
    ESP_LOGI(TAG, "BLE 广播中...");

    ble_set_write_callback(on_ble_write);   
    ble_set_read_callback(on_ble_read);     
}
```
详细内容请仔细阅读代码
