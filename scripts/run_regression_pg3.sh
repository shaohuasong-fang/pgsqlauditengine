#!/bin/bash
# =============================================================================
# run_regression_pg3.sh —— 任务组 28 八版本全链路回归编排（阶段三）
# -----------------------------------------------------------------------------
# 流程（design 4.2.5）：编译 -> 安装 -> 重启 -> CREATE EXTENSION
#   -> 裁剪脚本三段式执行（run_regression_pg.sh，含清理段受控）-> 归档
# 用法: ./run_regression_pg3.sh [版本列表，默认 11..18]
#   例: ./run_regression_pg3.sh 11 12 13   （分片执行，避免单次超时）
# 归档: scripts/verify_report/verify/ 下
#   build_pg<v>.log / install_pg<v>.log / restart_pg<v>.log / extension_pg<v>.log
#   out_test_pg<v>.log / audit_pg<v>.txt / rules_pg<v>.json / run_meta_pg<v>.log
# =============================================================================
set -u

SRC=/root/pgsqlauditengine_src/pgsqlauditengine
SCRIPTS=$SRC/scripts
OUT=$SCRIPTS/verify_report/verify
mkdir -p "$OUT"

declare -A PGC PGBIN DATA PORT API
for v in 11 12 13 14 15 16 18; do
  PGC[$v]=/usr/local/pgversion/$v/pgsql/bin/pg_config
  PGBIN[$v]=/usr/local/pgversion/$v/pgsql/bin
  DATA[$v]=/data/$v/data
  PORT[$v]=543$v
  API[$v]=89$v
done
PGC[17]=/usr/local/pgsql/bin/pg_config
PGBIN[17]=/usr/local/pgsql/bin
DATA[17]=/data/17/pgdata
PORT[17]=5432
API[17]=8917

if [ "$#" -gt 0 ]; then
  VERS=("$@")
else
  VERS=(11 12 13 14 15 16 17 18)
fi

for v in "${VERS[@]}"; do
  echo "===== [PG$v] 编译安装阶段 ====="
  cd "$SRC" || { echo "[PG$v] 无法进入源码目录"; continue; }
  rm -f *.o *.so
  make USE_PGXS=1 PG_CONFIG="${PGC[$v]}" > "$OUT/build_pg$v.log" 2>&1
  if grep -qE "error:|Error [0-9]" "$OUT/build_pg$v.log"; then
    echo "[PG$v] 编译失败（BLOCKER，见 build_pg$v.log），跳过后续"
    continue
  fi
  echo "[PG$v] 编译通过"
  make USE_PGXS=1 PG_CONFIG="${PGC[$v]}" install > "$OUT/install_pg$v.log" 2>&1
  echo "[PG$v] 安装完成"

  echo "===== [PG$v] 重启实例 + 扩展 ====="
  su - postgres -c "${PGBIN[$v]}/pg_ctl -D ${DATA[$v]} restart -m fast -w" > "$OUT/restart_pg$v.log" 2>&1
  sleep 2
  PGPASSWORD=postgres "${PGBIN[$v]}/psql" -h 127.0.0.1 -p "${PORT[$v]}" -U postgres -d postgres \
    -c "CREATE EXTENSION IF NOT EXISTS pgsqlauditengine;" > "$OUT/extension_pg$v.log" 2>&1
  echo "[PG$v] 扩展就绪"

  echo "===== [PG$v] 裁剪脚本三段式执行（WARNING 拦截验证）====="
  TEST_SQL="$SCRIPTS/pgsqlauditengine_test_pg$v.sql" bash "$SCRIPTS/run_regression_pg.sh" "$v" > "$OUT/run_meta_pg$v.log" 2>&1

  echo "===== [PG$v] 归档 ====="
  cp /tmp/reg_pg$v.log "$OUT/out_test_pg$v.log"
  grep 'SQL审核' "$OUT/out_test_pg$v.log" > "$OUT/audit_pg$v.txt"
  for attempt in 1 2 3 4 5; do
    curl -s -o "$OUT/rules_pg$v.json" \
      -w "rules 快照 http_code=%{http_code} size=%{size_download}\n" \
      "http://127.0.0.1:${API[$v]}/api/v1/rules" >> "$OUT/run_meta_pg$v.log" 2>&1
    if [ -s "$OUT/rules_pg$v.json" ]; then break; fi
    echo "[PG$v] rules 快照为空（第 $attempt 次），2s 后重试" >> "$OUT/run_meta_pg$v.log"
    sleep 2
  done
  echo "===== [PG$v] 完成（out_test_pg$v.log / audit_pg$v.txt / rules_pg$v.json 已归档）====="
done

echo "===== 全部版本回归完成 ====="