#!/bin/bash
LOG=/root/pgsqlauditengine_src/verify/api_test_pg18.txt
export PGPASSWORD=postgres
PGBIN=/usr/local/pgversion/18/pgsql/bin
PSQLBIN=$PGBIN/psql
PGCTL=$PGBIN/pg_ctl
PSQL="$PSQLBIN -h 127.0.0.1 -p 5439 -U postgres -d postgres"
BASE=http://127.0.0.1:8918/api/v1

rec() {
  local title="$1"; shift
  {
    echo ""
    echo "===== $title ====="
    echo "--- 命令 ---"
    echo "$*"
    echo "--- 返回 ---"
    "$@" 2>&1
    echo ""
  } >> "$LOG"
}

wait_api() {
  for i in $(seq 1 30); do
    if curl -s --max-time 2 $BASE/health >/dev/null 2>&1; then return 0; fi
    sleep 1
  done
  return 1
}

restart() {
  su - postgres -c "$PGCTL restart -D /data/18/data -o \"-p 5439\" -l /data/18/data/server.log"
}

# ---- S11-1 token 为空态（当前）免鉴权 ----
rec "S11-1 GET /rules 无鉴权头 (token 空 -> 200)" curl -s $BASE/rules

# ---- S2-2 enabled=off 切换（实测 reload 不更新 API bgworker，需重启生效）----
$PSQL -c "ALTER SYSTEM SET PGSAUDAUDITENGINE.enabled = off;" >> "$LOG" 2>&1
$PSQL -c "SELECT pg_reload_conf();" >> "$LOG" 2>&1
sleep 2
rec "S2-2 GET /health (enabled=off 仅 reload, 偏差: 仍 true)" curl -s $BASE/health
rec "S2-2 重启实例使 enabled=off 生效" restart
sleep 3
rec "S2-2 GET /health (enabled=off 重启后 -> false)" curl -s $BASE/health
$PSQL -c "ALTER SYSTEM RESET PGSAUDAUDITENGINE.enabled;" >> "$LOG" 2>&1
$PSQL -c "SELECT pg_reload_conf();" >> "$LOG" 2>&1
rec "S2-2 复位 enabled 并重启实例" restart
sleep 3
rec "S2-2 GET /health (enabled=on 重启后 -> true)" curl -s $BASE/health

# ---- S11-0 设置 token 并重启 ----
$PSQL -c "ALTER SYSTEM SET PGSAUDAUDITENGINE.api_token = 'test-token-123';" >> "$LOG" 2>&1
rec "S11-0 设置 api_token 并重启实例" restart
sleep 3
wait_api || { echo "API NOT UP after restart" >> "$LOG"; }

# ---- S7-2 空记录（重启后未执行任何语句）----
rec "S7-2 GET /audit-logs (重启后空态)" curl -s $BASE/audit-logs

# ---- S11-2/3/4/5 ----
rec "S11-2 GET /rules 正确 token (200)" curl -s -H 'Authorization: Bearer test-token-123' $BASE/rules
rec "S11-3 GET /rules 缺失 token (401)" curl -s $BASE/rules
rec "S11-4 GET /rules 错误 token (401)" curl -s -H 'Authorization: Bearer wrong-token' $BASE/rules
rec "S11-5 GET /health 免鉴权 (200)" curl -s $BASE/health

# ---- S11-6 复位 token 并重启 ----
$PSQL -c "ALTER SYSTEM RESET PGSAUDAUDITENGINE.api_token;" >> "$LOG" 2>&1
rec "S11-6 复位 api_token 并重启实例" restart
sleep 3
wait_api || { echo "API NOT UP after reset restart" >> "$LOG"; }
rec "S11-6 复位后 GET /rules 无鉴权 (200)" curl -s $BASE/rules

# ---- S5-8 批量循环复位 80 条 ----
rec "S5-8 批量逐条 PUT 复位 80 条 (无 PUT FAIL)" bash -c 'for id in $(curl -s '$BASE'/rules | grep -o "\"id\":\"[a-z_0-9]*\"" | sed "s/\"id\":\"//;s/\"//"); do def=$(curl -s '$BASE'/rules/$id); level=$(echo "$def" | grep -o "\"level\":\"[A-Z]*\"" | head -1 | sed "s/\"level\":\"//;s/\"//"); curl -s -X PUT -H "Content-Type: application/json" -d "{\"enabled\":true,\"level\":\"$level\"}" '$BASE'/rules/$id | grep -q "\"updated\":true" || echo "PUT FAIL $id"; done; echo BATCH-PUT-DONE'

# ---- S8-2 复位验收 ----
rec "S8-2 GET /config vs baseline diff (复位验收)" bash -c "curl -s $BASE/config | diff - /root/pgsqlauditengine_src/verify/baseline/config_pg18.json && echo DIFF-EMPTY || echo DIFF-NONEMPTY"

echo "DONE2" >> "$LOG"
wc -l "$LOG"
