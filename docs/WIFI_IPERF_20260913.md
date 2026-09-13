# Wi-Fi 到主机有线网卡 TCP 跑流

## 环境与结果

板端：FeatherTalk M55 / CYW5551x，串口 COM17；Wi-Fi IP 192.168.0.112，
网关 192.168.0.1。主机有线对端为 ASIX 千兆网卡“以太网 4”（接口28），
地址192.168.0.106、链路1Gbps。主机另有 WLAN 和隧道网卡，本次 socket
显式绑定192.168.0.106；板端连接日志确认了该对端地址。有线发送计数在5GHz
下行测试前后增加约11MB，与10.22MB TCP载荷量相符（含协议开销和其他流量）。

用户确认5GHz SSID为 Wakaka_5G（下划线）。密码通过隐藏输入传递，不写入代码或记录。
沿用现有country=AU配置，本次未更改国家码、主机路由或防火墙。

| AP | 信道 | 方向 | 时长 | 接收/传输字节 | TCP平均吞吐 |
|---|---:|---|---:|---:|---:|
| Wakaka | 6 | 有线主机→板子 | 12.128s | 5799936 | 3.826 Mbps |
| Wakaka | 6 | 板子→有线主机 | 15.024s | 1131973 | 0.603 Mbps |
| Wakaka_5G | 48 | 有线主机→板子 | 20.059s | 10223616 | 4.077 Mbps |
| Wakaka_5G | 48 | 板子→有线主机 | 20.011s | 10749364 | 4.297 Mbps |

5GHz上行板端5秒窗口：4.326、4.343、4.274、4.290 Mbps。
测试结束仍连接Wakaka_5G，state=6/error=0，RSSI=-51dBm。
ft_radio显示Wi-Fi和BT均claimed=1/state=3/error=0，reset计数各1；
此为结束状态快照，不等同于完整并发LE音频/无线隔离压力验收。
所有本次启动的iperf测试已停止，主机TCP监听已关闭。Wi-Fi保留5GHz关联。

## 测试实现与复现

SDK源码位于 projects/Edgi_Talk_M55_WIFI/packages/netutils-latest/iperf/iperf.c。
这是RT-Thread的iperf-like实现：TCP客户端循环send 4096字节，接收端循环recv；
不是iperf3控制协议，也不应把本次结果当作标准iperf3认证结果。
主机使用对应的原始TCP收发器和板端iperf互测，分别测量上下行；不是同时全双工。

- 连接助手：tools/freather/tests/wifi_join.py --ssid Wakaka_5G
- 下行：tools/freather/test-wifi-tcp.py --serial COM17 --board 192.168.0.112
  --bind 192.168.0.106 --seconds 20 --log ./tmp/wifi-wakaka5g-wired-tcp.jsonl
- 上行：tools/freather/tests/wifi_tcp_uplink.py --bind 192.168.0.106
  --board 192.168.0.112 --seconds 20
- Python使用tools/freather/serial-monitor/python/python.exe。
- 下行原始记录：tmp/wifi-wakaka-wired-tcp.jsonl、tmp/wifi-wakaka5g-wired-tcp.jsonl。
- 最终状态：tmp/wifi-iperf-final.log。临时日志不提交。

## 待排查

1. 首次扫描虽返回4个AP，结束状态却是error=-116；随后两次关联/DHCP均成功。
2. 2.4GHz上行偏低，期间出现WHD buffer request ID不匹配/IOCTL retry。
   不能仅凭一次短测认定射频或驱动根因，需要复测及SDIO/WHD统计。
3. 5GHz双向约4Mbps，尚未测得芯片最大能力；需要检查SDIO、TCP窗口和调度。
4. wifi status显示DataRate=0Mbps，但TCP实测有流量；这个字段目前不能用于推算PHY速率。
5. 本次未改固件、未提交推送；仅增加和改进本机诊断脚本。
