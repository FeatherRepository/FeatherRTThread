#!/bin/bash
# collect_data.sh — ② 长时板载数据采集（全自动，不需要人）
#
# 用法：
#   ./collect_data.sh                 # 默认 4 轮：P N P N
#   ./collect_data.sh 2               # 只跑 2 轮
#   ./collect_data.sh 6 P              # 6 轮全采正样本
#
# 轮型：
#   P = 正样本轮：wake_tts + wake_tts_v4（**PC 声卡 → 蓝牙音箱(花再) → 板载麦录**）
#       注意：不是板载喇叭自放！--source board 是"机内声路"（设备自问自答），
#       训练"用户对着设备说话"必须走 --source pc（外部音箱，0.5m）。
#   N = 负样本轮：nearword / nearword_sentence / zh_sentence / reply / transient
#   每轮末尾都带 2 次空场采集（房间底噪，负样本主力）
#
# 产物：
#   data/boardmic/<stamp>/            每次采集一个目录（rig 自己建）
#   data/boardmic/COLLECT_STAMPS.txt  所有成功轮次的 stamp 登记簿（追加）
#   data/boardmic_all_v32/manifest.csv  全部登记 stamp 合并后的训练语料
#
# 日志：logs/collect_<启动时间>.log（每轮明细 + 每轮汇总 + 最终合并结果）

cd /c/ft/projects/FeatherTalk_M55/applications/kws/toolchain
PY="C:/Users/34586/.workbuddy-ai/binaries/python/envs/kws/Scripts/python.exe"
STAMP_LOG=data/boardmic/COLLECT_STAMPS.txt
TS=$(date +%Y%m%d_%H%M%S)
LOG="logs/collect_${TS}.log"

ROUNDS="${1:-4}"
FORCE="${2:-}"          # P=只采正样本 N=只采负样本，缺省交替

log(){ echo "[$(date +%H:%M:%S)] $*" | tee -a "$LOG"; }

touch "$STAMP_LOG"
log "=== 采集开始：${ROUNDS} 轮（FORCE=${FORCE:-交替PN}） ==="

ok=0
for i in $(seq 1 "$ROUNDS"); do
    if [ "$FORCE" = "P" ]; then TYPE=P
    elif [ "$FORCE" = "N" ]; then TYPE=N
    elif [ $((i % 2)) -eq 1 ]; then TYPE=P
    else TYPE=N; fi

    if [ "$TYPE" = "P" ]; then
        SCEN="wake_tts,wake_tts_v4"; CAPS=12; NAME="正样本"
    else
        SCEN="nearword,nearword_sentence,zh_sentence,reply,transient"; CAPS=6; NAME="负样本"
    fi

    log "--- 第 $i/$ROUNDS 轮（$NAME 轮: $SCEN captures=$CAPS，PC+音箱） ---"
    "$PY" tools/kws_acoustic_rig.py --phase traindata --port COM9 \
        --source pc --pc-device "花再" --distance 0.5 \
        --scenarios "$SCEN" --captures "$CAPS" --ambient-captures 2 \
        --cap-sec 8 >> "$LOG" 2>&1
    rc=$?

    # rig 的汇总 JSON（<log>.json）与本轮日志
    RIGLOG=$(ls -t logs/kws_rig_traindata_*.log 2>/dev/null | head -1)
    RIGJSON="${RIGLOG%.log}.json"
    if [ $rc -ne 0 ] || [ ! -f "$RIGJSON" ]; then
        log "⚠ 第 $i 轮失败 rc=$rc（详见 $RIGLOG），跳过不登记"
        continue
    fi

    # 从汇总 JSON 取 stamp 与行数，登记
    read -r STAMP NPOS NNEG <<< "$("$PY" - "$RIGJSON" <<'EOF'
import json, sys, os
d = json.load(open(sys.argv[1], encoding="utf-8"))
out = d.get("out_dir", "")
rows = d.get("rows", []) or []
npos = sum(1 for r in rows if int(r.get("label", 0)) == 1)
nneg = sum(1 for r in rows if int(r.get("label", 0)) != 1)
print(os.path.basename(out), npos, nneg)
EOF
)"
    if [ -z "$STAMP" ] || [ "$STAMP" = "None" ]; then
        log "⚠ 第 $i 轮汇总无 out_dir，跳过不登记"
        continue
    fi
    echo "$STAMP" >> "$STAMP_LOG"
    ok=$((ok+1))
    log "✔ 第 $i 轮完成：$STAMP（$NPOS 正 / $NNEG 负），已登记（累计 $(wc -l < "$STAMP_LOG") 轮）"
done

log "=== 采集结束：$ok/$ROUNDS 轮成功 ==="

# ---- 自动合并全部登记 stamp → boardmic_all_v32 ----
log "=== 合并语料 → data/boardmic_all_v32 ==="
STAMPS=$(grep -v '^\s*$' "$STAMP_LOG" | sort -u | tr '\n' ' ')
log "登记簿 stamp 数：$(echo $STAMPS | wc -w)"
"$PY" training/make_merged_corpus.py --out data/boardmic_all_v32 --include $STAMPS 2>&1 | tee -a "$LOG" | tail -4
log "=== 合并完成，manifest: data/boardmic_all_v32/manifest.csv ==="
awk -F, 'NR>1 && $2==1' data/boardmic_all_v32/manifest.csv | wc -l | xargs log "最终正样本数:"
awk -F, 'NR>1 && $2!=1' data/boardmic_all_v32/manifest.csv | wc -l | xargs log "最终负样本数:"
