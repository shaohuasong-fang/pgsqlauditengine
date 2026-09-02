# pgsqlauditengine 跨版本验证命令（可重复执行）

> 远端：172.30.31.128（aarch64），root 密码 `Bigdata_123`。
> SSH/SCP 必须携带以下参数（否则认证失败）：
> `-o PreferredAuthentications=password -o PubkeyAuthentication=no -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null`
> 数据库账号统一 `postgres`/`postgres`。

## 1. 版本路径速查

```bash
declare -A PORTS=([11]=54311 [12]=54312 [13]=54313 [14]=54314 [15]=54315 [16]=54316 [17]=5432 [18]=54318)
# PG 监听端口 = 543<版本号>（PG17=5432）；RESTful 端口 = 89<版本号>
# 安装目录：PG11-16/18=/usr/local/pgversion/<v>/pgsql；PG17=/usr/local/pgsql
# 数据目录：PG11-16/18=/data/<v>/data；PG17=/data/17/pgdata
```

## 2. 同步源码

```bash
SRC=/root/pgsqlauditengine_src/pgsqlauditengine
# 方式一：scp 单个修改文件
sshpass -p 'Bigdata_123' scp <本地>/xxx.c root@172.30.31.128:$SRC/
# 方式二：rsync 全量（排除构建产物）
rsync -avz --exclude '*.o' --exclude '*.so' <本地>/pgsqlauditengine/ \
  -e "ssh -o PreferredAuthentications=password -o PubkeyAuthentication=no -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null" \
  root@172.30.31.128:$SRC/
```

## 3. 全量编译 + 安装（务必 `rm -f *.o *.so`）

> 关键：PGXS 的 Makefile 不跟踪 `.h` 依赖，compat.h 修改后**必须全量重编**，
> 否则残留旧 `.o` 会产生幽灵未定义符号（如 `U GetCommandTagName`）。

```bash
cd /root/pgsqlauditengine_src/pgsqlauditengine
for v in 11 12 13 14 15 16 17 18; do
  if [ "$v" = "17" ]; then cfg=/usr/local/pgsql/bin/pg_config
  else cfg=/usr/local/pgversion/$v/pgsql/bin/pg_config; fi
  rm -f *.o *.so
  make USE_PGXS=1 PG_CONFIG=$cfg >/tmp/build_pg$v.log 2>&1 \
    && make USE_PGXS=1 PG_CONFIG=$cfg install >/tmp/install_pg$v.log 2>&1 \
    && echo "PG$v: OK" || { echo "PG$v: FAIL"; grep -n "error:" /tmp/build_pg$v.log; }
done
```

## 4. 加载配置（postgresql.conf）

每个实例追加（RESTful 端口按版本独立，避免冲突）：

```ini
shared_preload_libraries = 'pgsqlauditengine'
PGSAUDAUDITENGINE.enabled = on
PGSAUDAUDITENGINE.check_dml = on
PGSAUDAUDITENGINE.api_listen = '127.0.0.1'
PGSAUDAUDITENGINE.api_port = 89<版本号>
PGSAUDAUDITENGINE.api_token = ''
```

## 5. 重启实例

```bash
su - postgres -c "<安装目录>/bin/pg_ctl restart -D <数据目录> -o \"-p <PG端口>\" -l <数据目录>/server.log"
# 例：PG11
su - postgres -c '/usr/local/pgversion/11/pgsql/bin/pg_ctl restart -D /data/11/data -o "-p 54311" -l /data/11/data/server.log'
```

## 6. 冒烟验证

```bash
export PGPASSWORD=postgres
# GUC
<安装目录>/bin/psql -h 127.0.0.1 -p <PG端口> -U postgres -d postgres \
  -tAc "SELECT name, boot_val, context FROM pg_settings WHERE name LIKE 'PGSAUDAUDITENGINE.%' ORDER BY 1;"
# 建扩展
<安装目录>/bin/psql -h 127.0.0.1 -p <PG端口> -U postgres -d postgres \
  -c "CREATE EXTENSION IF NOT EXISTS pgsqlauditengine;"
# 钩子生效（count(列) 应触发 dml_count_star 拒绝）
<安装目录>/bin/psql -h 127.0.0.1 -p <PG端口> -U postgres -d postgres \
  -c "SELECT count(name) FROM pg_settings WHERE name LIKE '%audit%';"
# RESTful
curl -s http://127.0.0.1:89<版本号>/api/v1/health
curl -s http://127.0.0.1:89<版本号>/api/v1/rules | grep -o '"id"' | wc -l   # 102
```

## 7. 功能等价回归（与 PG18 基线对比）

```bash
cd /root/pgsqlauditengine_src/verify
# 基线快照（PG18）
curl -s http://127.0.0.1:8918/api/v1/rules > baseline/rules_pg18.json
curl -s http://127.0.0.1:8918/api/v1/config > baseline/config_pg18.json
# 各版本 rules/config diff（应全 MATCH）
for v in 11 12 13 14 15 16 17; do
  curl -s http://127.0.0.1:89$v/api/v1/rules > rules_pg$v.json
  curl -s http://127.0.0.1:89$v/api/v1/config > config_pg$v.json
  diff -q baseline/rules_pg18.json rules_pg$v.json >/dev/null && echo "PG$v rules MATCH" || echo "PG$v rules DIFF"
done
# 用例集执行（scripts/multiversion_cases.sql / multiversion_cases2.sql）
<安装目录>/bin/psql -h 127.0.0.1 -p <PG端口> -U postgres -d postgres -f multiversion_cases.sql > out_pg$v.log 2>&1
grep "SQL审核" out_pg$v.log | diff - out_pg18.log 的对应提取    # 应为空
```

## 7.1 规则边界测试脚本与 API 测试文档验证（test.sql / api.md，PG18 基线）

> 资产：`scripts/pgsqlauditengine_test.sql`（80 条规则边界用例）、`pgsqlauditengine_api.md`（RESTful 返回样本）。
> 归档位置：`scripts/verify_report/verify/`（与既有 `out_pg18.log`/`audit_pg18.txt` 并列）。

```bash
cd /root/pgsqlauditengine_src/pgsqlauditengine
# 1) 同步测试脚本至远端
sshpass -p 'Bigdata_123' scp -o PreferredAuthentications=password -o PubkeyAuthentication=no -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
  <本地>/pgsqlauditengine/scripts/pgsqlauditengine_test.sql root@172.30.31.128:$SRC/scripts/
# 2) 测试脚本执行（归档日志）
/usr/local/pgversion/18/pgsql/bin/psql -h 127.0.0.1 -p 54318 -U postgres -d postgres \
  -f scripts/pgsqlauditengine_test.sql > /root/pgsqlauditengine_src/verify/out_test_pg18.log 2>&1
# 3) 实际输出规则 id 集合 vs 80 条全集（覆盖率核对：实际输出 ∪ 6 条白名单豁免 = 80 条全集）
grep -oE 'SQL审核\[[a-z_0-9]+\]' /root/pgsqlauditengine_src/verify/out_test_pg18.log \
  | sed 's/SQL审核\[//;s/\]//' | sort -u > /tmp/actual_rules.txt
# 4) 幂等性验证（二次执行后 SQL审核 行 diff 应为空）
/usr/local/pgversion/18/pgsql/bin/psql -h 127.0.0.1 -p 54318 -U postgres -d postgres \
  -f scripts/pgsqlauditengine_test.sql > /root/pgsqlauditengine_src/verify/out_test_pg18_2.log 2>&1
diff <(grep 'SQL审核' /root/pgsqlauditengine_src/verify/out_test_pg18.log) \
     <(grep 'SQL审核' /root/pgsqlauditengine_src/verify/out_test_pg18_2.log)
# 5) API 样本核对（按 pgsqlauditengine_api.md 逐条执行 curl，将命令+实际返回+结论记录到 api_test_pg18.txt）
curl -s http://127.0.0.1:8918/api/v1/health
curl -s http://127.0.0.1:8918/api/v1/rules | grep -o '"id"' | wc -l        # 80
# 6) 规则配置复位验收（将 80 条 PUT 恢复 enabled=true/level=default_level 后）
curl -s http://127.0.0.1:8918/api/v1/config | diff - /root/pgsqlauditengine_src/verify/baseline/config_pg18.json   # 应为空
```

## 8. 常用排查

```bash
# 未定义符号
nm -D <安装目录>/lib/pgsqlauditengine.so | grep ' U ' | grep -v '@GLIBC'
# 加载日志
tail -20 <数据目录>/server.log
# 端口占用
ss -tlnp | grep -E '89..|543.'
```

## 9. 本地验证（单机有 PGXS 的版本）

```bash
bash scripts/verify_local.sh          # 探测可用 PG_CONFIG 逐版本循环编译
```