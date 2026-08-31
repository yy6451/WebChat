#!/usr/bin/env bash
# ============================================================
# Redis Cluster 集群初始化脚本 (任意一台 VM 执行一次)
# 用法: ./backend/scripts/cluster_create.sh 192.168.118.131 192.168.118.132 192.168.118.133
# ============================================================
set -euo pipefail

if [[ $# -lt 3 ]]; then
    echo "用法: $0 <VM1_IP> <VM2_IP> <VM3_IP>"
    echo "示例: $0 192.168.118.131 192.168.118.132 192.168.118.133"
    exit 1
fi

VM1=$1
VM2=$2
VM3=$3

echo "============================================"
echo " Redis Cluster 集群初始化"
echo " VM1: ${VM1}"
echo " VM2: ${VM2}"
echo " VM3: ${VM3}"
echo "============================================"

# ---------- 1) 检查所有节点可达 ----------
for vm in "$VM1" "$VM2" "$VM3"; do
    for port in 7000 7001; do
        if ! redis-cli -h "$vm" -p "$port" PING >/dev/null 2>&1; then
            echo "ERROR: 无法连接 ${vm}:${port}，请确保已执行 cluster_deploy_node.sh" >&2
            exit 1
        fi
        echo "  ✓ ${vm}:${port} 可达"
    done
done

# ---------- 2) 创建集群 ----------
# 前 3 个是 Master，后 3 个是 Replica
# --cluster-replicas 1 表示每个 Master 配 1 个 Replica
echo ""
echo "正在创建集群..."

redis-cli --cluster create \
    "${VM1}:7000" \
    "${VM2}:7000" \
    "${VM3}:7000" \
    "${VM1}:7001" \
    "${VM2}:7001" \
    "${VM3}:7001" \
    --cluster-replicas 1 \
    --cluster-yes

echo ""
echo "============================================"

# ---------- 3) 验证集群状态 ----------
echo "集群节点列表:"
redis-cli -h "$VM1" -p 7000 CLUSTER NODES

echo ""
echo "集群信息:"
redis-cli -h "$VM1" -p 7000 CLUSTER INFO | head -6

echo ""
echo "============================================"
echo " ✅ Redis Cluster 集群创建完成!"
echo ""
echo "项目中配置为:"
echo "  redis_host = \"${VM1}\""
echo "  redis_port = 7000"
echo ""
echo "验证命令:"
echo "  redis-cli -h ${VM1} -p 7000 CLUSTER INFO"
echo "  redis-cli -h ${VM1} -p 7000 -c SET test hello"
echo "  redis-cli -h ${VM1} -p 7000 -c GET test"
echo "============================================"
