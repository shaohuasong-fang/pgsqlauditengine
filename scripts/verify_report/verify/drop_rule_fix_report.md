# ddl_high_risk_drop 规则关闭后仍输出问题修复报告

> 对应需求：spec §5.6 需求 28/29、决策 D21/D22（WARNING 拦截语义 + 规则状态受控执行）
> 对应设计：design §4.4.5.1（规则状态受控执行模型）、§4.4.5.2（WARNING 断言更新）
> 关联文档：`regression_report_pg3.md`（任务组 28）、`cic_fix_report.md`（任务组 22-24）、`run_regression_pg.sh`（任务组 26.2）

---

## 1. 问题现象

在任务组 26.2 编排脚本 PG18 首轮实测中发现：通过 RESTful `PUT /api/v1/rules/ddl_high_risk_drop` 设置 `{"enabled":false}` 后，`DROP DATABASE` 语句**仍然输出**：

```
WARNING: SQL审核[ddl_high_risk_drop]: 删除数据库 "se_db_test" 属于高危操作，请谨慎
```

该 WARNING 既不遵循 `enabled=false` 静默语义，也不受 WARNING 拦截语义（任务组 25）约束（仍为 WARNING 而非 ERROR 拦截），与 `audit_emit` 中「规则被关闭则静默返回」的逻辑（rule_registry.c:314-315）矛盾，导致编排脚本清理段规则状态受控失效（后置清理段日志残留 `ddl_high_risk_drop` WARNING）。

## 2. 根因分析

全量 grep 扫描 `ereport((WARNING|ERROR|NOTICE)` 后发现 **2 处规则输出绕过统一出口 `audit_emit`**，均位于 `rule_ddl.c`：

| 函数 | 位置 | 语句类型 | 绕过行为 |
|------|------|---------|---------|
| `audit_ddl_dropdb` | rule_ddl.c:1424-1427 | `DROP DATABASE` | 直接 `ereport(WARNING)`，无 enabled 检查、无级别覆盖、无拦截语义 |
| `audit_ddl_droptablespace` | rule_ddl.c:1436-1439 | `DROP TABLESPACE` | 同上 |

其余 102 条规则（含清理段涉及的 `obj_publication`/`obj_subscription`/`obj_fdw`/`obj_language`）均走 `audit_emit` 或 `audit_emit_fixed`，enabled 检查正常。`se_audit_echo`（hook_utility.c:122）直接 `ereport(NOTICE)` 为命令兜底回显（非清理段规则），`api_server.c`/`ruleparse.c`/`tools.c` 的 `ereport` 为框架自身告警或解析器错误，均不属规则输出，不在本次修复范围。

## 3. 修复方案（rule_ddl.c 两处）

将 `audit_ddl_dropdb` 与 `audit_ddl_droptablespace` 的直接 `ereport(WARNING)` 改为 `audit_emit(AUDIT_WARNING, "ddl_high_risk_drop", ...)`：

- **enabled 检查生效**：规则关闭时静默跳过，清理段受控执行得以成立；
- **配置级别覆盖生效**：RESTful 可临时降级/提升输出级别；
- **WARNING 拦截语义生效**：命中时输出 `ERROR: SQL审核[ddl_high_risk_drop]: ...` + `DETAIL: SQL审核级别: WARNING（语句被拦截）`，语句被拦截；
- 消息文案 `删除数据库 "..." 属于高危操作，请谨慎` / `删除表空间 "..." 属于高危操作，请谨慎` 逐字不变（契约保护）；
- `audit_record_write("DROP DATABASE"...)` / `audit_record_write("DROP TABLESPACE"...)` 动作日志保留（拦截时因 `ereport(ERROR)` 长跳转不执行，与 `audit_ddl_drop` 各分支行为一致）。

## 4. 回归验证记录

### 4.1 PG18 单点受控开关验证（修复后）

RESTful API 连通（`http://127.0.0.1:8918/api/v1`），对 `ddl_high_risk_drop` 做开关切换，用不存在的库验证（无需预建对象，幂等）：

| 步骤 | 操作 | 结果 |
|------|------|------|
| ① 关闭 | `PUT {"enabled":false}` | `{"code":200,...,"updated":true}` |
| ② 执行 | `DROP DATABASE IF EXISTS se_db_nonexist` | 仅 PG 自身 `NOTICE: database "se_db_nonexist" does not exist, skipping`，**无审核消息**，EXIT=0 |
| ③ 恢复 | `PUT {"enabled":true}` | `{"code":200,...,"updated":true}` |
| ④ 执行 | `DROP DATABASE IF EXISTS se_db_nonexist` | `ERROR: SQL审核[ddl_high_risk_drop]: 删除数据库 "se_db_nonexist" 属于高危操作，请谨慎` + `DETAIL: SQL审核级别: WARNING（语句被拦截）`，EXIT=1 |

### 4.2 PG18 端到端编排验证（run_regression_pg.sh 18）

- 三段式流程正常：快照（102 条）→ 前置清理段关闭 5 条规则 → 用例段规则全开 → 后置清理段关闭 5 条规则 → 还原（102 条）；
- 清理段受控生效：日志中 `ERROR: SQL审核[ddl_high_risk_drop]` 仅出现在**用例段**（seg_cases.sql:342 `DROP SCHEMA`、:343 `DROP DATABASE`，属 WARNING 拦截语义的预期触发），清理段（seg_p0/seg_p14）**零拦截**；
- 全日志 0 FATAL/PANIC/TRAP；用例段 76 条 `ERROR: SQL审核`（ERROR/WARNING 拦截预期）+ 168 条 `NOTICE: SQL审核`（放行回显）；
- 非审核 ERROR 核验：`current transaction is aborted`（事务内拦截后 aborted，WARNING 拦截语义标准行为，预期）；`security label provider ... not loaded`（obj_security_label 用例注释已声明「审核先输出后语句报错，属预期」）。

### 4.3 顺带修复：dml_merge_check 用例 INSERT 缺列

`PART 12` dml_merge_check「预期不触发」用例 `MERGE ... WHEN NOT MATCHED AND s.id > 0 THEN INSERT (id) VALUES (s.id)` 实际执行时 `se_t_dml` 的 `name`/`amount`/`status` 为 NOT NULL，逐次报 `null value in column "..."`。已补全为 `INSERT (id, name, amount, status) VALUES (s.id, 'merge-ok', 0.00, 'ok')`，重跑后 `null value` 归零。

### 4.4 契约保护核对

- 源码改动仅限 `pgsqlauditengine/rule_ddl.c`（2 处改 `audit_emit`），无内核改动；
- `ddl_high_risk_drop` 消息文案逐字未动；`audit_record_write` 动作日志保留；
- 5 个 GUC、RESTful 契约、共享内存标识、扩展命名均未变更；
- 本修复对 PG11-18 全版本生效（`audit_emit` 为全版本统一出口），八版本全量回归见 `regression_report_pg3.md`（任务组 28）。

## 5. 归档信息

- 报告归档：`scripts/verify_report/verify/drop_rule_fix_report.md`
- 源码：`pgsqlauditengine/rule_ddl.c`（`audit_ddl_dropdb` L1424、`audit_ddl_droptablespace` L1436）
- 脚本：`scripts/run_regression_pg.sh`（curl 输出改重定向 `/dev/null`，避免日志堆叠）
- 测试用例：`scripts/pgsqlauditengine_test.sql`（dml_merge_check 用例 INSERT 补全 NOT NULL 列）
- 验证产物：远端 `/tmp/reg_pg18.log`、`/tmp/verify_ctrl.sh` 输出、`/tmp/snap.json`