#!/usr/bin/env bash
# edge-env.sh — Edgi-Talk 调试环境状态管理（消除残余现场，杜绝绕弯子）
#
# 铁律（本文件即执行标准）：
#   1. 任何 OpenOCD 操作前必须先 clean（按 PID 精确清理孤儿进程）
#   2. 烧录 FeatherTalk 合并镜像只走 OpenOCD；PyOCD 仅作恢复手段
#      （实测 pyocd 烧合并镜像会把启动链搞 wedge，OpenOCD 重刷才恢复）
#   3. 目标锁死（DP 不应答/双fault lockup）时软件复位 = kitprog3 断电上电
#   4. 后台 OpenOCD 服务器一律经 server-start/server-stop 管理（PID 文件）
#
# 用法（在 Git Bash 中）:
#   tools/freather/edge-env.sh clean
#   tools/freather/edge-env.sh check
#   tools/freather/edge-env.sh powercycle
#   tools/freather/edge-env.sh flash <Project> [Image]
#   tools/freather/edge-env.sh server-start / server-stop / gdb-status
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
PROBE_UID="${PROBE_UID:-1C180A11022F2400}"
OOCD="$ROOT/tools/freather/openocd/bin/openocd.exe"
OOCD_SCRIPTS="$ROOT/tools/freather/openocd/scripts"
OOCD_FLM="$ROOT/tools/freather/openocd/flm/infineon/pse8x6"
QSPI_CFG="$ROOT/projects/libs/TARGET_APP_KIT_PSE84_EVAL_EPC2/config/GeneratedSource"
GUARD_TCL="$ROOT/tools/freather/openocd/feathertalk_prepare_cm33.tcl"
PID_FILE="$ROOT/tools/freather/logs/openocd-server.pid"

oocd_base() {
    "$OOCD" -s "$OOCD_SCRIPTS" -s "$OOCD_FLM" -s "$QSPI_CFG" \
        -f interface/kitprog3.cfg -f target/infineon/pse84xgxs2.cfg \
        -c "adapter serial $PROBE_UID" -c "transport select swd" \
        -c "adapter speed 1000" "$@"
}

kill_stray_openocd() {
    # 只杀 openocd.exe（按 PID 精确清理，不误伤其他进程）
    powershell -NoProfile -Command \
        "Get-Process openocd -ErrorAction SilentlyContinue | ForEach-Object { \$_.Id }" \
        2>/dev/null | tr -d '\r' | while read -r pid; do
            [ -n "$pid" ] && powershell -NoProfile -Command "Stop-Process -Id $pid -Force" 2>/dev/null
        done
    sleep 1
}

cmd_clean() {
    local before after
    before=$(powershell -NoProfile -Command "(Get-Process openocd -ErrorAction SilentlyContinue | Measure-Object).Count" 2>/dev/null | tr -d '\r' | tail -1)
    kill_stray_openocd
    after=$(powershell -NoProfile -Command "(Get-Process openocd -ErrorAction SilentlyContinue | Measure-Object).Count" 2>/dev/null | tr -d '\r' | tail -1)
    rm -f "$PID_FILE" 2>/dev/null
    echo "clean: openocd 进程 ${before:-?} -> ${after:-0}"
}

cmd_check() {
    # 探针 USB 应答 + 目标 DP 应答，两级检查；输出结论行
    local out
    out=$(oocd_base -c init -c shutdown 2>&1)
    if echo "$out" | grep -q "CMSIS-DAP: FW Version"; then
        echo "check: 探针 OK (KitProg3 FW 2.x)"
    else
        echo "check: 探针 FAIL"; echo "$out" | tail -3; return 1
    fi
    if echo "$out" | grep -q "Cortex-M33 r1p0 processor detected"; then
        echo "check: 目标 OK (CM33 在线)"
        return 0
    else
        echo "check: 目标 FAIL (DP 无应答，需要 powercycle)"
        return 2
    fi
}

cmd_powercycle() {
    echo "powercycle: 目标断电 2s 后上电..."
    oocd_base -c init -c "kitprog3 power_control off" -c "sleep 2000" \
              -c "kitprog3 power_control on" -c "sleep 500" -c shutdown 2>&1 \
        | grep -E "powering|VTarget" | tail -2
    sleep 1
}

cmd_flash() {
    local project="${1:?用法: edge-env.sh flash <Project> [Image]}"
    local image="${2:-}"
    local args=(-Project "$project" -Programmer OpenOCD -AdapterKHz 1000 -ProbeUid "$PROBE_UID")
    [ -n "$image" ] && args+=(-Image "$image")
    local attempt out

    cmd_clean
    # 目标可能处于崩溃 wedge 状态: 先断电复位再烧录, 失败最多两轮
    for attempt in 1 2; do
        cmd_powercycle
        echo "flash: OpenOCD 烧录 $project (第 $attempt 轮)..."
        out=$(powershell -NoProfile -ExecutionPolicy Bypass -File \
                "$ROOT/tools/freather/flash-demo.ps1" "${args[@]}" 2>&1)
        if echo "$out" | grep -q "verified"; then
            echo "$out" | grep -E "wrote|verified"
            echo "flash: 烧录成功，断电冷启动..."
            cmd_powercycle
            return 0
        fi
        echo "$out" | grep -E "Error|error" | head -3
        echo "flash: 第 $attempt 轮失败，复位后重试"
    done
    echo "flash: 两轮均失败，检查探针/目标连线" >&2
    return 1
}

cmd_server_start() {
    cmd_clean
    echo "server: 启动 OpenOCD GDB 服务器 (3333)..."
    oocd_base -f "$GUARD_TCL" -c init >/dev/null 2>&1 &
    local pid=$!
    echo "$pid" > "$PID_FILE"
    sleep 4
    if kill -0 "$pid" 2>/dev/null; then
        echo "server: 运行中 (shell pid $pid, 已登记 PID 文件)"
    else
        echo "server: 启动失败"; rm -f "$PID_FILE"; return 1
    fi
}

cmd_server_stop() {
    cmd_clean   # PID 文件与进程一并清
    echo "server: 已停止并清理"
}

case "${1:-}" in
    clean)       cmd_clean ;;
    check)       cmd_check ;;
    powercycle)  cmd_powercycle ;;
    flash)       shift; cmd_flash "$@" ;;
    server-start) cmd_server_start ;;
    server-stop)  cmd_server_stop ;;
    gdb-status)   cmd_check ;;
    *)
        echo "用法: $0 clean|check|powercycle|flash <Project> [Image]|server-start|server-stop"
        exit 1 ;;
esac
