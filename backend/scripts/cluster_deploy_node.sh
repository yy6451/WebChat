#!/usr/bin/env bash
# ============================================================
# Redis Cluster 单机部署脚本 (每台 VM 执行)
# 用法: ./backend/scripts/cluster_deploy_node.sh
# ============================================================
set -euo pipefail

REDIS_DATA_DIR="/opt/redis-cluster"
REDIS_PORTS=(7000 7001)

# 自动获取本机 IP（取第一个非 127.x 的地址）
MYIP=$(hostname -I | awk '{for(i=1;i<=NF;i++) if($i !~ /^127\./) {print $i; exit}}')
if [[ -z "$MYIP" ]]; then
    echo "ERROR: 无法获取本机 IP，请手动设置 MYIP 变量" >&2
    exit 1
fi

echo "============================================"
echo " Redis Cluster 节点部署"
echo " 本机 IP: $MYIP"
echo " 数据目录: $REDIS_DATA_DIR"
echo "============================================"

# ---------- 1) 创建数据目录 ----------
for port in "${REDIS_PORTS[@]}"; do
    sudo mkdir -p "${REDIS_DATA_DIR}/${port}"
    sudo chown -R "$(whoami):$(whoami)" "${REDIS_DATA_DIR}/${port}"
    echo "  ✓ 创建目录 ${REDIS_DATA_DIR}/${port}"
done

# ---------- 2) 生成配置文件 ----------
for port in "${REDIS_PORTS[@]}"; do
    BUS_PORT=$((port + 10000))
    cat > "${REDIS_DATA_DIR}/${port}/redis.conf" << EOF
# ======== 基础配置 ========
port ${port}
bind 0.0.0.0
protected-mode no
daemonize yes
dir ${REDIS_DATA_DIR}/${port}
pidfile ${REDIS_DATA_DIR}/${port}/redis.pid
logfile ${REDIS_DATA_DIR}/${port}/redis.log

# ======== Cluster 配置 ========
cluster-enabled yes
cluster-config-file ${REDIS_DATA_DIR}/${port}/nodes.conf
cluster-node-timeout 10000

# ======== Cluster Announce ========
cluster-announce-ip ${MYIP}
cluster-announce-port ${port}
cluster-announce-bus-port ${BUS_PORT}

# ======== 持久化 ========
appendonly yes
appendfilename "appendonly.aof"
appendfsync everysec

# RDB 关闭（学习环境）
save ""
EOF
    echo "  ✓ 生成配置 ${REDIS_DATA_DIR}/${port}/redis.conf"
done

# ---------- 3) 停止旧实例（如果存在） ----------
for port in "${REDIS_PORTS[@]}"; do
    if redis-cli -h "$MYIP" -p "$port" PING >/dev/null 2>&1; then
        echo "  ! 端口 ${port} 已有 Redis 在运行，先关闭..."
        redis-cli -h "$MYIP" -p "$port" SHUTDOWN NOSAVE 2>/dev/null || true
        sleep 1
    fi
done

# ---------- 4) 启动 Redis 实例 ----------
for port in "${REDIS_PORTS[@]}"; do
    redis-server "${REDIS_DATA_DIR}/${port}/redis.conf"
    sleep 0.5
    if redis-cli -h "$MYIP" -p "$port" PING >/dev/null 2>&1; then
        echo "  ✓ Redis :${port} 启动成功"
    else
        echo "  ✗ Redis :${port} 启动失败!" >&2
        exit 1
    fi
done

echo ""
echo "============================================"
echo " ✅ 本机部署完成"
echo " 节点: ${MYIP}:7000, ${MYIP}:7001"
echo "============================================"
echo ""
echo "下一步：在任意一台 VM 上执行集群初始化脚本:"
echo "  ./backend/scripts/cluster_create.sh <VM1_IP> <VM2_IP> <VM3_IP>"
echo ""
