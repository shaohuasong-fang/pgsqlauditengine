# pgsqlauditengine 跨版本差异点适配说明（PG11-18）

> 所有差异点均经各版本内核源码（postgresql-11.22~18.6）逐点核实，并在 `compat.h`
> 兼容层集中封装为宏；业务规则文件不散落裸 `#if`（钩子签名定义处、case 标签隔离、
> include 选择除外）。

## 1. 钩子签名差异

### 1.1 ProcessUtility 钩子三态

| 版本 | 签名 |
|------|------|
| PG14+ | `(pstmt, queryString, readOnlyTree, context, params, queryEnv, dest, QueryCompletion *qc)` |
| PG13 | `(pstmt, queryString, context, params, queryEnv, dest, QueryCompletion *qc)` |
| PG11-12 | `(pstmt, queryString, context, params, queryEnv, dest, char *completionTag)` |

- 适配：`compat.h` 定义 `se_ProcessUtility_hook_type`（三态 typedef）与
  `SE_PU_HOOK_PARAMS` 展开宏，`hook_utility.c` 的 `se_process_utility()` 用该类型
  定义并用宏做链式调用。
- 验证：PG11-18 全量编译通过，钩子链式调用（prev / standard）行为一致。

### 1.2 post_parse_analyze 钩子两态

| 版本 | 签名 |
|------|------|
| PG14+ | `(ParseState *pstate, Query *query, JumbleState *jstate)` |
| PG11-13 | `(ParseState *pstate, Query *query)` |

- 适配：`compat.h` 定义 `se_post_parse_analyze_hook_type`（两态 typedef）与
  `SE_ANALYZE_HOOK_PARAMS` 宏，`hook_analyze.c` 对应实现。
- 验证：八版本 DML 规则（`dml_no_where`/`dml_count_star`/`dml_left_fuzzy` 等）
  触发与消息文案与 PG18 一致。

## 2. 解析树节点结构差异

### 2.1 VACUUM：`VacuumStmt->options`

| 版本 | options 类型 | is_vacuumcmd |
|------|-------------|--------------|
| PG12+ | `List *`（DefElem） | 有 |
| PG11 | `int`（VACOPT_* 位掩码） | 无 |

- 适配：`SE_IS_VACUUMCMD`（PG12+ 读 `is_vacuumcmd`；PG11 判 `options & VACOPT_ANALYZE`）、
  `SE_VACOPT_PRESENT`（PG12+ 走 `se_vacopt_present(List, name)`；PG11 走
  `se_vacopt_bit(int, name)`，实现在 `rule_common.c`）。
- 验证：`VACUUM`/`VACUUM FULL`/`ANALYZE` 的 `cmd_vacuum`/`cmd_analyze` 结论与
  PG18 一致。

### 2.2 REINDEX：`ReindexStmt` 并发检测

| 版本 | 并发字段 |
|------|----------|
| PG14+ | `params` 为 `List *`（DefElem，含 `concurrently`） |
| PG12-13 | `bool concurrent` |
| PG11 | `int options` 位掩码，且**不支持 CONCURRENTLY 语法** |

- 适配：`SE_REINDEX_CONCURRENT`（PG14+ 走 `se_vacopt_present(params,"concurrently")`；
  PG12-13 读 `concurrent`；PG11 恒 false）。
- 验证：`REINDEX TABLE`/`REINDEX INDEX CONCURRENTLY` 的 `cmd_reindex` 消息与 PG18
  一致；PG11 上 CONCURRENTLY 为语法错误且不触发（符合预期）。

### 2.3 CreateTableAsStmt / AlterTableStmt 目标类型字段

| 版本 | 字段名 |
|------|--------|
| PG14+ | `objtype` |
| PG11-13 | `relkind`（同 `ObjectType` 类型） |

- 适配：`SE_CTAS_OBJTYPE` / `SE_ALT_OBJTYPE`。
- 验证：`CREATE TABLE AS`（`obj_ctas`）、物化视图判定（`T_CreateTableAsStmt` +
  `OBJECT_MATVIEW`）八版本一致。

### 2.4 MERGE：CMD_MERGE

- PG15 起引入 `CMD_MERGE`/`MergeAction`/`Query.mergeActionList`。PG11-14 编译期
  不得引用任何 MERGE 符号。
- 适配：`SE_HAS_MERGE` 宏 + `hook_analyze.c`/`hook_utility.c` 中
  `#if PG_VERSION_NUM >= 150000` 隔离 `CMD_MERGE` case。
- 验证：MERGE 仅 PG15+ 触发 `dml_merge_check`；PG11-14 语句正常执行（语法级
  不支持，无崩溃、无审核干扰）。

### 2.5 ALTER STATISTICS：AlterStatsStmt

- PG13 起引入 `AlterStatsStmt`。适配：`SE_HAS_ALTER_STATS` + case 标签
  `#if PG_VERSION_NUM >= 130000` 隔离（`hook_utility.c`、`rule_object.c`）。
- 验证：`ALTER STATISTICS` 仅 PG13+ 触发 `obj_advanced`；PG11-12 语句正常执行。

### 2.6 ALTER TYPE：AlterTypeStmt

- PG13 起引入 `AlterTypeStmt`（仅覆盖 `ALTER TYPE ... SET (options)`）。
  PG11-12 的 `ALTER TYPE ... ADD/DROP/ALTER ATTRIBUTE` 走 `AlterTableStmt`
  （`relkind = OBJECT_TYPE`），与 PG18 行为一致（均走 `audit_ddl_alter_table`）。
- 适配：`audit_ddl_alter_type` 声明/定义与 `T_AlterTypeStmt` case 用
  `#if PG_VERSION_NUM >= 130000` 隔离。
- 验证：PG11-18 全量编译通过；ALTER TYPE 属性操作跨版本走同一审核路径。

### 2.7 A_Const 字符串值访问

| 版本 | A_Const 结构 |
|------|--------------|
| PG15+ | `union ValUnion val`（`sval` 为 `String` 结构体值，字符串在 `String.sval`） |
| PG11-14 | `Value val` 结构体（字符串在 `val.val.str`） |

- 适配：`SE_A_CONST_STR(c)`（PG15+ 取 `c->val.sval.sval`；PG11-14 判
  `c->val.type == T_String` 后取 `c->val.val.str`）。
- 验证：`SET ROLE`/`ALTER SYSTEM` 的值解析八版本一致。

## 3. API / 头文件 / 运行期差异

### 3.1 保留字 API：ScanKeywordLookup

| 版本 | API |
|------|-----|
| PG12+ | `ScanKeywordLookup(name, &ScanKeywords)` 返回下标，分类在 `ScanKeywordCategories[]` |
| PG11 | `ScanKeywordLookup(name, ScanKeywords, NumScanKeywords)` 返回指针，分类在 `ScanKeyword->category` |

- 适配：`se_is_reserved_keyword()`（实现于 `rule_common.c`）。
- 验证：`name_reserved` 规则八版本一致（保留字表名被拒绝）。

### 3.2 表扫描 API

| 版本 | API |
|------|-----|
| PG12+ | `table_open`/`table_close`（`access/table.h`） |
| PG11 | `heap_open`/`heap_close`（`access/heapam.h`） |

- 适配：`SE_TABLE_OPEN`/`SE_TABLE_CLOSE` + `rule_exists.c` 按版本 include。
- 验证：`obj_exists_check` 流水线（Policy/Conversion/Collation 等目录扫描）
  八版本正常。

### 3.3 系统目录 tuple 取 OID

- PG12+：`FormData_*` 结构含 `oid` 成员（`GETSTRUCT(tp)->oid`）。
- PG11：无 `oid` 成员，用 `HeapTupleGetOid(tp)` 宏。
- 适配：`SE_TUPLE_OID`。验证：目录扫描取 OID 八版本一致。

### 3.4 头文件路径

- `access/relation.h` 为 PG12 起引入，PG11 中 `relation_open` 等声明于
  `access/heapam.h`（`compat.h` 统一处理）。
- PG11 的 `PolicyPolrelidPolnameIndexId` 需 include `catalog/indexing.h`。
- 验证：八版本全量编译通过。

### 3.5 共享内存请求时机：shmem_request_hook

- PG15+ 提供 `shmem_request_hook`；PG11-14 无此钩子，需在 `_PG_init` 中直接调用
  `RequestAddinShmemSpace()`。
- 适配：`pgsqlauditengine.c` 两态处理。
- 验证：八版本重启加载无 `out of shared memory` 错误。

### 3.6 命令名获取：CreateCommandName

| 版本 | API |
|------|-----|
| PG13+ | `CreateCommandName(Node*)`（static inline，内部 `GetCommandTagName(CreateCommandTag(...))`） |
| PG11-12 | `CreateCommandTag(Node*)`（直接返回 `const char *`，支持 `T_PlannedStmt`） |

- 适配：`SE_CMDNAME`（PG13+ 走 `CreateCommandName`；PG11-12 走 `CreateCommandTag`）。
- 说明：早期版本曾因**增量编译残留旧 `.o`** 导致 `U GetCommandTagName` 未定义
  符号；`make clean` 全量重编后消除（PGXS 不跟踪 `.h` 依赖，务必全量重编）。

### 3.7 bgw_library_name 数组大小

- `api_server.c:35` 原用 `sizeof(worker.bgw_library_name)`（错误），修正为
  `MAXPGPATH`，与 PG18 一致。

## 4. 天然兼容点（无需改造，已验证）

| 兼容点 | 说明 |
|--------|------|
| 表级约束节点 | `CreateStmt` 的表级主键/外键/唯一约束结构跨版本一致，`table_pk_*`/`table_no_foreign_key` 规则直接可用 |
| 共享内存 API | `ShmemInitStruct`/`ShmemInitHash`/环形缓冲结构跨版本一致 |
| GUC API | `DefineCustom*Variable` 跨版本一致 |
| BackgroundWorker API | `RegisterBackgroundWorker` 跨版本一致 |
| RESTful 路由 | `api_route.c` 无任何版本分支（契约文件未污染） |
| 规则注册表 | `rule_registry.c`（`g_rule_defs[]`）无任何版本分支（契约文件未污染） |