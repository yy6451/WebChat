#!/usr/bin/env bash
# ============================================================
# Nginx 四层 TCP 代理配置脚本
# 用途: 自动配置 Nginx stream 模块，作为 WebSocket/HTTP 反向代理
# 用法: sudo ./backend/scripts/nginx_setup.sh
#       sudo ./backend/scripts/nginx_setup.sh least_conn  # 指定负载均衡算法
#
# 支持的算法:
#   least_conn           — 最少连接数（推荐，测试环境首选）
#   hash                 — IP 哈希（生产环境，同 IP 粘在同一台服务器）
#   round_robin          — 轮询（默认，无状态场景适用）
# ============================================================
set -euo pipefail

ALGORITHM="${1:-least_conn}"
BACKENDS=(
    "192.168.118.131:9007"
    "192.168.118.133:9007"
    "192.168.118.134:9007"
)
CONF_FILE="/etc/nginx/stream.conf"
NGINX_CONF="/etc/nginx/nginx.conf"

echo "============================================"
echo " Nginx TCP Stream 代理配置"
echo " 算法: ${ALGORITHM}"
echo " 后端: ${BACKENDS[*]}"
echo "============================================"

# ---------- 1) 生成 upstream 配置 ----------
UPSTREAM_LINES=""
case "$ALGORITHM" in
    least_conn)
        UPSTREAM_LINES="        least_conn;"
        ;;
    hash)
        UPSTREAM_LINES="        hash \$remote_addr consistent;"
        ;;
    round_robin)
        UPSTREAM_LINES="        # round-robin (默认，无需显式声明)"
        ;;
    *)
        echo "错误: 不支持的算法 '${ALGORITHM}'，可选: least_conn | hash | round_robin" >&2
        exit 1
        ;;
esac

# ---------- 2) 写入 stream.conf ----------
sudo tee "$CONF_FILE" > /dev/null << EOF
# ============================================================
# Nginx 四层 TCP/UDP 代理配置
#
# 为什么用 stream (四层) 而不是 http (七层)?
#   - WebSocket 升级握手需要透传，七层 HTTP 代理会断开连接
#   - Stream 模块工作在 TCP 层，直接转发字节流，兼容 WS
#
# 为什么默认 least_conn?
#   - hash \$remote_addr: 同 IP 固定一台后端（生产推荐, WebSocket 粘滞）
#   - least_conn: 新连接分给活跃连接数最少的后端（测试/开发推荐）
#   - round_robin: 轮询均分
# ============================================================
stream {
    upstream chat_backend {
        ${UPSTREAM_LINES}

$(for b in "${BACKENDS[@]}"; do echo "        server ${b};"; done)
    }

    server {
        # 监听 80 端口，代理到后端 9007
        # backlog=65535: 调大 TCP 监听队列，支撑高并发连接
        listen 80 backlog=65535;

        proxy_pass chat_backend;

        # 连接超时: 10 秒
        proxy_connect_timeout 10s;

        # 读写超时: 10 分钟 (WebSocket 长连接)
        proxy_timeout 600s;
    }
}
EOF

echo "  ✓ 配置文件已写入 ${CONF_FILE}"

# ---------- 2.5) 调大 worker_connections (Nginx 并发连接上限) ----------
if grep -q "worker_connections" "$NGINX_CONF"; then
    sudo sed -i 's/worker_connections [0-9]*;/worker_connections 65535;/' "$NGINX_CONF"
    echo "  ✓ worker_connections 已调至 65535"
fi

# ---------- 3) 禁用默认 HTTP 站点（80 端口冲突） ----------
if [ -f /etc/nginx/sites-enabled/default ]; then
    sudo rm -f /etc/nginx/sites-enabled/default
    echo "  ✓ 已禁用默认 HTTP 站点 (解决 80 端口冲突)"
fi

# ---------- 4) 确保 nginx.conf 包含 stream.conf ----------
if grep -q "include ${CONF_FILE}" "$NGINX_CONF"; then
    echo "  ✓ ${NGINX_CONF} 已包含 stream.conf"
else
    echo "include ${CONF_FILE};" | sudo tee -a "$NGINX_CONF" > /dev/null
    echo "  ✓ 已在 ${NGINX_CONF} 末尾追加 include"
fi

# ---------- 5) 测试配置并重启 ----------
if sudo nginx -t 2>/dev/null; then
    echo "  ✓ Nginx 语法检查通过"

    # 先彻底停掉旧进程, 再启动 (systemd restart 可能无法解绑端口)
    sudo killall -9 nginx 2>/dev/null || true
    sleep 1

    if sudo systemctl start nginx 2>/dev/null; then
        echo "  ✓ Nginx 启动成功"
    else
        echo "  ✗ Nginx systemctl 启动失败, 尝试直接启动..."
        sudo nginx && echo "  ✓ Nginx 直接启动成功"
    fi
else
    echo "  ✗ Nginx 语法检查失败, 查看 /etc/nginx/stream.conf" >&2
    exit 1
fi

# ---------- 6) 验证 ----------
sleep 1
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" http://127.0.0.1:80/ 2>/dev/null || echo "000")
if [ "$HTTP_CODE" = "200" ]; then
    echo "  ✓ 代理验证成功 (HTTP ${HTTP_CODE})"
else
    echo "  ✗ 代理验证失败 (HTTP ${HTTP_CODE})" >&2
fi

echo ""
echo "============================================"
echo " ✅ Nginx 配置完成"
echo " 用户访问: http://192.168.118.131"
echo ""
echo " 管理命令:"
echo "   sudo nginx -t                     # 测试配置"
echo "   sudo systemctl reload nginx       # 热重载"
echo "   sudo systemctl restart nginx      # 重启"
echo "   sudo nginx -T | grep -A 20 stream # 查看完整 stream 配置"
echo ""
echo " 切换算法:"
echo "   sudo ./backend/scripts/nginx_setup.sh least_conn"
echo "   sudo ./backend/scripts/nginx_setup.sh hash"
echo "============================================"
