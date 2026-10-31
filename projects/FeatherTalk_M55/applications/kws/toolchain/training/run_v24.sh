#!/bin/bash
cd /c/ft/projects/FeatherTalk_M55/applications/kws/toolchain/training
PY="C:/Users/34586/.workbuddy-ai/binaries/python/envs/kws/Scripts/python.exe"
export KWS_VERSION=v24
export KWS_CORPUS="C:/ft/projects/FeatherTalk_M55/applications/kws/toolchain/data/boardmic_all_v19/manifest.csv"
export KWS_ARCH=mixconv
export KWS_POOL=gap
export KWS_BOARD_SIM=4
export KWS_SPEC_AUG=0
export KWS_SYNTH_NEG=600
export KWS_SYNTH_ONSET=1000
export KWS_NEG_MULT=1.5
export KWS_EPOCHS=40
export TF_CPP_MIN_LOG_LEVEL=2
export PYTHONIOENCODING=utf-8
echo "=== 训练 v24 (MixConv + 实测IR声路模拟×4 + 负权重×1.5) ==="
"$PY" train_kws.py 2>&1 | tee logs/train_v24.log
echo "=== v24 done rc=$? ==="
