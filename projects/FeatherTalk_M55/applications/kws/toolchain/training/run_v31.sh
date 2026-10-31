#!/bin/bash
cd /c/ft/projects/FeatherTalk_M55/applications/kws/toolchain/training
PY="C:/Users/34586/.workbuddy-ai/binaries/python/envs/kws/Scripts/python.exe"
NEG_STAMP="$1"
if [ -z "$NEG_STAMP" ]; then echo "用法: run_v31.sh <负样本采集stamp>"; exit 1; fi
echo "=== 合并语料（新负样本=$NEG_STAMP + v30 全部） ==="
"$PY" make_merged_corpus.py --out data/boardmic_all_v31 \
  --include "$NEG_STAMP" 20261023_204140 20261023_200453 20261023_015818 20261020_215243 20261020_230020 20261021_012058 20261021_024142 2>&1 | tail -3
export KWS_VERSION=v31
export KWS_CORPUS="C:/ft/projects/FeatherTalk_M55/applications/kws/toolchain/data/boardmic_all_v31/manifest.csv"
export KWS_ARCH=big
export KWS_WIDTH=2
export KWS_POOL=gap
export KWS_BOARD_SIM=4
export KWS_SPEC_AUG=0
export KWS_SYNTH_NEG=600
export KWS_SYNTH_ONSET=1000
export KWS_NEG_MULT=1.5
export KWS_EPOCHS=40
export TF_CPP_MIN_LOG_LEVEL=2
export PYTHONIOENCODING=utf-8
echo "=== 训练 v31 (大模型 + 73板载正样本 + 新增板载nearword/zh_sentence负样本 + 实测IR模拟×4) ==="
"$PY" train_kws.py 2>&1 | tee logs/train_v31.log
echo "=== v31 done rc=$? ==="
