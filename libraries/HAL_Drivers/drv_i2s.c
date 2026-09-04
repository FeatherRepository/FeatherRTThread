#include <rtthread.h>
#include <rtdevice.h>
#include "drv_i2s.h"
#include "drv_gpio.h"
#include "cy_pdl.h"
#include "mtb_hal.h"
#include "cybsp.h"
#include "IFX_asrc.h"
#include "drv_es8388.h"
#include <rthw.h>
#include <rtdevice.h>
#define DBG_TAG              "i2s"
#define DBG_LVL              DBG_INFO
#include <rtdbg.h>

#define ES8388_PA_PIN GET_PIN(21, 6)

typedef enum
{
    i2s_state_stop,
    i2s_state_read,
    i2s_state_write,
} ifx_i2s_state_t;

typedef struct
{
    rt_uint32_t data_len_bytes;
    const rt_uint8_t *data;
} i2s_playback_q_data_t;

/* One 2048-byte RT-Audio block expands to at most 2048 physical I2S words
 * (16-bit mono duplicated into the mandatory left/right slots). */
static rt_uint32_t i2s_playback_buffer1[PLAYBACK_DATA_FRAME_SIZE] __attribute__((section(".cy_socmem_data"))) = { 0 };
static rt_uint32_t i2s_playback_buffer2[PLAYBACK_DATA_FRAME_SIZE] __attribute__((section(".cy_socmem_data"))) = { 0 };

static rt_uint32_t *active_i2s_playback_buffer_ptr = RT_NULL;
static rt_uint32_t *inactive_i2s_playback_buffer_ptr = RT_NULL;
static volatile rt_uint32_t active_i2s_word_count;
static volatile rt_uint32_t active_i2s_word_offset;

bool first_frame = true;
bool i2s_deinit_flag = false;
bool i2s_skip_frame = false;

#define TX_FIFO_SIZE         (4096)

static volatile bool i2s_data_ready_flag = false;
static volatile rt_uint32_t i2s_sound_start_count;
static volatile rt_uint32_t i2s_task_message_count;
static volatile rt_uint32_t i2s_hardware_start_count;
static volatile rt_uint32_t i2s_irq_count;
static volatile rt_uint32_t i2s_irq_trigger_count;
static volatile rt_uint32_t i2s_frame_complete_count;
static volatile rt_uint32_t i2s_irq_underflow_count;
static volatile rt_uint32_t i2s_supply_stall_ms;   /* replay 队列空转累计 ms (供给断档) */
static volatile rt_uint32_t i2s_transmit_count;
static volatile rt_uint32_t i2s_transmit_fail_count;
static volatile rt_uint32_t i2s_last_transmit_size;
static volatile rt_uint32_t i2s_format_apply_count;
static volatile rt_uint32_t i2s_format_apply_fail_count;
static volatile rt_uint32_t i2s_format_rollback_fail_count;

uint8_t i2s_playback_volume = DEFAULT_VOLUME;
/* ASRC variables for down-sampling audio to 16000Hz */
IFX_ASRC_STRUCT_t asrc_mem_down_sampling;
#if SAMPLING_RATE_44_1kHz == SAMPLING_RATE
/* ASRC variables for up-sampling 44100Hz to 48000Hz */
IFX_ASRC_STRUCT_t asrc_mem_up_sampling;
#endif /* SAMPLING_RATE_44_1kHz */

// static rt_uint32_t input_sampling_freq = 0;
// static rt_uint32_t output_sampling_freq = 0;
// static uint16_t asrc_out_len = 0;

/* Definition of buffers for ASRC, AEC and I2S */
// static int32_t asrc_in_i2s_playback_buffer_32bit[FRAME_SIZE]           __attribute__ ((section(".cy_socmem_data"))) = { 0 };
// static int32_t asrc_out_aec_ref_buffer_32bit[ASRC_OUTPUT_BUFFER_SIZE]  __attribute__ ((section(".cy_socmem_data"))) = { 0 };
// static int16_t asrc_out_aec_ref_buffer_16bit[AEC_REF_FRAME_SIZE]       __attribute__ ((section(".cy_socmem_data"))) = { 0 };
#if SAMPLING_RATE_44_1kHz == SAMPLING_RATE
static int16_t asrc_out_up_sampled_buffer_16bit[PLAYBACK_DATA_FRAME_SIZE / 2]  __attribute__((section(".cy_socmem_data"))) = { 0 };
#endif /* SAMPLING_RATE_44_1kHz */
const cy_stc_sysint_t i2s_isr_txcfg =
{
    .intrSrc = (IRQn_Type) tdm_0_interrupts_tx_0_IRQn,
    .intrPriority = I2S_INTR_PRIORITY,
};

struct sound_device
{
    struct rt_audio_device audio;
    struct rt_audio_configure audio_config;
    char *dev_name;

    rt_uint8_t *tx_buff;
    rt_mq_t tx_mq;
    rt_sem_t tx_sem;

    rt_uint8_t *rx_buff;
    ifx_i2s_state_t i2s_state;
    rt_uint8_t volume;

    rt_thread_t playback_thread;
};
static struct sound_device snd_dev = {0};

static char msg_pool[512];

bool music_player_active = false;
bool music_player_pause = false;

typedef struct
{
    rt_uint32_t data_len;
    uint8_t *data;
} music_player_q_data_t;

void i2s_playback_task(void *arg);
static rt_uint32_t i2s_prepare_physical_frame(
    const i2s_playback_q_data_t *message, rt_uint32_t *output,
    const struct rt_audio_configure *config);

void ifx_i2s_init(void)
{
    /* Initialize the I2S interrupt */
    Cy_SysInt_Init(&i2s_isr_txcfg, i2s_tx_interrupt_handler);
    NVIC_EnableIRQ(i2s_isr_txcfg.intrSrc);

    /* Initialize the I2S */
    cy_en_tdm_status_t volatile return_status = Cy_AudioTDM_Init(TDM_STRUCT0, &CYBSP_TDM_CONTROLLER_0_config);
    if (CY_TDM_SUCCESS != return_status)
    {
        RT_ASSERT(0);
    }

    /* Clear TX interrupts */
    Cy_AudioTDM_ClearTxInterrupt(TDM_STRUCT0_TX, CY_TDM_INTR_TX_MASK);
    Cy_AudioTDM_SetTxInterruptMask(TDM_STRUCT0_TX, CY_TDM_INTR_TX_MASK);
}
void ifx_i2s_deinit()
{
    i2s_deinit_flag = true;

    if (!first_frame)
    {
        Cy_AudioTDM_DeActivateTx(TDM_STRUCT0_TX);
        Cy_AudioI2S_DisableTx(TDM_STRUCT0_TX);

        // Cy_AudioTDM_DeInit(TDM_STRUCT0_TX);
        first_frame = true;
    }
}

static bool sound_format_supported(const struct rt_audio_configure *config)
{
    if (config == RT_NULL) return false;
    if (config->samplerate != 16000U && config->samplerate != 24000U &&
        config->samplerate != 48000U && config->samplerate != 96000U)
        return false;
    if (config->samplebits != 16U && config->samplebits != 24U)
        return false;
    return config->channels == 1U || config->channels == 2U;
}

static rt_err_t ifx_set_samplerate(struct rt_audio_configure audio_config)
{
    rt_uint32_t divider;

    if (!sound_format_supported(&audio_config)) return -RT_EINVAL;
    /* HF7 is 49.152 MHz. TDM clkDiv=4 and physical I2S always has two
     * slots, so PCLK = Fs * 2 * slot_bits * 4. 24-bit samples use a
     * 32-bit slot, while logical mono/stereo does not change BCLK. */
    if (audio_config.samplebits == 16U)
    {
        switch (audio_config.samplerate)
        {
        case 16000U: divider = 23U; break;
        case 24000U: divider = 15U; break;
        case 48000U: divider = 7U; break;
        case 96000U: divider = 3U; break;
        default: return -RT_EINVAL;
        }
    }
    else
    {
        switch (audio_config.samplerate)
        {
        case 16000U: divider = 11U; break;
        case 24000U: divider = 7U; break;
        case 48000U: divider = 3U; break;
        case 96000U: divider = 1U; break;
        default: return -RT_EINVAL;
        }
    }
    Cy_SysClk_PeriPclkDisableDivider((en_clk_dst_t)CYBSP_TDM_CONTROLLER_0_CLK_DIV_GRP_NUM, CY_SYSCLK_DIV_16_5_BIT, 0U);
    Cy_SysClk_PeriPclkSetFracDivider(
        (en_clk_dst_t)CYBSP_TDM_CONTROLLER_0_CLK_DIV_GRP_NUM,
        CY_SYSCLK_DIV_16_5_BIT, 0U, divider, 0U);
    #if defined(BSP_USING_XiaoZhi)
        Cy_SysClk_PeriPclkSetFracDivider((en_clk_dst_t)CYBSP_TDM_CONTROLLER_0_CLK_DIV_GRP_NUM, CY_SYSCLK_DIV_16_5_BIT, 0U, 15U, 0U);
    #endif
    Cy_SysClk_PeriPclkEnableDivider((en_clk_dst_t)CYBSP_TDM_CONTROLLER_0_CLK_DIV_GRP_NUM, CY_SYSCLK_DIV_16_5_BIT, 0U);
    return RT_EOK;
}

static rt_err_t ifx_program_output_format(
    const struct rt_audio_configure *config)
{
    cy_en_tdm_status_t tdm_status;
    rt_err_t result;

    if (!sound_format_supported(config)) return -RT_EINVAL;

    NVIC_DisableIRQ(i2s_isr_txcfg.intrSrc);
    Cy_AudioTDM_DeActivateTx(TDM_STRUCT0_TX);
    Cy_AudioI2S_DisableTx(TDM_STRUCT0_TX);
    Cy_AudioTDM_DeInit(TDM_STRUCT0);
    CYBSP_TDM_CONTROLLER_0_tx_config.wordSize =
        config->samplebits == 24U ? CY_TDM_SIZE_24 : CY_TDM_SIZE_16;
    CYBSP_TDM_CONTROLLER_0_tx_config.channelSize =
        config->samplebits == 24U ? 32U : 16U;
    tdm_status = Cy_AudioTDM_Init(TDM_STRUCT0,
                                  &CYBSP_TDM_CONTROLLER_0_config);
    if (tdm_status != CY_TDM_SUCCESS)
    {
        NVIC_EnableIRQ(i2s_isr_txcfg.intrSrc);
        return -RT_ERROR;
    }
    Cy_AudioTDM_ClearTxInterrupt(TDM_STRUCT0_TX, CY_TDM_INTR_TX_MASK);
    Cy_AudioTDM_SetTxInterruptMask(TDM_STRUCT0_TX, CY_TDM_INTR_TX_MASK);
    first_frame = true;
    active_i2s_word_count = 0U;
    active_i2s_word_offset = 0U;
    result = ifx_set_samplerate(*config);
    if (result == RT_EOK)
        result = es8388_output_format_set(
            config->samplerate, (rt_uint8_t)config->samplebits);
    NVIC_EnableIRQ(i2s_isr_txcfg.intrSrc);
    return result;
}

static rt_err_t ifx_apply_output_format(
    const struct rt_audio_configure *config,
    const struct rt_audio_configure *previous)
{
    rt_err_t result;

    if (!sound_format_supported(config) ||
        !sound_format_supported(previous))
        return -RT_EINVAL;
    if (i2s_data_ready_flag) return -RT_EBUSY;

    i2s_format_apply_count++;
    result = ifx_program_output_format(config);
    if (result == RT_EOK) return RT_EOK;

    i2s_format_apply_fail_count++;
    /* The sound0 format is committed only after TDM clocks and ES8388 both
     * accept it.  Restore the complete previous path when either layer
     * rejects the candidate. */
    if (ifx_program_output_format(previous) != RT_EOK)
    {
        i2s_format_rollback_fail_count++;
        LOG_E("output format rollback failed");
    }
    return result;
}

void app_i2s_clear_tx_fifo(void)
{
    /* To clear FIFO there is no direct way so Disable and Enable Tx, FIFO will be cleared as a side effect */
    Cy_AudioI2S_DisableTx(TDM_STRUCT0_TX);
    Cy_AudioI2S_EnableTx(TDM_STRUCT0_TX);
    Cy_AudioI2S_DisableTx(TDM_STRUCT0_TX);
}

void app_i2s_activate(void)
{
    /* Activate and enable I2S TX interrupts */
    Cy_AudioTDM_ActivateTx(TDM_STRUCT0_TX);
}

void app_i2s_enable(void)
{
    /* Clear TX interrupts */
    Cy_AudioTDM_ClearTxInterrupt(TDM_STRUCT0_TX, CY_TDM_INTR_TX_MASK);
    Cy_AudioTDM_SetTxInterruptMask(TDM_STRUCT0_TX, CY_TDM_INTR_TX_MASK);

    /* Start the I2S TX */
    Cy_AudioTDM_EnableTx(TDM_STRUCT0_TX);
}

void app_i2s_deactivate(void)
{
    /* Deactivate and enable I2S TX interrupts */
    Cy_AudioTDM_DeActivateTx(TDM_STRUCT0_TX);
}

/**
 * RT-Thread Audio Device Driver Interface
 */
static rt_err_t sound_getcaps(struct rt_audio_device *audio, struct rt_audio_caps *caps)
{
    rt_err_t result = RT_EOK;
    struct sound_device *snd_dev;

    RT_ASSERT(audio != RT_NULL);
    snd_dev = (struct sound_device *)audio->parent.user_data;

    switch (caps->main_type)
    {
    case AUDIO_TYPE_QUERY: /* qurey the types of hw_codec device */
    {
        switch (caps->sub_type)
        {
        case AUDIO_TYPE_QUERY:
            // caps->udata.mask = AUDIO_TYPE_OUTPUT | AUDIO_TYPE_MIXER;
            break;

        default:
            result = -RT_ERROR;
            break;
        }

        break;
    }

    case AUDIO_TYPE_OUTPUT: /* Provide capabilities of OUTPUT unit */
    {
        switch (caps->sub_type)
        {
        case AUDIO_DSP_PARAM:
            caps->udata.config.samplerate   = snd_dev->audio_config.samplerate;
            caps->udata.config.channels     = snd_dev->audio_config.channels;
            caps->udata.config.samplebits   = snd_dev->audio_config.samplebits;
            break;

        case AUDIO_DSP_SAMPLERATE:
            caps->udata.config.samplerate   = snd_dev->audio_config.samplerate;
            break;

        case AUDIO_DSP_CHANNELS:
            caps->udata.config.channels     = snd_dev->audio_config.channels;
            break;

        case AUDIO_DSP_SAMPLEBITS:
            caps->udata.config.samplebits   = snd_dev->audio_config.samplebits;
            break;

        default:
            result = -RT_ERROR;
            break;
        }

        break;
    }

    case AUDIO_TYPE_MIXER: /* report the Mixer Units */
    {
        switch (caps->sub_type)
        {
        case AUDIO_MIXER_QUERY:
            caps->udata.mask = AUDIO_MIXER_VOLUME;
            break;

        case AUDIO_MIXER_VOLUME:
            caps->udata.value = es8388_volume_get();
            snd_dev->volume = caps->udata.value;
            break;

        default:
            result = -RT_ERROR;
            break;
        }

        break;
    }

    default:
        result = -RT_ERROR;
        break;
    }

    return result;
}

static rt_err_t sound_configure(struct rt_audio_device *audio, struct rt_audio_caps *caps)
{
    rt_err_t result = RT_EOK;
    struct sound_device *snd_dev;

    RT_ASSERT(audio != RT_NULL);
    snd_dev = (struct sound_device *)audio->parent.user_data;

    switch (caps->main_type)
    {
    case AUDIO_TYPE_MIXER:
    {
        switch (caps->sub_type)
        {
        case AUDIO_MIXER_VOLUME:
        {
            rt_uint8_t volume = caps->udata.value;
            snd_dev->volume = volume;
            es8388_volume_set(snd_dev->volume);
            LOG_D("set volume %d", volume);
            break;
        }

        default:
            result = -RT_ERROR;
            break;
        }

        break;
    }

    case AUDIO_TYPE_OUTPUT:
    {
        struct rt_audio_configure candidate = snd_dev->audio_config;
        switch (caps->sub_type)
        {
        case AUDIO_DSP_PARAM:
        {
            candidate = caps->udata.config;
            break;
        }

        case AUDIO_DSP_SAMPLERATE:
        {
            candidate.samplerate = caps->udata.config.samplerate;
            break;
        }

        case AUDIO_DSP_CHANNELS:
        {
            candidate.channels = caps->udata.config.channels;
            break;
        }

        case AUDIO_DSP_SAMPLEBITS:
        {
            candidate.samplebits = caps->udata.config.samplebits;
            break;
        }

        default:
            result = -RT_ERROR;
            break;
        }

        if (result == RT_EOK)
        {
            result = ifx_apply_output_format(&candidate,
                                             &snd_dev->audio_config);
            if (result == RT_EOK)
            {
                snd_dev->audio_config = candidate;
                LOG_I("output format %lu Hz, %u ch, %u bit",
                      (unsigned long)candidate.samplerate,
                      candidate.channels, candidate.samplebits);
            }
        }

        break;
    }

    default:
        break;
    }

    return result;
}

static rt_err_t sound_init(struct rt_audio_device *audio)
{
    rt_err_t result = RT_EOK;

    struct sound_device *snd_dev;

    RT_ASSERT(audio != RT_NULL);

    snd_dev = (struct sound_device *)audio->parent.user_data;

    ifx_i2s_init();

    if (es8388_init("i2c0", ES8388_PA_PIN) != RT_EOK)
    {
        LOG_E("ES8388 init failed.");
        return -RT_ERROR;
    }

    es8388_start(ES_MODE_DAC);

    es8388_volume_set(snd_dev->volume);
    if (ifx_set_samplerate(snd_dev->audio_config) != RT_EOK ||
        es8388_output_format_set(snd_dev->audio_config.samplerate,
                                 snd_dev->audio_config.samplebits) != RT_EOK)
        return -RT_ERROR;

    rt_thread_startup(snd_dev->playback_thread);

    LOG_I("ES8388 init success.");
    return result;
}

static rt_err_t sound_start(struct rt_audio_device *audio, int stream)
{
    RT_ASSERT(audio != RT_NULL);

    if (stream == AUDIO_STREAM_REPLAY)
    {
        i2s_sound_start_count++;
        (void)rt_sem_control(snd_dev.tx_sem, RT_IPC_CMD_RESET, RT_NULL);
        music_player_active = true;
        LOG_I("Ready for I2S output \r\n");
        rt_audio_tx_complete(audio);
    }

    return RT_EOK;
}

static rt_ssize_t sound_transmit(struct rt_audio_device *audio, const void *writeBuf, void *readBuf, rt_size_t size)
{
    struct sound_device *device;
    i2s_playback_q_data_t message;
    rt_uint32_t word_count;
    rt_uint32_t *temporary;
    rt_base_t level;
    bool start_hardware;

    RT_ASSERT(audio != RT_NULL);
    RT_UNUSED(readBuf);
    device = (struct sound_device *)audio->parent.user_data;
    i2s_transmit_count++;
    i2s_last_transmit_size = (rt_uint32_t)size;
    if (size == 0U || writeBuf == RT_NULL)
        return 0;

    message.data_len_bytes = (rt_uint32_t)size;
    message.data = (const rt_uint8_t *)writeBuf;
    word_count = i2s_prepare_physical_frame(
        &message, inactive_i2s_playback_buffer_ptr,
        &device->audio_config);
    if (word_count == 0U)
    {
        i2s_transmit_fail_count++;
        return 0;
    }

    level = rt_hw_interrupt_disable();
    temporary = active_i2s_playback_buffer_ptr;
    active_i2s_playback_buffer_ptr = inactive_i2s_playback_buffer_ptr;
    inactive_i2s_playback_buffer_ptr = temporary;
    active_i2s_word_count = word_count;
    active_i2s_word_offset = 0U;
    i2s_data_ready_flag = true;
    start_hardware = first_frame;
    first_frame = false;
    rt_hw_interrupt_enable(level);

    if (start_hardware && !i2s_deinit_flag)
    {
        rt_uint32_t index;
        i2s_hardware_start_count++;
        app_i2s_enable();
        for (index = 0U; index < HW_FIFO_SIZE; index++)
            Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, 0U);
        app_i2s_activate();
        es8388_volume_set(device->volume);
    }
    return size;
}

static rt_err_t sound_stop(struct rt_audio_device *audio, int stream)
{
    RT_ASSERT(audio != RT_NULL);
    if (stream == AUDIO_STREAM_REPLAY)
    {
        music_player_active = false;
        NVIC_DisableIRQ(i2s_isr_txcfg.intrSrc);
        Cy_AudioTDM_DeActivateTx(TDM_STRUCT0_TX);
        Cy_AudioI2S_DisableTx(TDM_STRUCT0_TX);
        first_frame = true;
        i2s_data_ready_flag = false;
        active_i2s_word_count = 0U;
        active_i2s_word_offset = 0U;
        NVIC_EnableIRQ(i2s_isr_txcfg.intrSrc);
        (void)rt_mq_control(snd_dev.tx_mq, RT_IPC_CMD_RESET, RT_NULL);
        (void)rt_sem_release(snd_dev.tx_sem);
//        rt_thread_detach(snd_dev->playback_thread);
//        while(audio->replay->queue.is_empty==0)
//        rt_data_queue_reset(&audio->replay->queue);
//        audio->replay->write_index = 0;
//        audio->replay->read_index = 0;
//        audio->replay->pos = 0;
        LOG_D("Sound Stop.");
    }

    return RT_EOK;
}

static void sound_buffer_info(struct rt_audio_device *audio, struct rt_audio_buf_info *info)
{
    struct sound_device *snd_dev;
    RT_ASSERT(audio != RT_NULL);
    snd_dev = (struct sound_device *)audio->parent.user_data;

    /**
     *               TX_FIFO
     * +----------------+----------------+
     * |     block1     |     block2     |
     * +----------------+----------------+
     *  \  block_size  /
     */
    info->buffer      = snd_dev->tx_buff;
    info->total_size  = TX_FIFO_SIZE;
    info->block_size  = TX_FIFO_SIZE / 2;
    info->block_count = 2;
}



static struct rt_audio_ops snd_ops =
{
    .getcaps     = sound_getcaps,
    .configure   = sound_configure,
    .init        = sound_init,
    .start       = sound_start,
    .stop        = sound_stop,
    .transmit    = sound_transmit,
    .buffer_info = sound_buffer_info,
};

int rt_hw_sound_init(void)
{
    rt_err_t ret = RT_EOK;
    rt_uint8_t *tx_buff;

    tx_buff = (rt_uint8_t *)rt_malloc(TX_FIFO_SIZE);

    rt_memset(tx_buff, 0, TX_FIFO_SIZE);
    if (tx_buff == RT_NULL)
        return -RT_ENOMEM;
    snd_dev.tx_buff = tx_buff;
    /* init default configuration */
    snd_dev.audio_config.samplerate = 16000;
    snd_dev.audio_config.channels   = 2;
    snd_dev.audio_config.samplebits = 16;
    snd_dev.volume                   = 70;

    snd_dev.audio.ops = &snd_ops;

    snd_dev.tx_mq = rt_mq_create(
                        "sound_tx_mq",
                        sizeof(i2s_playback_q_data_t),
                        sizeof(msg_pool),
                        RT_IPC_FLAG_FIFO);

    snd_dev.tx_sem = rt_sem_create("sound_tx_sem", 0, RT_IPC_FLAG_FIFO);
    if (snd_dev.tx_sem == RT_NULL)
    {
        LOG_E("create sound tx_sem failed.\n");
        return -RT_ERROR;
    }


    snd_dev.playback_thread = rt_thread_create("sound_thread", i2s_playback_task, (void *)&snd_dev.audio, I2S_PLAYBACK_TASK_STACK_SIZE, I2S_PLAYBACK_TASK_PRIORITY, 10);
    if (snd_dev.playback_thread == RT_NULL)
    {
        LOG_E("Error in I2S playback task \r\n");
        RT_ASSERT(snd_dev.playback_thread != RT_NULL);
    }

    ret = rt_audio_register(&snd_dev.audio, "sound0", RT_DEVICE_FLAG_WRONLY, &snd_dev);

    if (ret != RT_EOK)
    {
        LOG_E("rt_audio %s register failed, status=%d\n", "sound0", ret);
        return -RT_ERROR;
    }

    return RT_EOK;
}
INIT_DEVICE_EXPORT(rt_hw_sound_init);

#if 0 /* Replaced by the variable-format frame path below. */
void i2s_write_32_samples(void)
{
    i2s_32_samples_frame_count = 1;
    for (int i = 0; i < HW_FIFO_SIZE; i++)
    {
        /* Write same data for L,R channels(dual mono) in FIFO */
        Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, (rt_uint32_t) * ((i2s_playback_ptr + (HW_FIFO_SIZE * (i2s_32_samples_frame_count - 1))) + i));
        i++;
        Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, (rt_uint32_t) * ((i2s_playback_ptr + (HW_FIFO_SIZE * (i2s_32_samples_frame_count - 1))) + i));
    }
    i2s_32_samples_frame_count = 2;
}

void convert_mono_to_stereo(int16_t *mono_data, rt_uint32_t mono_data_num_samples,
                            int16_t *stereo_data)
{
    for (rt_uint32_t i = 0; i < mono_data_num_samples; i++)
    {
        *stereo_data++ = *mono_data;  // Copy the mono sample to the left channel
        *stereo_data++ = *mono_data;  // Copy the same mono sample to the right channel
        mono_data++;  // Move to the next mono sample
    }
}

void i2s_playback_task(void *arg)
{
    struct rt_audio_device *audio = (struct rt_audio_device *)arg;
    struct sound_device *snd_dev;
    RT_ASSERT(audio != RT_NULL);
    snd_dev = (struct sound_device *)audio->parent.user_data;
#if defined(PKG_USING_WAVPLAYER) && !defined(BSP_USING_XiaoZhi)
    static int count = 0;
#endif
    int16_t *temp_buffer_ptr;

    // int32_t* asrc_out_ptr = NULL;

    i2s_playback_q_data_t i2s_playback_q_data;

    active_i2s_playback_buffer_ptr = i2s_stereo_playback_buffer1;
    inactive_i2s_playback_buffer_ptr = i2s_stereo_playback_buffer2;

    while (1)
    {
        rt_mq_recv(snd_dev->tx_mq, &i2s_playback_q_data, sizeof(i2s_playback_q_data_t), RT_WAITING_FOREVER);

        /* Clear the flags, queues and notifications in case of I2S de-init.
         * This occurs when the "Stop Music" command is given.
         */
        if (i2s_deinit_flag)
        {
            i2s_deinit_flag = false;
            first_frame = true;
            rt_memset(&msg_pool[0], 0, sizeof(msg_pool));
            continue;
        }

#if SAMPLING_RATE_44_1kHz == SAMPLING_RATE
        /* The 'first_frame' flag is set only during the 1st frame of
        * playback for a new file.
        */
        if (first_frame)
        {

            /* Initialize ASRC for up sampling 44.1KHz to 48kHz playback data */
            input_sampling_freq = SAMPLING_RATE_44_1kHz;
            output_sampling_freq = SAMPLING_RATE_48kHz;

            init_IFX_asrc(&asrc_mem_up_sampling, input_sampling_freq, output_sampling_freq);
            IFX_SetClockDrift(&asrc_mem_up_sampling, 0);
        }

        frame_in_buffer = (int16_t *) i2s_playback_q_data.data;
        for (rt_uint32_t i = 0; i < FRAME_SIZE; i++)
        {
            asrc_in_i2s_playback_buffer_32bit[i] = (int32_t)
                                                   (*(int16_t *)frame_in_buffer);
            frame_in_buffer ++;
        }
        asrc_out_len = ASRC_OUTPUT_BUFFER_SIZE;
        asrc_out_ptr = asrc_out_aec_ref_buffer_32bit;
        frame_out_buffer = asrc_out_up_sampled_buffer_16bit;
        /* IFX_asrc() called 2 times to pass 441 samples, 240 and 201 in each call.*/
        for (rt_uint32_t i = 0; i < ASRC_NUM_ITERATIONS_PER_FRAME; i++)
        {
            if ((ASRC_NUM_ITERATIONS_PER_FRAME - 1) != i)
            {
                IFX_asrc(asrc_in_i2s_playback_buffer_32bit +
                         ASRC_INPUT_SAMPLES * i, ASRC_INPUT_SAMPLES,
                         asrc_out_ptr, &asrc_out_len, &asrc_mem_up_sampling);
            }
            else
            {
                IFX_asrc(asrc_in_i2s_playback_buffer_32bit +
                         ASRC_INPUT_SAMPLES * i, 201,
                         asrc_out_ptr, &asrc_out_len, &asrc_mem_up_sampling);
            }
            /* Convert int32_t to int16_t */
            for (rt_uint32_t j = 0; j < asrc_out_len; j++)
            {
                *(frame_out_buffer++) = (int16_t)(*(asrc_out_ptr + j));
            }
            asrc_out_ptr += asrc_out_len;
        }
#endif

        /* Convert the input mono channel audio to stereo by appropriate zero
            * padding for playback over I2S.
            */
#if SAMPLING_RATE_44_1kHz == SAMPLING_RATE
        convert_mono_to_stereo((int16_t *)asrc_out_up_sampled_buffer_16bit,
                               (PLAYBACK_DATA_FRAME_SIZE / 2),
                               inactive_i2s_playback_buffer_ptr);
#else
        convert_mono_to_stereo((int16_t *)i2s_playback_q_data.data,
                               (i2s_playback_q_data.data_len),
                               inactive_i2s_playback_buffer_ptr);
#endif

        temp_buffer_ptr = active_i2s_playback_buffer_ptr;
        active_i2s_playback_buffer_ptr = inactive_i2s_playback_buffer_ptr;
        inactive_i2s_playback_buffer_ptr = temp_buffer_ptr;
        i2s_data_ready_flag = true;

        /* The 'first_frame' flag is set only during the 1st frame of
         * playback for a new file.
         */
        if (first_frame)
        {
#if SAMPLING_RATE_16kHz != SAMPLING_RATE
            /* Initialize ASRC for 16kHz AEC reference */
            input_sampling_freq = SAMPLING_RATE;
            output_sampling_freq = AEC_REF_SAMPLING_RATE;

            /* ASRC for 16KHz aec ref from 44.1KHz/48KHz playback data */
            init_IFX_asrc(&asrc_mem_down_sampling, input_sampling_freq, output_sampling_freq);
            IFX_SetClockDrift(&asrc_mem_down_sampling, 0);
#endif
            /* Only for the first frame, the I2S write happens from this task. */
            if (!i2s_deinit_flag)
            {
                app_i2s_enable();

                for (int i = 0; i < HW_FIFO_SIZE; i++)
                {
                    /* Write same data for L,R channels(dual mono) in FIFO */
                    Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, (rt_uint32_t) 0);
                    i++;
                    Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, (rt_uint32_t) 0);
                }
                app_i2s_activate();
                es8388_volume_set(snd_dev->volume);
            }
            i2s_data_ready_flag = false;
        }

        /* Perform ASRC for sampling rates apart from 16kHz and enqueue the
         * AEC reference data in a circular buffer.
         */
#if SAMPLING_RATE_16kHz != SAMPLING_RATE
        frame_in_buffer = (int16_t *) i2s_playback_q_data.data;
        for (rt_uint32_t i = 0; i < FRAME_SIZE; i++)
        {
            asrc_in_i2s_playback_buffer_32bit[i] = (int32_t)
                                                   (*(int16_t *)frame_in_buffer);
            frame_in_buffer ++;
        }

        asrc_out_len = ASRC_OUTPUT_BUFFER_SIZE;
        asrc_out_ptr = asrc_out_aec_ref_buffer_32bit;
        frame_out_buffer = asrc_out_aec_ref_buffer_16bit;
        /* IFX_asrc() called 2 times to pass the samples */
        for (rt_uint32_t i = 0; i < ASRC_NUM_ITERATIONS_PER_FRAME; i++)
        {
            if (SAMPLING_RATE_48kHz == SAMPLING_RATE)
            {
                IFX_asrc(asrc_in_i2s_playback_buffer_32bit +
                         ASRC_INPUT_SAMPLES * i, ASRC_INPUT_SAMPLES,
                         asrc_out_ptr, &asrc_out_len, &asrc_mem_down_sampling);
            }
            if (SAMPLING_RATE_44_1kHz == SAMPLING_RATE)
            {
                if ((ASRC_NUM_ITERATIONS_PER_FRAME - 1) != i)
                {
                    IFX_asrc(asrc_in_i2s_playback_buffer_32bit +
                             ASRC_INPUT_SAMPLES * i, ASRC_INPUT_SAMPLES,
                             asrc_out_ptr, &asrc_out_len, &asrc_mem_down_sampling);
                }
                else
                {
                    IFX_asrc(asrc_in_i2s_playback_buffer_32bit +
                             ASRC_INPUT_SAMPLES * i, (FRAME_SIZE - ASRC_INPUT_SAMPLES),
                             asrc_out_ptr, &asrc_out_len, &asrc_mem_down_sampling);
                }
            }
            /* Convert int32_t to int16_t */
            for (rt_uint32_t j = 0; j < asrc_out_len; j++)
            {
                *(frame_out_buffer++) = (int16_t)(*(asrc_out_ptr + j));
            }
            asrc_out_ptr += asrc_out_len;
        }

#endif

        /* Only for the first frame, push the AEC reference data from the task. */

        /* Wait for notification from I2S ISR for completion of previous
         * audio frame playback.
         */
        if (!first_frame && !i2s_deinit_flag)
        {
            rt_sem_take(snd_dev->tx_sem, RT_WAITING_FOREVER);
        }

        /* Reset first frame flag. */
        first_frame = false;
        while (audio->replay->queue.is_empty == 1)
        {
            /* 供给断档统计: 队列空转 1ms/次 (≈一次可听爆音的起点),
             * UAC/BT 共用此路径, feather_i2s_diag 可读 */
            i2s_supply_stall_ms++;
            rt_thread_mdelay(1);
#if defined(PKG_USING_WAVPLAYER) && !defined(BSP_USING_XiaoZhi)
            if(count>=50){
                rt_completion_done(&audio->replay->cmp);
                count=0;
            }
            count++;
#endif
        }
        rt_audio_tx_complete(audio);
    }
}


bool is_music_player_active(void)
{
    return music_player_active;
}

bool is_music_player_paused(void)
{
    return music_player_pause;
}

void i2s_tx_interrupt_handler(void)
{
    rt_interrupt_enter();

    /* Get interrupt status and check for tigger interrupt and errors */
    rt_uint32_t intr_status = Cy_AudioTDM_GetTxInterruptStatusMasked(TDM_STRUCT0_TX);

    if (CY_TDM_INTR_TX_FIFO_TRIGGER & intr_status)
    {
        if (((PLAYBACK_DATA_FRAME_SIZE / HW_FIFO_SIZE) + 1) == i2s_32_samples_frame_count)
        {
            /* When 5 frames of the data is written into I2S call 10msec frame handlling() */
            {
                // music_player_q_data_t music_player_q_data;

                /* Initially, set the I2S playback and AEC reference to zero buffers. */
                int16_t *aec_ref_temp_ptr = aec_ref_cb_ptr;
                aec_ref_cb_ptr = aec_ref_zero_buffer;

                /* Set the I2S playback and AEC reference pointers to actual data in case
                * the data is ready and audio playback is active.
                */
                if ((!i2s_deinit_flag && i2s_data_ready_flag) ||
                        (first_frame))
                {
                    i2s_data_ready_flag = false;
                    i2s_playback_ptr = active_i2s_playback_buffer_ptr;
                    aec_ref_cb_ptr = aec_ref_temp_ptr;
                }

                /* Unless deinitialized, play the data over I2S and send the corresponding
                * AEC reference pointer to the AEC reference queue.
                */
                if (!i2s_deinit_flag)
                {
                    i2s_write_32_samples();
                }

                /* If the non-zero data is played, notify the I2S task for processing of
                * playback data of next frames.
                */
                if ((i2s_playback_ptr == active_i2s_playback_buffer_ptr))
                {
                    rt_sem_release(snd_dev.tx_sem);
                }
            }
        }
        else
        {
            if (is_music_player_paused() || i2s_data_ready_flag == false)
            {
                for (int i = 0; i < HW_FIFO_SIZE; i++)
                {
                    /* Write same data for L,R channels(dual mono) in FIFO */
                    Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, (rt_uint32_t) 0);
                    i++;
                    Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, (rt_uint32_t) 0);
                }
            }
            else
            {
                for (int i = 0; i < HW_FIFO_SIZE; i++)
                {
                    /* Write same data for L,R channels(dual mono) in FIFO */
                    Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, (rt_uint32_t) * ((i2s_playback_ptr + (HW_FIFO_SIZE * (i2s_32_samples_frame_count - 1))) + i));
                    i++;
                    Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, (rt_uint32_t) * ((i2s_playback_ptr + (HW_FIFO_SIZE * (i2s_32_samples_frame_count - 1))) + i));
                }
                i2s_32_samples_frame_count++;
            }

        }
    }
    else if (CY_TDM_INTR_TX_FIFO_UNDERFLOW & intr_status)
    {
        rt_kprintf("Error: I2S transmit underflowed\r\n");
    }

    /* Clear all Tx I2S Interrupt */
    Cy_AudioTDM_ClearTxInterrupt(TDM_STRUCT0_TX, CY_TDM_INTR_TX_MASK);

    rt_interrupt_leave();
}
#endif

static rt_uint32_t i2s_unpack_sample(const rt_uint8_t *data,
                                     rt_uint8_t sample_bits)
{
    if (sample_bits == 16U)
        return (rt_uint32_t)((rt_uint16_t)data[0] |
                             ((rt_uint16_t)data[1] << 8));
    return (rt_uint32_t)data[0] |
           ((rt_uint32_t)data[1] << 8) |
           ((rt_uint32_t)data[2] << 16);
}

static rt_uint32_t i2s_prepare_physical_frame(
    const i2s_playback_q_data_t *message, rt_uint32_t *output,
    const struct rt_audio_configure *config)
{
    rt_uint32_t bytes_per_sample = config->samplebits / 8U;
    rt_uint32_t bytes_per_frame = bytes_per_sample * config->channels;
    rt_uint32_t frame_count;
    rt_uint32_t frame;

    if (bytes_per_frame == 0U) return 0U;
    frame_count = message->data_len_bytes / bytes_per_frame;
    if (frame_count > PLAYBACK_DATA_FRAME_SIZE / 2U)
        frame_count = PLAYBACK_DATA_FRAME_SIZE / 2U;
    for (frame = 0U; frame < frame_count; frame++)
    {
        const rt_uint8_t *input = message->data + frame * bytes_per_frame;
        rt_uint32_t left = i2s_unpack_sample(input,
                                             config->samplebits);
        rt_uint32_t right = config->channels == 2U ?
            i2s_unpack_sample(input + bytes_per_sample,
                              config->samplebits) : left;
        output[frame * 2U] = left;
        output[frame * 2U + 1U] = right;
    }
    return frame_count * 2U;
}

void i2s_playback_task(void *arg)
{
    struct rt_audio_device *audio = (struct rt_audio_device *)arg;
    struct sound_device *device;

    RT_ASSERT(audio != RT_NULL);
    device = (struct sound_device *)audio->parent.user_data;
    active_i2s_playback_buffer_ptr = i2s_playback_buffer1;
    inactive_i2s_playback_buffer_ptr = i2s_playback_buffer2;

    while (1)
    {
        if (rt_sem_take(device->tx_sem, RT_WAITING_FOREVER) != RT_EOK)
            continue;
        i2s_task_message_count++;
        if (i2s_deinit_flag)
        {
            i2s_deinit_flag = false;
            first_frame = true;
            continue;
        }
        if (music_player_active)
            rt_audio_tx_complete(audio);
    }
}

bool is_music_player_active(void)
{
    return music_player_active;
}

bool is_music_player_paused(void)
{
    return music_player_pause;
}

void i2s_tx_interrupt_handler(void)
{
    rt_uint32_t intr_status;

    rt_interrupt_enter();
    i2s_irq_count++;
    intr_status = Cy_AudioTDM_GetTxInterruptStatusMasked(TDM_STRUCT0_TX);
    if ((intr_status & CY_TDM_INTR_TX_FIFO_TRIGGER) != 0U)
    {
        i2s_irq_trigger_count++;
        rt_uint32_t index;
        bool completed = false;
        for (index = 0U; index < HW_FIFO_SIZE; index++)
        {
            rt_uint32_t value = 0U;
            if (!music_player_pause && i2s_data_ready_flag &&
                active_i2s_word_offset < active_i2s_word_count)
            {
                value = active_i2s_playback_buffer_ptr[
                    active_i2s_word_offset++];
                if (active_i2s_word_offset >= active_i2s_word_count)
                    completed = true;
            }
            Cy_AudioTDM_WriteTxData(TDM_STRUCT0_TX, value);
        }
        if (completed)
        {
            i2s_frame_complete_count++;
            i2s_data_ready_flag = false;
            active_i2s_word_offset = 0U;
            active_i2s_word_count = 0U;
            (void)rt_sem_release(snd_dev.tx_sem);
        }
    }
    if ((intr_status & CY_TDM_INTR_TX_FIFO_UNDERFLOW) != 0U)
    {
        i2s_irq_underflow_count++;
        LOG_W("I2S transmit underflow");
    }
    Cy_AudioTDM_ClearTxInterrupt(TDM_STRUCT0_TX, CY_TDM_INTR_TX_MASK);
    rt_interrupt_leave();
}

#ifdef RT_USING_FINSH
static int feather_i2s_diag(int argc, char **argv)
{
    rt_uint32_t codec_rate = 0U;
    rt_uint16_t codec_ratio = 0U;
    rt_uint8_t codec_bits = 0U;
    rt_err_t codec_result;

    RT_UNUSED(argc);
    RT_UNUSED(argv);
    codec_result = es8388_output_format_get(&codec_rate, &codec_bits,
                                            &codec_ratio);
    rt_kprintf("I2S format=%lu Hz/%u ch/%u bit apply=%lu fail=%lu "
               "rollback-fail=%lu\n",
               (unsigned long)snd_dev.audio_config.samplerate,
               snd_dev.audio_config.channels,
               snd_dev.audio_config.samplebits,
               (unsigned long)i2s_format_apply_count,
               (unsigned long)i2s_format_apply_fail_count,
               (unsigned long)i2s_format_rollback_fail_count);
    if (codec_result == RT_EOK)
        rt_kprintf("ES8388 format=%lu Hz/%u bit MCLK=%u*Fs readback=ok\n",
                   (unsigned long)codec_rate, codec_bits, codec_ratio);
    else
        rt_kprintf("ES8388 format readback failed: %d\n", codec_result);
    rt_kprintf("I2S first=%u deinit=%u ready=%u active=%u pause=%u "
               "words=%lu/%lu fifo=%u rp=%u wp=%u irqstat=%08lx\n",
               first_frame ? 1U : 0U,
               i2s_deinit_flag ? 1U : 0U,
               i2s_data_ready_flag ? 1U : 0U,
               music_player_active ? 1U : 0U,
               music_player_pause ? 1U : 0U,
               (unsigned long)active_i2s_word_offset,
               (unsigned long)active_i2s_word_count,
               Cy_AudioTDM_GetNumInTxFifo(TDM_STRUCT0_TX),
               Cy_AudioTDM_GetTxReadPointer(TDM_STRUCT0_TX),
               Cy_AudioTDM_GetTxWritePointer(TDM_STRUCT0_TX),
               (unsigned long)Cy_AudioTDM_GetTxInterruptStatusMasked(
                   TDM_STRUCT0_TX));
    rt_kprintf("I2S start=%lu task=%lu hw=%lu irq=%lu trigger=%lu "
               "complete=%lu underflow=%lu sem=%u mq=%u\n",
               (unsigned long)i2s_sound_start_count,
               (unsigned long)i2s_task_message_count,
               (unsigned long)i2s_hardware_start_count,
               (unsigned long)i2s_irq_count,
               (unsigned long)i2s_irq_trigger_count,
               (unsigned long)i2s_frame_complete_count,
               (unsigned long)i2s_irq_underflow_count,
               snd_dev.tx_sem != RT_NULL ? snd_dev.tx_sem->value : 0U,
               snd_dev.tx_mq != RT_NULL ? snd_dev.tx_mq->entry : 0U);
    rt_kprintf("I2S transmit=%lu fail=%lu last=%lu replay-buffer=%u/%u supply-stall=%lu ms\n",
               (unsigned long)i2s_transmit_count,
               (unsigned long)i2s_transmit_fail_count,
               (unsigned long)i2s_last_transmit_size,
               snd_dev.audio.replay != RT_NULL ?
                   snd_dev.audio.replay->buf_info.block_size : 0U,
               snd_dev.audio.replay != RT_NULL ?
                   snd_dev.audio.replay->buf_info.total_size : 0U,
               (unsigned long)i2s_supply_stall_ms);
    return RT_EOK;
}
MSH_CMD_EXPORT(feather_i2s_diag, Show FeatherTalk I2S playback diagnostics.);
#endif

