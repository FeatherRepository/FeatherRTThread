#!/bin/bash
cd /c/ft/projects/FeatherTalk_M55/applications/kws/toolchain/training
PY="C:/Users/34586/.workbuddy-ai/binaries/python/envs/kws/Scripts/python.exe"
NEG_STAMP="$1"
NEG_MULT="${2:-2.0}"
if [ -z "$NEG_STAMP" ]; then echo "用法: run_v19.sh <负样本采集stamp> [NEG_MULT]"; exit 1; fi
echo "=== 合并语料 (负样本=$NEG_STAMP) ==="
"$PY" make_merged_corpus.py --out data/boardmic_all_v19 --include "$NEG_STAMP" 20261023_015818 20261020_215243 20261020_230020 20261021_012058 20261021_024142 2>&1 | tail -3
export KWS_VERSION=v19
export KWS_CORPUS="C:/ft/projects/FeatherTalk_M55/applications/kws/toolchain/data/boardmic_all_v19/manifest.csv"
export KWS_POOL=gapmax
export KWS_SPEC_AUG=0
export KWS_SYNTH_NEG=600
export KWS_SYNTH_ONSET=1000
export KWS_NEG_MULT="$NEG_MULT"
export KWS_EPOCHS=40
export TF_CPP_MIN_LOG_LEVEL=2
export PYTHONIOENCODING=utf-8
echo "=== 训练 v19 (NEG_MULT=$NEG_MULT) ==="
"$PY" train_kws.py 2>&1 | tee logs/train_v19.log
echo "=== v19 done rc=$? ==="
