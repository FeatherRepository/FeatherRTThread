/******************************************************************************
 * Copyright 2020-2026 The RT-Thread Development Team. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#include "drv_spi.h"

#ifdef BSP_USING_SPI

#include <rtdevice.h>
#include <drivers/pin.h>
#include "cy_scb_spi.h"
#include "cy_sysint.h"
#include "mtb_hal_spi.h"

#define GET_PIN(PORTx, PIN) ((((uint8_t)(PORTx)) << 3U) + ((uint8_t)(PIN)))
#define INT_PRIORITY        7u
/*
 * The audio bridge sends 768-byte bursts through SPI9.  The generic MTB HAL
 * transfer is asynchronous, but its blocking wrapper waits forever for an
 * IRQ-driven completion notification.  A lost/masked SCB completion IRQ then
 * permanently stalls the USB-audio output worker.  Keep this driver strictly
 * synchronous and bounded instead: feed TX and drain RX FIFOs directly.
 */
#define IFX_SPI_POLL_TIMEOUT_MS 20u
/* A second guard is independent of the RTOS tick, which may itself be
 * starved by a malformed SCB transfer. */
#define IFX_SPI_POLL_IDLE_LIMIT 2000000u

struct ifx_spi {
    struct rt_spi_bus spi_bus;
    const char* name;

    CySCB_Type* base;
    cy_stc_scb_spi_config_t runtime_cfg;
    const cy_stc_scb_spi_config_t* default_cfg;
    const mtb_hal_spi_configurator_t* hal_cfg;
    cy_stc_scb_spi_context_t* context;
    mtb_hal_spi_t spi_obj;

    uint16_t cs_pin;

    cy_stc_sysint_t intr_cfg;

    uint32_t freq;
    struct rt_completion cpt;
};

#ifdef BSP_USING_SPI1
static cy_stc_scb_spi_context_t scb_9_spi_context;

extern const cy_stc_scb_spi_config_t CYBSP_SPI_9_config;
extern const mtb_hal_spi_configurator_t CYBSP_SPI_9_hal_config;

#define scb_9_HW  SCB9
#define scb_9_IRQ scb_9_interrupt_IRQn

#ifndef BSP_SPI1_CS_PIN
#define BSP_SPI1_CS_PIN    GET_PIN(15, 3)
#endif
#endif

static struct ifx_spi ifx_spi_obj[] = {
#ifdef BSP_USING_SPI1
    {
        .name = "spi1",
        .base = scb_9_HW,
        .default_cfg = &CYBSP_SPI_9_config,
        .hal_cfg = &CYBSP_SPI_9_hal_config,
        .context = &scb_9_spi_context,
        .cs_pin = BSP_SPI1_CS_PIN,
        .intr_cfg = { .intrSrc = scb_9_IRQ, .intrPriority = INT_PRIORITY },
    },
#endif
};

static void ifx_spi_set_cs(struct ifx_spi* spi, rt_base_t level)
{
    RT_ASSERT(spi);
    rt_pin_write(spi->cs_pin, level);
}

static void ifx_spi_irq_handler_generic(struct ifx_spi* spi)
{
    rt_interrupt_enter();

    mtb_hal_spi_process_interrupt(&spi->spi_obj);

    if (!mtb_hal_spi_is_busy(&spi->spi_obj)) {
        rt_completion_done(&spi->cpt);
    }

    rt_interrupt_leave();
}

#ifdef BSP_USING_SPI1
static void ifx_spi1_irq_handler(void)
{
    ifx_spi_irq_handler_generic(&ifx_spi_obj[0]);
}
#endif

static rt_err_t ifx_hw_spi_init(struct ifx_spi* spi)
{
    RT_ASSERT(spi);

    /* Configure CS pin as output, set high (inactive) */
    rt_pin_mode(spi->cs_pin, PIN_MODE_OUTPUT);
    ifx_spi_set_cs(spi, PIN_HIGH);

    spi->runtime_cfg = *spi->default_cfg;
    if (Cy_SCB_SPI_Init(spi->base, &spi->runtime_cfg, spi->context) != CY_SCB_SPI_SUCCESS)
        return -RT_ERROR;
    Cy_SCB_SPI_Enable(spi->base);

    if (mtb_hal_spi_setup(&spi->spi_obj, spi->hal_cfg, spi->context, NULL) != CY_RSLT_SUCCESS)
        return -RT_ERROR;

    cy_israddress isr_func = RT_NULL;
    if (rt_strcmp(spi->name, "spi1") == 0)
        isr_func = ifx_spi1_irq_handler;

    if (isr_func) {
        Cy_SysInt_Init(&spi->intr_cfg, isr_func);
        NVIC_EnableIRQ(spi->intr_cfg.intrSrc);
    }

    rt_completion_init(&spi->cpt);
    spi->freq = 1000000;
    if (mtb_hal_spi_set_frequency(&spi->spi_obj, spi->freq) != CY_RSLT_SUCCESS)
        return -RT_ERROR;

    return RT_EOK;
}

static rt_err_t spi_configure(struct rt_spi_device* device, struct rt_spi_configuration* cfg)
{
    RT_ASSERT(device);
    RT_ASSERT(cfg);

    struct ifx_spi* spi = rt_container_of(device->bus, struct ifx_spi, spi_bus);

    if (cfg->data_width == 0 || cfg->data_width > 16)
        return -RT_EINVAL;

    spi->spi_obj.data_bits = (cfg->data_width <= 8) ? 8 : 16;
    spi->freq = cfg->max_hz;
    if (mtb_hal_spi_set_frequency(&spi->spi_obj, spi->freq) != CY_RSLT_SUCCESS)
        return -RT_ERROR;

    spi->runtime_cfg.enableMsbFirst = ((cfg->mode & RT_SPI_MSB) == RT_SPI_MSB) ? true : false;

    switch (cfg->mode & RT_SPI_MODE_3) {
    case RT_SPI_MODE_0:
        spi->runtime_cfg.sclkMode = CY_SCB_SPI_CPHA0_CPOL0;
        break;
    case RT_SPI_MODE_1:
        spi->runtime_cfg.sclkMode = CY_SCB_SPI_CPHA1_CPOL0;
        break;
    case RT_SPI_MODE_2:
        spi->runtime_cfg.sclkMode = CY_SCB_SPI_CPHA0_CPOL1;
        break;
    case RT_SPI_MODE_3:
        spi->runtime_cfg.sclkMode = CY_SCB_SPI_CPHA1_CPOL1;
        break;
    }

    Cy_SCB_SPI_Disable(spi->base, spi->context);
    if (Cy_SCB_SPI_Init(spi->base, &spi->runtime_cfg, spi->context) != CY_SCB_SPI_SUCCESS) {
        rt_kprintf("Cy_SCB_SPI_Init fail!!\n");
        return -RT_ERROR;
    }

    Cy_SCB_SPI_Enable(spi->base);

    return RT_EOK;
}

static rt_bool_t ifx_spi_poll_xfer(struct ifx_spi* spi, const struct rt_spi_message* message)
{
    const rt_uint8_t* tx = (const rt_uint8_t*)message->send_buf;
    rt_uint8_t* rx = (rt_uint8_t*)message->recv_buf;
    const rt_uint32_t element_bytes = (spi->spi_obj.data_bits > 8u) ? 2u : 1u;
    const rt_uint32_t element_count = message->length / element_bytes;
    rt_uint32_t written = 0u;
    rt_uint32_t read = 0u;
    rt_uint32_t idle_spins = 0u;
    const rt_uint32_t start_ms = rt_tick_get_millisecond();

    if ((message->length % element_bytes) != 0u)
        return RT_FALSE;

    Cy_SCB_SPI_ClearRxFifo(spi->base);
    Cy_SCB_SPI_ClearTxFifo(spi->base);

    while (read < element_count)
    {
        rt_bool_t progressed = RT_FALSE;
        while (written < element_count)
        {
            rt_uint32_t value = 0u;

            if (tx != RT_NULL)
            {
                value = tx[written * element_bytes];
                if (element_bytes == 2u)
                    value |= ((rt_uint32_t)tx[(written * element_bytes) + 1u] << 8u);
            }

            if (Cy_SCB_SPI_Write(spi->base, value) == 0u)
                break;

            written++;
            progressed = RT_TRUE;
        }

        while ((read < element_count) && (Cy_SCB_SPI_GetNumInRxFifo(spi->base) != 0u))
        {
            rt_uint32_t value = Cy_SCB_SPI_Read(spi->base);

            if (rx != RT_NULL)
            {
                rx[read * element_bytes] = (rt_uint8_t)value;
                if (element_bytes == 2u)
                    rx[(read * element_bytes) + 1u] = (rt_uint8_t)(value >> 8u);
            }
            read++;
            progressed = RT_TRUE;
        }

        idle_spins = progressed ? 0u : idle_spins + 1u;
        if ((rt_uint32_t)(rt_tick_get_millisecond() - start_ms) >= IFX_SPI_POLL_TIMEOUT_MS ||
            idle_spins >= IFX_SPI_POLL_IDLE_LIMIT)
        {
            Cy_SCB_SPI_ClearRxFifo(spi->base);
            Cy_SCB_SPI_ClearTxFifo(spi->base);
            return RT_FALSE;
        }
    }

    while (!Cy_SCB_SPI_IsTxComplete(spi->base))
    {
        idle_spins++;
        if ((rt_uint32_t)(rt_tick_get_millisecond() - start_ms) >= IFX_SPI_POLL_TIMEOUT_MS ||
            idle_spins >= IFX_SPI_POLL_IDLE_LIMIT)
        {
            Cy_SCB_SPI_ClearRxFifo(spi->base);
            Cy_SCB_SPI_ClearTxFifo(spi->base);
            return RT_FALSE;
        }
    }

    return RT_TRUE;
}

static rt_ssize_t spixfer(struct rt_spi_device* device, struct rt_spi_message* message)
{
    RT_ASSERT(device);
    RT_ASSERT(message);

    struct ifx_spi* spi = rt_container_of(device->bus, struct ifx_spi, spi_bus);
    rt_bool_t transfer_ok = RT_TRUE;

    if (message->cs_take && !(device->config.mode & RT_SPI_NO_CS)) {
        ifx_spi_set_cs(spi, (device->config.mode & RT_SPI_CS_HIGH) ? PIN_HIGH : PIN_LOW);
    }

    if (message->length > 0) {
        if (message->send_buf == RT_NULL && message->recv_buf == RT_NULL) {
            transfer_ok = RT_FALSE;
            goto __exit;
        }

        transfer_ok = ifx_spi_poll_xfer(spi, message);
    }

__exit:
    if (message->cs_release && !(device->config.mode & RT_SPI_NO_CS)) {
        ifx_spi_set_cs(spi, (device->config.mode & RT_SPI_CS_HIGH) ? PIN_LOW : PIN_HIGH);
    }

    if (!transfer_ok)
        return 0;

    return message->length;
}

static const struct rt_spi_ops ifx_spi_ops = {
    .configure = spi_configure,
    .xfer = spixfer,
};

static int rt_hw_spi_init(void)
{
    for (int i = 0; i < sizeof(ifx_spi_obj) / sizeof(ifx_spi_obj[0]); i++) {
        struct ifx_spi* obj = &ifx_spi_obj[i];

        if (ifx_hw_spi_init(obj) == RT_EOK) {
            if (rt_spi_bus_register(&obj->spi_bus, obj->name, &ifx_spi_ops) != RT_EOK)
                return -RT_ERROR;
        } else {
            return -RT_ERROR;
        }
    }
    return RT_EOK;
}
INIT_BOARD_EXPORT(rt_hw_spi_init);

#endif /* BSP_USING_SPI */
