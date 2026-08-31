#!/usr/bin/env bash
set -euo pipefail

BASE_URL=${BASE_URL:-http://127.0.0.1:9007}
USERNAME=${USERNAME:-alice}
PASSWORD=${PASSWORD:-123456}
FRIEND=${FRIEND:-bob}

echo "[1/4] 登录..."
LOGIN=$(curl -s -X POST "${BASE_URL}/api/login" -d "username=${USERNAME}&password=${PASSWORD}")
echo "$LOGIN"
TOKEN=$(printf '%s' "$LOGIN" | sed -n 's/.*"token":"\([^"]*\)".*/\1/p')
if [[ -z "$TOKEN" ]]; then
  echo "token 解析失败" >&2
  exit 1
fi

echo "[2/4] 未读统计(读取前)..."
curl -s "${BASE_URL}/api/friends" -H "Authorization: Bearer ${TOKEN}"
echo

echo "[3/4] 标记 ${FRIEND} 会话已读..."
curl -s -X POST "${BASE_URL}/api/readfriend" -H "Authorization: Bearer ${TOKEN}" -d "friend=${FRIEND}"
echo

echo "[4/4] 未读统计(读取后)..."
curl -s "${BASE_URL}/api/friends" -H "Authorization: Bearer ${TOKEN}"
echo