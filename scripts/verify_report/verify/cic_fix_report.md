# CIC（CREATE INDEX CONCURRENTLY）崩溃修复与其余风险点排查报告

> 对应需求：spec §5.6 需求 34/35/36/37
> 对应设计：design §4.3.1（复现与根因）、§4.3.2（修复）、§4.3.3（排查清单）、§4.3.4（回归验证）、§4.2.6（归档）
> 关联文档：`regression_report_pg3.md`（任务组 28，八版本全量回归，交叉引用）

---

## 1. 根因分析（引用任务组 22.2）

### 1.1 崩溃现象

在 PG17 实例（172.30.31.128:5432）执行 `CREATE INDEX CONCURRENTLY` 语句时，服务端稳定崩溃，客户端 psql 报：

```
server closed the connection unexpectedly
```

PG17 `server.log` 捕获到 TRAP 断言失败：

```
TRAP: failed Assert("SysCache[cacheId]->cc_nkeys == 2"), File: "syscache.c", Line: 237, PID: ...
```

调用链（崩溃堆栈）指向扩展内两处符号：

```
pgsqlauditengine.so(+0x7c34) = audit_ddl_index   （rule_ddl.c）
pgsqlauditengine.so(+0xd77c) = se_post_parse_analyze（hook_analyze.c，post_parse_analyze 钩子）
```

### 1.2 直接根因：SearchSysCache2 键数不匹配

`rule_ddl.c` `check_index_selectivity`（L657）对系统缓存 **`STATRELATTINH`（pg_statistic，starelid/staattnum/staiinh）** 使用两键调用：

```c
tup = SearchSysCache2(STATRELATTINH,
                      ObjectIdGetDatum(relid),
                      Int16GetDatum(attno));
```

但 `STATRELATTINH` 是 **3 键缓存**（`starelid=2`、`staattnum=3`、`staiinh=4`）。PG 在 debug 构建下断言 `cc_nkeys == 2` 失败即触发 TRAP，导致服务端崩溃（非 debug 构建下表现为未定义行为/读取未初始化缓存键）。

### 1.3 结构性根因：ProcessUtility 钩子内对 CIC 语句做目录元数据访问

`audit_ddl_index`（rule_ddl.c:681）静态区（解析树检查）安全通过后，进入**目录元数据区**（原 L738-756）：

- `relation_openrv(stmt->relation, AccessShareLock)` —— 打开目标表关系；
- `RelationGetIndexList(rel)` —— 查询 pg_index 目录；
- `check_redundant_index`（L581-613）—— 含 `index_open(idxid, ...)` + `BuildIndexInfo(idxrel)`；
- `check_index_selectivity`（L636-678）—— 含 `get_attnum` + `SearchSysCache2(STATRELATTINH)`。

`CREATE INDEX CONCURRENTLY` 使用内核独立的**两阶段事务/快照机制**（第一阶段创建 INVALID 索引后提交，第二阶段在独立快照中构建）。ProcessUtility 钩子内对 CIC 语句执行关系打开与系统目录访问，与内核 CIC 事务管理冲突，叠加 1.2 的键数缺陷触发崩溃。

同时 `rule_exists.c` 第一级流水线 `audit_obj_exists_check`（L779-825）对 `T_IndexStmt`（含 CIC）走存在性链：`exists_index`（L425-428）→ `get_namespace_oid` + `get_relname_relid` + `SearchSysCache1(RELOID)`，同样构成目录访问路径（rule_exists.c:7-9 既有头注释"禁止 SPI 避免 CIC 段错误"即为此风险的同类佐证）。

### 1.4 结论

根因在 **ProcessUtility 钩子内对 CIC 语句执行系统目录/关系元数据访问**（CIC 特殊事务/快照机制冲突），直接崩溃点为 `SearchSysCache2(STATRELATTINH)` 键数不匹配。修复限定在 `pgsqlauditengine` 扩展源码内，不修改 PostgreSQL 内核。

---

## 2. 修复方案（任务组 23）

### 2.1 改造 rule_ddl.c audit_ddl_index 两段式拆分（23.1）

`audit_ddl_index` 拆分为「静态区 + CIC 特判 + 目录元数据区」：

1. **isconstraint 分支**不变；
2. **静态区**（命名 `audit_check_name`/`audit_check_prefix`、`stmt->concurrent` 判 `idx_concurrently`、`accessMethod` 判 `idx_method`、`list_length(indexParams)` 判 `idx_field_count`）—— 全版本、CIC 与非 CIC 均执行；
3. **CIC 特判**（rule_ddl.c:745-750，位于 `relation_openrv` 之前）：

```c
if (stmt->concurrent)
{
    audit_record_write("CREATE INDEX (concurrent)", idxname ? idxname : tblname,
                       "index", AUDIT_NOTICE);
    return;
}
```

CIC 场景仅写底层记录，跳过目录元数据区（`relation_openrv`/`RelationGetIndexList`/`check_redundant_index`/`check_index_selectivity`）与 `idx_table_count` 评估（降级为"不可判定"），附防呆注释（rule_ddl.c:738-744）。

### 2.2 修正 check_index_selectivity 键数（23.1 附带）

`rule_ddl.c:657` 两键调用改为三键：

```c
tup = SearchSysCache3(STATRELATTINH,
                      ObjectIdGetDatum(relid),
                      Int16GetDatum(attno),
                      BoolGetDatum(false));
```

非 CIC 场景的 `idx_selectivity` 目录型检查得以在键数正确的前提下继续工作。

### 2.3 改造 rule_exists.c audit_obj_exists_check CIC 特判（23.2）

`audit_obj_exists_check`（rule_exists.c:792-797）在进入存在性判定循环前：

```c
if (nodeTag(stmt) == T_IndexStmt &&
    ((IndexStmt *) stmt)->concurrent)
    return EXISTS_CHECK_PASS;
```

CIC（含 `IF NOT EXISTS` 组合）直接 PASS，存在性冲突交由内核 `DefineIndex` 处理；其余对象类型（表/视图/序列等普通 CREATE）保持既有存在性检查。

---

## 3. 其余崩溃/连接断开风险点排查结论（任务组 24）

### 3.1 全量 grep 扫描（24.1）

执行命令：

```
grep -rnE "relation_open|index_open|table_open|heap_open|SearchSysCache|systable_beginscan|get_relname_relid|get_namespace_oid|get_database_oid|get_extension_oid|BuildIndexInfo|RangeVarGetRelid" rule_*.c hook_*.c
```

扫描结果：目录访问点**全部集中在 `rule_ddl.c` 索引族与 `rule_exists.c` 存在性链**，与 design §4.3.3 表格一致。

### 3.2 逐点评估结论（design §4.3.3 排查清单 9 点）

| # | 排查点 | 源码位置 | 风险评估 | 结论 |
|---|--------|---------|---------|------|
| ① | `audit_ddl_index` 目录元数据区 | rule_ddl.c:752-769、check_redundant_index L597-613、check_index_selectivity L657 | 高危（CIC 崩溃根因） | **已修复**（23.1 CIC 特判在 `relation_openrv` 之前生效；23.2 附 SearchSysCache3 键数修正） |
| ② | `audit_obj_exists_check` 存在性链 | rule_exists.c:779-826、exists_* L360-660 | 中高危 | **已修复**（23.2 CIC 特判 PASS）；其余对象类型为普通 CREATE（非特殊执行机制），保持既有目录访问，跨版本回归无崩溃 |
| ③ | REINDEX INDEX CONCURRENTLY | rule_cmd.c:106-124 | 低 | 无风险：仅读 `ReindexStmt` 字段（relation->relname / name / concurrent），无目录访问 |
| ④ | REFRESH MATERIALIZED VIEW CONCURRENTLY | rule_cmd.c:129-143 | 低 | 无风险：仅读 `RefreshMatViewStmt` 字段 |
| ⑤ | VACUUM/CLUSTER/LOCK/COPY 等 cmd_* | rule_cmd.c | 低 | 无风险：仅读 stmt 字段（relation->relname 等） |
| ⑥ | DML 审核（post_parse_analyze） | hook_analyze.c | 低 | 无风险：`get_func_name`/`get_opname` 为 syscache 快捷函数，解析阶段快照正常 |
| ⑦ | rule_object/rule_program/rule_dcl/rule_tcl | 各 rule_*.c | 无目录访问 | 无风险：grep 确认不含目录访问函数 |
| ⑧ | `audit_ddl_alter_table`（含 AT_AddIndex/ADD CONSTRAINT 自动索引） | rule_ddl.c:518-542 | 低 | 无风险：仅读 `AlterTableStmt` 命令表，无目录访问；ADD CONSTRAINT 自动索引由内核处理，不在 hook 内做目录访问 |
| ⑨ | 并发/时序类异常场景 | spec §5.6.3 异常 1 | 待回归 | 单语句已验证无崩溃；并发偶发场景结合任务组 28 八版本全量回归与 server.log/core 核验 |

### 3.3 修复/验证结论（24.2）

**无其余崩溃/连接断开点**。排查清单 9 点中，① 与 ② 已由 23.1/23.2 修复并单点验证，③~⑧ 均为"仅读语句字段"或"解析阶段 syscache 快捷函数"的低/无风险点，无新增需修复项。

---

## 4. 修复回归验证记录

### 4.1 PG17 单点验证（design §4.3.4 第 1 条）

修复后八版本全量重编译（`rm -f *.o *.so` + `make USE_PGXS=1 PG_CONFIG=<cfg> install`，PG11-18 全部 OK）并重启 PG17 实例后执行：

| 用例 | 输入 | 结果 |
|------|------|------|
| CIC 首次创建 | `CREATE INDEX CONCURRENTLY idx_se_cic_t_id ON se_cic_t(id);` | `CREATE INDEX`，索引创建成功，无崩溃/断言 |
| CIC + INE 重复 | `CREATE INDEX CONCURRENTLY IF NOT EXISTS idx_se_cic_t_id ON se_cic_t(id);` | 仅内核 `NOTICE: relation "idx_se_cic_t_id" already exists, skipping`，**无** `obj_exists_check` WARNING（CIC 特判 PASS 生效） |
| 非 CIC 拦截 | `CREATE INDEX idx_se_cic_t_name ON se_cic_t(name);` | `ERROR: SQL审核[idx_concurrently]: 创建索引 "idx_se_cic_t_name" 必须使用 CONCURRENTLY，避免锁住写表操作`（行为不变） |
| 非 CIC + INE 已存在 | `CREATE INDEX IF NOT EXISTS idx_se_cic_t_id ON se_cic_t(id);` | `WARNING: SQL审核[obj_exists_check]: 对象已存在, IF NOT EXISTS 生效, 跳过创建 (索引: idx_se_cic_t_id)`（行为不变） |
| server.log | 修复后 grep TRAP/PANIC/assert | 无新增 TRAP（仅 3 条修复前复现时的历史遗留，PID 35433/35511/37758） |

测试表 `se_cic_t`：`id int PRIMARY KEY, name text NOT NULL, val numeric(10,2) NOT NULL`，含 1000 行数据并 ANALYZE（满足 table_pk_required/table_column_not_null/type_min_size 等建表规则）。

### 4.2 契约保护核对（design §4.3.4 第 3 条）

- 源码改动仅限 `pgsqlauditengine` 扩展：`rule_ddl.c`、`rule_exists.c`（无内核文件改动）；
- 既有规则消息前缀 `SQL审核:` / `SQL审核[rule_id]:` 与文案不变；`idx_concurrently`/`idx_field_count`/`idx_method`/`idx_redundant`/`idx_selectivity`/`obj_exists_check` 消息逐字未动；
- 5 个 GUC、RESTful 契约（api_route.c 零改动）、共享内存标识、`se_*` 前缀、bgworker 标识、扩展命名均未变更。

### 4.3 全版本回归（design §4.3.4 第 2 条）

八版本（PG11-18）重编译/安装/加载后，`pgsqlauditengine_test.sql` 全量用例（含 CIC 用例）无崩溃/连接中断的完整回归结果见 **`regression_report_pg3.md`（任务组 28）**。

---

## 5. 归档信息

- 报告归档：`scripts/verify_report/verify/cic_fix_report.md`
- 源码：`pgsqlauditengine/rule_ddl.c`（L745 CIC 特判、L657 SearchSysCache3）、`pgsqlauditengine/rule_exists.c`（L792 CIC 特判）
- 验证产物：远端 `/tmp/cic_verify.out`、`/tmp/noncic_verify.out`、PG17 `/data/17/pgdata/server.log`