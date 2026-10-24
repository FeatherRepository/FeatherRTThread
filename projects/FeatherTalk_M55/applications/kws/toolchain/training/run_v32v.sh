#!/bin/bash
cd /c/ft/projects/FeatherTalk_M55/applications/kws/toolchain/training
PY="C:/Users/34586/.workbuddy-ai/binaries/python/envs/kws/Scripts/python.exe"
export KWS_VERSION=v32v
export KWS_ROLE=verifier
export KWS_CORPUS="C:/ft/projects/FeatherTalk_M55/applications/kws/toolchain/data/boardmic_all_v31/manifest.csv"
export KWS_ARCH=big
export KWS_WIDTH=2
export KWS_POOL=gap
export KWS_BOARD_SIM=4
export KWS_SPEC_AUG=0
export KWS_SYNTH_NEG=600
export KWS_SYNTH_ONSET=1000
export KWS_NEG_MULT=2.0
export KWS_HARD_NEG=1
export KWS_EPOCHS=60
export TF_CPP_MIN_LOG_LEVEL=2
export PYTHONIOENCODING=utf-8
echo "=== 训练 v32v (二级 verifier：大模型 + 169条难负样本 + 负权重×2.0) ==="
"$PY" train_kws.py 2>&1 | tee logs/train_v32v.log
echo "=== v32v done rc=$? ==="
