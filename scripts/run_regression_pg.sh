#!/bin/bash
# =============================================================================
# run_regression_pg.sh —— 八版本回归编排脚本（阶段三，任务组 26.2/28）
# -----------------------------------------------------------------------------
# 三段式规则状态受控执行模型（design 4.4.5.1 第 2 条、决策 D22）：
#   快照 -> 清理段关闭/降级 -> 用例段恢复 -> 还原
# 背景：WARNING 拦截语义（任务组 25）下，test.sql 清理段（PART 0/14）的
#   DROP SCHEMA/PUBLICATION/SUBSCRIPTION/FDW/LANGUAGE 等语句会被
#   ddl_high_risk_drop/obj_publication/obj_subscription/obj_fdw/obj_language
#   规则以 WARNING 拦截，导致对象残留、幂等性破坏。本脚本在执行清理段前
#   临时关闭上述规则，执行用例段（PART 1~13/15，WARNING 拦截需验证）前恢复，
#   结束后还原配置快照。
# 用法: ./run_regression_pg.sh <版本号 11-18> [--skip-clean]
#   --skip-clean : 跳过清理段受控（用于纯用例段验证）
# 环境变量: TEST_SQL=... 覆盖默认测试脚本（任务组 28 使用八版本裁剪脚本）
#   如 TEST_SQL=scripts/pgsqlauditengine_test_pg17.sql ./run_regression_pg.sh 17
# 依赖: RESTful API 端口可用（api_port=89<v>）；RESTful 不可用时（Q12 兜底）
#   本脚本打印警告并跳过受控操作，清理段语句可能被拦截（记录于日志）。
# =============================================================================
set -u

V="$1"
SKIP_CLEAN=0
[ "${2:-}" = "--skip-clean" ] && SKIP_CLEAN=1

case "$V" in
  11) PG_PORT=54311; API_PORT=8911; PG_BIN=/usr/local/pgversion/11/pgsql/bin/psql ;;
  12) PG_PORT=54312; API_PORT=8912; PG_BIN=/usr/local/pgversion/12/pgsql/bin/psql ;;
  13) PG_PORT=54313; API_PORT=8913; PG_BIN=/usr/local/pgversion/13/pgsql/bin/psql ;;
  14) PG_PORT=54314; API_PORT=8914; PG_BIN=/usr/local/pgversion/14/pgsql/bin/psql ;;
  15) PG_PORT=54315; API_PORT=8915; PG_BIN=/usr/local/pgversion/15/pgsql/bin/psql ;;
  16) PG_PORT=54316; API_PORT=8916; PG_BIN=/usr/local/pgversion/16/pgsql/bin/psql ;;
  17) PG_PORT=5432;  API_PORT=8917; PG_BIN=/usr/local/pgsql/bin/psql ;;
  18) PG_PORT=54318; API_PORT=8918; PG_BIN=/usr/local/pgversion/18/pgsql/bin/psql ;;
  *) echo "ERROR: 不支持的版本号 $V（支持 11-18）"; exit 1 ;;
esac

BASE="http://127.0.0.1:${API_PORT}/api/v1"
TEST_SQL="${TEST_SQL:-/root/pgsqlauditengine_src/pgsqlauditengine/scripts/pgsqlauditengine_test.sql}"
TMPDIR="/tmp/reg_pg$V"
LOG="/tmp/reg_pg$V.log"
SNAPSHOT="$TMPDIR/config_snapshot.json"
: > "$LOG"

mkdir -p "$TMPDIR"
PSQL="$PG_BIN -h 127.0.0.1 -p $PG_PORT -U postgres -d postgres -f"
export PGPASSWORD=postgres
# 供 test.sql 用例段内 `\! curl` 规则状态受控切换使用（任务组 26.3，见 PART 4 说明）
export SE_API_PORT="$API_PORT"

log() { echo "[$(date '+%H:%M:%S')] $*" | tee -a "$LOG"; }

# 清理段涉及的高危 WARNING 规则（design 4.4.5.1 第 2 条）
CLEAN_RULES="ddl_high_risk_drop obj_publication obj_subscription obj_fdw obj_language"

# --- 快照当前规则配置 ---
snapshot() {
  curl -s "$BASE/config" > "$SNAPSHOT"
  if grep -q '"code":200' "$SNAPSHOT"; then
    log "配置快照已保存: $SNAPSHOT"
  else
    log "WARN: 无法获取配置快照（RESTful 不可用？）——跳过受控操作（Q12 兜底）"
    return 1
  fi
}

# --- 关闭/降级清理段高危规则 ---
disable_clean_rules() {
  for id in $CLEAN_RULES; do
    curl -s -X PUT -H 'Content-Type: application/json' \
      -d '{"enabled":false}' "$BASE/rules/$id" > /dev/null 2>&1
    log "  临时关闭 $id"
  done
}

# --- 恢复全部规则为快照状态 ---
restore_snapshot() {
  local cnt=0
  while IFS='|' read -r id enabled level; do
    [ -z "$id" ] && continue
    curl -s -X PUT -H 'Content-Type: application/json' \
      -d "{\"enabled\":${enabled},\"level\":\"${level}\"}" \
      "$BASE/rules/$id" > /dev/null 2>&1
    cnt=$((cnt+1))
  done < <(python3 -c "
import json,sys
d=json.load(open('$SNAPSHOT'))
for r in d['data']['rules']:
    print(r['id']+'|'+('true' if r['enabled'] else 'false')+'|'+r['level'])
" 2>/dev/null || sed -n 's/.*"id":"\([a-z_0-9]*\)","enabled":\([a-z]*\),"level":"\([A-Z]*\)".*/\1|\2|\3/p' "$SNAPSHOT")
  log "配置快照还原完成（$cnt 条规则）"
}

# --- 将 test.sql 按段切分为 清理段0 / 用例段 / 清理段14 ---
split_test_sql() {
  awk -v d="$TMPDIR" '
    BEGIN { part="pre"; c0=""; cases=""; c14="" }
    /^-- PART 0:/ { part="p0"; next }
    /^-- PART 1:/ { part="cases"; next }
    /^-- PART 14:/ { part="p14"; next }
    /^-- PART 9:/ { part="p9"; next }
    /^-- PART 15:/ { part="cases"; next }
    /^-- RULE COVERAGE/ { part="cov"; next }
    {
      if (part=="p0") c0 = c0 $0 "\n";
      else if (part=="cases") cases = cases $0 "\n";
      else if (part=="p14" || part=="p9") c14 = c14 $0 "\n";
    }
    END {
      print "\\set VERBOSITY terse" > d "/seg_p0.sql";
      print c0 >> d "/seg_p0.sql";
      print "\\set VERBOSITY terse" > d "/seg_cases.sql";
      print cases >> d "/seg_cases.sql";
      print "\\set VERBOSITY terse" > d "/seg_p14.sql";
      print c14 >> d "/seg_p14.sql";
    }
  ' "$TEST_SQL"
  log "test.sql 已切分: seg_p0/seg_cases/seg_p14（$TMPDIR）"
}

# =============================================================================
log "===== PG$V 回归编排开始（端口 $PG_PORT / API $API_PORT）====="

if [ ! -f "$TEST_SQL" ]; then
  log "ERROR: $TEST_SQL 不存在"
  exit 1
fi

if ! snapshot; then
  RESTFUL_OK=0
else
  RESTFUL_OK=1
fi

split_test_sql

# ---- 第 1 段：前置清理（PART 0），清理段高危规则关闭下执行 ----
if [ "$SKIP_CLEAN" = "0" ] && [ "$RESTFUL_OK" = "1" ]; then
  disable_clean_rules
  log "--- 执行前置清理段（PART 0）---"
  $PSQL "$TMPDIR/seg_p0.sql" >> "$LOG" 2>&1
  restore_snapshot
else
  log "--- 跳过前置清理段受控（SKIP_CLEAN/RESTFUL 不可用），直接执行 PART 0 ---"
  $PSQL "$TMPDIR/seg_p0.sql" >> "$LOG" 2>&1
fi

# ---- 第 2 段：用例段（PART 1~13/15），规则全开（WARNING 拦截生效）----
log "--- 执行用例段（PART 1~13/15，WARNING 拦截验证）---"
$PSQL "$TMPDIR/seg_cases.sql" >> "$LOG" 2>&1

# ---- 第 3 段：后置清理（PART 14 + PART 9），清理段高危规则关闭下执行 ----
if [ "$SKIP_CLEAN" = "0" ] && [ "$RESTFUL_OK" = "1" ]; then
  disable_clean_rules
  log "--- 执行后置清理段（PART 14 + PART 9）---"
  $PSQL "$TMPDIR/seg_p14.sql" >> "$LOG" 2>&1
  restore_snapshot
else
  log "--- 跳过后续清理段受控，直接执行 PART 14 + PART 9 ---"
  $PSQL "$TMPDIR/seg_p14.sql" >> "$LOG" 2>&1
fi

log "===== PG$V 回归编排完成，日志: $LOG ====="