#!/bin/sh
# 板子上网：加载网卡 → 写配置 → 连热点 → 拿 IP。板子每次开机跑一遍。
# 用法（在电脑上）：adb shell "sh /root/wifi.sh"
# 板子上坏了就照《上板流程速查.txt》附录 2 重建，或重新推一次本文件。

killall wpa_supplicant 2>/dev/null      # 重复跑本脚本时，先清掉上一次的
sleep 1
grep -q '^8723bu' /proc/modules || insmod /lib/modules/4.9.88/extra/8723bu.ko

# 下面到 EOF 之间的内容建议顶格写：heredoc 里的缩进会原样写进配置文件
# （实测带着缩进 wpa_supplicant 也认，但顶格最保险）
cat > /tmp/wpa.conf <<'EOF'
ctrl_interface=/tmp/wpa_supplicant
update_config=1
network={
	ssid="sta"
	psk="88888888"
}
EOF

wpa_supplicant -B -i wlan0 -c /tmp/wpa.conf -D wext
udhcpc -i wlan0 -n -q -t 8 -T 3 || echo "!! 没拿到 IP —— 查 wpa_cli -p /tmp/wpa_supplicant status"
ifconfig wlan0 | grep "inet addr"
