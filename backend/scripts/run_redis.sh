#!/usr/bin/env bash
# ============================================================
# Redis 启动脚本 (支持两种模式)
#
# 模式 1 — 本地单节点开发 (默认):
#   ./backend/scripts/run_redis.sh
#   启动单节点 Redis Cluster @ 7000，自分配 slots
#
# 模式 2 — 三节点集群模式:
#   ./backend/scripts/run_redis.sh cluster
#   调用 cluster_deploy_node.sh 部署本机 7000+7001 实例
#   (需要先在另外两台 VM 上执行相同命令，然后执行 cluster_create.sh)
# ============================================================
set -euo pipefail

MODE="${1:-standalone}"

if [[ "$MODE" == "cluster" ]]; then
    # ---------- 三节点集群模式 ----------
    SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
    exec bash "${SCRIPT_DIR}/cluster_deploy_node.sh"
fi

# ---------- 本地单节点开发模式 ----------
PORT="${1:-7000}"
CONF_FILE="/tmp/nodes-${PORT}.conf"
DATA_DIR="/tmp/redis-data-${PORT}"

if redis-cli -p "$PORT" PING >/dev/null 2>&1; then
    echo "Redis Cluster port $PORT 已在运行"
    exit 0
fi

mkdir -p "$DATA_DIR"

echo "启动 Redis Cluster 单节点 (port $PORT) [开发模式]..."
redis-server \
    --port "$PORT" \
    --cluster-enabled yes \
    --cluster-config-file "$CONF_FILE" \
    --cluster-node-timeout 5000 \
    --appendonly no \
    --dir "$DATA_DIR" \
    --protected-mode no \
    --bind 0.0.0.0 \
    --daemonize yes

sleep 1

echo "分配 hash slots..."
redis-cli -p "$PORT" CLUSTER ADDSLOTS $(seq 0 16383) >/dev/null

echo "Redis Cluster 就绪 (端口 $PORT)"
echo "验证: redis-cli -p $PORT PING"
echo ""
echo "提示: 如需部署三节点集群，请在每台 VM 上执行:"
echo "  ./backend/scripts/run_redis.sh cluster"
echo "然后在任意一台 VM 上执行:"
echo "  ./backend/scripts/cluster_create.sh <VM1_IP> <VM2_IP> <VM3_IP>"
