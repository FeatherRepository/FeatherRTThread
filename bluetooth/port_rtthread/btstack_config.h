/* btstack_config.h - P0 minimal config (HCI only; BLE/CLASSIC profiles off) */
#ifndef BTSTACK_CONFIG_H
#define BTSTACK_CONFIG_H

#define HAVE_ASSERT
#define ENABLE_BLE
#define ENABLE_CLASSIC
#define ENABLE_LE_PERIPHERAL
#define ENABLE_LE_ADVERTISING
#define HAVE_LOCAL_NAME_COMPLETE_LOCAL_NAME

/* HCI buffer sizes (required by hci.h) */
#define HCI_ACL_PAYLOAD_SIZE (1691 + 4)
#define HCI_SCO_PACKET_SIZE  64
#define HAVE_EMBEDDED_TIME_MS   /* run loop 定时器支持: 缺了它 process_timers
                                 * 被条件编译掉, 所有 btstack 定时器不触发
                                 * (实测: 保活/Notify 定时器全死, 日志零触发) */
#define MAX_NR_LE_DEVICE_DB_ENTRIES 4
#define MAX_NR_SM_KEYS 4
#define MAX_NR_GATT_CLIENTS 1
#define MAX_NR_HCI_CONNECTIONS 2
#define MAX_NR_L2CAP_SERVICES 8   /* M4a 起: SDP 1 + AVDTP/AVRCP 预留 */
#define MAX_NR_L2CAP_CHANNELS 8
#define MAX_NR_RFCOMM_MULTIPLEXERS 1
#define MAX_NR_RFCOMM_SERVICES 1
#define MAX_NR_RFCOMM_AND_BNEP_CHANNELS 2
#define MAX_NR_BTSTACK_LINK_KEY_DB 0
/* M4a: Classic SSP 配对的 link key 内存库 (A5: flash 持久化是 M3' 打磨项) */
#define MAX_NR_BTSTACK_LINK_KEY_DB_MEMORY_ENTRIES 8
/* A0-4: A2DP/AVDTP/AVRCP 静态池 (源已编入; 运行时在 M4b 才初始化) */
#define MAX_NR_AVDTP_STREAM_ENDPOINTS 2
#define MAX_NR_AVDTP_CONNECTIONS 1
#define MAX_NR_AVRCP_CONNECTIONS 1
/* M4b: SDP 动态服务记录池 (sdp_register_service 走该池; 为 0 时注册全部
 * 静默失败 -> Windows 枚举不到 A2DP Sink, 实测根因) */
#define MAX_NR_SERVICE_RECORD_ITEMS 8

/* logging off for P0 skeleton (avoids hci_dump dependency) */
#define ENABLE_PRINTF_HEXDUMP
#define ENABLE_LOG_INFO
#define ENABLE_LOG_ERROR
#define ENABLE_LOG_DEBUG
/* M4b: ACL 媒体包超过 64B 只打一行摘要, 否则 A2DP 流期间全量 hexdump
 * 会刷爆 115200 控制台并拖慢 btloop (信令包 <64B 仍全量可见) */
#define HCI_DUMP_STDOUT_MAX_SIZE_ACL 64
/* M4b: bt_a2dp sdp 诊断命令的 de_dump_data_element 依赖它 (printf 输出) */
#define ENABLE_SDP_DES_DUMP

#endif
