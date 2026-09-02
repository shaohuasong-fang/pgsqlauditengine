#!/bin/bash
# -------------------------------------------------------------------------
# verify_local.sh
#
# pgsqlauditengine 跨版本（PG11-18）本地编译冒烟脚本。
# 遍历本地可用的 PostgreSQL pg_config，逐个执行
#   make clean && make USE_PGXS=1 PG_CONFIG=<pg_config>
# 逐版本记录 PASS/FAIL 与编译错误（含文件/行号），任一版本失败不阻断其他版本。
#
# 用法:
#   scripts/verify_local.sh [pg_config1 [pg_config2 ...]]
# 不带参数时自动探测本地已知安装路径中的 pg_config。
#
# 输出: scripts/verify_report_<版本>.log 与汇总 verify_local_summary.txt
# -------------------------------------------------------------------------

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPORT_DIR="$SCRIPT_DIR/verify_report"
SUMMARY="$SCRIPT_DIR/verify_local_summary.txt"
mkdir -p "$REPORT_DIR"

# 已知安装路径（按需追加）
CANDIDATES=(
  /usr/local/postgresql-11.22/bin/pg_config
  /usr/local/postgresql-12.22/bin/pg_config
  /usr/local/postgresql-13.23/bin/pg_config
  /usr/local/postgresql-14.22/bin/pg_config
  /usr/local/postgresql-15.17/bin/pg_config
  /usr/local/postgresql-16.13/bin/pg_config
  /usr/local/postgresql-17.9/bin/pg_config
  /usr/local/postgresql-18.4/bin/pg_config
  /usr/local/pgversion/18/pgsql/bin/pg_config
  /usr/local/pgsql/bin/pg_config
)

if [ "$#" -gt 0 ]; then
  PGCONFIGS=("$@")
else
  PGCONFIGS=()
  for c in "${CANDIDATES[@]}"; do
    [ -x "$c" ] && PGCONFIGS+=("$c")
  done
fi

if [ "${#PGCONFIGS[@]}" -eq 0 ]; then
  echo "错误: 未发现任何可用 pg_config（本地可能未安装 PostgreSQL 或 PGXS 不完整）。"
  echo "提示: 可显式传入 pg_config 路径，例如:"
  echo "  scripts/verify_local.sh /usr/local/pgsql/bin/pg_config"
  exit 1
fi

echo "=== 本地多版本编译冒烟汇总 ===" > "$SUMMARY"

FAILED=0
for pgc in "${PGCONFIGS[@]}"; do
  ver=$("$pgc" --version 2>/dev/null | awk '{print $2}')
  ver_tag=${ver%%.*}
  log="$REPORT_DIR/verify_report_${ver}.log"

  {
    echo "### 版本: $ver  (pg_config: $pgc)"
    echo "时间: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "---"
  } > "$log"

  # 确认 PGXS 可用性
  pgxs=$("$pgc" --pgxs 2>/dev/null)
  if [ -z "$pgxs" ] || [ ! -f "$pgxs" ]; then
    echo "RESULT: FAIL (PGXS 不可用: $pgxs)" | tee -a "$log"
    echo "FAIL $ver  PGXS 不可用" >> "$SUMMARY"
    FAILED=1
    continue
  fi

  ( cd "$SRC_DIR" && make clean >/dev/null 2>&1
    make USE_PGXS=1 PG_CONFIG="$pgc" ) >> "$log" 2>&1

  if [ $? -eq 0 ] && [ -f "$SRC_DIR/pgsqlauditengine.so" ]; then
    echo "RESULT: PASS" | tee -a "$log"
    echo "PASS $ver" >> "$SUMMARY"
  else
    echo "RESULT: FAIL" | tee -a "$log"
    grep -nE "error:|错误|No such file" "$log" | head -20 >> "$SUMMARY"
    echo "FAIL $ver  见 $log" >> "$SUMMARY"
    FAILED=1
  fi
done

echo
echo "=== 汇总 ==="
cat "$SUMMARY"
echo
echo "详细日志位于: $REPORT_DIR/"
exit $FAILED