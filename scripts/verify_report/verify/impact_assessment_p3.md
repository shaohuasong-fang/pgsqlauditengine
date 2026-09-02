# 阶段三不可变契约影响评估记录（任务组 27.2）

> 事实源：`g_rule_defs[]`（rule_registry.c:26-159）实核 102 条；级别语义定稿于任务组 25，
> 用户已确认采纳 WARNING 拦截（决策 Q8）；分布实核 32/45/25（决策 D21/Q7，修正早期 42/40/20 偏差）。

## 1. 变更项（规则集与行为级变更）

| 变更项 | 说明 |
|--------|------|
| 规则总数 | 80 → 102（`g_rule_defs[]` 尾部追加 22 条，前 80 条逐字不变） |
| `g_cmd_map[]` 命令映射 | 阶段二扩展（新增规则命令族映射；api_route.c 零改动） |
| GET /rules、GET /config 返回条数 | data 数组长度 80 → 102（**属预期数据长度变化，非契约破坏**，design 3.6.2） |
| audit-logs rule 取值 | 新增 22 个合法 rule id 取值 |
| 验证基线快照 / api.md 样本 | 同步至 102 条规则集（baseline 重新生成） |
| 级别→放行/拦截行为 | **NOTICE→放行、WARNING→拦截、ERROR→拦截**（阶段二语义为 WARNING 不拦截） |

## 2. 不变项（不可变契约，零破坏）

- 消息前缀 `SQL审核:` / `SQL审核[rule_id]:` 与既有消息文案（阶段二 80 条逐字一致，仅尾部追加）
- 5 个 GUC（`PGSAUDAUDITENGINE.enabled/check_dml/api_listen/api_port/api_token`）名与默认值
- RESTful 路径、响应包裹 `{code,error_code,error_message,data}`、错误码、Bearer 鉴权（api_route.c 未修改）
- 共享内存标识、`se_*` 前缀、bgworker 标识、扩展命名

## 3. WARNING 拦截影响面（需求 40）

- 影响范围：45 条 WARNING 规则行为变更 = 既有 37 条（40 条早期估算修正为 37）+ 阶段二新增 8 条
  （`tcl_prepared`/`dcl_grant`/`dcl_grant_role`/`dcl_default_privileges`/`dcl_role`/
  `prog_trigger`/`prog_event_trigger`/`prog_rule`）
- 冲突点：与阶段二决策 D10「新增规则级别限 NOTICE/WARNING、不新增 ERROR」冲突——8 条新增
  WARNING 规则实质成为拦截规则；test.sql 中 WARNING 级用例预期输出从「告警不阻断」更新为
  「输出后语句被拦截」（任务组 26.1 已批量更新断言注释）
- 取舍选项：① 采纳 WARNING 拦截（新语义）② 维持 WARNING 不拦截 ③ 仅对新规则生效
- **取舍结论**：用户已确认采纳**主方案 A：WARNING 拦截**——`audit_emit`/`audit_emit_fixed`
  WARNING 分支 `ereport(ERROR, ERRCODE_CHECK_VIOLATION)` + `errdetail("WARNING 级别审核拦截: ...")`，
  输出级别前缀为 ERROR 并带 WARNING 标识（Q8 用户确认记录；任务组 25 实现）

## 4. 五方面影响面分析（design 3.6.2）

| 方面 | 影响面 |
|------|--------|
| RESTful | 路径/包裹/错误码/鉴权零变化；仅返回条数 80→102（纯追加，Q5 默认方案：前 80 顺序不变 + 尾部 22） |
| 审计日志 | 新增 22 个 rule 取值；WARNING 拦截语句也写共享内存审计日志（VIOLATION 记录） |
| 跨版本编译 | 级别语义在 PG11-18 编译产物统一实现（`compat.h`/`audit_emit` 全版本生效） |
| 性能 | 判定均为 O(1) 检查，无新增查询；CIC 特判减少误触发 |
| 文档 | pgauditrule.md 级别语义、HOOKS_API.md 消息格式、api.md 核对说明、spec §6.7.2 同步更新 |

## 5. 关联产物

- 级别实核清单：`level_inventory_102.txt`（102 条 rule_id+级别，任务组 27.1）
- spec §6.7.2 修正：ERROR 42→32 / WARNING 40→45 / NOTICE 20→25（含既有/新增拆分）
- 归档日志：`warn_verify_pg18.out`（WARNING 拦截实测）、`cic_fix_report.md`、`drop_rule_fix_report.md`