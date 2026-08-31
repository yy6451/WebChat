#!/usr/bin/env bash
# ============================================================
# 日志清理脚本 — 自动清理一周以前的 ServerLog
#
# 用法:
#   ./backend/scripts/cleanup_logs.sh              # 默认清理 7 天前
#   ./backend/scripts/cleanup_logs.sh 14           # 清理 14 天前
#   ./backend/scripts/cleanup_logs.sh -n           # 试运行(dry-run)，只列出不删除
#   ./backend/scripts/cleanup_logs.sh -n 14        # 试运行 + 自定义天数
#
# 匹配模式: logs/YYYY_MM_DD_ServerLog(.N)?  滚动日志一并清理
# ============================================================
set -euo pipefail

# ---------- 参数解析 ----------
DRY_RUN=false
RETENTION_DAYS=7

for arg in "$@"; do
    case "$arg" in
        -n|--dry-run) DRY_RUN=true ;;
        -h|--help)
            echo "用法: $0 [-n|--dry-run] [保留天数]"
            echo "  -n, --dry-run   试运行，只列出要删除的文件"
            echo "  保留天数        默认 7 天"
            exit 0
            ;;
        *) RETENTION_DAYS="$arg" ;;
    esac
done

# ---------- 路径 ----------
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
LOG_DIR="${PROJECT_ROOT}/logs"

if [[ ! -d "$LOG_DIR" ]]; then
    echo "日志目录不存在: $LOG_DIR"
    exit 1
fi

# ---------- 计算截止日期 ----------
CUTOFF_DATE=$(date -d "$RETENTION_DAYS days ago" +%Y%m%d)
echo "============================================"
echo " 日志清理工具"
echo " 保留天数: ${RETENTION_DAYS} 天 (截止 $CUTOFF_DATE)"
echo " 日志目录: $LOG_DIR"
[[ "$DRY_RUN" == true ]] && echo " 模式:     试运行 (不会删除任何文件)"
echo "============================================"

# ---------- 收集待删除文件 ----------
DELETED_COUNT=0
DELETED_SIZE=0

shopt -s nullglob
for logfile in "$LOG_DIR"/*_*_*_ServerLog*; do
    filename="$(basename "$logfile")"

    # 从文件名中提取日期: YYYY_MM_DD
    if [[ "$filename" =~ ^([0-9]{4})_([0-9]{2})_([0-9]{2})_ServerLog ]]; then
        year="${BASH_REMATCH[1]}"
        month="${BASH_REMATCH[2]}"
        day="${BASH_REMATCH[3]}"
        file_date="${year}${month}${day}"

        # 验证日期有效性
        if ! date -d "${year}-${month}-${day}" >/dev/null 2>&1; then
            echo "[跳过] 无效日期: $filename"
            continue
        fi

        if [[ "$file_date" -lt "$CUTOFF_DATE" ]]; then
            size=$(stat -c%s "$logfile" 2>/dev/null || echo 0)
            size_display=$(numfmt --to=iec "$size" 2>/dev/null || echo "${size}B")

            if [[ "$DRY_RUN" == true ]]; then
                echo "[试运行] 将删除: $filename (${size_display})"
            else
                rm -f "$logfile"
                echo "[已删除] $filename (${size_display})"
            fi
            ((DELETED_COUNT++)) || true
            ((DELETED_SIZE += size)) || true
        fi
    fi
done

# ---------- 汇总 ----------
echo "============================================"
if [[ "$DRY_RUN" == true ]]; then
    echo " 试运行完成，共找到 $DELETED_COUNT 个文件待删除"
else
    total_display=$(numfmt --to=iec "$DELETED_SIZE" 2>/dev/null || echo "${DELETED_SIZE}B")
    echo " 清理完成，共删除 $DELETED_COUNT 个文件，释放 ${total_display}"
fi
echo "============================================"
