"""kws_listen.py — 让板子进入监听并实时把串口日志写入文件（供真人喊话验证）

用法：后台运行，持续 CAPTURE_SEC 秒；用户对着板子说「小麦小麦」。
日志：toolchain/logs/kws_listen_<日期>.log（脚本同级自动创建，可再生、不入库）
"""
import serial, time, sys, os, datetime

PORT = 'COM9'
DATE = datetime.datetime.now().strftime('%Y%m%d')
LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'logs')
os.makedirs(LOG_DIR, exist_ok=True)
LOG = os.path.join(LOG_DIR, f'kws_listen_{DATE}.log')
CAPTURE_SEC = 600

s = serial.Serial(PORT, 115200, timeout=1)
s.dtr = True; s.rts = True
time.sleep(1.0)
s.reset_input_buffer()
s.write(b'\r\n'); time.sleep(1.5)
s.write(b'kws start\r\n')

f = open(LOG, 'a', encoding='utf-8')
f.write(f"\n===== listen start {datetime.datetime.now()} =====\n"); f.flush()

end = time.time() + CAPTURE_SEC
try:
    while time.time() < end:
        line = s.readline()
        if line:
            t = line.decode('utf-8', 'replace')
            sys.stdout.write(t); sys.stdout.flush()
            f.write(t); f.flush()
finally:
    try:
        s.write(b'kws stop\r\n')
    except Exception:
        pass
    time.sleep(0.3)
    s.close(); f.close()
    print(f"\n===== listen end {datetime.datetime.now()} =====")
