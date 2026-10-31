#!/bin/bash
cd /c/Users/34586/Desktop/git_edgi_talk/FeatherRTThread/projects/FeatherTalk_M55/applications/kws/toolchain/training
export KWS_VERSION=v12
export KWS_POOL=gapmax
export KWS_SPEC_AUG=2
export KWS_SYNTH_NEG=1200
export KWS_SYNTH_ONSET=1200
export TF_CPP_MIN_LOG_LEVEL=2
export PYTHONIOENCODING=utf-8
PY="C:/Users/34586/.workbuddy-ai/binaries/python/envs/kws/Scripts/python.exe"
echo "=== v12 train start $(date +%H:%M:%S) ==="
"$PY" train_kws.py 2>&1 | tee logs/train_v12.log
echo "=== v12 train end $(date +%H:%M:%S) rc=$? ==="
