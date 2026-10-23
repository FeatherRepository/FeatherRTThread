#!/bin/bash
cd /c/Users/34586/Desktop/git_edgi_talk/FeatherRTThread/projects/FeatherTalk_M55/applications/kws/toolchain/training
export KWS_VERSION=v17
export KWS_POOL=gapmax
export KWS_SPEC_AUG=0
export KWS_SYNTH_NEG=600
export KWS_SYNTH_ONSET=1000
export KWS_EPOCHS=40
export TF_CPP_MIN_LOG_LEVEL=2
export PYTHONIOENCODING=utf-8
PY="C:/Users/34586/.workbuddy-ai/binaries/python/envs/kws/Scripts/python.exe"
echo "=== v17 start $(date +%H:%M:%S) dataset_v11 + clip ==="
"$PY" train_kws.py 2>&1 | tee logs/train_v17.log
echo "=== v17 end $(date +%H:%M:%S) rc=$? ==="
