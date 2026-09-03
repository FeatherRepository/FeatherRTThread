#!/usr/bin/env bash
# edge-env.sh — Edgi-Talk 调试环境状态管理（消除残余现场，杜绝绕弯子）
#
# 铁律（本文件即执行标准）：
#   1. 任何 OpenOCD 操作前必须先 clean（按 PID 精确清理孤儿进程）
#   2. 烧录 FeatherTalk 合并镜像只走 OpenOCD；PyOCD 仅作恢复手段
#      （实测 pyocd 烧合并镜像会把启动链搞 wedge，OpenOCD 重刷才恢复）
#   3. 目标锁死（DP 不应答/双fault lockup）时软件复位 = kitprog3 断电上电
#   4. 后台 OpenOCD 服务器一律经 server-start/server-stop 管理（PID 文件）
#   5. 【互斥锁】同一时间只允许一个 OpenOCD 会话（多实例双开会 wedge 探针，
#      实测）。所有入口（flash/probe/server-*）自动持锁；手动跑 openocd 是
#      违规操作，必须改走 `edge-env.sh probe <script.tcl>`
#   6. 【自动恢复】acquire 失败（目标死循环复位撞断 SWD / 探针残余状态）时
#      自动做 check 式恢复并重试，不再手动救
#
# 用法（在 Git Bash 中）:
#   tools/freather/edge-env.sh clean
#   tools/freather/edge-env.sh check
#   tools/freather/edge-env.sh powercycle
#   tools/freather/edge-env.sh flash <Project> [Image]
#   tools/freather/edge-env.sh probe <script.tcl> [guard]   # 一次性调试脚本(带锁+恢复)
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
LOCK_DIR="$ROOT/tools/freather/logs/openocd.lock"
LOCK_MAX_AGE_S=600   # 锁超过 10 分钟视为死锁残留(崩溃), 自动回收

# --- 互斥锁 (mkdir 原子; 持有者 PID + 时间戳; 防双开 wedge 探针) ---
oocd_lock() {
    local tries=0 pid mtime now
    while ! mkdir "$LOCK_DIR" 2>/dev/null; do
        pid=$(cat "$LOCK_DIR/pid" 2>/dev/null | tr -d '\r')
        mtime=$(stat -c %Y "$LOCK_DIR/pid" 2>/dev/null || echo 0)
        now=$(date +%s)
        if [ -n "$pid" ] && ! kill -0 "$pid" 2>/dev/null; then
            echo "lock: 持有者 $pid 已死, 回收锁" >&2
            rm -rf "$LOCK_DIR"; continue
        fi
        if [ $((now - mtime)) -gt $LOCK_MAX_AGE_S ]; then
            echo "lock: 锁超龄 ($((now - mtime))s), 回收" >&2
            rm -rf "$LOCK_DIR"; continue
        fi
        tries=$((tries + 1))
        if [ $tries -gt 60 ]; then
            echo "lock: 等待超时(60s), 持锁者 pid=$pid" >&2
            return 1
        fi
        sleep 1
    done
    echo "$$" > "$LOCK_DIR/pid"
    trap 'rm -rf "$LOCK_DIR"' EXIT
    return 0
}

oocd_unlock() {
    rm -rf "$LOCK_DIR" 2>/dev/null
    trap - EXIT
}

# --- acquire 自动恢复重试: 目标死循环复位会撞断 SWD acquire,
#     探针残余状态会让下一次 acquire 失败; check 式 init/shutdown 可恢复 ---
oocd_run_recover() {
    # $@ = oocd_base 参数 (须含 init 及后续); 输出原文到 stdout
    local attempt out
    for attempt in 1 2 3; do
        out=$(oocd_base "$@" 2>&1)
        if echo "$out" | grep -q "failed to acquire"; then
            echo "oocd: acquire 失败, 恢复重试 ($attempt/3)..." >&2
            oocd_base -c init -c shutdown >/dev/null 2>&1
            sleep 1
            continue
        fi
        printf '%s\n' "$out"
        return 0
    done
    printf '%s\n' "$out"
    echo "oocd: 3 轮 acquire 均失败, 探针需要物理重插" >&2
    return 1
}

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

    oocd_lock || return 1
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
            oocd_unlock
            return 0
        fi
        echo "$out" | grep -E "Error|error" | head -3
        echo "flash: 第 $attempt 轮失败，复位后重试"
    done
    echo "flash: 两轮均失败，检查探针/目标连线" >&2
    oocd_unlock
    return 1
}

# 一次性调试脚本入口: probe <script.tcl> [guard]
#   guard = 传任意值则在脚本前注入 feathertalk_prepare_cm33.tcl
#           (目标死循环/崩溃态时必须: 先 reset_halt 停核再跑脚本)
# 脚本内自行 shutdown。带互斥锁 + acquire 自动恢复 + 用完即 clean。
cmd_probe() {
    local script="${1:?用法: edge-env.sh probe <script.tcl> [guard]}"
    local guard="${2:-}"
    local args=(-c init)
    [ -n "$guard" ] && args+=(-f "$GUARD_TCL")
    args+=(-f "$script")

    oocd_lock || return 1
    cmd_clean
    oocd_run_recover "${args[@]}"
    local rc=$?
    cmd_clean
    oocd_unlock
    return $rc
}

cmd_server_start() {
    oocd_lock || return 1
    cmd_clean
    echo "server: 启动 OpenOCD GDB 服务器 (3333)..."
    oocd_base -f "$GUARD_TCL" -c init >/dev/null 2>&1 &
    local pid=$!
    echo "$pid" > "$PID_FILE"
    sleep 4
    if kill -0 "$pid" 2>/dev/null; then
        echo "server: 运行中 (shell pid $pid, 已登记 PID 文件)"
        echo "server: 用完必须 server-stop (锁由 stop 释放)"
    else
        echo "server: 启动失败"; rm -f "$PID_FILE"; oocd_unlock; return 1
    fi
}

cmd_server_stop() {
    cmd_clean   # PID 文件与进程一并清
    oocd_unlock
    echo "server: 已停止并清理"
}

case "${1:-}" in
    clean)       cmd_clean ;;
    check)       cmd_check ;;
    powercycle)  cmd_powercycle ;;
    flash)       shift; cmd_flash "$@" ;;
    probe)       shift; cmd_probe "$@" ;;
    server-start) cmd_server_start ;;
    server-stop)  cmd_server_stop ;;
    gdb-status)   cmd_check ;;
    *)
        echo "用法: $0 clean|check|powercycle|flash <Project> [Image]|probe <script.tcl> [guard]|server-start|server-stop"
        exit 1 ;;
esac
