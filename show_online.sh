#!/usr/bin/env bash
# 查所有在线用户及所在服务器（跨 Redis Cluster 节点扫描）
# 用法: ./show_online.sh [redis_host] [redis_port]
set -euo pipefail

REDIS_HOST="${1:-127.0.0.1}"
REDIS_PORT="${2:-7000}"
PATTERN='online:server:*'

echo "============================================"
echo " 在线用户查询"
echo " Redis: ${REDIS_HOST}:${REDIS_PORT}"
echo "============================================"
echo ""

total=0
online_users=""
vm_count=0

# 获取所有 Master 节点
declare -a masters
while read -r node; do
    ip=$(echo "$node" | cut -d: -f1)
    port=$(echo "$node" | cut -d: -f2)
    masters+=("$ip:$port")
done < <(redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" CLUSTER NODES 2>/dev/null \
    | grep master \
    | awk '{print $2}' \
    | cut -d@ -f1)

if [[ ${#masters[@]} -eq 0 ]]; then
    # 不是集群模式，直接扫描
    echo "  (单节点模式)"
    while read -r key; do
        [[ -z "$key" ]] && continue
        user=$(echo "$key" | cut -d'{' -f2 | cut -d'}' -f1)
        server=$(redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" GET "$key" 2>/dev/null)
        echo "  ${user}  →  ${server}"
        ((total++)) || true
    done < <(redis-cli -h "$REDIS_HOST" -p "$REDIS_PORT" --scan --pattern "$PATTERN" 2>/dev/null)
else
    # 集群模式：逐 Master 扫描
    for node in "${masters[@]}"; do
        ip="${node%:*}"
        port="${node#*:}"
        echo "── ${node} ──"
        count=0
        while read -r key; do
            [[ -z "$key" ]] && continue
            user=$(echo "$key" | cut -d'{' -f2 | cut -d'}' -f1)
            server=$(redis-cli -h "$ip" -p "$port" GET "$key" 2>/dev/null)
            echo "  ${user}  →  ${server}"
            ((count++)) || true
            ((total++)) || true
        done < <(redis-cli -h "$ip" -p "$port" --scan --pattern "$PATTERN" 2>/dev/null)
        if [[ $count -eq 0 ]]; then
            echo "  (无)"
        fi
        ((vm_count++)) || true
    done
fi

echo ""
echo "============================================"
echo " 总计: ${total} 个在线用户 (扫描 ${vm_count:-1} 个节点)"
echo "============================================"
