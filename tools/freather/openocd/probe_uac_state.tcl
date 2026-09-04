# 一次性探针: 读 UAC 速率闭环关键变量 (M55 .bss, 全局地址可达)
echo "=== s_requested_generation @2001ee38 ==="
mdw 0x2001ee38
echo "=== s_negotiated_output_rate @2001ee3c ==="
mdw 0x2001ee3c
echo "=== s_status 区域 @2001eda8 (64 words, 找 0xBB80=48000 / 0x3E80=16000) ==="
mdw 0x2001eda8 64
echo "=== i2s_format_apply_count @200366ec / i2s_data_ready_flag @200366bd ==="
mdw 0x200366ec
mdw 0x200366bc
echo "=== snd_dev->audio_config (snd_dev @200366f8, audio_config 偏移见 struct) ==="
mdw 0x200366f8 16
shutdown
