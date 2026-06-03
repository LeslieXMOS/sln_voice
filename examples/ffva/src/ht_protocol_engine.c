// Copyright 2022-2024 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.

/* STD headers */
#include <platform.h>
#include <xs1.h>
#include <xcore/hwtimer.h>

/* FreeRTOS headers */
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"

/* App headers */
#include "app_conf.h"
#include "platform/driver_instances.h"
#include "ht_protocol_engine.h"
#include "device_memory_impl.h"
#include "leds.h"

#if ON_TILE(UART_TILE_NO)

#define HT_XMOS_ID  (0x02)
#define HT_STX      (0x7F)
#define HT_ETX      (0xEE)
// VDDCP Defines
#define HT_VDDCP_TP     (0x05)
#define HT_VDDCP_CMD    (0x85)

// VRCP Defines
#define HT_VRCP_TP                      (0x04)
#define HT_VRCP_CMD_RESPONSE            (0x00) 

typedef struct {
    uint8_t type;
    uint8_t sid;
} ht_protocol_ack_t;

/** CRC8 lookup table */
static const uint8_t CRC8_TABLE[256] = {
    0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15,
    0x38, 0x3f, 0x36, 0x31, 0x24, 0x23, 0x2a, 0x2d,
    0x70, 0x77, 0x7e, 0x79, 0x6c, 0x6b, 0x62, 0x65,
    0x48, 0x4f, 0x46, 0x41, 0x54, 0x53, 0x5a, 0x5d,
    0xe0, 0xe7, 0xee, 0xe9, 0xfc, 0xfb, 0xf2, 0xf5,
    0xd8, 0xdf, 0xd6, 0xd1, 0xc4, 0xc3, 0xca, 0xcd,
    0x90, 0x97, 0x9e, 0x99, 0x8c, 0x8b, 0x82, 0x85,
    0xa8, 0xaf, 0xa6, 0xa1, 0xb4, 0xb3, 0xba, 0xbd,
    0xc7, 0xc0, 0xc9, 0xce, 0xdb, 0xdc, 0xd5, 0xd2,
    0xff, 0xf8, 0xf1, 0xf6, 0xe3, 0xe4, 0xed, 0xea,
    0xb7, 0xb0, 0xb9, 0xbe, 0xab, 0xac, 0xa5, 0xa2,
    0x8f, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9d, 0x9a,
    0x27, 0x20, 0x29, 0x2e, 0x3b, 0x3c, 0x35, 0x32,
    0x1f, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0d, 0x0a,
    0x57, 0x50, 0x59, 0x5e, 0x4b, 0x4c, 0x45, 0x42,
    0x6f, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7d, 0x7a,
    0x89, 0x8e, 0x87, 0x80, 0x95, 0x92, 0x9b, 0x9c,
    0xb1, 0xb6, 0xbf, 0xb8, 0xad, 0xaa, 0xa3, 0xa4,
    0xf9, 0xfe, 0xf7, 0xf0, 0xe5, 0xe2, 0xeb, 0xec,
    0xc1, 0xc6, 0xcf, 0xc8, 0xdd, 0xda, 0xd3, 0xd4,
    0x69, 0x6e, 0x67, 0x60, 0x75, 0x72, 0x7b, 0x7c,
    0x51, 0x56, 0x5f, 0x58, 0x4d, 0x4a, 0x43, 0x44,
    0x19, 0x1e, 0x17, 0x10, 0x05, 0x02, 0x0b, 0x0c,
    0x21, 0x26, 0x2f, 0x28, 0x3d, 0x3a, 0x33, 0x34,
    0x4e, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5c, 0x5b,
    0x76, 0x71, 0x78, 0x7f, 0x6a, 0x6d, 0x64, 0x63,
    0x3e, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2c, 0x2b,
    0x06, 0x01, 0x08, 0x0f, 0x1a, 0x1d, 0x14, 0x13,
    0xae, 0xa9, 0xa0, 0xa7, 0xb2, 0xb5, 0xbc, 0xbb,
    0x96, 0x91, 0x98, 0x9f, 0x8a, 0x8d, 0x84, 0x83,
    0xde, 0xd9, 0xd0, 0xd7, 0xc2, 0xc5, 0xcc, 0xcb,
    0xe6, 0xe1, 0xe8, 0xef, 0xfa, 0xfd, 0xf4, 0xf3
};

static uint8_t crc8_table(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc = CRC8_TABLE[crc ^ data[i]];
    }
    return crc;
}

ht_protocol_msg_t* ht_protocol_msg_validate(uint8_t* buf, size_t len) {
    // Only parse the message when ETX is correct, otherwise just drop the message
    if (buf[len - 1] != HT_ETX) {
        return NULL;
    }
    // Check the message crc
    uint8_t message_crc = crc8_table(buf, len - 2);  // exclude CRC and ETX
    if (message_crc != buf[len - 2]) {
        return NULL;
    }
    // CRC correct, parse the message
    ht_protocol_msg_t* msg = (ht_protocol_msg_t*)buf;
    // Only support src and dst addr len == 1
    if (msg->opt != 0) {
        return NULL;
    }
    // Check if we are the receiver
    if (msg->dst_id != HT_XMOS_ID) {
        return NULL;
    }
    // Update the len to args len
    msg->len = len - 10;   // exclude STX, LEN, TYPE, OPT, SRC, DST, CMD, SID, CRC and ETX
    return msg;
}

ht_protocol_msg_t* ht_protocol_vrcp_process(ht_protocol_handle_t* handler, ht_protocol_msg_t* msg) {
    // Validate if the command is correct
    switch(msg->cmd) {
        case HT_VRCP_CMD_ALIVE:
        case HT_VRCP_CMD_READY_WALLPAD:
        case HT_VRCP_CMD_CHANGE_STATUS_SCREEN:
        case HT_VRCP_CMD_CHANGE_STATUS_TELL:
        case HT_VRCP_CMD_CHANGE_STATUS_SECURITY_ALERT:
        case HT_VRCP_CMD_CHANGE_STATUS_AUDIO:
        case HT_VRCP_CMD_CHANGE_STATUS_AUDIO_APP:
        case HT_VRCP_CMD_CHANGE_STATUS_SET:
        case HT_VRCP_CMD_CHANGE_ADDR:
        case HT_VRCP_CMD_EXCHANGE_KEY_PCB_TYPE:
            if (handler->vrcp_cb) {
                handler->vrcp_cb(handler->app_data, msg->src_id, msg->cmd, msg->args, msg->len);
            }
            break;
        case 0x00: {
            ht_protocol_ack_t ack = {
                .type = msg->type,
                .sid = msg->sid
            };
            xQueueSend(handler->ack_queue, &ack, portMAX_DELAY);
            return NULL;
        }
        default:
            return NULL;
    }
    // We will resuse the buffer for ack
    msg->dst_id = msg->src_id;
    msg->src_id = HT_XMOS_ID;
    msg->args[0] = msg->cmd;        // Return CMD
    msg->cmd = 0x00;                // ACK cmd
    msg->len = 1;                   // Only return CMD
    return msg;
}

ht_protocol_msg_t* ht_protocol_vddcp_process(ht_protocol_handle_t* handler, ht_protocol_msg_t* msg) {
    // Check if this is an ack
    if (msg->cmd == 0) {
        ht_protocol_ack_t ack = {
            .type = msg->type,
            .sid = msg->sid
        };
        xQueueSend(handler->ack_queue, &ack, portMAX_DELAY);
        return NULL;
    }
    // Validate if the command is correct
    if (msg->cmd != HT_VDDCP_CMD) {
        return NULL;
    }
    // Check if there is only 2 args
    if (msg->len != 2) {
        return NULL;
    }
    // Nofiy the app with this
    if (handler->vddcp_cb) {
        handler->vddcp_cb(handler->app_data, msg->src_id, msg->args[0], msg->args[1]);
    }
    // We will resuse the buffer for ack
    msg->dst_id = msg->src_id;
    msg->src_id = HT_XMOS_ID;
    msg->cmd = 0x00;                // ACK cmd
    msg->args[0] = HT_VDDCP_CMD;    // Return CMD
    msg->len = 1;                   // Only return CMD
    return msg;
}

ht_protocol_msg_t* ht_protocol_msg_process(ht_protocol_handle_t* handler, ht_protocol_msg_t* msg) {
    switch (msg->type) {
        case HT_VDDCP_TP:
            return ht_protocol_vddcp_process(handler, msg);
        case HT_VRCP_TP:
            return ht_protocol_vrcp_process(handler, msg);
        default:
            // Unsupported protocol type, just ignore
            return NULL;
    }
    return NULL;
}

uint8_t ht_protocol_msg_build(ht_protocol_msg_t* msg) {
    uint8_t* buf = (uint8_t*)msg;
    uint8_t len = msg->len + 10;
    // Update message len
    msg->len = len - 2;   // exclude STX and LEN
    // calculate the CRC
    uint8_t crc = crc8_table(buf, len - 2);   // exclude CRC and ETX
    buf[len - 2] = crc;
    buf[len - 1] = HT_ETX;
    return len;
}

RTOS_UART_RX_CALLBACK_ATTR
void rtos_uart_rx_error(rtos_uart_rx_t *ctx, uint8_t err_flags) {
    rtos_printf("UART rx error: %02X\n", err_flags);
}

RTOS_UART_RX_CALLBACK_ATTR
void rtos_uart_rx_complete(rtos_uart_rx_t *ctx) {
    ht_protocol_handle_t* handler = (ht_protocol_handle_t*)(ctx->app_data);
    size_t num_rx;
    while ((num_rx = xStreamBufferBytesAvailable(ctx->app_byte_buffer)) != 0) {
        if (handler->recv_byte == 0) {
            uint8_t stx;
            xStreamBufferReceive(
                ctx->app_byte_buffer,
                &stx, 1,
                portMAX_DELAY);
            if (stx == HT_STX) {
                handler->recv_byte = 1;
            }
        } else if (handler->recv_byte == 1) {
            uint8_t len;
            xStreamBufferReceive(
                ctx->app_byte_buffer,
                &len, 1,
                portMAX_DELAY);
            handler->recv_byte = len + 2;
        } else if (num_rx + 2 >= handler->recv_byte) {
            uint8_t* buf_ptr;
            xQueueReceive(handler->buf_ptr_queue, &buf_ptr, portMAX_DELAY);
            buf_ptr[0] = HT_STX;
            buf_ptr[1] = handler->recv_byte - 2;
            xStreamBufferReceive(
                ctx->app_byte_buffer,
                &buf_ptr[2], buf_ptr[1],
                portMAX_DELAY);
            // Got enough data, start parse the data
            ht_protocol_msg_t* msg = ht_protocol_msg_validate(buf_ptr, handler->recv_byte);
            if (msg) {
                msg = ht_protocol_msg_process(handler, msg);
            }
            if (msg) {
                xQueueSend(handler->send_msg_queue, &msg, portMAX_DELAY);
            }
            if (msg == NULL) {
                // Invalid message, just return the buffer and wait for next message
                xQueueSend(handler->buf_ptr_queue, &buf_ptr, portMAX_DELAY);
            }
            // Parse completed, reset the handler for next message
            handler->recv_byte = 0;
        } else {
            break;
        }
    }
}

bool ht_protocol_send_vddcp(ht_protocol_handle_t* handler, uint8_t cmd, uint8_t param, uint8_t dst)
{
    // Check if we can get a buffer ptr to send
    uint8_t* buf_ptr;
    if (xQueueReceive(handler->buf_ptr_queue, &buf_ptr, pdMS_TO_TICKS(10)) != pdTRUE) {
        return false;
    }
    // Build the message
    ht_protocol_msg_t* msg = (ht_protocol_msg_t*)buf_ptr;
    msg->stx = HT_STX;
    msg->len = 2;
    msg->type = HT_VDDCP_TP;
    msg->opt = 0;
    msg->src_id = HT_XMOS_ID;
    msg->dst_id = dst;
    msg->cmd = HT_VDDCP_CMD;
    msg->sid = handler->vddcp_sid++;
    msg->args[0] = cmd;
    msg->args[1] = param;
    // Send the message
    xQueueSend(handler->send_msg_queue, &msg, portMAX_DELAY);
    return true;
}

bool ht_protocol_send_vrcp(ht_protocol_handle_t* handler, uint8_t cmd, uint8_t* params, uint8_t len, uint8_t dst){
    // Check if we can get a buffer ptr to send
    uint8_t* buf_ptr;
    if (xQueueReceive(handler->buf_ptr_queue, &buf_ptr, pdMS_TO_TICKS(10)) != pdTRUE) {
        return false;
    }
    // Build the message
    ht_protocol_msg_t* msg = (ht_protocol_msg_t*)buf_ptr;
    msg->stx = HT_STX;
    msg->len = len;
    msg->type = HT_VRCP_TP;
    msg->opt = 0;
    msg->src_id = HT_XMOS_ID;
    msg->dst_id = dst;
    msg->cmd = cmd;
    msg->sid = handler->vrcp_sid++;
    for (int i = 0; i < len; i++) {
        msg->args[i] = params[i];
    }
    // Send the message
    xQueueSend(handler->send_msg_queue, &msg, portMAX_DELAY);
    return true;
}

void ht_protocol_engine_task(void *args) {
    ht_protocol_handle_t* handler = (ht_protocol_handle_t*)args;

    // push buffer pointer to queue first
    for (int i = 0; i < HT_PROTOCOL_BUF_SIZE; i++) {
        uint8_t* buf_ptr = handler->raw_buf[i];
        uint32_t buf_ptr_addr = (uint32_t)buf_ptr;
        xQueueSend(handler->buf_ptr_queue, &buf_ptr_addr, portMAX_DELAY);
    }

    // Here we call rtos_uart_rx_start as the protocol engine will take the whole uart
    rtos_uart_rx_start(
        uart_rx_ctx,
        args,
        NULL,
        rtos_uart_rx_complete,
        rtos_uart_rx_error,
        appconfUART_RX_INTERRUPT_CORE,
        appconfSTARTUP_TASK_PRIORITY,
        256 // Big enough to hold 2x max len message
        );

    for (;;) {
        ht_protocol_msg_t* msg;
        xQueueReceive(handler->send_msg_queue, &msg, portMAX_DELAY);
        uint8_t len = ht_protocol_msg_build(msg);
        rtos_uart_tx_write(uart_tx_ctx, (uint8_t*)msg, len);
        if (msg->cmd != 0) {
            // Need to wait for ack if this is not an ack message
            TickType_t start_tick, end_tick, elapsed_tick;
            ht_protocol_ack_t ack;
            int retry_count = 0;
            start_tick = xTaskGetTickCount();
            elapsed_tick = 0;
            while (retry_count < 2) {
                if (xQueueReceive(handler->ack_queue, &ack, pdMS_TO_TICKS(1000)-elapsed_tick) == pdTRUE) {
                    if (ack.type == msg->type && ack.sid == msg->sid) {
                        // Got the ack, break the loop
                        break;
                    }
                }
                end_tick = xTaskGetTickCount();
                elapsed_tick = end_tick - start_tick;
                if (elapsed_tick >= pdMS_TO_TICKS(1000)) {
                    // Timeout, need to retry
                    rtos_uart_tx_write(uart_tx_ctx, (uint8_t*)msg, len);
                    start_tick = xTaskGetTickCount();
                    elapsed_tick = 0;
                    retry_count++;
                }
            }
        }
        uint8_t* buf_ptr = (uint8_t*)msg;
        xQueueSend(handler->buf_ptr_queue, &buf_ptr, portMAX_DELAY);
    }
}

void ht_protocol_engine_task_create(
    unsigned priority,
    ht_protocol_handle_t* handler
) {
    handler->buf_ptr_queue = xQueueCreate(HT_PROTOCOL_BUF_SIZE+1, sizeof(uint8_t*));
    handler->send_msg_queue = xQueueCreate(HT_PROTOCOL_BUF_SIZE+1, sizeof(ht_protocol_msg_t*));
    handler->ack_queue = xQueueCreate(HT_PROTOCOL_BUF_SIZE+1, sizeof(ht_protocol_ack_t));
    
    xTaskCreate((TaskFunction_t) ht_protocol_engine_task,
                "ht_protocol_engine_task",
                RTOS_THREAD_STACK_SIZE(ht_protocol_engine_task),
                handler,
                priority,
                NULL);
}

#endif /* ON_TILE(ASR_TILE_NO) */