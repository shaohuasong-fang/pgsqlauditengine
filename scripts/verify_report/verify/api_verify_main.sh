#!/bin/bash
LOG=/root/pgsqlauditengine_src/verify/api_test_pg18.txt
: > "$LOG"
export PGPASSWORD=postgres
PSQL='/usr/local/pgversion/18/pgsql/bin/psql -h 127.0.0.1 -p 5439 -U postgres -d postgres -tAc'
BASE=http://127.0.0.1:8918/api/v1

echo "===== 前置: 重建 se_t_dml（S5/S6/S7 触发语句依赖）====="
$PSQL "CREATE TABLE IF NOT EXISTS se_t_dml (id serial PRIMARY KEY, name text NOT NULL, amount numeric(12,2) NOT NULL, status varchar(20) NOT NULL);" >> "$LOG" 2>&1
$PSQL "INSERT INTO se_t_dml (name, amount, status) VALUES ('a', 1.00, 'ok'), ('b', 2.00, 'ok') ON CONFLICT DO NOTHING;" >> "$LOG" 2>&1

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

# ---------- S2-1 ----------
rec "S2-1 GET /api/v1/health (enabled=true)" curl -s $BASE/health

# ---------- S3 ----------
rec "S3-1 GET /api/v1/rules (全量 80 条)" curl -s $BASE/rules
rec "S3-1b GET /rules 条数核对(应 80)" bash -c "curl -s $BASE/rules | grep -o '\"id\"' | wc -l"
rec "S3-1c GET /rules vs baseline diff" bash -c "curl -s $BASE/rules | diff - /root/pgsqlauditengine_src/verify/baseline/rules_pg18.json && echo DIFF-EMPTY || echo DIFF-NONEMPTY"
rec "S3-2 批量逐一 GET /rules/{id} 核对(无 MISMATCH)" bash -c 'for id in $(curl -s '$BASE'/rules | grep -o "\"id\":\"[a-z_0-9]*\"" | sed "s/\"id\":\"//;s/\"//"); do curl -s '$BASE'/rules/$id | grep -q "\"id\":\"$id\"" || echo "MISMATCH $id"; done; echo BATCH-GET-DONE'

# ---------- S4 ----------
rec "S4-1 GET /rules/dml_left_fuzzy" curl -s $BASE/rules/dml_left_fuzzy
rec "S4-2 GET /rules/{6 豁免规则}" bash -c 'for id in table_comment_required type_date_time name_bak_prefix dml_select_star dml_delete_truncate prog_name; do curl -s '$BASE'/rules/$id; echo; done'
rec "S4-3 GET /rules/no_such_rule (404)" curl -s $BASE/rules/no_such_rule

# ---------- S5 ----------
rec "S5-1 PUT dml_left_fuzzy enabled=false" curl -s -X PUT -H 'Content-Type: application/json' -d '{"enabled":false}' $BASE/rules/dml_left_fuzzy
rec "S5-1 实时生效: 关闭后触发语句(应无 SQL审核输出)" bash -c "psql -h 127.0.0.1 -p 5439 -U postgres -d postgres -c \"SELECT * FROM se_t_dml WHERE name LIKE '%x';\" 2>&1; echo ---"
rec "S5-1 核对 GET /rules/dml_left_fuzzy (enabled=false)" curl -s $BASE/rules/dml_left_fuzzy
rec "S5-1 复位 PUT enabled=true level=ERROR" curl -s -X PUT -H 'Content-Type: application/json' -d '{"enabled":true,"level":"ERROR"}' $BASE/rules/dml_left_fuzzy

rec "S5-2 PUT dml_left_fuzzy level=WARNING" curl -s -X PUT -H 'Content-Type: application/json' -d '{"level":"WARNING"}' $BASE/rules/dml_left_fuzzy
rec "S5-2 核对 GET /rules/dml_left_fuzzy (level=WARNING)" curl -s $BASE/rules/dml_left_fuzzy
rec "S5-2 实时生效: WARNING 级触发语句(应输出 WARNING 非阻断)" bash -c "psql -h 127.0.0.1 -p 5439 -U postgres -d postgres -c \"SELECT * FROM se_t_dml WHERE name LIKE '%x';\" 2>&1; echo ---"
rec "S5-2 复位 PUT enabled=true level=ERROR" curl -s -X PUT -H 'Content-Type: application/json' -d '{"enabled":true,"level":"ERROR"}' $BASE/rules/dml_left_fuzzy

rec "S5-3 PUT dml_left_fuzzy enabled=true level=ERROR" curl -s -X PUT -H 'Content-Type: application/json' -d '{"enabled":true,"level":"ERROR"}' $BASE/rules/dml_left_fuzzy
rec "S5-3 核对 GET /rules/dml_left_fuzzy (默认态)" curl -s $BASE/rules/dml_left_fuzzy
rec "S5-3 实时生效: ERROR 级触发语句(应输出 ERROR 阻断)" bash -c "psql -h 127.0.0.1 -p 5439 -U postgres -d postgres -c \"SELECT * FROM se_t_dml WHERE name LIKE '%x';\" 2>&1; echo ---"

rec "S5-4 PUT /rules/no_such_rule (404)" curl -s -X PUT -H 'Content-Type: application/json' -d '{"enabled":false}' $BASE/rules/no_such_rule
rec "S5-5 PUT 缺请求体 (400)" curl -s -X PUT -H 'Content-Type: application/json' $BASE/rules/dml_left_fuzzy
rec "S5-6 PUT 缺 enabled/level 字段 (400)" curl -s -X PUT -H 'Content-Type: application/json' -d '{"foo":"bar"}' $BASE/rules/dml_left_fuzzy
rec "S5-7 PUT 非法级别 INFO (400)" curl -s -X PUT -H 'Content-Type: application/json' -d '{"level":"INFO"}' $BASE/rules/dml_left_fuzzy

# ---------- S6 ----------
rec "S6-1 DELETE /rules/dml_left_fuzzy (204)" curl -s -X DELETE $BASE/rules/dml_left_fuzzy
rec "S6-1 副作用核对 GET /rules/dml_left_fuzzy (enabled=false)" curl -s $BASE/rules/dml_left_fuzzy
rec "S6-1 行为核对: 关闭后触发语句(应无 SQL审核输出)" bash -c "psql -h 127.0.0.1 -p 5439 -U postgres -d postgres -c \"SELECT * FROM se_t_dml WHERE name LIKE '%x';\" 2>&1; echo ---"
rec "S6-1 复位 PUT enabled=true level=ERROR" curl -s -X PUT -H 'Content-Type: application/json' -d '{"enabled":true,"level":"ERROR"}' $BASE/rules/dml_left_fuzzy
rec "S6-2 DELETE /rules/no_such_rule (204, D8-2)" curl -s -X DELETE $BASE/rules/no_such_rule

# ---------- S7 ----------
rec "S7-1 先触发审核 (dml_left_fuzzy VIOLATION)" bash -c "psql -h 127.0.0.1 -p 5439 -U postgres -d postgres -c \"SELECT * FROM se_t_dml WHERE name LIKE '%x';\" 2>&1; echo ---"
rec "S7-1 GET /api/v1/audit-logs (结构匹配)" curl -s $BASE/audit-logs

# ---------- S8 ----------
rec "S8-1 GET /api/v1/config" curl -s $BASE/config
rec "S8-1b GET /config 条数核对(应 80)" bash -c "curl -s $BASE/config | grep -o '\"id\"' | wc -l"
rec "S8-1c GET /config vs baseline diff" bash -c "curl -s $BASE/config | diff - /root/pgsqlauditengine_src/verify/baseline/config_pg18.json && echo DIFF-EMPTY || echo DIFF-NONEMPTY"

# ---------- S9 ----------
rec "S9-1 PUT /api/v1/config (附加端点, D8-3)" curl -s -X PUT -H 'Content-Type: application/json' -d '{"enabled":false}' $BASE/config
rec "S9-1 不写库核对 GET /config (enabled 仍 true)" bash -c "curl -s $BASE/config | grep -o '\"enabled\":true' | head -1"

# ---------- S10 ----------
rec "S10-1 畸形请求行 (nc, 400)" bash -c "printf 'GARBAGE\r\n\r\n' | nc 127.0.0.1 8918"
rec "S10-2 POST /rules/dml_left_fuzzy (405)" curl -s -X POST -H 'Content-Type: application/json' -d '{}' $BASE/rules/dml_left_fuzzy
rec "S10-3 GET /health 无前缀 (404)" curl -s http://127.0.0.1:8918/health
rec "S10-4 GET /api/v1/unknown (404)" curl -s $BASE/unknown

echo "DONE" >> "$LOG"
wc -l "$LOG"