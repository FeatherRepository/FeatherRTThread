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
#define MAX_NR_LE_DEVICE_DB_ENTRIES 4
#define MAX_NR_SM_KEYS 4
#define MAX_NR_GATT_CLIENTS 1
#define MAX_NR_HCI_CONNECTIONS 2
#define MAX_NR_L2CAP_SERVICES 4
#define MAX_NR_L2CAP_CHANNELS 4
#define MAX_NR_RFCOMM_MULTIPLEXERS 1
#define MAX_NR_RFCOMM_SERVICES 1
#define MAX_NR_RFCOMM_AND_BNEP_CHANNELS 2
#define MAX_NR_BTSTACK_LINK_KEY_DB 0

/* logging off for P0 skeleton (avoids hci_dump dependency) */

#endif
