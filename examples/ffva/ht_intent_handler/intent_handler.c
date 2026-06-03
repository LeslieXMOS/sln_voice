// Copyright 2022-2024 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.

/* STD headers */
#include <platform.h>
#include <xs1.h>
#include <xcore/hwtimer.h>

/* FreeRTOS headers */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* App headers */
#include "app_conf.h"
#include "platform/driver_instances.h"
#include "intent_handler.h"
#include "fs_support.h"
#include "ff.h"
#include "audio_response.h"
#include "intent_engine.h"
#include "ht_protocol_engine.h"

#define WAKEUP_LOW  (appconfINTENT_WAKEUP_EDGE_TYPE)
#define WAKEUP_HIGH (appconfINTENT_WAKEUP_EDGE_TYPE == 0)

#if ON_TILE(ASR_TILE_NO)

static bool audio_response_playing = 0;

#define ASR_NUMBER_OF_COMMANDS  (17)
#define WALLPAD_DST_ID          (0x00)

typedef struct {
    uint8_t cmd;
    uint8_t param;
} ht_vddcp_pair_t;

// Here we fake the vddcp with default command set, it should be replaced with actual word id
static ht_vddcp_pair_t vddcp_lut[ASR_NUMBER_OF_COMMANDS] = {
    {HT_VDDCP_CTRL_MAIN_LIGHT, HT_VDDCP_STATUS_ON}, // Switch on the TV
    {HT_VDDCP_CTRL_MAIN_LIGHT, HT_VDDCP_STATUS_OFF}, // Switch off the TV
    {HT_VDDCP_CTRL_SUB_LIGHT, HT_VDDCP_STATUS_ON}, // Channel up
    {HT_VDDCP_CTRL_SUB_LIGHT, HT_VDDCP_STATUS_OFF}, // Channel down
    {HT_VDDCP_CTRL_CORRIDOR_LIGHT, HT_VDDCP_STATUS_ON}, // Volume up
    {HT_VDDCP_CTRL_CORRIDOR_LIGHT, HT_VDDCP_STATUS_OFF}, // Volume down
    {HT_VDDCP_CTRL_INDIRECT_LIGHT, HT_VDDCP_STATUS_ON}, // Switch on the lights
    {HT_VDDCP_CTRL_INDIRECT_LIGHT, HT_VDDCP_STATUS_OFF}, // Switch off the lights
    {HT_VDDCP_CTRL_WHOLE_LIGHT, HT_VDDCP_STATUS_ON}, // Brightness up
    {HT_VDDCP_CTRL_WHOLE_LIGHT, HT_VDDCP_STATUS_OFF}, // Brightness down
    {HT_VDDCP_CTRL_BATCH_LIGHT, HT_VDDCP_STATUS_ON}, // Switch on the fan
    {HT_VDDCP_CTRL_BATCH_LIGHT, HT_VDDCP_STATUS_OFF}, // Switch off the fan
    {HT_VDDCP_CTRL_COOKTOP, HT_VDDCP_STATUS_ON}, // Speed up the fan
    {HT_VDDCP_CTRL_COOKTOP, HT_VDDCP_STATUS_OFF}, // Slow down the fan
    {HT_VDDCP_CTRL_OUTING_MODE, HT_VDDCP_STATUS_ON}, // Set higher temperature
    {HT_VDDCP_CTRL_OUTING_MODE, HT_VDDCP_STATUS_OFF} // Set lower temperature
};

static void proc_keyword_res(void *args) {
    void** arg_array = (void**) args;
    QueueHandle_t q_intent = (QueueHandle_t) arg_array[0];
    ht_protocol_handle_t* ht_protocol_handler = (ht_protocol_handle_t*) arg_array[1];
    int32_t id = 0;
    int32_t host_status = 0;

    configASSERT(q_intent != 0);
    configASSERT(ht_protocol_handler != 0);

    const rtos_gpio_port_id_t p_out_wakeup = rtos_gpio_port(GPIO_OUT_HOST_WAKEUP_PORT);
    const rtos_gpio_port_id_t p_in_host_status = rtos_gpio_port(GPIO_IN_HOST_STATUS_PORT);

    rtos_gpio_port_enable(gpio_ctx_t0, p_out_wakeup);
    rtos_gpio_port_enable(gpio_ctx_t0, p_in_host_status);

    rtos_gpio_port_out(gpio_ctx_t0, p_out_wakeup, WAKEUP_LOW);

#if appconfAUDIO_PLAYBACK_ENABLED
    audio_response_init();
#endif

    while(1) {
        xQueueReceive(q_intent, &id, portMAX_DELAY);

        host_status = rtos_gpio_port_in(gpio_ctx_t0, p_in_host_status);

        if (host_status == 0) { /* Host is not awake */
            rtos_gpio_port_out(gpio_ctx_t0, p_out_wakeup, WAKEUP_HIGH);
            rtos_printf("Delay for host wake up\n");
            vTaskDelay(pdMS_TO_TICKS(appconfINTENT_TRANSPORT_DELAY_MS));
            rtos_gpio_port_out(gpio_ctx_t0, p_out_wakeup, WAKEUP_LOW);
        }
#if appconfINTENT_I2C_MASTER_OUTPUT_ENABLED
        i2c_res_t ret;
        uint32_t buf = id;
        size_t sent = 0;

        ret = rtos_i2c_master_write(
            i2c_master_ctx,
            appconfINTENT_I2C_MASTER_DEVICE_ADDR,
            (uint8_t*)&buf,
            sizeof(uint32_t),
            &sent,
            1
        );

        if (ret != I2C_ACK) {
            rtos_printf("I2C inference output was not acknowledged\n\tSent %d bytes\n", sent);
        }
#endif
// #if appconfINTENT_UART_OUTPUT_ENABLED && (UART_TILE_NO == ASR_TILE_NO)
        // uint32_t buf_uart = id;
        // rtos_uart_tx_write(uart_tx_ctx, (uint8_t*)&buf_uart, sizeof(uint32_t));
        if (id == 0) {
            // Deactivate
            uint8_t param[1] = {0x00};
            ht_protocol_send_vrcp(ht_protocol_handler, HT_VRCP_CMD_ACTIVATE_SPEECH, param, 1, WALLPAD_DST_ID);
        } else if (id == 1) {
            // Activate
            uint8_t param[1] = {0x01};
            ht_protocol_send_vrcp(ht_protocol_handler, HT_VRCP_CMD_ACTIVATE_SPEECH, param, 1, WALLPAD_DST_ID);
        } else {
            ht_vddcp_pair_t pair = vddcp_lut[id-2];
            ht_protocol_send_vddcp(ht_protocol_handler, pair.cmd, pair.param, WALLPAD_DST_ID);
        }
// #endif
#if appconfAUDIO_PLAYBACK_ENABLED
        audio_response_playing = true;
        audio_response_play(id);
        audio_response_playing = false;
#endif
    }
}

bool intent_handler_response_playing() {
    return audio_response_playing;
}

int32_t intent_handler_create(uint32_t priority, void *args)
{
    xTaskCreate((TaskFunction_t)proc_keyword_res,
                "proc_keyword_res",
                RTOS_THREAD_STACK_SIZE(proc_keyword_res),
                args,
                priority,
                NULL);
    return 0;
}

#endif /* ON_TILE(ASR_TILE_NO) */
