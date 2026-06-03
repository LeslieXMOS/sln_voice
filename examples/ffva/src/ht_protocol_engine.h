// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.

#ifndef HT_PROTOCOL_ENGINE_H_
#define HT_PROTOCOL_ENGINE_H_

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "FreeRTOS.h"
#include "queue.h"

// VDDCP control codes
#define HT_VDDCP_CTRL_MAIN_LIGHT        (0x01)
#define HT_VDDCP_CTRL_SUB_LIGHT         (0x02)
#define HT_VDDCP_CTRL_CORRIDOR_LIGHT    (0x03)
#define HT_VDDCP_CTRL_INDIRECT_LIGHT    (0x04)
#define HT_VDDCP_CTRL_WHOLE_LIGHT       (0x05)
#define HT_VDDCP_CTRL_BATCH_LIGHT       (0x06)
#define HT_VDDCP_CTRL_COOKTOP           (0x07)
#define HT_VDDCP_CTRL_CALL_ELEVATOR     (0x08)
#define HT_VDDCP_CTRL_OUTING_MODE       (0x09)
#define HT_VDDCP_CTRL_START_APP         (0x0A)
// VDDCP status values
#define HT_VDDCP_STATUS_OFF             (0x00)
#define HT_VDDCP_STATUS_ON              (0x01)
// Wallpad -> Voice module
#define HT_VRCP_CMD_ALIVE                        (0x01)
#define HT_VRCP_CMD_READY_WALLPAD                (0x11)
#define HT_VRCP_CMD_CHANGE_STATUS_SCREEN         (0x12)
#define HT_VRCP_CMD_CHANGE_STATUS_TELL           (0x21)
#define HT_VRCP_CMD_CHANGE_STATUS_SECURITY_ALERT (0x22)
#define HT_VRCP_CMD_CHANGE_STATUS_AUDIO          (0x23)
#define HT_VRCP_CMD_CHANGE_STATUS_AUDIO_APP      (0x24)
#define HT_VRCP_CMD_CHANGE_STATUS_SET            (0x25)
#define HT_VRCP_CMD_CHANGE_ADDR                  (0x26)
#define HT_VRCP_CMD_EXCHANGE_KEY_PCB_TYPE        (0x29)
// Voice module -> Wallpad
#define HT_VRCP_CMD_READY_SPEECH                 (0x80)
#define HT_VRCP_CMD_CHANGE_STATUS_SPEECH         (0x81)
#define HT_VRCP_CMD_GET_STATUS_WALLPAD           (0x82)
#define HT_VRCP_CMD_KEY_EVENT                    (0x83)
#define HT_VRCP_CMD_GET_ADDR                     (0x84)
#define HT_VRCP_CMD_ACTIVATE_SPEECH              (0x86)

#define HT_PROTOCOL_BUF_SIZE    (5)

#define HT_VDDCP_CALLBACK_ATTR __attribute__((fptrgroup("ht_vddcp_callback_fptr_grp")))
#define HT_VRCP_CALLBACK_ATTR __attribute__((fptrgroup("ht_vrcp_callback_fptr_grp")))

typedef void (*ht_protocol_vddcp_cb_t)(void* app_data, uint8_t src, uint8_t cmd, uint8_t param);
typedef void (*ht_protocol_vrcp_cb_t)(void* app_data, uint8_t src, uint8_t cmd, uint8_t* params, uint8_t len);

typedef struct {
    int recv_byte;      // how many bytes remain unreceived, 0 means STX not received, 1 means LEN not received
    uint8_t raw_buf[HT_PROTOCOL_BUF_SIZE][138];
    QueueHandle_t buf_ptr_queue;
    QueueHandle_t send_msg_queue;
    QueueHandle_t ack_queue;
    uint8_t vddcp_sid;
    uint8_t vrcp_sid;
    HT_VDDCP_CALLBACK_ATTR ht_protocol_vddcp_cb_t vddcp_cb;
    HT_VRCP_CALLBACK_ATTR ht_protocol_vrcp_cb_t vrcp_cb;
    void* app_data;
} ht_protocol_handle_t;

// This structure is specifically design for VRCP/VDDCP, other cmd may not fit
typedef struct __attribute__((packed)) {
    uint8_t stx;
    uint8_t len;
    uint8_t type;   // TYPE/PROTO in document
    uint8_t opt;
    uint8_t src_id;
    uint8_t dst_id;
    uint8_t cmd;
    uint8_t sid;
    uint8_t args[128];
} ht_protocol_msg_t;

bool ht_protocol_send_vddcp(ht_protocol_handle_t* handler, uint8_t cmd, uint8_t param, uint8_t dst);
bool ht_protocol_send_vrcp(ht_protocol_handle_t* handler, uint8_t cmd, uint8_t* params, uint8_t len, uint8_t dst);

// ht_protocol_data_t ht_protocol_create_vddcp_request(uint8_t dst, uint8_t cmd, uint8_t param);
void ht_protocol_engine_task(void *args);
void ht_protocol_engine_task_create(
    unsigned priority,
    ht_protocol_handle_t* handler);

#endif /* HT_PROTOCOL_ENGINE_H_ */