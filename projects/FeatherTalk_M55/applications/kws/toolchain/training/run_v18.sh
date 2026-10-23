#!/bin/bash
cd /c/ft/projects/FeatherTalk_M55/applications/kws/toolchain/training
PY="C:/Users/34586/.workbuddy-ai/binaries/python/envs/kws/Scripts/python.exe"
NEW_STAMP="$1"
if [ -z "$NEW_STAMP" ]; then echo "用法: run_v18.sh <新采集stamp>"; exit 1; fi
echo "=== 合并语料 (新=$NEW_STAMP) ==="
"$PY" make_merged_corpus.py --out data/boardmic_all_v18 --include "$NEW_STAMP" 20261020_215243 20261020_230020 20261021_012058 20261021_024142 2>&1 | tail -4
export KWS_VERSION=v18
export KWS_CORPUS="C:/ft/projects/FeatherTalk_M55/applications/kws/toolchain/data/boardmic_all_v18/manifest.csv"
export KWS_POOL=gapmax
export KWS_SPEC_AUG=0
export KWS_SYNTH_NEG=600
export KWS_SYNTH_ONSET=1000
export KWS_EPOCHS=40
export TF_CPP_MIN_LOG_LEVEL=2
export PYTHONIOENCODING=utf-8
echo "=== 训练 v18 ==="
"$PY" train_kws.py 2>&1 | tee logs/train_v18.log
echo "=== v18 done rc=$? ==="
