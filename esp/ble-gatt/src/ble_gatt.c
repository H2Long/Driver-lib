//
// Created by hhlong on 2026/4/26.
// BLE GATT Server — 单 Profile (A: 控制)
// 文件说明：本文件实现了完整的 BLE GATT Server 端功能，
//          包含一个 Profile A，支持广播、连接、读写、通知、指示等功能，
//          并可通过回调函数与外部模块交互。
//

#include "ble_gatt.h"  // 引入自定义的 BLE GATT 头文件（包含类型定义、宏定义、函数声明等）

/* ========================== 准备写入环境 ========================== */
/**
 * @brief 准备写入环境结构体
 * @details 用于存储大数据分片写入的临时数据（准备写入模式）
 *          当客户端需要写入超过 MTU 大小的数据时，会使用 Prepare Write 方式分片写入
 */
typedef struct {
    uint8_t *prepare_buf;   // 准备写入缓冲区指针（动态分配内存，用于存储所有分片数据）
    int prepare_len;        // 缓冲区已写入长度（记录当前已累积的数据字节数）
} prepare_type_env_t;       // 定义结构体别名 prepare_type_env_t


/* ========================== 前置声明 ========================== */
/**
 * @brief Profile A 事件处理回调函数（前置声明）
 * @param event GATT事件类型（如注册、读、写、连接、断开等）
 * @param gatts_if GATT Server接口句柄（用于标识是哪个GATT服务实例收到的事件）
 * @param param 事件参数结构体，包含事件相关的具体数据（如连接句柄、数据内容等）
 */
static void gatts_profile_a_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);

/**
 * @brief 通用写事件处理函数（处理准备写入/普通写入）（前置声明）
 * @param gatts_if GATT Server接口句柄
 * @param prepare_write_env 准备写入环境结构体指针（用于管理分片写入缓冲区）
 * @param param GATT写事件参数（包含写入的数据、偏移量、句柄等信息）
 */
void example_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);

/**
 * @brief 通用执行写事件处理函数（确认/取消准备写入）（前置声明）
 * @param prepare_write_env 准备写入环境结构体指针
 * @param param GATT执行写事件参数（包含执行标志：确认或取消）
 */
void example_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param);

/* ========================== 模块私有状态 ========================== */
static ble_config_t   s_config;       // [全局静态] BLE 配置结构体实例（存储设备名、UUID等配置信息）
static ble_state_t    s_state = BLE_STATE_OFF; // [全局静态] BLE 当前状态（初始状态：关闭）
static ble_write_cb_t s_write_cb = NULL;       // [全局静态] 数据写入回调函数指针（外部模块注册，当收到数据时调用）
static ble_read_cb_t  s_read_cb  = NULL;       // [全局静态] 数据读取回调函数指针（外部模块注册，当读取数据时调用）
static uint8_t  s_char_value_a[64];            // [全局静态] Profile A 特征值的存储数组（最大64字节）

/* ========================== 全局配置常量 ========================== */
// 注意：设备名称现在从 s_config.device_name 获取（运行时可配置）
#define TEST_MANUFACTURER_DATA_LEN  17         // 厂商自定义数据固定长度（17字节，用于厂商特定信息）
#define GATTS_DEMO_CHAR_VAL_LEN_MAX 0x40       // 特征值最大允许长度（0x40 = 64 字节）
#define PREPARE_BUF_MAX_SIZE        1024        // 准备写入缓冲区最大容量（1KB，用于大数据分片写入时的临时存储）

/* ========================== 全局变量 ========================== */
static uint16_t s_desc_value = 0x0;             // [全局静态] CCCD 描述符当前值（0x0000=无通知, 0x0001=通知开启, 0x0002=指示开启）
static uint16_t s_local_mtu = 23;               // [全局静态] 本地协商后的 MTU 值（初始值 23 字节，即蓝牙默认最小 MTU）

/* ========================== 服务A特征值属性结构体 ========================== */
// 该结构体定义了 Profile A 特征值的属性信息（最大长度、当前长度、数据指针）
static esp_attr_value_t gatts_demo_char1_val =  // 初始化特征值属性结构体
{
    .attr_max_len = GATTS_DEMO_CHAR_VAL_LEN_MAX,  // 特征值允许的最大数据长度（64字节）
    .attr_len     = 1,                              // 特征值当前的有效数据长度（初始化为1字节）
    .attr_value   = s_char_value_a,                 // 指向实际存储特征值数据的数组（Profile A 的数据缓冲区）
};

/* ========================== 广播配置相关 ========================== */
static uint8_t adv_config_done = 0;              // [全局静态] 广播配置完成标志位（bit0=主广播包, bit1=扫描响应包，全0表示都配置完成）
#define adv_config_flag      (1 << 0)             // 主广播包配置完成标志位（bit 0 置位表示主广播包正在配置或已配置）
#define scan_rsp_config_flag (1 << 1)             // 扫描响应包配置完成标志位（bit 1 置位表示扫描响应包正在配置或已配置）

/**
 * @brief 广播包中携带的128位服务UUID数组（仅服务A一个128位UUID）
 * @note 蓝牙128位UUID格式：
 *       前12字节为基础UUID（XXXXXXXX-0000-1000-8000-00805F9B34FB），后4字节为自定义短UUID
 *       格式：小端传输（低字节在前）
 *       - 服务A UUID（0x00FF）
 */
static uint8_t adv_service_uuid128[16] = {       // 16字节 = 1个128位UUID
    /* ===== 服务A的128位UUID (0x00FF) ===== */
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
};

/**
 * @brief 主广播包（Advertising Data）配置结构体
 * @note 主广播包是设备主动对外发送的数据包，包含设备的基本信息
 *       主广播包有31字节的长度限制，需要精心安排携带的信息
 *       主要用于：设备发现、设备识别、初步过滤
 */
static esp_ble_adv_data_t adv_data =
{
    .set_scan_rsp = false,                        // 标识此配置为主广播包（false）而非扫描响应包（true）
    .include_name = true,                         // 在广播包中包含设备名称（让扫描方知道设备名字）
    .include_txpower = false,                     // 不在广播包中包含发射功率（节省空间，发射功率可在扫描响应包中携带）
    .min_interval = 0x0006,                       // 连接间隔最小值（广播模式下此字段仅作参考，实际意义不大，单位1.25ms）
    .max_interval = 0x0010,                       // 连接间隔最大值（同上，单位1.25ms）
    .appearance = 0x00,                           // 设备外观类别（0x00表示未定义外观，如需指定可设为 0x03C2（心率带）等）
    .manufacturer_len = 0,                        // 厂商自定义数据的长度（0表示不携带厂商数据）
    .p_manufacturer_data = NULL,                  // 厂商自定义数据的指针（NULL 表示无数据）
    .service_data_len = 0,                        // 附加服务数据的长度（0表示不携带服务数据）
    .p_service_data = NULL,                       // 附加服务数据的指针（NULL 表示无数据）
    .service_uuid_len = sizeof(adv_service_uuid128), // 要广播的服务UUID的总字节数（16字节 = 1个128位UUID）
    .p_service_uuid = adv_service_uuid128,        // 指向服务UUID数组的指针（让扫描设备知道本机提供哪些服务）
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT), // 广播标志位组合：
                                                                          // ESP_BLE_ADV_FLAG_GEN_DISC = 通用可发现模式（任何设备都能扫描到）
                                                                          // ESP_BLE_ADV_FLAG_BREDR_NOT_SPT = 仅支持BLE（不支持经典蓝牙BR/EDR）
};

/**
 * @brief 扫描响应包（Scan Response Data）配置结构体
 * @note 扫描响应包是可选的辅助数据包，仅在主广播包被扫描时才发送
 *       用于补充主广播包因长度限制未能携带的信息（如发射功率、完整设备名等）
 *       扫描响应包同样有31字节的长度限制
 */
static esp_ble_adv_data_t scan_rsp_data =
{
    .set_scan_rsp = true,                         // 标识此配置为扫描响应包（true）
    .include_name = true,                         // 在扫描响应包中也包含设备名称（增强可被发现性）
    .include_txpower = true,                      // 在扫描响应包中包含发射功率（帮助扫描端评估信号质量）
    .appearance = 0x00,                           // 设备外观类别（与主广播包保持一致）
    .manufacturer_len = 0,                        // 不携带厂商数据
    .p_manufacturer_data = NULL,
    .service_data_len = 0,                        // 不携带附加服务数据
    .p_service_data = NULL,
    .service_uuid_len = sizeof(adv_service_uuid128), // 同样携带服务UUID（增强服务发现的可靠性）
    .p_service_uuid = adv_service_uuid128,
    .flag = (ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT), // 广播标志（与主广播包保持一致）
};

/**
 * @brief 广播参数配置结构体
 * @note 控制广播行为的核心参数：广播间隔、广播类型、通道选择、过滤策略等
 *       合理配置这些参数可优化功耗、连接速度和抗干扰能力
 */
static esp_ble_adv_params_t adv_params =
{
    .adv_int_min        = 0x20,                   // 最小广播间隔（0x20 × 0.625ms = 20ms，单位为0.625ms）
                                                   // 广播间隔会在 min 和 max 之间随机变化，避免多设备同步干扰
    .adv_int_max        = 0x40,                   // 最大广播间隔（0x40 × 0.625ms = 40ms）
                                                   // 较短的间隔 → 更快被发现在但更高功耗
                                                   // 较长的间隔 → 省电但连接建立慢
    .adv_type           = ADV_TYPE_IND,           // 广播类型：ADV_TYPE_IND = 可连接非定向广播（最常用模式）
                                                   // 可连接：其他设备可以发起连接请求
                                                   // 非定向：向周围所有设备广播（不针对特定设备）
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,   // 本机使用的蓝牙地址类型：公有地址（Public Address）
                                                   // 公有地址：出厂时烧录的唯一地址（不可更改）
    .channel_map        = ADV_CHNL_ALL,           // 广播通道映射：使用全部3个广播通道（37、38、39）
                                                   // 三个通道都在 2.4GHz ISM 频段，使用全部通道提高抗干扰能力
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY, // 广播过滤策略：
                                                              // 允许任意设备扫描（SCAN_ANY）+ 允许任意设备连接（CON_ANY）
                                                              // 即不过滤，对所有扫描和连接请求都响应
};

/* ========================== GATT Profile管理 ========================== */
#define PROFILE_NUM 1               // 定义的 Profile 总数量（本项目仅定义了 A 一个 Profile）
#define PROFILE_A_APP_ID 0          // Profile A 的应用ID（在 Profile 表中的索引位置为 0）

/**
 * @brief GATT Profile 实例结构体（核心数据结构）
 * @details 存储每个 Profile 运行时的完整状态信息，包括回调函数、接口句柄、服务/特征/描述符的句柄和UUID等
 *          每个 Profile 对应一个独立的服务，拥有自己的特征值和描述符
 */
struct gatts_profile_inst {
    esp_gatts_cb_t gatts_cb;        // Profile 的 GATT 事件回调函数指针（指向该 Profile 的事件处理函数）
    uint16_t gatts_if;              // GATT Server 接口句柄（由协议栈在注册成功后分配，用于标识该 Profile 的接口实例）
    uint16_t app_id;                // 应用ID / Profile 索引（区分不同 Profile 的标识符，对应 PROFILE_A_APP_ID）
    uint16_t conn_id;               // 当前连接 ID（标识与哪个远端设备的连接，断开后失效）
    uint16_t service_handle;        // 服务句柄（创建服务后由协议栈分配，用于后续操作服务的唯一标识）
    esp_gatt_srvc_id_t service_id;  // 服务 ID 结构体（包含服务的 UUID 和是否为主服务的标识）
    uint16_t char_handle;           // 特征句柄（添加特征后由协议栈分配，用于读写该特征的唯一标识）
    esp_bt_uuid_t char_uuid;        // 特征 UUID（标识该特征的功能类型，如心率测量、电池电量等）
    esp_gatt_perm_t perm;           // 特征权限（控制对该特征的访问权限：可读/可写/需要加密等）
    esp_gatt_char_prop_t property;  // 特征属性（定义该特征的能力：支持读/写/通知/指示等）
    uint16_t descr_handle;          // 描述符句柄（添加描述符后由协议栈分配）
    esp_bt_uuid_t descr_uuid;       // 描述符 UUID（标识描述符的类型，如 CCCD 为 0x2902）
};

/**
 * @brief Profile 实例表（数组形式存储所有 Profile 的运行状态）
 * @details 本项目仅定义了 1 个 Profile：
 *          - Profile A（索引 0）：控制类服务（用于指令下发、状态上报等）
 */
static struct gatts_profile_inst gl_profile_tab[PROFILE_NUM] = {  // 创建包含 PROFILE_NUM 个元素的静态数组
    [PROFILE_A_APP_ID] = {                                        // 初始化 Profile A（使用 C99 指定初始化器）
        .gatts_cb = gatts_profile_a_event_handler,                // 设置 Profile A 的事件回调函数
        .gatts_if = ESP_GATT_IF_NONE,                             // 初始化 GATT 接口句柄为无效值（注册后由协议栈分配有效值）
                                                                    // 其他字段将在运行时逐步填充（创建服务、添加特征时赋值）
    },
};

static prepare_type_env_t a_prepare_write_env = {NULL, 0}; // [全局静态] Profile A 的准备写入环境（用于大数据分片写入）
                                                            // 初始化时缓冲区为 NULL，已写入长度为 0

/* ========================== GAP事件回调函数 ========================== */
/**
 * @brief BLE GAP（Generic Access Profile）事件回调函数
 * @details GAP 是蓝牙协议栈的最上层，负责设备发现、连接管理、广播控制等功能
 *          此回调函数处理所有 GAP 层级的事件，包括广播配置、启动、停止、连接参数更新等
 *
 * @param event GAP 事件类型枚举（如广播配置完成、广播启动完成、连接参数更新等）
 * @param param GAP 事件参数联合体（根据事件类型包含不同的具体参数）
 */
static void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {                    // 根据事件类型分发到对应的处理逻辑

        // ========== 主广播包配置完成事件 ==========
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            // 当 esp_ble_gap_config_adv_data() 配置的主广播包被协议栈成功处理后触发
            adv_config_done &= (~adv_config_flag);  // 使用位清除操作将主广播配置标志位清零
                                                     // ~adv_config_flag 将 bit0 取反（变为1110），再与操作清除 bit0
            if (adv_config_done == 0) {             // 检查是否所有广播配置都已完成（主广播 + 扫描响应都配置完则值为0）
                esp_ble_gap_start_advertising(&adv_params); // 启动广播（传入之前定义好的广播参数结构体）
                                                               // 设备开始向周围发送广播包，等待被扫描和连接
            }
            break;                                  // 退出 switch（重要！防止穿透到下一个 case）

        // ========== 扫描响应包配置完成事件 ==========
        case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
            // 当 esp_ble_gap_config_adv_data() 配置的扫描响应包被协议栈成功处理后触发
            adv_config_done &= (~scan_rsp_config_flag); // 使用位清除操作将扫描响应配置标志位清零
                                                         // 清除 bit1（扫描响应包的标志位）
            if (adv_config_done == 0) {                 // 检查是否所有广播配置都已完成
                esp_ble_gap_start_advertising(&adv_params); // 启动广播（条件满足时才开始广播）
            }
            break;

        // ========== 广播启动完成事件 ==========
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            // 当 esp_ble_gap_start_advertising() 被协议栈执行完毕后触发
            /* 广播启动完成，可在此处添加日志输出（如打印"广播已启动"）或状态处理逻辑 */
            break;

        // ========== 广播停止完成事件 ==========
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            // 当停止广播的操作完成后触发
            /* 广播停止完成，可在此处添加日志输出或状态处理逻辑 */
            break;

        // ========== 连接参数更新完成事件 ==========
        case ESP_GAP_BLE_UPDATE_CONN_PARAMS_EVT:
            // 当连接参数更新请求被远端设备接受并生效后触发
            /* 连接参数更新完成，可在此处校验更新的参数是否符合预期（如检查连接间隔是否在合理范围内） */
            break;

        // ========== 数据包长度更新完成事件 ==========
        case ESP_GAP_BLE_SET_PKT_LENGTH_COMPLETE_EVT:
            // 当 BLE 4.2+ 的数据包长度更新（DLE）完成后触发
            /* 数据包长度更新完成，可在此处处理 MTU 相关的逻辑（如启用更大的数据包传输） */
            break;

        default:
            break;  // 未处理的事件类型，直接忽略
    }
}

/* ========================== 通用写事件处理 ========================== */
/**
 * @brief 通用 GATT 写事件处理函数
 * @details 统一处理普通写入和 Prepare Write（准备写入/长写）两种写入模式
 *          - 普通写入：一次性写入少量数据（≤MTU-3字节），直接返回响应
 *          - Prepare Write：大数据分片写入（>MTU-3字节），先缓存所有分片，最后由 Execute Write 确认
 *
 * @param gatts_if GATT Server 接口句柄（标识是哪个 Profile 收到的写请求）
 * @param prepare_write_env 准备写入环境指针（用于管理该 Profile 的分片写入缓存区）
 * @param param 写事件的参数结构体（包含写入的数据、目标句柄、偏移量、是否Prepare Write等信息）
 */
void example_write_event_env(esp_gatt_if_t gatts_if, prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param)
{
    esp_gatt_status_t status = ESP_GATT_OK; // 定义 GATT 操作状态变量，初始化为成功状态（ESP_GATT_OK = 0x00）

    // 步骤1：判断本次写请求是否需要响应（Write Command 不需要响应，Write Request 需要响应）
    if (param->write.need_rsp) {
        // need_rsp == true：这是 Write Request（需要服务端回复确认）

        // 步骤2：判断是否为 Prepare Write（长写/分片写入）
        if (param->write.is_prep) {
            // is_prep == true：客户端使用 Prepare Write 方式进行大数据分片写入

            // 步骤3：校验写入偏移量是否超出缓冲区最大范围
            if (param->write.offset > PREPARE_BUF_MAX_SIZE) {
                // 如果偏移量超过缓冲区总大小，说明请求非法
                status = ESP_GATT_INVALID_OFFSET;  // 设置错误状态码：无效偏移量
            } else if ((param->write.offset + param->write.len) > PREPARE_BUF_MAX_SIZE) {
                // 如果（偏移量 + 本次写入长度）超过缓冲区总大小，会导致溢出
                status = ESP_GATT_INVALID_ATTR_LEN; // 设置错误状态码：无效属性长度
            }

            // 步骤4：如果是第一次 Prepare Write，需要初始化缓冲区（动态分配内存）
            if (status == ESP_GATT_OK && prepare_write_env->prepare_buf == NULL) {
                // 只有前面没有错误且缓冲区尚未分配时才进行分配
                prepare_write_env->prepare_buf = (uint8_t *)malloc(PREPARE_BUF_MAX_SIZE * sizeof(uint8_t));
                                                                 // 动态分配 1024 字节的缓冲区（堆内存）
                prepare_write_env->prepare_len = 0;  // 初始化已写入长度为 0
                if (prepare_write_env->prepare_buf == NULL) {
                    // 内存分配失败（系统内存不足）
                    status = ESP_GATT_NO_RESOURCES;  // 设置错误状态码：资源不足
                }
            }

            // 步骤5：分配并填充 GATT 响应结构体（用于回送给客户端的 Prepare Write Response）
            esp_gatt_rsp_t *gatt_rsp = (esp_gatt_rsp_t *)calloc(1, sizeof(esp_gatt_rsp_t));
                                                                     // 使用 calloc 分配并自动清零内存
                                                                     // esp_gatt_rsp_t 是统一的 GATT 响应结构体
            if (gatt_rsp) {                 // 检查内存分配是否成功
                // 填充响应结构体的各个字段（模拟 echo 回显，将客户端发送的数据原样返回）
                gatt_rsp->attr_value.len = param->write.len;          // 响应数据长度 = 请求数据长度
                gatt_rsp->attr_value.handle = param->write.handle;    // 目标特征/描述符句柄（与请求一致）
                gatt_rsp->attr_value.offset = param->write.offset;    // 数据偏移量（与请求一致）
                gatt_rsp->attr_value.auth_req = ESP_GATT_AUTH_REQ_NONE; // 认证需求：无需认证
                                                                        // （某些安全特征可能要求加密/认证）
                memcpy(gatt_rsp->attr_value.value, param->write.value, param->write.len);
                                                                         // 将客户端发送的数据拷贝到响应结构体中
                                                                         // （Prepare Write 要求服务器回显数据以供客户端校验）
                // 发送 Prepare Write 响应给客户端
                esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, gatt_rsp);
                                                                             // 参数依次为：接口句柄、连接ID、事务ID、状态码、响应数据
                free(gatt_rsp);              // 释放响应结构体内存（发送完毕后不再需要，防止内存泄漏）
            } else {
                // 响应结构体分配失败
                status = ESP_GATT_NO_RESOURCES; // 标记资源不足错误
            }

            // 步骤6：如果前面的步骤没有出错，将本次收到的数据拷贝到准备写入缓冲区的对应位置
            if (status != ESP_GATT_OK) {
                return;     // 如果有错误（偏移量越界、长度越界、内存不足等），直接返回不缓存数据
            }
            memcpy(prepare_write_env->prepare_buf + param->write.offset,  // 目标地址 = 缓冲区起始 + 偏移量
                   param->write.value,                                     // 源数据 = 客户端发送的数据
                   param->write.len);                                      // 拷贝长度 = 本次写入的数据长度
            prepare_write_env->prepare_len += param->write.len;            // 累加已写入的总字节数

        } else {
            // is_prep == false：这是普通的 Write Request（非分片的一次性写入）
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, status, NULL);
                                                                               // 发送写响应给客户端
                                                                               // 最后一个参数 NULL 表示不需要附带额外数据
        }
    }
    // 注：如果 need_rsp == false（Write Command），则不需要做任何响应（蓝牙规范规定 Write Command 无需响应）
}

/**
 * @brief 执行写事件处理函数
 * @details 处理客户端发送的 "Execute Write Request"
 *          Execute Write 有两种模式：
 *          - EXEC（执行）：确认之前的所有 Prepare Write 分片，将缓存的数据正式提交
 *          - CANCEL（取消）：放弃之前的所有 Prepare Write 分片，丢弃缓存数据
 *          无论哪种模式，都需要释放准备写入缓冲区
 *
 * @param prepare_write_env 准备写入环境指针（包含待释放的缓冲区和长度信息）
 * @param param 执行写事件的参数（包含执行标志：ESP_GATT_PREP_WRITE_EXEC 或 ESP_GATT_PREP_WRITE_CANCEL）
 */
void example_exec_write_event_env(prepare_type_env_t *prepare_write_env, esp_ble_gatts_cb_param_t *param)
{
    // 根据执行标志判断是确认还是取消
    if (param->exec_write.exec_write_flag == ESP_GATT_PREP_WRITE_EXEC) {
        // 执行标志 == EXEC：客户端确认提交所有 Prepare Write 分片数据
        /* 执行准备写入：此处可添加大数据合并/持久化/业务逻辑处理等代码 */
        /* 示例：可以将 prepare_write_env->prepare_buf 中的完整数据进行解析、存储或转发 */
    } else {
        // 执行标志 == CANCEL（或其他值）：客户端取消了之前的 Prepare Write 操作
        /* 取消准备写入：仅需清理资源，不做数据处理 */
    }

    // 释放准备写入缓冲区（无论执行还是取消，都需要释放动态分配的内存）
    if (prepare_write_env->prepare_buf) {
        // 检查缓冲区指针是否有效（非空）
        free(prepare_write_env->prepare_buf);  // 调用 free() 释放之前 malloc() 分配的内存
        prepare_write_env->prepare_buf = NULL;  // 将指针置为 NULL（防止悬垂指针/dangling pointer）
    }
    prepare_write_env->prepare_len = 0;         // 重置已写入长度为 0（恢复到初始状态）
}

/* ========================== Profile A 事件处理 ========================== */
/**
 * @brief Profile A 的 GATT 事件处理回调函数
 * @details 处理 Profile A 的所有 GATT 层级事件，包括：
 *          - 注册事件：创建服务、配置广播
 *          - 读/写事件：处理客户端对特征值和描述符的读写请求
 *          - 连接/断开事件：管理连接生命周期
 *          - MTU 协商：更新本地 MTU 以支持更大数据包
 *
 * @param event GATT 事件类型
 * @param gatts_if GATT Server 接口句柄
 * @param param GATT 事件参数
 */
static void gatts_profile_a_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    switch (event) {                    // 根据事件类型分发处理逻辑

        // ========== GATT Server 注册完成事件 ==========
        case ESP_GATTS_REG_EVT:
            // 触发时机：调用 esp_ble_gatts_app_register() 后，协议栈完成 Profile 注册时触发
            // 这是整个 GATT 服务构建流程的起点

            // ---- 配置服务A的服务ID ----
            gl_profile_tab[PROFILE_A_APP_ID].service_id.is_primary = true;  // 标记为主服务（Primary Service）
                                                                            // GATT 中服务分为 Primary（主服务）和 Secondary（从服务/ Included Service）
            gl_profile_tab[PROFILE_A_APP_ID].service_id.id.inst_id = 0x00;  // 服务实例 ID（同一 UUID 可有多个实例，此处用第1个实例）
            gl_profile_tab[PROFILE_A_APP_ID].service_id.id.uuid.len = ESP_UUID_LEN_16;  // UUID 长度：16位短 UUID（2字节）
            gl_profile_tab[PROFILE_A_APP_ID].service_id.id.uuid.uuid.uuid16 = s_config.profile_a.service_uuid;
                                                                                          // 服务 UUID 值（从配置结构体获取）

            // ---- 设置 BLE 设备名称 ----
            esp_ble_gap_set_device_name(s_config.device_name);
                                                          // 调用 GAP API 设置设备名称
                                                          // 设备名称将出现在广播包和扫描结果中
                                                          // 最长不超过 29 字节（UTF-8 编码）

            // ---- 配置并启动广播 ----
            esp_ble_gap_config_adv_data(&adv_data);       // 配置主广播包（Advertising Data）
                                                           // 传入之前定义好的 adv_data 结构体
            adv_config_done |= adv_config_flag;            // 将主广播配置标志位（bit0）置1，标记"正在配置中"
            esp_ble_gap_config_adv_data(&scan_rsp_data);   // 配置扫描响应包（Scan Response Data）
                                                           // 传入之前定义好的 scan_rsp_data 结构体
            adv_config_done |= scan_rsp_config_flag;       // 将扫描响应配置标志位（bit1）置1
                                                           // 当两个配置完成的回调都将对应 flag 清零且结果为 0 时，才会真正启动广播

            // ---- 创建服务A ----
            esp_ble_gatts_create_service(gatts_if, &gl_profile_tab[PROFILE_A_APP_ID].service_id, s_config.profile_a.num_handles);
                                                                              // 调用 GATT API 创建新服务
                                                                              // 参数：接口句柄、服务ID、所需句柄数量（决定协议栈预留多少句柄空间）
            break;

        // ========== GATT 读事件 ==========
        case ESP_GATTS_READ_EVT: {
            // 触发时机：客户端发送 Read Request 读取某个特征值或描述符时触发
            // 使用花括号 {} 创建局部作用域（因为需要在内部声明局部变量）

            if (!param->read.need_rsp) { // 检查是否需要发送响应（某些特殊情况下可能不需要）
                return;                     // 无需响应则直接返回
            }

            // ---- 初始化读响应结构体 ----
            esp_gatt_rsp_t rsp;            // 声明 GATT 响应结构体（用于构造发给客户端的读响应）
            memset(&rsp, 0, sizeof(esp_gatt_rsp_t));  // 将结构体所有字段清零初始化（防止残留脏数据）
            rsp.attr_value.handle = param->read.handle;  // 设置响应的目标句柄（必须与请求的句柄匹配）

            // ---- 判断读取目标：CCCD 描述符 ----
            if (param->read.handle == gl_profile_tab[PROFILE_A_APP_ID].descr_handle) {
                // 客户端读取的是 CCCD（Client Characteristic Configuration Descriptor）
                // CCCD 存储了通知/指示的开关状态
                memcpy(rsp.attr_value.value, &s_desc_value, 2);  // 将 CCCD 值（2字节）拷贝到响应数据中
                rsp.attr_value.len = 2;                           // 响应数据长度为 2 字节
                // 发送读响应给客户端
                esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
                                                                                    // 参数：接口句柄、连接ID、事务ID、成功状态、响应数据
                return;                 // CCCD 读取已完成，直接返回（不继续后面的特征值读取逻辑）
            }

            // ---- 判断读取目标：特征值 ----
            if (param->read.handle == gl_profile_tab[PROFILE_A_APP_ID].char_handle) {
                // 客户端读取的是 Profile A 的特征值
                uint16_t offset = param->read.offset;  // 获取读取偏移量（用于 Long Read，即分多次读取大数据的场景）
                uint16_t mtu_size = s_local_mtu - 1;  // 单次最大发送长度 = 协商后的 MTU - 1
                uint16_t rd_len = 0;                  // 本次实际读取的数据长度

                if (s_read_cb) {
                    // 如果注册了外部读取回调函数，通过回调获取数据
                    rd_len = s_read_cb(offset, rsp.attr_value.value, mtu_size);
                } else {
                    // 没有回调函数，使用默认的静态数据（s_char_value_a 数组）
                    rd_len = sizeof(s_char_value_a) - offset;   // 计算剩余可读字节数
                    if (rd_len > mtu_size) rd_len = mtu_size;  // 如果剩余数据超过 MTU 限制，则截断为 MTU 大小
                    memcpy(rsp.attr_value.value, &s_char_value_a[offset], rd_len);
                }

                rsp.attr_value.len = rd_len;  // 设置本次响应的实际数据长度
                // 发送读响应给客户端
                esp_ble_gatts_send_response(gatts_if, param->read.conn_id, param->read.trans_id, ESP_GATT_OK, &rsp);
            }
            break;
        }

        // ========== GATT 写事件 ==========
        case ESP_GATTS_WRITE_EVT: {
            // 触发时机：客户端发送 Write Request 或 Write Command 时触发
            // 包括普通写入、CCCD 写入、Prepare Write 等

            if (!param->write.is_prep) {   // 非 Prepare Write（即普通的一次性写入）

                // ---- 判断写入目标：CCCD 描述符 ----
                if (gl_profile_tab[PROFILE_A_APP_ID].descr_handle == param->write.handle && param->write.len == 2) {
                    // 客户端写入的是 CCCD（Client Characteristic Configuration Descriptor）
                    // 且写入长度为 2 字节（CCCD 固定为 2 字节）

                    // 解析 CCCD 值（蓝牙使用小端字节序，需要手动组装）
                    s_desc_value = param->write.value[1] << 8 | param->write.value[0];
                                                                       // 高字节左移8位 + 低字节 = 16位 CCCD 值
                                                                       // value[0] 是低字节（LSB），value[1] 是高字节（MSB）

                    // ---- 处理通知使能（CCCD = 0x0001）----
                    if (s_desc_value == 0x0001) {
                        // CCCD 值为 0x0001：客户端开启了 Notification（通知）功能
                        if (s_config.profile_a.char_properties & ESP_GATT_CHAR_PROP_BIT_NOTIFY) {
                            // 先检查该特征是否支持 Notify 属性（位运算检查）
                            // 构造通知数据（示例数据：0x00~0x0E 循环）
                            uint8_t notify_data[15];     // 声明 15 字节的示例数据数组
                            for (int i = 0; i < sizeof(notify_data); ++i) {
                                notify_data[i] = i % 0xff;  // 用循环方式填充示例数据
                            }
                            // 发送通知给客户端（Notification，无需客户端确认）
                            esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_profile_tab[PROFILE_A_APP_ID].char_handle,
                                                        sizeof(notify_data), notify_data, false);
                                                                                        // 参数：接口句柄、连接ID、特征句柄、数据长度、数据指针、need_confirm=false
                                                                                        // false = Notification（通知，单向发送）
                        }
                    }
                    // ---- 处理指示使能（CCCD = 0x0002）----
                    else if (s_desc_value == 0x0002) {
                        // CCCD 值为 0x0002：客户端开启了 Indication（指示）功能
                        if (s_config.profile_a.char_properties & ESP_GATT_CHAR_PROP_BIT_INDICATE) {
                            // 检查该特征是否支持 Indicate 属性
                            // 构造指示数据（示例数据：0x00~0x0E 循环）
                            uint8_t indicate_data[15];
                            for (int i = 0; i < sizeof(indicate_data); ++i) {
                                indicate_data[i] = i % 0xff;
                            }
                            // 发送指示给客户端（Indication，需要客户端确认）
                            esp_ble_gatts_send_indicate(gatts_if, param->write.conn_id, gl_profile_tab[PROFILE_A_APP_ID].char_handle,
                                                        sizeof(indicate_data), indicate_data, true);
                                                                                         // true = Indication（指示，需要客户端 ACK 确认）
                        }
                    }
                }
                // ---- 判断写入目标：特征值 ----
                else if (gl_profile_tab[PROFILE_A_APP_ID].char_handle == param->write.handle) {
                    // 客户端写入的是 Profile A 的特征值（非 CCCD）
                    if (param->write.len <= sizeof(s_char_value_a)) {
                        // 安全检查：写入长度不能超过特征值数组的最大容量（64字节）
                        memcpy(s_char_value_a, param->write.value, param->write.len);
                                                                      // 将客户端发送的数据拷贝到特征值存储数组
                                                                      // 后续客户端读取时会从这里取出数据
                    }
                    // 如果注册了外部写入回调函数，调用它通知上层应用
                    if (s_write_cb) {
                        s_write_cb(param->write.value, param->write.len);
                                                                   // 调用外部注册的回调，传递接收到的数据和长度
                                                                   // 上层应用可以在回调中进行业务逻辑处理
                    }
                }
            }
            // ---- 调用通用写事件处理（处理 Prepare Write 和发送写响应）----
            example_write_event_env(gatts_if, &a_prepare_write_env, param);
                                                                     // 传入 Profile A 的准备写入环境和事件参数
                                                                     // 该函数内部会处理 Prepare Write 缓存和普通写响应
            break;
        }

        // ========== GATT 执行写事件 ==========
        case ESP_GATTS_EXEC_WRITE_EVT:
            // 触发时机：客户端发送 Execute Write Request（确认或取消 Prepare Write）时触发
            // 发送执行写响应给客户端
            esp_ble_gatts_send_response(gatts_if, param->write.conn_id, param->write.trans_id, ESP_GATT_OK, NULL);
                                                                                // 参数：接口句柄、连接ID、事务ID、成功状态、无额外数据
            // 调用通用执行写处理（释放 Prepare Write 缓冲区）
            example_exec_write_event_env(&a_prepare_write_env, param);
                                                                           // 传入 Profile A 的准备写入环境
                                                                           // 内部会释放缓冲区内存并重置状态
            break;

        // ========== MTU 协商完成事件 ==========
        case ESP_GATTS_MTU_EVT:
            // 触发时机：客户端发起 MTU Exchange Request 并完成协商后触发
            // MTU（Maximum Transmission Unit）决定了单次 ATT PDU 能传输的最大数据量
            // 默认 MTU 为 23 字节，协商后可达 512 字节（BLE 4.2+）
            s_local_mtu = param->mtu.mtu;  // 更新本地 MTU 值为协商后的最终值
                                            // 后续读写操作会使用这个值来计算单次可传输的最大数据量
            break;

        // ========== 服务创建完成事件 ==========
        case ESP_GATTS_CREATE_EVT:
            // 触发时机：调用 esp_ble_gatts_create_service() 成功后触发
            // 此时服务已在协议栈内部创建完成，获得了有效的服务句柄

            // 保存服务句柄（后续添加特征、启动服务等都需要用到）
            gl_profile_tab[PROFILE_A_APP_ID].service_handle = param->create.service_handle;
                                                                                  // 从事件参数中提取服务句柄并保存

            // 配置特征 UUID
            gl_profile_tab[PROFILE_A_APP_ID].char_uuid.len = ESP_UUID_LEN_16;  // UUID 类型：16 位短 UUID
            gl_profile_tab[PROFILE_A_APP_ID].char_uuid.uuid.uuid16 = s_config.profile_a.char_uuid;
                                                                                  // 特征 UUID 值（从配置结构体获取）

            // 启动服务 A（服务创建后必须启动才能被客户端发现和使用）
            esp_ble_gatts_start_service(gl_profile_tab[PROFILE_A_APP_ID].service_handle);
                                                                                 // 传入服务句柄来启动该服务
                                                                                 // 启动后服务进入"活跃"状态，可被 Discover

            // 向服务 A 添加特征（Characteristics）
            esp_ble_gatts_add_char(gl_profile_tab[PROFILE_A_APP_ID].service_handle,  // 所属服务句柄
                                   &gl_profile_tab[PROFILE_A_APP_ID].char_uuid,      // 特征 UUID
                                   s_config.profile_a.char_permissions,              // 特征权限（可读/可写/需要加密等）
                                   s_config.profile_a.char_properties,               // 特征属性（支持读/写/通知等）
                                   &gatts_demo_char1_val,                           // 特征初始值（传入预定义的特征值结构体）
                                   NULL);                                           // 特征描述字符串（NULL = 无描述）
            break;

        // ========== 添加特征完成事件 ==========
        case ESP_GATTS_ADD_CHAR_EVT: {
            // 触发时机：调用 esp_ble_gatts_add_char() 成功后触发
            // 特征已添加到服务中，获得特征句柄

            // 保存特征句柄（后续读写特征值、添加描述符等都需要用到）
            gl_profile_tab[PROFILE_A_APP_ID].char_handle = param->add_char.attr_handle;
                                                                               // 从事件参数中提取特征句柄并保存

            // 配置 CCCD 描述符 UUID（Client Characteristic Configuration Descriptor）
            gl_profile_tab[PROFILE_A_APP_ID].descr_uuid.len = ESP_UUID_LEN_16;   // UUID 类型：16 位
            gl_profile_tab[PROFILE_A_APP_ID].descr_uuid.uuid.uuid16 = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
                                                                                  // 使用标准 CCCD UUID（0x2902）
                                                                                  // CCCD 用于控制通知(Notification)和指示(Indication)的开关

            // 向特征 A 添加 CCCD 描述符
            esp_ble_gatts_add_char_descr(gl_profile_tab[PROFILE_A_APP_ID].service_handle,  // 所属服务句柄
                                         &gl_profile_tab[PROFILE_A_APP_ID].descr_uuid,     // 描述符 UUID
                                         ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,         // 描述符权限：可读 + 可写
                                         NULL,                                             // 描述符初始值（NULL = 默认值 0x0000）
                                         NULL);                                           // 描述符文本描述（NULL = 无描述）
            break;
        }

        // ========== 添加描述符完成事件 ==========
        case ESP_GATTS_ADD_CHAR_DESCR_EVT:
            // 触发时机：调用 esp_ble_gatts_add_char_descr() 成功后触发
            // 保存描述符句柄（后续判断客户端读写目标是否为该描述符时需要用到）
            gl_profile_tab[PROFILE_A_APP_ID].descr_handle = param->add_char_descr.attr_handle;
                                                                                   // 从事件参数中提取描述符句柄并保存
            break;

        // ========== 服务启动完成事件 ==========
        case ESP_GATTS_START_EVT:
            // 触发时机：调用 esp_ble_gatts_start_service() 成功后触发
            // 服务已完全激活，客户端可以通过 Discover Procedures 发现该服务及其特征
            /* 服务启动完成，可在此处添加服务级别的初始化逻辑（如设置初始特征值等） */
            break;

        // ========== 客户端连接事件 ==========
        case ESP_GATTS_CONNECT_EVT: {
            // 触发时机：远端客户端设备成功建立 BLE 连接后触发
            // 连接建立后可以进行数据交换、MTU 协商等操作

            // 构造连接参数更新请求结构体
            esp_ble_conn_update_params_t conn_params = {0};  // 声明并清零连接参数结构体
            memcpy(conn_params.bda, param->connect.remote_bda, sizeof(esp_bd_addr_t));
                                                                     // 拷贝远端设备的 BD 地址（Bluetooth Device Address）
                                                                     // BD 地址是 6 字节的 MAC 地址，用于唯一标识远端设备
            conn_params.latency = 0;                    // 从机延迟（Slave Latency）
                                                         // = 0 表示不允许跳过连接事件（每次都必须响应）
                                                         // 延迟越大越省电但实时性降低
            conn_params.max_int = 0x20;                 // 最大连接间隔（Connection Interval Maximum）
                                                         // 0x20 × 1.25ms = 25ms（单位为 1.25ms）
                                                         // 连接间隔决定了两次数据传输之间的最小时间
            conn_params.min_int = 0x10;                 // 最小连接间隔（Connection Interval Minimum）
                                                         // 0x10 × 1.25ms = 12.5ms
                                                         // 实际连接间隔会在 min 和 max 之间由主控设备选择
            conn_params.timeout = 400;                  // 监督超时（Supervision Timeout）
                                                         // 400 × 10ms = 4000ms = 4 秒
                                                         // 如果超过此时间未收到任何数据包，连接将被视为丢失

            // 保存连接 ID（后续发送通知/指示/响应时需要用到）
            gl_profile_tab[PROFILE_A_APP_ID].conn_id = param->connect.conn_id;

            // 发起连接参数更新请求
            esp_ble_gap_update_conn_params(&conn_params);
                                                       // 请求远端设备使用新的连接参数
                                                       // 远端设备可能会接受或提出替代值

            s_state = BLE_STATE_CONNECTED;  // 更新 BLE 状态为"已连接"
            break;
        }

        // ========== 客户端断开连接事件 ==========
        case ESP_GATTS_DISCONNECT_EVT:
            // 触发时机：远端客户端断开 BLE 连接或连接超时/信号丢失后触发
            // 重启广播（设备再次变得可被发现和连接）
            esp_ble_gap_start_advertising(&adv_params);
                                                 // 重新开始广播，使用相同的广播参数
                                                 // 这样其他设备又可以搜索并连接本设备
            s_local_mtu = 23;                    // 重置 MTU 为默认值（下次连接时会重新协商）
            s_state = BLE_STATE_ADVERTISING;     // 更新 BLE 状态为"广播中"
            break;

        // ========== 通知/指示确认事件 ==========
        case ESP_GATTS_CONF_EVT:
            // 触发时机：客户端确认收到 Indication（指示）后触发
            // 注意：Notification（通知）不会触发此事件（Notification 无需确认）
            /* 通知/指示发送完成并可在此处处理确认逻辑，例如：*/
            /* - 重试失败的指示发送 */
            /* - 更新发送队列状态 */
            /* - 记录传输统计信息 */
            break;

        default:
            break;  // 未处理的事件类型，忽略
    }
}

/* ========================== GATT总事件回调 ========================== */
/**
 * @brief GATT 总事件回调函数（事件分发器）
 * @details 这是注册到 ESP-IDF 协议栈的全局 GATT 回调函数
 *          所有 GATT 事件都会先到达这里，然后被分发（dispatch）到对应的 Profile 处理函数
 *
 * 工作原理：
 * 1. 对于 REG（注册）事件：先将协议栈分配的 gatts_if 句柄保存到 Profile A
 * 2. 对于所有其他事件：遍历 Profile 表，找到匹配的 Profile 并调用其回调函数
 *
 * @param event GATT 事件类型
 * @param gatts_if GATT Server 接口句柄（用于匹配目标 Profile）
 * @param param GATT 事件参数
 */
static void gatts_event_handler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    // ---- 特殊处理：注册事件 ----
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            gl_profile_tab[param->reg.app_id].gatts_if = gatts_if;
        } else {
            return;
        }
    }

    // ---- 事件分发：遍历所有 Profile ----
    for (int idx = 0; idx < PROFILE_NUM; idx++) {
        if (gatts_if == ESP_GATT_IF_NONE || gatts_if == gl_profile_tab[idx].gatts_if) {
            if (gl_profile_tab[idx].gatts_cb) {
                gl_profile_tab[idx].gatts_cb(event, gatts_if, param);
            }
        }
    }
}

/* ========================== 公共函数（API 接口）========================== */

/**
 * @brief BLE 初始化函数（模块入口）
 * @details 完成 BLE 协议栈的完整初始化流程，包括：
 *          1. NVS 初始化
 *          2. 蓝牙控制器初始化
 *          3. Bluedroid 协议栈初始化
 *          4. 回调函数注册（GAP + GATT）
 *          5. Profile 注册
 *          6. MTU 配置
 *
 * @param config BLE 配置结构体指针（包含设备名、UUID、权限等），若传 NULL 则使用默认配置
 * @return ESP_OK 成功；ESP_FAIL 失败
 */
esp_err_t ble_init(const ble_config_t *config)
{
    if (config) {
        memcpy(&s_config, config, sizeof(ble_config_t));
    } else {
        s_config = (ble_config_t)BLE_CONFIG_DEFAULT();
    }
    s_char_value_a[0] = 0x00;

    esp_err_t ret;

    // 初始化 NVS
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 释放经典蓝牙内存
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

    // 初始化蓝牙控制器
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret) return ESP_FAIL;

    // 启用 BLE 模式
    ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (ret) return ESP_FAIL;

    // 初始化 Bluedroid 协议栈
    ret = esp_bluedroid_init();
    if (ret) return ESP_FAIL;
    ret = esp_bluedroid_enable();
    if (ret) return ESP_FAIL;

    // 注册 GATT 和 GAP 回调
    ret = esp_ble_gatts_register_callback(gatts_event_handler);
    if (ret) return ESP_FAIL;
    ret = esp_ble_gap_register_callback(gap_event_handler);
    if (ret) return ESP_FAIL;

    // 注册 Profile A
    ret = esp_ble_gatts_app_register(PROFILE_A_APP_ID);
    if (ret) return ESP_FAIL;

    // 设置本地 MTU
    esp_ble_gatt_set_local_mtu(s_config.local_mtu);

    s_state = BLE_STATE_ADVERTISING;
    return ESP_OK;
}

/**
 * @brief BLE 发送通知函数（公共 API）
 * @details 通过 Profile A 的特征向已连接的客户端发送通知数据
 *          通知（Notification）是单向的，不需要客户端确认
 *          典型用途：传感器数据推送、实时状态更新等
 *
 * @param data 要发送的数据指针
 * @param len 数据长度（字节）
 * @return ESP_OK 成功；ESP_ERR_INVALID_STATE 未连接或其他错误
 */
esp_err_t ble_notify(const uint8_t *data, uint16_t len)
{
    if (s_state != BLE_STATE_CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    // 通过 Profile A 的特征发送通知
    return esp_ble_gatts_send_indicate(gl_profile_tab[PROFILE_A_APP_ID].gatts_if,
                                       gl_profile_tab[PROFILE_A_APP_ID].conn_id,
                                       gl_profile_tab[PROFILE_A_APP_ID].char_handle,
                                       len, (uint8_t *)data, false);
}

/**
 * @brief 获取 BLE 当前状态
 * @return 当前 BLE 状态枚举值
 */
ble_state_t ble_get_state(void)
{
    return s_state;
}

/**
 * @brief 注册数据写入回调函数
 * @details 当客户端向 Profile A 的特征值写入数据时，会调用此回调通知上层应用
 *
 * @param cb 回调函数指针（签名为 void (*)(const uint8_t*, uint16_t)）
 */
void ble_set_write_callback(ble_write_cb_t cb)
{
    s_write_cb = cb;
}

/**
 * @brief 注册数据读取回调函数
 * @details 当客户端读取 Profile A 的特征值时，会调用此回调获取要返回的数据
 *
 * @param cb 回调函数指针（签名为 uint16_t (*)(uint16_t, uint8_t*, uint16_t)）
 */
void ble_set_read_callback(ble_read_cb_t cb)
{
    s_read_cb = cb;
}
