# pgsqlauditengine 跨版本（PG11-18）验证版本矩阵

> 生成日期：2026-08-30
> 验证范围：编译 / 安装 / 加载 / GUC / 建扩展 / 钩子生效 / 功能等价回归 / RESTful

## 1. 版本矩阵总览

| 版本 | 本地内核源码 | 远端安装目录 | pg_config | 数据目录 | PG 监听端口 | RESTful 端口 | 验证结论 |
|------|--------------|--------------|-----------|----------|-------------|--------------|----------|
| PG11 | `postgresql-11.22` | `/usr/local/pgversion/11/pgsql` | `/usr/local/pgversion/11/pgsql/bin/pg_config` | `/data/11/data` | 54311 | 8911 | PASS |
| PG12 | `postgresql-12.22` | `/usr/local/pgversion/12/pgsql` | `/usr/local/pgversion/12/pgsql/bin/pg_config` | `/data/12/data` | 54312 | 8912 | PASS |
| PG13 | `postgresql-13.23` | `/usr/local/pgversion/13/pgsql` | `/usr/local/pgversion/13/pgsql/bin/pg_config` | `/data/13/data` | 54313 | 8913 | PASS |
| PG14 | `postgresql-14.24` | `/usr/local/pgversion/14/pgsql` | `/usr/local/pgversion/14/pgsql/bin/pg_config` | `/data/14/data` | 54314 | 8914 | PASS |
| PG15 | `postgresql-15.19` | `/usr/local/pgversion/15/pgsql` | `/usr/local/pgversion/15/pgsql/bin/pg_config` | `/data/15/data` | 54315 | 8915 | PASS |
| PG16 | `postgresql-16.15` | `/usr/local/pgversion/16/pgsql` | `/usr/local/pgversion/16/pgsql/bin/pg_config` | `/data/16/data` | 54316 | 8916 | PASS |
| PG17 | `postgresql-17.11` | `/usr/local/pgsql` | `/usr/local/pgsql/bin/pg_config` | `/data/17/pgdata` | 5432 | 8917 | PASS |
| PG18 | `postgresql-18.6` | `/usr/local/pgversion/18/pgsql` | `/usr/local/pgversion/18/pgsql/bin/pg_config` | `/data/18/data` | 54318 | 8918 | PASS |

> 注：远端 PG17 安装于 `/usr/local/pgsql`（非 `/usr/local/pgversion/17`）；数据目录为 `/data/17/pgdata`（非 `/data/17/data`）。PG 监听端口为 543<版本号>（PG17 为 5432）、RESTful 端口为 89<版本号>，与 `run_regression_pg*.sh` 及实测一致。

## 2. 验证结论明细

| 验证项 | PG11 | PG12 | PG13 | PG14 | PG15 | PG16 | PG17 | PG18 |
|--------|------|------|------|------|------|------|------|------|
| 全量编译（`make clean` 后） | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| 安装（`make install`） | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| shared_preload 加载 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| 5 个 GUC 注册（名/默认值/访问级别） | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| CREATE EXTENSION | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| 审核钩子生效（左模糊触发） | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| RESTful `/api/v1/rules`（102 条） | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| RESTful `/api/v1/config` | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| RESTful `/api/v1/audit-logs` | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| RESTful 错误码/鉴权 | PASS | PASS | PASS | PASS | PASS | PASS | PASS | PASS |
| 功能等价回归（与 PG18 基线 diff） | PASS | PASS | PASS | PASS | PASS | PASS | PASS | 基线 |

## 3. 分界能力验证（符合预期）

| 能力 | PG11 | PG12 | PG13 | PG14 | PG15 | PG16 | PG17 | PG18 |
|------|------|------|------|------|------|------|------|------|
| `REINDEX CONCURRENTLY` 审核 | 无语法（不触发） | 触发 | 触发 | 触发 | 触发 | 触发 | 触发 | 触发 |
| `ALTER STATISTICS` 审核 | 无语法（不触发） | 无语法（不触发） | 触发 | 触发 | 触发 | 触发 | 触发 | 触发 |
| `MERGE` 审核 | 无语法（不触发） | 无语法（不触发） | 无语法（不触发） | 无语法（不触发） | 触发 | 触发 | 触发 | 触发 |

## 4. 共享配置

所有实例统一配置（`postgresql.conf`）：

```ini
shared_preload_libraries = 'pgsqlauditengine'
PGSAUDAUDITENGINE.enabled = on
PGSAUDAUDITENGINE.check_dml = on
PGSAUDAUDITENGINE.api_listen = '127.0.0.1'
PGSAUDAUDITENGINE.api_port = 89<版本号>   # 各版本独立端口，避免 8900 冲突
PGSAUDAUDITENGINE.api_token = ''
```

## 5. 版本分界速查

| 分界点 | 版本 |
|--------|------|
| `VACUUM options`：List（DefElem）/ int 位掩码 | PG12 起 List |
| `REINDEX params`：List / options+concurrent / options | PG14 起 List |
| `CreateCommandName`（PG13+）/ `CreateCommandTag`（PG11-12） | PG13 |
| `ALTER STATISTICS`（AlterStatsStmt） | PG13+ |
| `MERGE`（CMD_MERGE） | PG15+ |
| `A_Const` 值访问（ValUnion / Value） | PG15 起 ValUnion |
| `table_open/table_close`（PG12+）/ `heap_open/heap_close`（PG11） | PG12 |
| `ScanKeyword` API 差异 | PG12 起返回下标 |
| `shmem_request_hook`（PG15+，PG11-14 直接 `RequestAddinShmemSpace`） | PG15 |
| `ProcessUtility` 钩子 `readOnlyTree` 参数 | PG14+ |
| `ProcessUtility` 钩子 `QueryCompletion` | PG13+ |
| `post_parse_analyze` 钩子 `JumbleState` 参数 | PG14+ |
| `CreateTableAsStmt.objtype` / `AlterTableStmt.objtype` | PG14 起 `objtype` |
| `FormData_*` 结构 `oid` 成员（PG11 用 `HeapTupleGetOid`） | PG12 起有 oid |