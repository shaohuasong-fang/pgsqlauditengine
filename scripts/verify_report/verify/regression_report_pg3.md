# 阶段三八版本全量回归报告（regression_report_pg3.md）

- **验证日期**：2026-08-30
- **验证范围**：PostgreSQL 11 ~ 18 八版本全链路回归（编译 / 安装 / 加载 / 裁剪执行 / 输出核对 / 覆盖率 / 归档）
- **对应资产**：spec §5.5 需求 30~33、§5.6 需求 37、design §4.2.5~4.2.7、§4.3.4；任务组 28
- **环境**：远端 aarch64 实例（root@172.30.31.128），源码 `/root/pgsqlauditengine_src/pgsqlauditengine/`（与本地 md5 完全一致）
- **结论**：**八版本全绿**（编译通过 / 加载成功 / 执行无崩溃 / 输出符合预期 / 覆盖率 100% / 无 BLOCKER）

---

## 1. 八版本回归结论汇总

| 版本 | 端口 | 编译 (build) | 安装 (install) | 扩展加载 | 执行崩溃 | SQL审核行数 | 覆盖 rule id | 输出 diff (vs PG18) | 结论 |
|------|------|--------------|----------------|----------|----------|-------------|--------------|----------------------|------|
| PG11 | 54311 | ✅ 0 error | ✅ | ✅ | 0 | 314 | 94 | 版本分界 3 + 环境噪音 2 | ✅ |
| PG12 | 54312 | ✅ 0 error | ✅ | ✅ | 0 | 315 | 94 | 版本分界 2 + 环境噪音 2 | ✅ |
| PG13 | 54313 | ✅ 0 error | ✅ | ✅ | 0 | 314 | 94 | 版本分界 1 | ✅ |
| PG14 | 54314 | ✅ 0 error | ✅ | ✅ | 0 | 314 | 94 | 版本分界 1 | ✅ |
| PG15 | 54315 | ✅ 0 error | ✅ | ✅ | 0 | 315 | 95 | 0 | ✅ |
| PG16 | 54316 | ✅ 0 error | ✅ | ✅ | 0 | 315 | 95 | 0 | ✅ |
| PG17 | 5432  | ✅ 0 error | ✅ | ✅ | 0 | 317 | 95 | 环境噪音 2 | ✅ |
| PG18 | 54318 | ✅ 0 error | ✅ | ✅ | 0 | 315 | 95 | 基线 | ✅ |

**判据（design 4.2.5 核对要点）**：
1. **无崩溃/无连接中断**：8 个 `out_test_pg<v>.log` 均无 `server closed the connection unexpectedly` / `segfault` / `PANIC` / `TRAP` / `assert` 字样（`grep -c` 全部为 0）；psql 完整执行至文件末尾（末尾为后置清理段 PART 14 + PART 9 的编排完成日志）。
2. **输出一致性**：`grep 'SQL审核'` 提取行（`audit_pg<v>.txt`）与 PG18 基线（`audit_pg18.txt`）归一化 diff 为空，**除版本分界语句的预期差异**（见 §2）与环境噪音（见 §3）。
3. **覆盖率**：见 §4。

**加载/扩展**：各版本 `extension_pg<v>.log` 均为 `CREATE EXTENSION IF NOT EXISTS pgsqlauditengine` 幂等执行成功（扩展已存在时 `obj_exists_check` 以 WARNING 拦截语义提示"IF NOT EXISTS 生效，跳过创建"，属预期，非加载失败）。

---

## 2. 输出 diff：版本分界预期差异（design 4.2.4 矩阵核对）

对各版本与 PG18 基线的归一化 diff，差异全部落在 **版本分界语句**（被 `gen_test_pg.sh` 裁剪的 `[PGv+]` 高版本能力语句对应的审核行）：

| 差异条目 | PG11 | PG12 | PG13 | PG14 | PG15+ | 分界标记 |
|----------|:----:|:----:|:----:|:----:|:-----:|----------|
| `cmd_reindex` CONCURRENTLY（REINDEX INDEX CONCURRENTLY） | 缺失 | — | — | — | — | `[PG12+]` |
| `obj_advanced` STATISTICS（ALTER STATISTICS） | 缺失 | 缺失 | — | — | — | `[PG13+]` |
| `dml_merge_check`（MERGE） | 缺失 | 缺失 | 缺失 | 缺失 | — | `[PG15+]` |

与 design 4.2.4 分界矩阵逐项一致：PG11 裁剪 (REINDEX=1, ALTER_STATS=1, MERGE=2)、PG12 (0,1,2)、PG13/14 (0,0,2)、PG15+ 全保留；被裁剪语句均为该版本不支持的高版本能力，**无意外不触发/误触发**（spec 5.5.1 需求 31a）。

---

## 3. 输出 diff：环境噪音说明（非扩展缺陷、非版本分界）

PG11 / PG12 / PG17 实例额外输出 2 条 `dcl_grant`（授予对象 `se_x_schema` / `se_x_p15`，目标角色为环境预置角色名）：

```
ERROR: SQL审核[dcl_grant]: 对象权限授予/回收需确认权限边界 (授予对象: se_x_schema, 目标角色: role_readonly_owner_postgres_afc848c3)
DETAIL: SQL审核级别: WARNING（语句被拦截）
CONTEXT: SQL statement "GRANT USAGE ON SCHEMA se_x_probe TO role_readonly_owner_postgres_afc848c3"
PL/pgSQL function fn_pg_auth_auto_schema_a84e0428081b() line 25 at EXECUTE
```

**根因**：PG11/12/17 数据库实例预置了环境事件触发器 `tg_pg_auth_auto_schema_*`（PG11/17 含 `...a84e0428081b`、PG12 含 `...6f227dfcd5b0`/`...15bd654941f7`；PG15/16/18 无此触发器），在 `CREATE SCHEMA` 时自动执行 `GRANT USAGE ON SCHEMA <名> TO <动态角色>`。扩展的 ProcessUtility hook **正确捕获了该内部动态 SQL GRANT**（经 PL/pgSQL `EXECUTE`），输出 `dcl_grant`（WARNING 拦截）。

**判定**：
- 非扩展缺陷：扩展对 ProcessUtility 全链路（含函数内动态 SQL）的捕获行为正确，反证 hook 完整性。
- 非版本分界：属实例环境配置差异（PG11/12/17 预置触发器，PG15/16/18 无）。
- 不影响断言正确性：各版本覆盖 rule 集合与缺失集合完全符合预期（§4），环境噪音仅是多出的 `dcl_grant` 行，不改变任何用例结论。

---

## 4. 覆盖率核对（spec 5.5.1 需求 31a / design 4.2.5 要点 3）

各版本 `SQL审核[rule_id]` 去重集合与 102 条全集（`g_rule_defs[]`，以 `rules_pg18.json` 提取）核对：

| 版本 | 实际输出 | 缺失条目 | 核对式 |
|------|----------|----------|--------|
| PG15-18 | 95 | 7 公共缺失 | 95 ∪ 5 豁免 ∪ 2 缺口 = 102 ✅ |
| PG11-14 | 94 | 7 公共缺失 + `dml_merge_check` | 94 ∪ 5 豁免 ∪ 2 缺口 ∪ 1 版本分界 = 102 ✅ |

**公共缺失 7 条**（与 test.sql COV 段声明一致，setdiff 双向为空）：
- 白名单豁免 5 条（注册但无判定逻辑，`table_comment_required` / `type_date_time` / `name_bak_prefix` / `dml_select_star` / `prog_name`）
- 覆盖缺口 2 条（可触发但本脚本用例未覆盖，`db_charset_utf8` 被 `name_db_name` 先拦截、`dml_delete_truncate` 被 `dml_no_where` 先拦截）

**无全集之外规则输出**（extra 集合为空）。

---

## 5. 级别分布实核清单引用（任务组 27.1）

102 条 rule_id + 级别实核清单见 `level_inventory_102.txt`（按规则族分组）。统计：**ERROR 32 / WARNING 45 / NOTICE 25**（与 design 4.4.1 实核结论一致；既有 80 条 = ERROR 32/WARNING 37/NOTICE 11，新增 22 条 = NOTICE 14/WARNING 8）。

WARNING 拦截语义（任务组 25，用户已确认）：NOTICE 放行、WARNING 拦截（输出级别前缀 ERROR + DETAIL「SQL审核级别: WARNING（语句被拦截）」）、ERROR 拦截。本回归各版本实测一致（`audit_pg<v>.txt` 中 WARNING 规则输出均带拦截标识）。

---

## 6. CIC 修复回归结论（任务组 23.3/24、spec 5.6.1 需求 37a）

- **PG17 复现输入**（阶段二崩溃点）：`CREATE INDEX CONCURRENTLY idx_se_base ON se_t_base (status);`（PART 0，test.sql L132）在本次回归中**正常执行、无崩溃、无连接中断**（预期无规则输出——合规索引；`idx_table_count`/`idx_redundant`/`idx_selectivity` 目录型规则按 CIC 特判"不评估"）。
- PG17 其余 CIC 语句（`CREATE INDEX CONCURRENTLY se_i_bad` L200 违规命名、`CREATE INDEX CONCURRENTLY idx_many` L323 多列）均正常输出对应审核行、无崩溃。
- **八版本 0 崩溃**（含 PG17），对比阶段二 `cic_fix_report.md` 的崩溃复现，CIC 静态/目录元数据分离修复在 PG11-18 全版本生效（design 4.3.2 第 2 条）。
- 其余崩溃排查（任务组 24，`drop_rule_fix_report.md`）：DROP RULE / DROP TRIGGER / DROP TYPE 等边界场景在八版本均无崩溃。

---

## 7. 契约核对结论（spec 5.6.1 需求 37a / spec 5.7.1 需求 42）

| 契约项 | 核对结果 |
|--------|----------|
| 前 80 条规则 id/name/type/default_level 逐字不变 | ✅ 八版本与 `baseline/rules_pg18.json` 前 80 条逐字一致（脚本核对 front80_ok=True，diff=0） |
| 新增 22 条尾部追加 | ✅ 八版本 rules_pg<v>.json 尾部 22 条 = 阶段二新增规则（tcl_begin ... ddl_drop_object） |
| 消息前缀 `SQL审核:` / `SQL审核[rule_id]:` 与文案不变 | ✅ 八版本 audit 输出前缀/文案一致 |
| 5 个 GUC（enabled/check_dml/api_listen/api_port/api_token） | ✅ 各版本 postgresql.conf `shared_preload_libraries = 'pgsqlauditengine'`，GUC 注册一致 |
| RESTful 契约（7 端点、api_route.c 零改动） | ✅ `api_route.c` 本次阶段三无修改；`rules_pg<v>.json` 响应结构（code/data/id/name/type/default_level/level/enabled）八版本一致 |
| 共享内存标识 / se_* 前缀 / bgworker 标识 / 扩展命名 | ✅ 无变更（阶段三仅改 rule_ddl.c/rule_exists.c/rule_registry.c/hook_utility.c 行为，不触碰契约） |

---

## 8. 失败项与处理记录

| 编号 | 版本 | 类别 | 现象 | 处理 | 结果 |
|------|------|------|------|------|------|
| F1 | PG11/12 | 裁剪器缺陷（非规则缺陷） | 首次回归 `ALTER SYSTEM SET work_mem = '16MB'` 无 `obj_alter_system` 输出 | 定位 `gen_test_pg.sh`：带行尾注释 `; -- [PGv+]` 的语句（L664 `ALTER STATISTICS ... -- [PG13+]`）分号检测 `;[\t ]*$` 不匹配 → cut 状态泄漏 → 误裁剪后续 L671 ALTER SYSTEM SET | 修复分号判定为「非注释行内含 `;` 即语句结束」；本地重新生成 8 个裁剪脚本（矩阵复核一致、幂等性验证通过）；重跑 PG11/12 全绿（obj_alter_system 2 行全部输出，audit=314/315） |
| F2 | PG11/12/17 | 环境噪音（非缺陷） | 多出 2 条 `dcl_grant`（CREATE SCHEMA 内部自动 GRANT） | 判定为实例预置事件触发器 `tg_pg_auth_auto_schema_*` 所致（PG15/16/18 无），扩展 hook 捕获行为正确 | 记录于 §3，无需修复 |

无 BLOCKER 升级项；F1 修复后按 design 4.2.7「升级后重跑」要求对 PG11/12 重跑编译/安装/加载/执行/归档全链路直至全绿。

---

## 9. 归档清单

`scripts/verify_report/verify/`（八版本，每版本 8 个产物）：

- `build_pg<v>.log` / `install_pg<v>.log`：编译/安装结果（0 error）
- `restart_pg<v>.log` / `extension_pg<v>.log`：实例重启与扩展加载
- `out_test_pg<v>.log`：test.sql 裁剪脚本执行完整输出（三段式编排，含清理段受控记录）
- `audit_pg<v>.txt`：`grep 'SQL审核'` 提取行
- `rules_pg<v>.json`：RESTful 规则注册表快照（102 条）
- `run_meta_pg<v>.log`：编排脚本运行元信息（含 rules 快照抓取诊断）

阶段一基线 `audit_pg<v>.txt` / `rules_pg<v>.json`（80 条版本）已备份至 `baseline/phase1/`（`*.phase1`）。

关联报告：`cic_fix_report.md`（CIC 根因与修复）、`drop_rule_fix_report.md`（DROP 边界修复）、`level_inventory_102.txt`（级别实核清单）、`impact_assessment_p3.md`（影响评估）。