# pgsqlauditengine 跨版本（PG11-18）验证报告

> 验证日期：2026-08-30
> 原始产物：`scripts/verify_report/verify/`（八版本 rules/config/GUC/audit 提取与
> 用例执行日志、PG18 基线快照）

## 1. 编译 / 安装 / 加载（任务组 6）

八版本全量（`rm -f *.o *.so` 后）编译、安装、shared_preload 加载、重启均通过；
`pgsqlauditengine.so` 无幽灵未定义符号（曾因 PGXS 增量编译残留旧 `.o` 产生
`U GetCommandTagName`，全量重编后消除）。

| 版本 | 编译 | 安装 | 加载 | GUC(5) | CREATE EXTENSION |
|------|------|------|------|--------|------------------|
| PG11-18 | PASS | PASS | PASS | PASS | PASS |

## 2. 不可变契约专项验收（任务组 8.1）

| 契约项 | 验证方式 | 结果 |
|--------|----------|------|
| 80 条规则（id/name/type/default_level） | 八版本 `GET /api/v1/rules` 与 PG18 基线 diff | 全 MATCH |
| 消息前缀 `SQL审核[rule_id]:` 与文案 | 八版本用例执行日志提取逐字 diff | 全 MATCH |
| 5 个 GUC（名/默认值/访问级别） | `pg_settings` boot_val/vartype/context 对比 | 全 MATCH |
| RESTful 路径/响应包裹/错误码 | health/rules/{id}/PUT/audit-logs/config + 404/401 | 全一致 |
| Bearer token 鉴权 | 设置 token 后无/错/对 token 请求 | 全一致（health 免鉴权） |
| `se_*` 函数前缀 | 头文件静态检查 | PASS |
| `bgw_function_name = "se_api_server_main"` | api_server.c 静态检查 | PASS |
| 共享内存标识（Rule Configs / Record Buffer） | 静态检查 | PASS |
| `AUDIT_RECORD_BUF_CAP = 4096` | audit_record.h 静态检查 | PASS |
| 扩展命名 `pgsqlauditengine` | 八版本控制文件/共享库/扩展 | PASS |

## 3. 功能等价回归（任务组 7.2/7.4）

用例集（`multiversion_cases.sql` / `multiversion_cases2.sql`）覆盖 DDL/DML/维护/
对象/兜底命令，八版本触发消息与 PG18 基线一致，**差异仅为预期分界**：

| 分界能力 | 行为 |
|----------|------|
| `REINDEX INDEX CONCURRENTLY` | 仅 PG12+ 触发 `cmd_reindex` 回显（PG11 无语法） |
| `ALTER STATISTICS` | 仅 PG13+ 触发 `obj_advanced`（PG11-12 无语法） |
| `MERGE` | 仅 PG15+ 触发 `dml_merge_check`（PG11-14 无语法） |

DML 规则（`dml_no_where`/`dml_count_star`/`dml_left_fuzzy`/`dml_in_to_exists`/
`dml_batch_copy`）、DDL 规则（`table_pk_required`/`table_no_foreign_key`/
`table_column_not_null`/`name_*`）、维护命令（`cmd_vacuum`/`cmd_analyze`/
`cmd_reindex`/`cmd_checkpoint`/`cmd_lock`/`cmd_discard`）、对象命令（`obj_ctas`/
`obj_advanced`）在八版本全部触发且文案一致。

## 4. GUC 行为（任务组 7.5）

- `PGSAUDAUDITENGINE.enabled = off` 时执行触发语句无任何 `SQL审核:` 输出（PG11/PG18
  验证一致）。
- 规则级别/开关通过 `PUT /api/v1/rules/{id}` 实时生效（八版本一致）。

## 5. RESTful（任务组 7.6）

| 接口 | 行为 |
|------|------|
| `GET /health` | 200（免鉴权） |
| `GET /rules` | 200 + 80 条 |
| `GET /rules/{id}` | 200；未知 id → 404 `RULE_NOT_FOUND`（"规则不存在"） |
| `PUT /rules/{id}` | 200 `{"updated":true}`；未知 id → 404 `RULE_NOT_FOUND` |
| `GET /audit-logs` | 200 + 记录（字段 ts/stmt_type/object/rule/level） |
| `GET /config` | 200 + enabled + 80 条规则开关/级别 |
| 鉴权 | token 为空免鉴权；token 非空时无/错 token → 401 `UNAUTHORIZED`（"未认证或认证失败"） |

## 6. 审计记录与性能（任务组 7.7）

- audit-logs 记录字段语义八版本一致（`ts`/`stmt_type`/`object`/`rule`/`level`）。
- 单条 SQL 审核开销实测 ≈ 0.02ms（PG11/PG18，200 条 `SELECT 1` on/off 差值），
  满足 ≤1ms。

## 7. 源码版本宏组织审查（任务组 8.2）

- 版本判断统一基于 `PG_VERSION_NUM`，无其他版本判断方式。
- 差异访问统一走 `compat.h` 宏；`#if PG_VERSION_NUM` 仅出现于：compat.h（宏定义）、
  hook 签名定义处、case 标签隔离（T_AlterStatsStmt/T_AlterTypeStmt/CMD_MERGE）、
  include 选择、shmem 两态、辅助函数实现（rule_common.c）。
- 每个 `#if` 分支均附中文注释。
- 不可变契约文件 `rule_registry.c`（`g_rule_defs[]`）与 `api_route.c` 无任何
  版本分支。

## 8. PG18 回归保护（任务组 7.8）

- PG18 上全部用例集执行结论/消息/RESTful 响应自洽（作为基线，八版本 diff 为空）。
- `g_rule_defs[]`、`api_route.c` 未被修改（无版本分支污染）。

## 9. 遗留说明

- 多版本共存时 RESTful 端口按版本独立分配（8911~8918），避免 `api_port=8900`
  被先启动实例占用导致其余实例 bind 失败（WARNING 不致命）。
- 部署时 `api_port` 为 POSTMASTER 级 GUC，修改需重启实例。