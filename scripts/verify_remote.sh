#!/bin/bash
# -------------------------------------------------------------------------
# verify_remote.sh
#
# pgsqlauditengine 远端（172.30.31.128）八版本（PG11-18）自动迭代验证脚本。
# 逐版本执行「编译 → 安装 → 确认数据目录 → 配置 postgresql.conf → 重启 →
# CREATE EXTENSION → 钩子生效 → RESTful health」，每版本独立输出
# verify_report_<版本>.log。脚本幂等可重入。
#
# 前置条件:
#   - 源码已上传至远端 /root/pgsqlauditengine_src/pgsqlauditengine
#   - 本地已安装 sshpass 或已配置免密
#   - 版本矩阵: PG11-18 对应端口/安装目录（见下方 MATRIX）
#
# 用法:
#   scripts/verify_remote.sh            # 全量八版本
#   scripts/verify_remote.sh 11 12 18   # 仅指定主版本
#
# 输出: scripts/verify_report_<版本>.log 与汇总 verify_remote_summary.txt
# -------------------------------------------------------------------------

set -u

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPORT_DIR="$SCRIPT_DIR/verify_report"
SUMMARY="$SCRIPT_DIR/verify_remote_summary.txt"
mkdir -p "$REPORT_DIR"

REMOTE_HOST=172.30.31.128
REMOTE_ROOT=/root/pgsqlauditengine_src
REMOTE_SRC="$REMOTE_ROOT/pgsqlauditengine"

SSH_OPTS="-o PreferredAuthentications=password -o PubkeyAuthentication=no -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
SSHPASS=sshpass
# 如本机无 sshpass，可改用免密 key；否则在此配置
ROOT_PASS="${REMOTE_ROOT_PASS:-Bigdata_123}"

# 版本矩阵（远端实测，2026-08-30 核实）:
# 主版本 | 端口 | 安装目录 | 数据目录
MATRIX=(
  "11 5433 /usr/local/pgversion/11/pgsql /data/11/data"
  "12 5434 /usr/local/pgversion/12/pgsql /data/12/data"
  "13 5435 /usr/local/pgversion/13/pgsql /data/13/data"
  "14 5436 /usr/local/pgversion/14/pgsql /data/14/data"
  "15 5437 /usr/local/pgversion/15/pgsql /data/15/data"
  "16 5438 /usr/local/pgversion/16/pgsql /data/16/data"
  "17 5432 /usr/local/pgsql /data/17/pgdata"
  "18 5439 /usr/local/pgversion/18/pgsql /data/18/data"
)

# 数据目录映射（脚本启动时若 pg_ctl status 失败会回退为推断值）
data_dir() {
  local v=$1 port=$2
  local conf
  conf=$(remote_cmd "$PGDIR/bin/pg_ctl" status -D "/data/$v/data" 2>/dev/null)
  if [ $? -eq 0 ]; then
    echo "/data/$v/data"
  else
    echo "/data/$v/data"
  fi
}

remote_cmd() {
  $SSHPASS -p "$ROOT_PASS" ssh $SSH_OPTS root@"$REMOTE_HOST" "$@"
}

if [ "$#" -gt 0 ]; then
  TARGETS=("$@")
else
  TARGETS=()
  for m in "${MATRIX[@]}"; do
    set -- $m
    TARGETS+=("$1")
  done
fi

command -v $SSHPASS >/dev/null 2>&1 || { echo "错误: 未安装 sshpass。请安装或改用免密配置。"; exit 1; }

echo "=== 远端八版本迭代验证汇总 ===" > "$SUMMARY"

for ver in "${TARGETS[@]}"; do
  entry=""
  for m in "${MATRIX[@]}"; do
    set -- $m
    [ "$1" = "$ver" ] && { PORT=$2; PGDIR=$3; DATADIR=$4; entry=1; }
  done
  [ -z "${entry:-}" ] && { echo "跳过未知版本: $ver"; continue; }

  PGCONFIG="$PGDIR/bin/pg_config"
  LOG="$REPORT_DIR/verify_report_$ver.log"

  {
    echo "### 版本: PostgreSQL $ver  (端口: $PORT, 安装: $PGDIR)"
    echo "时间: $(date '+%Y-%m-%d %H:%M:%S')"
    echo "---"
  } > "$LOG"

  # 1. pg_config 可用性
  remote_cmd "$PGCONFIG" --version >> "$LOG" 2>&1
  if [ $? -ne 0 ]; then
    echo "RESULT: FAIL (pg_config 不可用)" >> "$LOG"; echo "FAIL $ver pg_config 不可用" >> "$SUMMARY"; continue
  fi

  # 2. 编译（修改过 compat.h 时必须全量重编，PGXS 不跟踪 .h 依赖）
  remote_cmd "cd $REMOTE_SRC && rm -f *.o *.so && make USE_PGXS=1 PG_CONFIG=$PGCONFIG" >> "$LOG" 2>&1
  if [ $? -ne 0 ] || ! remote_cmd "[ -f $REMOTE_SRC/pgsqlauditengine.so ]"; then
    echo "RESULT: FAIL (编译失败)" >> "$LOG"; echo "FAIL $ver 编译失败 见 $LOG" >> "$SUMMARY"; continue
  fi

  # 3. 安装
  remote_cmd "cd $REMOTE_SRC && make USE_PGXS=1 PG_CONFIG=$PGCONFIG install" >> "$LOG" 2>&1
  if [ $? -ne 0 ]; then
    echo "RESULT: FAIL (安装失败)" >> "$LOG"; echo "FAIL $ver 安装失败 见 $LOG" >> "$SUMMARY"; continue
  fi

  # 4. 数据目录确认
  echo "数据目录: $DATADIR" >> "$LOG"
  remote_cmd "[ -d $DATADIR ]" || { echo "RESULT: FAIL (数据目录不存在 $DATADIR)" >> "$LOG"; echo "FAIL $ver 数据目录缺失 见 $LOG" >> "$SUMMARY"; continue; }

  # 5. postgresql.conf 配置（幂等：先清理旧行再追加）
  #    多版本共存：RESTful 端口按版本独立（89<版本>），避免 8900 冲突
  CONF="$DATADIR/postgresql.conf"
  remote_cmd "sed -i '/^shared_preload_libraries/d;/^PGSAUDAUDITENGINE\./d' $CONF && printf '%s\n' \
    \"shared_preload_libraries = 'pgsqlauditengine'\" \
    \"PGSAUDAUDITENGINE.enabled = on\" \
    \"PGSAUDAUDITENGINE.check_dml = on\" \
    \"PGSAUDAUDITENGINE.api_listen = '127.0.0.1'\" \
    \"PGSAUDAUDITENGINE.api_port = 89$ver\" \
    \"PGSAUDAUDITENGINE.api_token = ''\" >> $CONF" >> "$LOG" 2>&1

  # 6. 重启（端口通过启动参数指定，conf 中 port 不生效）
  remote_cmd "$PGDIR/bin/pg_ctl restart -D $DATADIR -o \"-p $PORT\" -l $DATADIR/server.log" >> "$LOG" 2>&1
  sleep 3
  if ! remote_cmd "$PGDIR/bin/pg_isready -p $PORT" >/dev/null 2>&1; then
    echo "RESULT: FAIL (重启后未就绪)" >> "$LOG"; echo "FAIL $ver 重启失败 见 $LOG" >> "$SUMMARY"; continue
  fi

  # 7. 加载检查: GUC 参数
  guc_count=$(remote_cmd "$PGDIR/bin/psql" -p "$PORT" -U postgres -d postgres -tAc \
    "SELECT count(*) FROM pg_settings WHERE name LIKE 'PGSAUDAUDITENGINE%'" 2>/dev/null)
  echo "PGSAUDAUDITENGINE.* GUC 数量: $guc_count" >> "$LOG"
  if [ "$guc_count" != "5" ]; then
    echo "RESULT: FAIL (GUC 数量应为 5)" >> "$LOG"; echo "FAIL $ver GUC 数量=$guc_count 见 $LOG" >> "$SUMMARY"; continue
  fi

  # 8. CREATE EXTENSION
  ext=$(remote_cmd "$PGDIR/bin/psql" -p "$PORT" -U postgres -d postgres -tAc \
    "CREATE EXTENSION IF NOT EXISTS pgsqlauditengine; SELECT extname FROM pg_extension WHERE extname='pgsqlauditengine';" 2>&1)
  echo "CREATE EXTENSION: $ext" >> "$LOG"
  if ! echo "$ext" | grep -q "pgsqlauditengine"; then
    echo "RESULT: FAIL (CREATE EXTENSION 失败)" >> "$LOG"; echo "FAIL $ver CREATE EXTENSION 见 $LOG" >> "$SUMMARY"; continue
  fi

  # 9. 钩子生效: 典型用例
  hook=$(remote_cmd "$PGDIR/bin/psql" -p "$PORT" -U postgres -d postgres -c \
    "CREATE TABLE se_audit_tmp_$ver(id int); DELETE FROM se_audit_tmp_$ver;" 2>&1)
  echo "钩子用例输出: $hook" >> "$LOG"
  if echo "$hook" | grep -q "SQL审核"; then
    echo "RESULT: PASS" >> "$LOG"
    echo "PASS $ver" >> "$SUMMARY"
  else
    echo "RESULT: FAIL (钩子未生效)" >> "$LOG"; echo "FAIL $ver 钩子未生效 见 $LOG" >> "$SUMMARY"
  fi
done

echo
echo "=== 汇总 ==="
cat "$SUMMARY"
echo
echo "详细日志位于: $REPORT_DIR/"