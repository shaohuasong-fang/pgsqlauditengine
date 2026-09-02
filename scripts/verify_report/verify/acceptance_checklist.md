# pgsqlauditengine 规则边界与 API 返回测试验证 —— 21 条需求验收核对记录

> 验收日期：2026-08-30
> 验证基线：PG18（远端 172.30.31.128，PG 端口 5439 / RESTful 端口 8918）
> 资产：`scripts/pgsqlauditengine_test.sql`（900 行，80 条规则边界用例）、`pgsqlauditengine_api.md`（S1~S12 样本）、
> 归档产物 `scripts/verify_report/verify/out_test_pg18.log`、`out_test_pg18_2.log`、`api_test_pg18.txt`
> 对应 spec §5.1~§5.3 全部 21 条 EARS 需求（tasks.md 任务组 12.3）

## 1. 验收结果总览

| 序号 | 需求 | 验收结果 | 说明 |
|------|------|----------|------|
| 1 | 测试脚本交付规则 | PASS | test.sql 可通过 `psql -f` 直接执行，无语法错误，产生审核输出（任务组 10.1） |
| 2 | 80 条规则全覆盖规则 | PASS | 实际输出 74 条 ∪ 6 条豁免 = 80 条全集，setdiff 为空，无多余输出（任务组 10.2） |
| 3 | 边界与有效性验证规则 | PASS | 每条规则含触发+反例；恒触发类以「规则关闭后不触发」/注释声明验证（test.sql L178 等） |
| 4 | 消息输出格式规则 | PASS | 前缀 `SQL审核[rule_id]:`、级别与 default_level 一致、文案与 audit_pg18.txt 基线一致（抽查 table_pk_required/name_charset/dml_no_where/cmd_vacuum 全 MATCH） |
| 5 | 级别阻断语义验证规则 | PASS | ERROR 用例后语句报错阻断、后续独立用例继续执行（psql ON_ERROR_STOP=off）；WARNING/NOTICE 后正常执行 |
| 6 | 幂等可重复执行规则 | PASS | 二次执行 `grep 'SQL审核'` diff 为空（IDEMPOTENT-DIFF-EMPTY） |
| 7 | 用例组织与可读性规则 | PASS | 14 规则族分区 + 标题注释，与 spec §6.1 分组一致（PART 1-14 + COV） |
| 8 | API 测试文档交付规则 | PASS | api.md 存在、完整（757 行），含全部接口成功/错误样本与测试步骤 |
| 9 | 接口端点全覆盖规则 | PASS | 7 个端点（health/rules/rules/{id}/PUT rules/{id}/DELETE rules/{id}/audit-logs/config）均有独立章节与样本，另附 D8-3 附加端点说明 |
| 10 | 规则级返回样本覆盖规则 | PASS | GET /rules、GET /rules/{id}、PUT /rules/{id} 字段结构与 baseline/rules_pg18.json 一致（S3/S4/S5 样本） |
| 11 | PUT 规则配置样本规则 | PASS | 三态请求体（enabled=false / level=WARNING / enabled=true+level=ERROR）返回 `{"updated":true}`，测试步骤含 GET 核对 + 重触发验证实时生效（S5） |
| 12 | 错误码场景覆盖规则 | PASS | RULE_NOT_FOUND(404)/UNAUTHORIZED(401)/BAD_REQUEST(400×4 态)/METHOD_NOT_ALLOWED(405)/NOT_FOUND(404×2) 全场景，与实际 curl 一致（S10） |
| 13 | 鉴权场景覆盖规则 | PASS | token 空免鉴权(200)、正确(200)、缺失/错误(401 UNAUTHORIZED)、/health 免鉴权(200)；S11 全部实测一致 |
| 14 | audit-logs 与 config 样本规则 | PASS | audit-logs 字段 ts/stmt_type/object/rule/level 与 baseline 语义一致；config 返回 enabled+80 条与 baseline/config_pg18.json diff 空（S7/S8） |
| 15 | 样本可执行与可核对规则 | PASS | 每条样本含 Base URL/curl（含 Authorization 头）/请求体/预期返回/测试步骤，全部按文档可复现执行（api_test_pg18.txt） |
| 16 | PG18 基线执行核对规则 | PASS | out_test_pg18.log 中同规则消息与 audit_pg18.txt 基线逐字一致；用例注释预期与实测输出一致（任务组 10.3） |
| 17 | API 样本真实返回核对规则 | PASS | 全部样本执行 curl 记录命令/返回/结论至 api_test_pg18.txt（421 行），动态字段（ts/health.enabled）按结构匹配核对（任务组 10.5/11） |
| 18 | 规则配置复位规则 | PASS | S5-8 批量 PUT 复位 80 条 enabled=true/level=default_level（无 PUT FAIL）；S8-2 GET /config diff baseline DIFF-EMPTY |
| 19 | 验证结果归档规则 | PASS | out_test_pg18.log、out_test_pg18_2.log、api_test_pg18.txt 已拉回 `scripts/verify_report/verify/`，与既有产物并列，含 pg18 版本标识 |
| 20 | 源码与契约零改动规则 | PASS | 本地与远端源码 37 文件 MD5 完全一致（ZERO-DIFF）；新增资产仅 test.sql / api.md / 归档产物（任务组 12.1） |
| 21 | 跨版本复用兼容规则 | PASS | 版本分界注释齐备：dml_merge_check `[PG15+]`（L637）、obj_advanced ALTER STATISTICS `[PG13+]`（L580）、cmd_reindex CONCURRENTLY `[PG12+]`（L474）；格式与 multiversion_cases*.sql 同构（`\set VERBOSITY terse`） |

**结论：21 条需求全部 PASS。**

## 2. 行为偏差与豁免说明（本阶段新增，均以存量行为为准）

1. **D8-4（enabled 切换生效需重启）**：`PGSAUDAUDITENGINE.enabled` 为 SUSET 级，但 `pg_reload_conf()` 仅更新
   psql 会话的 GUC 值（`SHOW` 返回 off），API Background Worker（api_server.c 独立进程）内
   `pgsql_audit_engine_enabled` 全局变量不随 reload 更新；实测 `ALTER SYSTEM SET ... = off` + reload 后
   `GET /health` 仍返回 `enabled:true`，**重启实例后**才返回 `enabled:false`。api.md S2-2 已按此修正
   （步骤含重启），S12 汇总已更新为 D8-1~D8-4。
2. **S7-2 空态核对的前置条件**：设置 api_token 后 `/audit-logs` 同样受鉴权保护（`authorized()` 对全部
   端点统一生效，health 除外），无鉴权头请求返回 `401 UNAUTHORIZED`；空态核对须在 token 复位后执行
   （实测复位后 `data:[]`）。api.md S7-2 已补充该前置条件说明。
3. **6 条白名单豁免规则**：table_comment_required / type_date_time / name_bak_prefix / dml_select_star /
   prog_name（注册但无判定逻辑）+ dml_delete_truncate（无独立触发点，无 WHERE DELETE 实际输出 dml_no_where），
   COV 段已列豁免清单与原因，覆盖率 74+6=80 达成。

## 3. 性能约束

- 单条用例 ≤5s、全量执行 ≤10min：满足（沿用 verify_report.md §6 既有结论，单条 SQL 审核开销 ≈ 0.02ms；
  全量 test.sql 执行含 80 条触发/反例用例在秒级内完成）。

## 4. 遗留跟进项

- 无未跟进差异项。上述 D8-4/D8-5 已记录于 api.md 对应样本节与 S12 汇总，属「存量行为为准」的文档化修正，
  不修改扩展源码。