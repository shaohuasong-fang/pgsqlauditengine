# pgsqlauditengine

PostgreSQL SQL 审核扩展（SQL Audit Engine for PostgreSQL）：以**共享库 + Background Worker + 内嵌 RESTful API** 方式为 PostgreSQL 提供 DDL / DML / DCL / TCL / 命令类语句的静态审核能力，支持 ** PostgreSQL 11 ~ 18 版本。

pgsqlauditengine 通过挂载 `ProcessUtility_hook` 与 `post_parse_analyze_hook`，在语句执行前完成审核判定，并按规则级别执行 **NOTICE 放行 / WARNING 拦截 / ERROR 拦截** 语义；审核过程不修改任何语句、不依赖外部服务，规则开关与级别可通过内嵌 RESTful 管理服务实时调整。


---

## 1. 项目简介

pgsqlauditengine 是一个 PostgreSQL 扩展（Extension），在数据库内核**语句执行前**完成 SQL 静态审核，帮助 DBA 与研发将数据库对象设计规范、SQL 编写规范前置到执行环节：

- **审核范围**：覆盖 DDL（建表/建索引/视图/函数/物化视图等）、DML（UPDATE/DELETE/SELECT 规范）、DCL（权限/角色）、TCL（事务/保存点）、命令类（VACUUM/COPY/REINDEX 等），并有命令兜底回显，保证"无任何命令静默放行"。
- **实现形态**：共享库 `pgsqlauditengine.so`（审核逻辑）+ Background Worker（RESTful 管理服务）+ 共享内存（规则配置区与审计日志环形缓冲）。
- **零侵入**：仅通过扩展钩子挂载，不修改 PostgreSQL 内核源码；同一份源码兼容 PG11-18（跨版本差异全部封装于 `compat.h`）。

## 2. 核心特性


- **三档级别语义**（PG11-18 全版本生效）：

  | 规范级别 | 输出级别 | 行为 |
  |----------|----------|------|
  | 强制 | ERROR（`ERRCODE_CHECK_VIOLATION`） | **拦截**：语句不执行 |
  | 建议 | WARNING | **拦截**：语句不执行（输出级别前缀为 ERROR 并带 `SQL审核级别: WARNING（语句被拦截）` errdetail 标识） |
  | 推荐 | NOTICE | **放行**：语句正常执行 |



## 3. 版本支持矩阵


| 版本 | 内核源码 | 远端安装目录 | PG 监听端口 | RESTful 端口 | 验证结论 |
|------|----------|--------------|-------------|--------------|----------|
| PG11 | `postgresql-11.22` | `/usr/local/pgversion/11/pgsql` | 54311 | 8911 | PASS |
| PG12 | `postgresql-12.22` | `/usr/local/pgversion/12/pgsql` | 54312 | 8912 | PASS |
| PG13 | `postgresql-13.23` | `/usr/local/pgversion/13/pgsql` | 54313 | 8913 | PASS |
| PG14 | `postgresql-14.24` | `/usr/local/pgversion/14/pgsql` | 54314 | 8914 | PASS |
| PG15 | `postgresql-15.19` | `/usr/local/pgversion/15/pgsql` | 54315 | 8915 | PASS |
| PG16 | `postgresql-16.15` | `/usr/local/pgversion/16/pgsql` | 54316 | 8916 | PASS |
| PG17 | `postgresql-17.11` | `/usr/local/pgsql` | 5432 | 8917 | PASS |
| PG18 | `postgresql-18.6` | `/usr/local/pgversion/18/pgsql` | 54318 | 8918 | PASS |


### 版本分界能力

| 能力 | PG11 | PG12 | PG13 | PG14 | PG15 | PG16 | PG17 | PG18 |
|------|------|------|------|------|------|------|------|------|
| `REINDEX CONCURRENTLY` 审核 | 无语法 | 触发 | 触发 | 触发 | 触发 | 触发 | 触发 | 触发 |
| `ALTER STATISTICS` 审核 | 无语法 | 无语法 | 触发 | 触发 | 触发 | 触发 | 触发 | 触发 |
| `MERGE` 审核 | 无语法 | 无语法 | 无语法 | 无语法 | 触发 | 触发 | 触发 | 触发 |

## 4. 快速开始


### 4.1 编译与安装（PGXS）

```bash
cd postgresql-${version}/contrib/
git clone 

# 单版本编译安装（PG18）
rm -f *.o *.so                              # PGXS 不跟踪 .h 依赖，修改 compat.h 后必须全量重编
make -j$(nproc) USE_PGXS=1 PG_CONFIG=/usr/local/pgversion/18/pgsql/bin/pg_config
make -j$(nproc) USE_PGXS=1 PG_CONFIG=/usr/local/pgversion/18/pgsql/bin/pg_config install   # 需 root 权限

```

### 4.2 配置（postgresql.conf或者postgresql.auto.conf）

追加以下配置并重启实例（`api_*` 为 POSTMASTER 级，需重启生效）：

```
shared_preload_libraries = 'pgsqlauditengine'
PGSAUDAUDITENGINE.enabled = on
PGSAUDAUDITENGINE.check_dml = on
PGSAUDAUDITENGINE.api_listen = '127.0.0.1'
PGSAUDAUDITENGINE.api_port = 8918            # 各版本独立，如 8911~8918
PGSAUDAUDITENGINE.api_token = ''
```

```bash
# 重启实例（PG18）
su - postgres -c '/usr/local/pgversion/18/pgsql/bin/pg_ctl restart -D /data/18/data -o "-p 54318" -l /data/18/data/server.log'
```

### 4.3 5 个 GUC 参数

| 参数名 | 类型 | 默认值 | 上下文 | 说明 |
|--------|------|--------|--------|------|
| `PGSAUDAUDITENGINE.enabled` | bool | off | SUSET | 全局审核开关 |
| `PGSAUDAUDITENGINE.check_dml` | bool | on | SUSET | DML 语句审核开关 |
| `PGSAUDAUDITENGINE.api_listen` | string | 127.0.0.1 | POSTMASTER | RESTful 监听地址 |
| `PGSAUDAUDITENGINE.api_port` | int | 8900 | POSTMASTER | RESTful 监听端口 |
| `PGSAUDAUDITENGINE.api_token` | string | 空 | POSTMASTER | API 鉴权 Token（空则免鉴权） |

### 4.4 创建扩展并验证

```bash
export PGPASSWORD=postgres
PSQL=/usr/local/pgversion/18/pgsql/bin/psql

# 创建扩展（仅登记元数据；钩子由 shared_preload 加载）
$PSQL -h 127.0.0.1 -p 54318 -U postgres -d postgres -c "CREATE EXTENSION IF NOT EXISTS pgsqlauditengine;"

# 验证 GUC 注册
$PSQL -h 127.0.0.1 -p 54318 -U postgres -d postgres \
  -tAc "SELECT name, boot_val, context FROM pg_settings WHERE name LIKE 'PGSAUDAUDITENGINE.%' ORDER BY 1;"

# 验证审核钩子生效（count(列) 触发 dml_count_star，应为 ERROR 拦截）
$PSQL -h 127.0.0.1 -p 54318 -U postgres -d postgres \
  -c "SELECT count(name) FROM pg_settings WHERE name LIKE '%audit%';"

# 验证 RESTful 服务
curl -s http://127.0.0.1:8918/api/v1/health
curl -s http://127.0.0.1:8918/api/v1/rules | grep -o '"id"' | wc -l    # 102
```

## 5. 使用示例

### 5.1 psql 审核输出（三档级别）

```sql
-- 触发 ERROR 拦截：表名大写（name_charset，ERROR 级 → 拦截）
-- 触发 WARNING 拦截：物化视图须评估刷新策略（obj_matview，WARNING 级 → 拦截）
-- 触发 NOTICE 放行：对象删除回显（ddl_drop_object，NOTICE 级 → 放行）
```

实际输出（与 verify 基线 `out_test_pg18.log` 一致）：

```text
psql:/tmp/reg_pg18/seg_cases.sql:7: ERROR:  SQL审核[name_charset]: 表名称 "SeTUpper" 只能由小写字母(a-z)、数字(0-9)、下划线(_)构成
psql:/tmp/reg_pg18/seg_p0.sql:46: ERROR:  SQL审核[obj_matview]: 物化视图需评估数据新鲜度与刷新策略 (物化视图: vw_se_mv)
psql:/tmp/reg_pg18/seg_p0.sql:10: NOTICE:  SQL审核[ddl_drop_object]: 对象删除回显 (对象类型: 表, 名称: se_t_base)
```

### 5.2 RESTful API 调用示例

Base URL：`http://127.0.0.1:8918/api/v1`；鉴权头 `Authorization: Bearer <api_token>`（token 为空时免鉴权；`/health` 始终免鉴权）。响应统一包裹 `{"code","error_code","error_message","data"}`。

```bash
# 1) 健康检查（免鉴权）
curl -s http://127.0.0.1:8918/api/v1/health

# 2) 规则列表（102 条）
curl -s -H 'Authorization: Bearer test-token-123' http://127.0.0.1:8918/api/v1/rules

# 3) 单个规则详情
curl -s -H 'Authorization: Bearer test-token-123' http://127.0.0.1:8918/api/v1/rules/dml_left_fuzzy

# 4) 修改规则（实时生效）
curl -s -X PUT -H 'Authorization: Bearer test-token-123' -H 'Content-Type: application/json' \
     -d '{"enabled":false}' http://127.0.0.1:8918/api/v1/rules/dml_left_fuzzy

# 5) 恢复规则默认配置
curl -s -X DELETE -H 'Authorization: Bearer test-token-123' http://127.0.0.1:8918/api/v1/rules/dml_left_fuzzy

# 6) 审计日志（环形缓冲）
curl -s -H 'Authorization: Bearer test-token-123' http://127.0.0.1:8918/api/v1/audit-logs

# 7) 运行期配置全量导出
curl -s -H 'Authorization: Bearer test-token-123' http://127.0.0.1:8918/api/v1/config
```

| 方法 | 路径 | 鉴权 | 说明 |
|------|------|------|------|
| GET | `/api/v1/health` | 否 | 健康检查 |
| GET | `/api/v1/rules` | 是 | 规则列表 |
| GET | `/api/v1/rules/{id}` | 是 | 单个规则详情 |
| PUT | `/api/v1/rules/{id}` | 是 | 修改规则开关/级别 |
| DELETE | `/api/v1/rules/{id}` | 是 | 恢复规则默认配置 |
| GET | `/api/v1/audit-logs` | 是 | 审计日志（环形缓冲） |
| GET | `/api/v1/config` | 是 | 运行期配置全量导出 |



## 8. 目录结构

```
PostgreSQLClass/
├── pgsqlauditengine/               # 扩展源码与文档（唯一交付物）
│   ├── rule_registry.c/.h          # 规则注册表 g_rule_defs[]（102 条）与命令映射 g_cmd_map[]
│   ├── rule_ddl.c / rule_exists.c  # DDL/索引审核（含 CIC 静态/目录检查分离修复）
│   ├── rule_dml* 与 hook_analyze.c # DML 审核（post_parse_analyze 钩子）
│   ├── hook_utility.c              # ProcessUtility 钩子与命令兜底
│   ├── api_route.c / api_server.c  # RESTful 路由与 Background Worker
│   ├── audit_record.c / compat.h   # 审计环形缓冲 / 跨版本差异封装
│   ├── pgsqlauditengine--1.0.sql   # 扩展 SQL 元数据
│   ├── Makefile / .control
```


## 10. 参考资料

- PostgreSQL 官方命令参考：https://www.postgresql.org/docs/current/sql-commands.html
