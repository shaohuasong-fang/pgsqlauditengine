# pgsqlauditengine 审核规则总览（pgauditrule.md）

> **状态：正式版（v1.0）**——规则与命令覆盖的唯一权威说明书，作为 `g_rule_defs[]` 与 `g_cmd_map[]`（`rule_registry.c`）两份编译期数据的可读投影。
> **核对基准**：PG18/current 官方 `sql-commands.html`（https://www.postgresql.org/docs/current/sql-commands.html），版本分界与 `docs/version_matrix.md` §3 一致。
> **对应需求**：spec §5.4 需求 22（命令全集覆盖复查）、需求 23（缺失命令规则补充）、需求 24（命令兜底不放行）、需求 26（pgauditrule.md 交付）；design 3.5。
> **契约**：既有 80 条规则逐字不变（仅 `g_rule_defs[]` 尾部追加）；新增 22 条级别限 NOTICE/WARNING（决策 D10/D11），无新增 ERROR。
> **数据来源一致性**：第 1/2 章由 `g_rule_defs[]`（rule_registry.c:26-159）与 `g_cmd_map[]`（rule_registry.c:405-458）直接生成，无人工抄写漂移；第 4 章示例输出以 19.3 实际验证日志回填核对为准。

---

## 1. 规则总表（102 条，与 g_rule_defs[] 逐条一致）

规则元数据四元组 `rule_id/规则名称/类型/默认级别` 与 `g_rule_defs[]`（rule_registry.c:26-159）逐条一致；「是否可配置」全部为**是**——102 条规则均经共享内存动态开关（enabled/level）配置，初始化为 `enabled=true、level=default_level`（rule_registry.c:190-194），通过 RESTful `PUT /api/v1/rules/{id}` 实时调整。级别含义见第 3 章。

### 1.1 命名规范（12 条，既有）

| rule_id | 规则名称 | 类型 | 默认级别 | 可配置 |
|---------|---------|------|---------|--------|
| name_charset | 对象名称仅由小写字母/数字/下划线构成 | 强制 | ERROR | 是 |
| name_start | 对象名称必须以字母开头 | 强制 | ERROR | 是 |
| name_len | 对象名称长度不超过32个字符 | 强制 | ERROR | 是 |
| name_reserved | 对象名称禁止使用PostgreSQL保留字 | 强制 | ERROR | 是 |
| name_pk_prefix | 主键约束名必须以pk_为前缀 | 强制 | ERROR | 是 |
| name_uk_prefix | 唯一约束名必须以uk_为前缀 | 强制 | ERROR | 是 |
| name_ck_prefix | 检查约束名必须以ck_为前缀 | 强制 | ERROR | 是 |
| name_idx_prefix | 普通索引名必须以idx_为前缀 | 强制 | ERROR | 是 |
| name_vw_prefix | 视图名必须以vw_为前缀 | 强制 | ERROR | 是 |
| name_tmp_prefix | 临时对象名必须以tmp为前缀并以日期为后缀 | 强制 | ERROR | 是 |
| name_bak_prefix | 备份对象名必须以bak为前缀并以日期为后缀 | 建议 | WARNING | 是 |
| name_db_name | 库名以应用系统缩写为前缀并以环境类型为后缀 | 建议 | WARNING | 是 |

> 说明：`name_bak_prefix` 当前扩展版本无判定逻辑（「注册但无判定」白名单豁免，见 design D3），预期不触发。

### 1.2 字段数据类型（5 条，既有）

| rule_id | 规则名称 | 类型 | 默认级别 | 可配置 |
|---------|---------|------|---------|--------|
| type_varchar | 字符串类型使用varchar/text | 建议 | WARNING | 是 |
| type_numeric | 货币与精确计算字段使用numeric | 建议 | WARNING | 是 |
| type_date_time | 日期使用date、时间使用time/timestamp | 建议 | WARNING | 是 |
| type_jsonb | JSON数据使用jsonb类型 | 建议 | WARNING | 是 |
| type_min_size | 优先选择符合存储需要的最小数据类型 | 强制 | ERROR | 是 |

> 说明：`type_date_time` 无判定逻辑（白名单豁免，design D3），预期不触发。

### 1.3 表结构与字段设计（8 条，既有）

| rule_id | 规则名称 | 类型 | 默认级别 | 可配置 |
|---------|---------|------|---------|--------|
| table_pk_required | 每张表必须定义主键 | 强制 | ERROR | 是 |
| table_pk_name_id | 主键列名必须为id | 强制 | ERROR | 是 |
| table_pk_type_serial | 主键id使用序列(serial/int)类型 | 强制 | ERROR | 是 |
| table_column_not_null | 表中所有字段必须为NOT NULL | 强制 | ERROR | 是 |
| table_comment_required | 每张表必须有表级和字段级注释 | 强制 | ERROR | 是 |
| table_no_foreign_key | 禁止在表中定义外键 | 强制 | ERROR | 是 |
| table_no_reserved_column | 禁止建立预留字段 | 建议 | WARNING | 是 |
| db_charset_utf8 | 数据库字符集使用UTF8、排序规则使用C | 强制 | ERROR | 是 |

> 说明：`table_comment_required` 无判定逻辑（白名单豁免，design D3），预期不触发。

### 1.4 索引规范（6 条，既有）

| rule_id | 规则名称 | 类型 | 默认级别 | 可配置 |
|---------|---------|------|---------|--------|
| idx_concurrently | 创建索引必须使用CONCURRENTLY | 强制 | ERROR | 是 |
| idx_field_count | 单个索引字段数不超过5个 | 建议 | WARNING | 是 |
| idx_table_count | 单表索引数量不超过5个 | 建议 | WARNING | 是 |
| idx_method | 根据场景合理选择索引方法(btree/hash/gin/gist/brin) | 强制 | ERROR | 是 |
| idx_selectivity | 索引必须创建在选择性较高的列上 | 强制 | ERROR | 是 |
| idx_redundant | 避免冗余或重复索引 | 强制 | ERROR | 是 |

### 1.5 视图设计（3 条，既有）

| rule_id | 规则名称 | 类型 | 默认级别 | 可配置 |
|---------|---------|------|---------|--------|
| view_select_star | 视图禁止使用select * | 强制 | ERROR | 是 |
| view_order_by | 视图中禁止使用order by | 强制 | ERROR | 是 |
| view_nested | 视图禁止嵌套其他视图 | 建议 | WARNING | 是 |

### 1.6 SQL 编写（DML，8 条，既有）

| rule_id | 规则名称 | 类型 | 默认级别 | 可配置 |
|---------|---------|------|---------|--------|
| dml_select_star | select只获取必要字段, 禁止select * | 强制 | ERROR | 是 |
| dml_no_where | UPDATE/DELETE缺少WHERE条件将影响全部行 | 强制 | ERROR | 是 |
| dml_delete_truncate | 清空表建议使用TRUNCATE而非无WHERE的DELETE | 强制 | ERROR | 是 |
| dml_count_star | 统计行数使用count(*) | 强制 | ERROR | 是 |
| dml_left_fuzzy | 避免左模糊查询(LIKE '%x') | 强制 | ERROR | 是 |
| dml_batch_copy | 插入大量数据时建议使用COPY | 推荐 | NOTICE | 是 |
| dml_in_to_exists | 使用EXISTS子句代替IN操作符 | 推荐 | NOTICE | 是 |
| dml_array_vs_tmp | 使用数组代替临时表 | 推荐 | NOTICE | 是 |

> 说明：`dml_select_star` 无判定逻辑（白名单豁免，design D3），预期不触发；`dml_delete_truncate` 无独立触发点（无 WHERE DELETE 实际输出 `dml_no_where`，白名单豁免，design D3 补充第 6 条），预期输出 `dml_no_where` 而非该 id。

### 1.7 可编程对象（4 条，既有）

| rule_id | 规则名称 | 类型 | 默认级别 | 可配置 |
|---------|---------|------|---------|--------|
| prog_name | 函数/过程/触发器命名应符合命名规范 | 强制 | ERROR | 是 |
| prog_ddl_forbidden | 函数体内禁止执行DDL | 建议 | WARNING | 是 |
| prog_business_logic | 不建议在数据库中存放业务逻辑 | 建议 | WARNING | 是 |
| prog_close_cursor | 游标使用后必须及时关闭 | 建议 | WARNING | 是 |

> 说明：`prog_name` 无判定逻辑（白名单豁免，design D3），函数命名实际走 `name_*` 规则，预期不输出。

### 1.8 对象变更与删除（2 条，既有）

| rule_id | 规则名称 | 类型 | 默认级别 | 可配置 |
|---------|---------|------|---------|--------|
| ddl_high_risk_drop | 高危删除操作(DATABASE/TABLESPACE/SCHEMA) | 建议 | WARNING | 是 |
| ddl_truncate_warn | 生产环境谨慎使用TRUNCATE | 建议 | WARNING | 是 |

### 1.9 事务控制（1 条，既有）

| rule_id | 规则名称 | 类型 | 默认级别 | 可配置 |
|---------|---------|------|---------|--------|
| tcl_multi_ddl_in_txn | 禁止在显式事务中执行多条DDL | 强制 | ERROR | 是 |

### 1.10 命令类 cmd_*（14 条，既有）

| rule_id | 规则名称 | 类型 | 默认级别 | 可配置 |
|---------|---------|------|---------|--------|
| cmd_vacuum | VACUUM 操作需谨慎(FULL 获取排他锁并重写表) | 建议 | WARNING | 是 |
| cmd_analyze | ANALYZE 统计信息采集应在业务低峰期进行 | 推荐 | NOTICE | 是 |
| cmd_checkpoint | 手动 CHECKPOINT 仅建议排障场景使用 | 推荐 | NOTICE | 是 |
| cmd_cluster | CLUSTER 需排他锁并重写表, 生产需谨慎 | 建议 | WARNING | 是 |
| cmd_reindex | REINDEX 建议使用 CONCURRENTLY | 建议 | WARNING | 是 |
| cmd_refresh_matview | REFRESH MATVIEW 建议使用 CONCURRENTLY | 建议 | WARNING | 是 |
| cmd_lock | LOCK TABLE 需注意锁模式与持锁时长 | 建议 | WARNING | 是 |
| cmd_load | LOAD 加载共享库属安全敏感操作 | 建议 | WARNING | 是 |
| cmd_discard | DISCARD 将重置会话全部状态 | 推荐 | NOTICE | 是 |
| cmd_explain | EXPLAIN ANALYZE 将真实执行被分析语句 | 推荐 | NOTICE | 是 |
| cmd_copy | COPY 批量导入导出需确认数据源/格式/敏感范围 | 建议 | WARNING | 是 |
| cmd_prepare | 预编译语句(PREPARE/EXECUTE/DEALLOCATE)识别回显 | 推荐 | NOTICE | 是 |
| cmd_cursor | 游标使用后必须 CLOSE, 避免长事务持有 | 建议 | WARNING | 是 |
| cmd_notify | 通知通道需评估性能开销与使用范围 | 建议 | WARNING | 是 |

### 1.11 对象类 obj_*（12 条，既有）

| rule_id | 规则名称 | 类型 | 默认级别 | 可配置 |
|---------|---------|------|---------|--------|
| obj_policy | 行级安全策略变更需评估既有访问路径 | 建议 | WARNING | 是 |
| obj_publication | 发布变更影响逻辑复制链路 | 建议 | WARNING | 是 |
| obj_subscription | 订阅变更影响逻辑复制链路 | 建议 | WARNING | 是 |
| obj_fdw | 外部数据对象涉及跨系统访问与凭据配置 | 建议 | WARNING | 是 |
| obj_extension | 扩展需评估来源/版本与影响面 | 建议 | WARNING | 是 |
| obj_language | 过程语言(尤其非受信语言)需评估执行风险 | 建议 | WARNING | 是 |
| obj_advanced | 高级对象定义/变更需由资深 DBA 实施 | 建议 | WARNING | 是 |
| obj_alter_system | ALTER SYSTEM 修改服务器级配置影响全局 | 建议 | WARNING | 是 |
| obj_reassign_owned | 对象所有权批量转移需评估影响 | 建议 | WARNING | 是 |
| obj_ctas | 评估是否确需创建实体表(CTAS/SELECT INTO) | 建议 | WARNING | 是 |
| obj_matview | 物化视图需评估数据新鲜度与刷新策略 | 建议 | WARNING | 是 |
| obj_security_label | SECURITY LABEL 识别回显 | 推荐 | NOTICE | 是 |

### 1.12 DCL / TCL / DML 补充（3 条，既有）

| rule_id | 规则名称 | 类型 | 默认级别 | 可配置 |
|---------|---------|------|---------|--------|
| dcl_set_role | 会话身份切换需确认权限边界 | 建议 | WARNING | 是 |
| tcl_set_transaction | SET TRANSACTION/CONSTRAINTS 识别回显 | 推荐 | NOTICE | 是 |
| dml_merge_check | MERGE 需仔细评估影响行范围 | 建议 | WARNING | 是 |

### 1.13 对象存在性（1 条，既有）

| rule_id | 规则名称 | 类型 | 默认级别 | 可配置 |
|---------|---------|------|---------|--------|
| obj_exists_check | CREATE 对象已存在冲突(无INE=ERROR/有INE=WARNING) | 强制 | ERROR | 是 |

### 1.14 命令兜底（1 条，既有，单独标注）

| rule_id | 规则名称 | 类型 | 默认级别 | 可配置 |
|---------|---------|------|---------|--------|
| echo | 未映射规则的命令识别回显（**命令兜底**，非普通规则族） | 推荐 | NOTICE | 是 |

> `echo` 语义见第 3 章：无专门规则命令经 `se_audit_echo`（hook_utility.c:112-125）输出 NOTICE 兜底回显，保证「无任何命令静默放行」。

### 1.15 阶段二新增（22 条，spec 5.4 需求 23，决策 D9/D10/D11）

> 统一追加在 `g_rule_defs[]` 中 `echo` 之后、数组末尾（rule_registry.c:136-158），既有 80 条内容/顺序/位置零改动（决策 D9）；新增 22 条级别限 NOTICE（14 条）/WARNING（8 条），无 ERROR（决策 D10）。

**事务控制族（5 条）**

| rule_id | 规则名称 | 类型 | 默认级别 | 可配置 |
|---------|---------|------|---------|--------|
| tcl_begin | 显式事务开启(BEGIN/START TRANSACTION)识别回显 | 推荐 | NOTICE | 是 |
| tcl_commit | 显式事务提交(COMMIT/END)识别回显 | 推荐 | NOTICE | 是 |
| tcl_rollback | 显式事务回滚(ROLLBACK/ABORT)识别回显 | 推荐 | NOTICE | 是 |
| tcl_savepoint | 保存点操作(SAVEPOINT/RELEASE/ROLLBACK TO)识别回显 | 推荐 | NOTICE | 是 |
| tcl_prepared | 两阶段事务(PREPARE/COMMIT/ROLLBACK PREPARED)需评估分布式一致性 | 建议 | WARNING | 是 |

**命令类 cmd_*（4 条）**

| rule_id | 规则名称 | 类型 | 默认级别 | 可配置 |
|---------|---------|------|---------|--------|
| cmd_set | 会话参数设置(SET/RESET)识别回显 | 推荐 | NOTICE | 是 |
| cmd_show | 会话参数查询(SHOW)识别回显 | 推荐 | NOTICE | 是 |
| cmd_call | 存储过程调用(CALL)识别回显 | 推荐 | NOTICE | 是 |
| cmd_do | 匿名块执行(DO)识别回显 | 推荐 | NOTICE | 是 |

**DCL 权限族（4 条）**

| rule_id | 规则名称 | 类型 | 默认级别 | 可配置 |
|---------|---------|------|---------|--------|
| dcl_grant | 对象权限授予/回收(GRANT/REVOKE)需确认权限边界 | 建议 | WARNING | 是 |
| dcl_grant_role | 角色成员授予/回收(GRANT/REVOKE)需确认角色边界 | 建议 | WARNING | 是 |
| dcl_default_privileges | 默认权限变更(ALTER DEFAULT PRIVILEGES)影响后续创建对象 | 建议 | WARNING | 是 |
| dcl_role | 角色创建/修改/删除需评估权限与归属 | 建议 | WARNING | 是 |

**对象类新增（3 条）**

| rule_id | 规则名称 | 类型 | 默认级别 | 可配置 |
|---------|---------|------|---------|--------|
| obj_comment | 对象注释(COMMENT)识别回显 | 推荐 | NOTICE | 是 |
| obj_rename_owner | 对象重命名/属主变更/模式迁移识别回显 | 推荐 | NOTICE | 是 |
| obj_drop_owned | DROP OWNED 批量回收对象需评估影响范围 | 推荐 | NOTICE | 是 |

**可编程对象族新增（3 条）**

| rule_id | 规则名称 | 类型 | 默认级别 | 可配置 |
|---------|---------|------|---------|--------|
| prog_trigger | 触发器创建/变更/删除需评估执行时机与开销 | 建议 | WARNING | 是 |
| prog_event_trigger | 事件触发器创建/变更/删除影响全局事件处理 | 建议 | WARNING | 是 |
| prog_rule | 重写规则(RULE)创建/变更/删除需评估查询重写影响 | 建议 | WARNING | 是 |

**对象创建/变更/删除回显族（3 条）**

| rule_id | 规则名称 | 类型 | 默认级别 | 可配置 |
|---------|---------|------|---------|--------|
| ddl_create_object | 对象创建回显(SCHEMA/SEQUENCE/DOMAIN/TYPE/TABLESPACE) | 推荐 | NOTICE | 是 |
| ddl_alter_object | 对象结构变更回显(ALTER TABLE/INDEX/VIEW/SEQUENCE/TYPE/DATABASE/FUNCTION) | 推荐 | NOTICE | 是 |
| ddl_drop_object | 对象删除回显(DROP TABLE/VIEW/INDEX/SEQUENCE/DOMAIN/FUNCTION) | 推荐 | NOTICE | 是 |

**合计核对**：既有 80 条 + 阶段二新增 22 条 = **102 条**；实核 **ERROR 级 32 条**（全部为既有强制规则，新增无 ERROR）、**WARNING 级 45 条**（既有 37 + 新增 8）、**NOTICE 级 25 条**（既有 11 + 新增 14）。早期记录 42/40/20 为统计偏差，已按 `g_rule_defs[]` 实核修正（D21/Q7，见 3.1）。与 `g_rule_defs[]` 提取比对（`grep -oE '\{"[a-z_0-9]+"' rule_registry.c | sed 's/[{" ]//g'`）setdiff 为空。

---

## 2. 命令→规则覆盖映射（A~L 分组，PG18/current 全集 186 条目）

> 审核归属三态定义：**专门规则**＝可见 `SQL审核[rule_id]:` 输出；**echo 兜底**＝`hook_utility.c` default 分支 `se_audit_echo`（hook_utility.c:539-541 → 112-125）输出 NOTICE；**静默放行**＝仅 `ereport(DEBUG1)` 或仅 `audit_record_write` 写共享内存、无可见消息（任务组 14 已消除，见附录 A）。
> 「新增」＝阶段二新增 22 条规则之一（80→102）。本表与 `g_cmd_map[]`（rule_registry.c:405-458）逐条目核对一致。

### A. 事务控制（14 命令）

| 命令 | 审核归属 | 版本分界 |
|------|---------|---------|
| ABORT | tcl_rollback（新增） | 全版本 |
| BEGIN | tcl_begin（新增） | 全版本 |
| COMMIT | tcl_commit（新增） | 全版本 |
| COMMIT PREPARED | tcl_prepared（新增） | 全版本 |
| END | tcl_commit（新增，解析为 COMMIT） | 全版本 |
| PREPARE TRANSACTION | tcl_prepared（新增） | 全版本 |
| RELEASE SAVEPOINT | tcl_savepoint（新增） | 全版本 |
| ROLLBACK | tcl_rollback（新增） | 全版本 |
| ROLLBACK PREPARED | tcl_prepared（新增） | 全版本 |
| ROLLBACK TO SAVEPOINT | tcl_savepoint（新增） | 全版本 |
| SAVEPOINT | tcl_savepoint（新增） | 全版本 |
| START TRANSACTION | tcl_begin（新增） | 全版本 |
| SET CONSTRAINTS | tcl_set_transaction（既有） | 全版本 |
| SET TRANSACTION | tcl_set_transaction（既有） | 全版本 |

### B. 会话设置（5 命令）

| 命令 | 审核归属 | 版本分界 |
|------|---------|---------|
| RESET | cmd_set（新增，VAR_RESET/VAR_RESET_ALL） | 全版本 |
| SET（普通变量） | cmd_set（新增，VAR_SET_* 非 multi 非 role） | 全版本 |
| SET ROLE | dcl_set_role（既有） | 全版本 |
| SET SESSION AUTHORIZATION | dcl_set_role（既有） | 全版本 |
| SHOW | cmd_show（新增） | 全版本 |

### C. DCL 权限（5 命令）

| 命令 | 审核归属 | 版本分界 |
|------|---------|---------|
| ALTER DEFAULT PRIVILEGES | dcl_default_privileges（新增） | 全版本 |
| GRANT（对象权限） | dcl_grant（新增） | 全版本 |
| GRANT（角色成员） | dcl_grant_role（新增） | 全版本 |
| REVOKE（对象权限） | dcl_grant（新增） | 全版本 |
| REVOKE（角色成员） | dcl_grant_role（新增） | 全版本 |

### D. 角色管理（3 语法族）

| 命令 | 审核归属 | 版本分界 |
|------|---------|---------|
| CREATE ROLE/USER/GROUP | dcl_role（新增） | 全版本 |
| ALTER ROLE/USER/GROUP、ALTER ROLE ... SET | dcl_role（新增） | 全版本 |
| DROP ROLE/USER/GROUP | dcl_role（新增） | 全版本 |

### E. 表/索引/视图族（21 命令）

| 命令 | 审核归属 | 版本分界 |
|------|---------|---------|
| CREATE TABLE | table_*/name_*/type_*（既有，条件触发） | 全版本 |
| CREATE TABLE AS | obj_ctas（既有） | 全版本 |
| CREATE FOREIGN TABLE | table_* + obj_fdw（既有） | 全版本 |
| CREATE INDEX | idx_*（既有，条件触发） | 全版本 |
| CREATE VIEW | view_*（既有，条件触发） | 全版本 |
| CREATE MATERIALIZED VIEW | obj_matview（既有） | 全版本 |
| SELECT INTO | dml_*（条件触发） | 全版本 |
| ALTER TABLE | ddl_alter_object（新增；MATVIEW 特判走 obj_matview） | 全版本 |
| ALTER INDEX | ddl_alter_object（新增，解析为 T_AlterTableStmt） | 全版本 |
| ALTER VIEW | ddl_alter_object（新增，解析为 T_AlterTableStmt） | 全版本 |
| ALTER FOREIGN TABLE | ddl_alter_object（新增） | 全版本 |
| ALTER MATERIALIZED VIEW | obj_matview（既有） | 全版本 |
| DROP TABLE | ddl_drop_object（新增） | 全版本 |
| DROP INDEX | ddl_drop_object（新增） | 全版本 |
| DROP VIEW | ddl_drop_object（新增） | 全版本 |
| DROP FOREIGN TABLE | ddl_drop_object（新增） | 全版本 |
| DROP MATERIALIZED VIEW | obj_matview（既有） | 全版本 |
| REINDEX | cmd_reindex（既有） | CONCURRENTLY [PG12+] |
| REFRESH MATERIALIZED VIEW | cmd_refresh_matview（既有） | 全版本 |
| CLUSTER | cmd_cluster（既有） | 全版本 |
| TRUNCATE | ddl_truncate_warn（既有） | 全版本 |

### F. 序列/模式/数据库/表空间/类型/域族（17 命令）

| 命令 | 审核归属 | 版本分界 |
|------|---------|---------|
| CREATE DATABASE | name_db_name + db_charset_utf8（既有，条件触发） | 全版本 |
| CREATE SCHEMA | ddl_create_object（新增；名字违规 name_*） | 全版本 |
| CREATE SEQUENCE | ddl_create_object（新增） | 全版本 |
| CREATE TABLESPACE | ddl_create_object（新增） | 全版本 |
| CREATE TYPE（复合/枚举/范围/Define） | ddl_create_object（新增） | 全版本 |
| CREATE DOMAIN | ddl_create_object（新增） | 全版本 |
| ALTER SCHEMA | ddl_alter_object（新增） | 全版本 |
| ALTER SEQUENCE | ddl_alter_object（新增） | 全版本 |
| ALTER TABLESPACE | ddl_alter_object（新增） | 全版本 |
| ALTER TYPE | ddl_alter_object（新增，AlterTypeStmt） | **[PG13+]** |
| ALTER DOMAIN | ddl_alter_object（新增） | 全版本 |
| ALTER DATABASE | ddl_alter_object（新增） | 全版本 |
| DROP SCHEMA | ddl_high_risk_drop（既有 WARNING） | 全版本 |
| DROP SEQUENCE | ddl_drop_object（新增） | 全版本 |
| DROP TABLESPACE | ddl_high_risk_drop（既有 WARNING） | 全版本 |
| DROP DATABASE | ddl_high_risk_drop（既有 WARNING） | 全版本 |
| DROP DOMAIN | ddl_drop_object（新增） | 全版本 |

> 补充：`ALTER TABLESPACE ... SET/RESET (options)`（T_AlterTableSpaceOptionsStmt）与 `ALTER DATABASE ... SET/RESET`（T_AlterDatabaseSetStmt）无专门 case，走 **echo 兜底**（NOTICE，见第 3 章）。

### G. 函数/过程族（10 命令）

| 命令 | 审核归属 | 版本分界 |
|------|---------|---------|
| CREATE FUNCTION | prog_*（既有，条件触发） | 全版本 |
| CREATE PROCEDURE | prog_*（既有，条件触发） | 全版本 |
| ALTER FUNCTION | ddl_alter_object（新增，T_AlterFunctionStmt） | 全版本 |
| ALTER PROCEDURE | ddl_alter_object（新增） | 全版本 |
| ALTER ROUTINE | ddl_alter_object（新增） | 全版本 |
| DROP FUNCTION | ddl_drop_object（新增） | 全版本 |
| DROP PROCEDURE | ddl_drop_object（新增） | 全版本 |
| DROP ROUTINE | ddl_drop_object（新增） | 全版本 |
| CALL | cmd_call（新增） | 全版本（PG11 起支持） |
| DO | cmd_do（新增） | 全版本 |

### H. 触发器/规则/事件触发器族（10 命令）

| 命令 | 审核归属 | 版本分界 |
|------|---------|---------|
| CREATE TRIGGER | prog_trigger（新增） | 全版本 |
| ALTER TRIGGER（RENAME） | prog_trigger（新增，T_RenameStmt 特判） | 全版本 |
| ALTER TRIGGER（ENABLE/DISABLE） | prog_trigger（新增，T_AlterTableStmt 内判 trigger 命令） | 全版本 |
| DROP TRIGGER | prog_trigger（新增，T_DropStmt OBJECT_TRIGGER 特判） | 全版本 |
| CREATE EVENT TRIGGER | prog_event_trigger（新增） | 全版本 |
| ALTER EVENT TRIGGER | prog_event_trigger（新增） | 全版本 |
| DROP EVENT TRIGGER | prog_event_trigger（新增，T_DropStmt 特判） | 全版本 |
| CREATE RULE | prog_rule（新增） | 全版本 |
| ALTER RULE（RENAME） | prog_rule（新增，T_RenameStmt OBJECT_RULE 特判） | 全版本 |
| DROP RULE | prog_rule（新增，T_DropStmt OBJECT_RULE 特判） | 全版本 |

### I. 对象类命令（既有 obj_*，可见，非静默）

| 命令 | 审核归属 | 版本分界 |
|------|---------|---------|
| CREATE/ALTER/DROP POLICY | obj_policy | 全版本 |
| CREATE/ALTER/DROP PUBLICATION | obj_publication | 全版本 |
| CREATE/ALTER/DROP SUBSCRIPTION | obj_subscription | 全版本 |
| CREATE/ALTER/DROP FOREIGN DATA WRAPPER、SERVER、USER MAPPING、IMPORT FOREIGN SCHEMA | obj_fdw | 全版本 |
| CREATE/ALTER/DROP EXTENSION | obj_extension | 全版本 |
| CREATE/DROP LANGUAGE | obj_language | 全版本 |
| CREATE/ALTER/DROP MATERIALIZED VIEW | obj_matview | 全版本 |
| CREATE/ALTER/DROP STATISTICS | obj_advanced | ALTER STATISTICS **[PG13+]** |
| CREATE/ALTER/DROP ACCESS METHOD、CAST、COLLATION、CONVERSION、OPERATOR、OPERATOR CLASS/FAMILY、AGGREGATE、TRANSFORM、TEXT SEARCH 系列 | obj_advanced | 全版本 |
| ALTER SYSTEM | obj_alter_system | 全版本 |
| REASSIGN OWNED | obj_reassign_owned | 全版本 |
| DROP OWNED | obj_drop_owned（新增） | 全版本 |
| SECURITY LABEL | obj_security_label | 全版本 |

### J. 维护/会话/数据类（既有 cmd_*，可见，非静默）

| 命令 | 审核归属 | 版本分界 |
|------|---------|---------|
| ANALYZE / VACUUM | cmd_analyze / cmd_vacuum | 全版本 |
| CHECKPOINT | cmd_checkpoint | 全版本 |
| CLUSTER | cmd_cluster | 全版本 |
| REINDEX | cmd_reindex | CONCURRENTLY [PG12+] |
| REFRESH MATERIALIZED VIEW | cmd_refresh_matview | 全版本 |
| LOCK | cmd_lock | 全版本 |
| LOAD | cmd_load | 全版本 |
| DISCARD | cmd_discard | 全版本 |
| EXPLAIN | cmd_explain | 全版本 |
| COPY | cmd_copy | 全版本 |
| PREPARE / EXECUTE / DEALLOCATE | cmd_prepare | 全版本 |
| DECLARE / FETCH / MOVE / CLOSE | cmd_cursor | 全版本 |
| LISTEN / NOTIFY / UNLISTEN | cmd_notify | 全版本 |

### K. DML 族（既有 dml_*，条件触发，非静默放行）

| 命令 | 审核归属 | 版本分界 |
|------|---------|---------|
| SELECT | dml_count_star/dml_left_fuzzy/dml_in_to_exists 等（条件触发） | 全版本 |
| INSERT | dml_batch_copy 等（条件触发） | 全版本 |
| UPDATE | dml_no_where 等（条件触发） | 全版本 |
| DELETE | dml_no_where/dml_delete_truncate 等（条件触发） | 全版本 |
| MERGE | dml_merge_check | **[PG15+]** |
| TABLE | dml_*（条件触发） | [PG15+] |
| VALUES | dml_*（条件触发） | 全版本 |

### L. 注释/重命名/其他

| 命令 | 审核归属 | 版本分界 |
|------|---------|---------|
| COMMENT | obj_comment（新增） | 全版本 |
| ALTER ... RENAME | obj_rename_owner（新增；TRIGGER/RULE RENAME 特判 prog_trigger/prog_rule） | 全版本 |
| ALTER ... OWNER | obj_rename_owner（新增） | 全版本 |
| ALTER ... SET SCHEMA | obj_rename_owner（新增） | 全版本 |
| ALTER LARGE OBJECT | echo 兜底（既有，T_AlterLargeObjectStmt 无 case） | 全版本 |
| 空语句 | 不产生 utility stmt、不经过审核钩子，非命令语义（豁免） | 全版本 |

**覆盖结论**：PG18/current 全集 186 个 TOC 条目均已落到「专门规则（含新增 22 条）或 echo 兜底」，静默放行状态清零；版本分界命令（ALTER TYPE [PG13+]、MERGE/TABLE [PG15+]、REINDEX CONCURRENTLY [PG12+]、ALTER STATISTICS [PG13+]、CALL/PROCEDURE/ROUTINE [PG11+]）标注完整。与 `g_cmd_map[]`（rule_registry.c:405-458）核对：无残留应映射而未映射的 `echo` 条目；`echo` 仅剩 ALTER LARGE OBJECT、ALTER TABLESPACE/DATABASE ... SET/RESET 等真正无专门规则命令（行为级验收见 19.6）。

---

## 3. 级别语义与兜底策略

### 3.1 三档级别语义（与 audit_emit 行为一致）

`audit_emit(def_level, rule_id, fmt, ...)`（rule_registry.c:299-344）与 `audit_emit_fixed(level, rule_id, fmt, ...)`（rule_registry.c:351-391）统一实现级别输出；`audit_emit` 先查规则配置（rule_registry.c:311-317）：规则关闭（enabled=false）静默返回，否则用共享内存配置的 level 覆盖默认级别；`audit_emit_fixed` 传入 level 即最终级别、不被覆盖（同样受 enabled 开关控制）。

| 级别 | 语义 | 输出行为 | 是否拦截命令 |
|------|------|---------|-------------|
| NOTICE | 识别回显（识别到命令/对象，供留痕审计） | `ereport(NOTICE)`：`SQL审核[rule_id]: 文案` | 否（放行，语句正常执行） |
| WARNING | 建议（提示评估，**命中即拦截**） | `ereport(ERROR, ERRCODE_CHECK_VIOLATION)` + `errdetail("WARNING 级别审核拦截: ...")`：输出级别前缀为 ERROR 并带 WARNING 标识 | **是**（语句不执行） |
| ERROR | 强制（违反规范，阻断执行） | `ereport(ERROR, ERRCODE_CHECK_VIOLATION)`：`SQL审核[rule_id]: 文案` | **是**（语句不执行） |

- **生效版本范围**：NOTICE→放行 / WARNING→拦截 / ERROR→拦截 语义在 **PG11-18 全版本生效**（需求 38a）；跨版本编译产物统一实现（`audit_emit`/`audit_emit_fixed` 的 WARNING 分支 `ereport(ERROR)`+`errdetail` 标识，任务组 25），由任务组 28 八版本回归复核。
- **级别统计（D21/Q7 实核，修正早期 42/40/20 偏差）**：ERROR 32 / WARNING 45 / NOTICE 25；既有 80 条 = ERROR 32 + WARNING 37 + NOTICE 11，新增 22 条 = NOTICE 14 + WARNING 8（无 ERROR，决策 D10）。
- **CIC 目录型规则标注（design 4.3.2 第 4 条）**：`idx_table_count`/`idx_redundant`/`idx_selectivity` 三规则在 **CREATE INDEX CONCURRENTLY 场景不评估**（CIC 特判返回，任务组 23 修复，rule_ddl.c:745-750）；验证经规则状态受控切换以非 CIC 路径触发（任务组 26.3）。
- **obj_exists_check INE 分支拦截语义（Q10）**：`CREATE ... IF NOT EXISTS` 且对象已存在时——无 INE 场景为 ERROR 拦截（对象已存在冲突），有 INE 场景为 WARNING 拦截（存在即静默跳过，提示评估）；详见第 1 章 obj_exists_check 条目。

### 3.2 echo 兜底机制（"不放行任何命令"）

- **兜底链路**：`se_process_utility`（hook_utility.c:127-551）default 分支（hook_utility.c:539-541）→ `se_audit_echo`（hook_utility.c:112-125）→ `ereport(NOTICE, "SQL审核[echo]: 未映射命令识别: <命令名>")` + `audit_record_write(cmdname, NULL, "echo", AUDIT_NOTICE)`。
- **语义**：未映射到专门规则的命令一律输出 NOTICE 兜底回显——**NOTICE 放行、WARNING 拦截、ERROR 拦截、任何命令均不被静默放行**；`echo` 规则默认 NOTICE 恒触发（规则关闭后不触发）。
- **"无任何命令静默放行"双重验收判据（design 3.3.3）**：
  1. **代码级**（任务组 15.1，已完成）：`grep -rn "DEBUG1"` 仅剩条件触发族入口调试点（rule_program.c:113、rule_ddl.c:373/702/851/980、hook_analyze.c:282，均有对应专门规则族 audit_emit，非静默分支）；`audit_record_write` 全部调用点均有 audit_emit/audit_emit_fixed/ereport 可见输出配对（唯一例外 rule_ddl.c:698 约束索引附属记录分支，为防御性路径，主语句已输出，非独立命令）。
  2. **行为级**（任务组 19.6）：对命令全集逐命令执行，每条命令至少一条 `SQL审核` 可见输出。

---

## 4. 触发示例（每规则族 1-2 个 psql 示例）

> 示例与 `pgsqlauditengine_test.sql` 对应用例一一对应（PART 15 及既有 PART 1-14）；**期望输出以 19.3 实际验证日志回填核对为准**。级别以 `default_level`（=共享内存实际输出）为准（design D2）。

| 规则族 | 示例 SQL | 期望输出（SQL审核[...]） |
|--------|---------|------------------------|
| 命名规范 | `CREATE TABLE "BadName"(id serial PRIMARY KEY);` | `SQL审核[name_charset]: ...` ERROR（阻断） |
| 字段类型 | `CREATE TABLE se_t_x(a numeric(10,2) NOT NULL, b varchar(10) NOT NULL, c double precision NOT NULL, id serial PRIMARY KEY);` | `SQL审核[type_numeric]: ...` / `SQL审核[type_varchar]: ...` / `SQL审核[type_min_size]: ...` WARNING/ERROR |
| 表结构 | `CREATE TABLE se_t_nopk(name text NOT NULL);` | `SQL审核[table_pk_required]: ...` ERROR（阻断） |
| 索引 | `CREATE INDEX se_i_bad ON se_t_base(name);`（无 CONCURRENTLY） | `SQL审核[idx_concurrently]: ...` ERROR（阻断） |
| 视图 | `CREATE VIEW se_v_star AS SELECT * FROM se_t_base;` | `SQL审核[view_select_star]: ...` ERROR（阻断） |
| DML | `DELETE FROM se_t_base;`（无 WHERE） | `SQL审核[dml_no_where]: ...` ERROR（阻断） |
| 可编程对象 | `CREATE FUNCTION se_f_x() RETURNS void AS 'BEGIN NULL; END' LANGUAGE plpgsql;` | 合规不告警（prog_* 条件触发） |
| 对象变更删除 | `DROP TABLE se_t_base;` | `SQL审核[ddl_drop_object]: ...` NOTICE |
| 事务控制（新增） | `BEGIN; COMMIT;` | `SQL审核[tcl_begin]: ...` / `SQL审核[tcl_commit]: ...` NOTICE |
| 会话设置（新增） | `SET work_mem='16MB'; SHOW work_mem;` | `SQL审核[cmd_set]: ...` / `SQL审核[cmd_show]: ...` NOTICE |
| 过程调用（新增） | `CALL se_p_test();` / `DO $$ BEGIN NULL; END $$;` | `SQL审核[cmd_call]: ...` / `SQL审核[cmd_do]: ...` NOTICE |
| DCL（新增） | `GRANT SELECT ON se_t_base TO se_r_x;` | `SQL审核[dcl_grant]: ...` WARNING |
| 对象类新增 | `COMMENT ON TABLE se_t_base IS 'x';` / `ALTER TABLE se_t_base RENAME TO se_t_base2;` | `SQL审核[obj_comment]: ...` / `SQL审核[obj_rename_owner]: ...` NOTICE |
| 触发器（新增） | `CREATE TRIGGER trg_x BEFORE INSERT ON se_t_base FOR EACH ROW EXECUTE FUNCTION se_f_x();` | `SQL审核[prog_trigger]: ...` WARNING |
| 对象存在性 | `CREATE TABLE se_t_base(id serial PRIMARY KEY, name text NOT NULL);`（二次创建） | `SQL审核[obj_exists_check]: ...` ERROR/WARNING |
| 命令兜底 | `ALTER DATABASE postgres SET search_path TO public;` | `SQL审核[echo]: 未映射命令识别: ALTER DATABASE` NOTICE |

---

## 5. 配置与扩展

### 5.1 五个 GUC（与 pgsqlauditengine.c:71-116 定义一致）

| GUC | 类型 | 默认值 | 上下文 | 说明 |
|-----|------|--------|--------|------|
| `PGSAUDAUDITENGINE.enabled` | bool | false | SUSET | 全局审核开关（false 时全部审核输出与记录停用） |
| `PGSAUDAUDITENGINE.check_dml` | bool | true | SUSET | DML 语句审核开关 |
| `PGSAUDAUDITENGINE.api_listen` | string | 127.0.0.1 | POSTMASTER | RESTful 服务监听地址 |
| `PGSAUDAUDITENGINE.api_port` | int | 8900 | POSTMASTER | RESTful 服务监听端口 |
| `PGSAUDAUDITENGINE.api_token` | string | ''（空） | POSTMASTER | RESTful 服务鉴权 Token（空即免鉴权） |

### 5.2 共享内存动态开关（enabled/level）

- 启动初始化：102 条规则 `enabled=true`、`level=default_level`（rule_registry.c:190-194），随扩展加载自动完成。
- 运行期调整：经 RESTful `PUT /api/v1/rules/{id}` 修改单条规则 enabled/level，实时生效（`audit_emit` 每次输出前读取共享内存配置）。
- 覆盖语义：`audit_emit` 的 def_level 被共享内存 level 覆盖；`audit_emit_fixed` 的 level 不被覆盖；规则关闭后该规则静默返回（不输出、不写记录）。

### 5.3 扩展与后台进程

- 扩展名：`pgsqlauditengine`（`bgw_function_name="se_api_server_main"`，RESTful 后台进程）。
- RESTful 契约（不可变）：Base URL `http://<host>:<api_port>/api/v1`、响应包裹 `{"code","error_code","error_message","data"}`、Bearer 鉴权、`/health` 免鉴权；规则条数 80→102 后 `GET /rules`/`GET /config` 返回 data 数组长度变化属预期（非契约破坏），新增 22 个可查 id。
- 符号前缀：`se_*` 函数前缀、共享内存标识、扩展命名全部不变（design 3.6.1）。

---

## 6. 版本分界说明

### 6.1 分界命令清单（与 version_matrix.md §3 一致）

| 命令 | 版本分界 | 说明 |
|------|---------|------|
| CALL / CREATE PROCEDURE / ALTER PROCEDURE / DROP PROCEDURE / ALTER ROUTINE / DROP ROUTINE | [PG11+] | 随存储过程支持引入 |
| REINDEX CONCURRENTLY | [PG12+] | PG11 无 CONCURRENTLY 语法 |
| ALTER TYPE（完整命令） | [PG13+] | 源码 `T_AlterTypeStmt` 需 `#if PG_VERSION_NUM >= 130000`（hook_utility.c:225-230） |
| ALTER STATISTICS | [PG13+] | 源码 `T_AlterStatsStmt` 需 `#if PG_VERSION_NUM >= 130000`（hook_utility.c:492-494） |
| MERGE | [PG15+] | PG15 引入 |
| TABLE（SELECT 语法糖） | [PG15+] | 官方 sql-commands 无独立条目，作为 SELECT 别名收录 |

### 6.2 跨版本执行裁剪约定

- 测试脚本（`pgsqlauditengine_test.sql`）中分界命令用例标注 `[PGn+]`；在低于分界版本的实例执行时裁剪或跳过（如 `ALTER STATISTICS` 用例仅 PG13+ 执行、`MERGE` 用例仅 PG15+ 执行）。
- 编译期：`ALTER TYPE` 分支在 PG11/12 下被 `#if` 裁剪，低版本编译产物无该 case；`CALL`（T_CallStmt）PG11 起存在，全版本编译无差异；预计无需新增跨版本兼容宏（design 3.6.2 第 3 点）。
- 版本分界专项验证（任务组 19.8）：ALTER TYPE→PG13、MERGE/TABLE→PG15、REINDEX CONCURRENTLY→PG12。

---

## 附录 A：静默放行消除方案（任务组 14 工作清单）

> 以下为 13.2 源码逐点核对结果：每条命令的存量行为、消除方式（新增规则 rule_id）。消除方式与 design 3.1.2「消除方式」列、3.2.1 新增规则元数据表一致；**源码位置为任务组 14 改造前位置，仅供追溯，改造后行号以第 1/2 章及源码为准**；无「静默放行→未安排消除」遗留。

| # | 命令族 | 具体命令 | 存量行为（改造前位置） | 消除方式（rule_id，级别） |
|---|--------|---------|--------------------|--------------------------|
| 1 | 事务控制 | BEGIN/START/COMMIT/ROLLBACK/SAVEPOINT/RELEASE/ROLLBACK TO/PREPARE TRANSACTION/COMMIT PREPARED/ROLLBACK PREPARED | `audit_tcl_transaction`（rule_tcl.c:58-100）仅 DEBUG1（L63）+ audit_record_write | tcl_begin/tcl_commit/tcl_rollback/tcl_savepoint/tcl_prepared（NOTICE×4，tcl_prepared WARNING） |
| 2 | 会话设置 | SET（普通变量）/RESET | `audit_ddl_variable_set`（rule_ddl.c:1417，DEBUG1） | cmd_set（NOTICE）→ 函数移入 rule_cmd.c |
| 3 | 会话查询 | SHOW | `audit_ddl_variable_show`（rule_ddl.c:1429，DEBUG1） | cmd_show（NOTICE）→ 函数移入 rule_cmd.c |
| 4 | 过程调用 | CALL | `T_CallStmt`（hook_utility.c:516-520，DEBUG1）+ audit_record_write("call") | cmd_call（NOTICE） |
| 5 | 匿名块 | DO | `audit_program_do`（rule_program.c:276，DEBUG1） | cmd_do（NOTICE） |
| 6 | 权限 | GRANT/REVOKE（对象权限） | `audit_dcl_grant`（rule_dcl.c:56，DEBUG1） | dcl_grant（WARNING） |
| 7 | 权限 | GRANT/REVOKE（角色成员） | `audit_dcl_grant_role`（rule_dcl.c:87，DEBUG1） | dcl_grant_role（WARNING） |
| 8 | 默认权限 | ALTER DEFAULT PRIVILEGES | `audit_dcl_default_privileges`（rule_dcl.c:126，DEBUG1） | dcl_default_privileges（WARNING） |
| 9 | 角色管理 | CREATE/ALTER ROLE/USER/GROUP、ALTER ROLE ... SET | `audit_ddl_create_role`/`alter_role`/`alter_role_set`（rule_ddl.c，DEBUG1） | dcl_role（WARNING） |
| 10 | 角色管理 | DROP ROLE/USER/GROUP | `audit_ddl_droprole`（rule_ddl.c:1358，DEBUG1） | dcl_role（WARNING） |
| 11 | 对象注释 | COMMENT | `audit_ddl_comment`（rule_ddl.c:1221，DEBUG1） | obj_comment（NOTICE） |
| 12 | 重命名 | ALTER ... RENAME | `audit_ddl_rename`（rule_ddl.c:1193，DEBUG1） | obj_rename_owner（NOTICE；TRIGGER/RULE 特判 prog_trigger/prog_rule） |
| 13 | 属主 | ALTER ... OWNER | `audit_ddl_alter_owner`（rule_ddl.c:1101，DEBUG1） | obj_rename_owner（NOTICE） |
| 14 | 模式迁移 | ALTER ... SET SCHEMA | `audit_ddl_alter_object_schema`（rule_ddl.c:1125，DEBUG1） | obj_rename_owner（NOTICE） |
| 15 | 触发器 | CREATE/ALTER TRIGGER、DROP TRIGGER | `audit_program_trigger`（DEBUG1）；DROP 走 T_DropStmt default | prog_trigger（WARNING；DROP/ENABLE/DISABLE 特判） |
| 16 | 事件触发器 | CREATE/ALTER/DROP EVENT TRIGGER | `audit_program_event_trigger`/`alter_event_trigger`（DEBUG1）；DROP 走 T_DropStmt default | prog_event_trigger（WARNING；DROP 特判） |
| 17 | 规则 | CREATE/ALTER(RENAME)/DROP RULE | `audit_program_rule`（DEBUG1）；DROP 走 T_DropStmt default | prog_rule（WARNING；DROP/RENAME 特判） |
| 18 | 所有权 | DROP OWNED | `audit_ddl_dropowned`（DEBUG1） | obj_drop_owned（NOTICE） |
| 19 | 对象创建回显 | CREATE SCHEMA/SEQUENCE/DOMAIN/TYPE(复合/枚举/范围/Define)/TABLESPACE | `audit_ddl_create_seq` 等 8 函数（DEBUG1）；名字合规时无可见消息 | ddl_create_object（NOTICE） |
| 20 | 对象结构变更 | ALTER TABLE/INDEX/VIEW、ALTER SEQUENCE、ALTER DOMAIN、ALTER TYPE、ALTER ENUM、ALTER DATABASE、ALTER FUNCTION/PROCEDURE/ROUTINE | `audit_ddl_alter_table` 等函数（DEBUG1） | ddl_alter_object（NOTICE；ALTER MATERIALIZED VIEW 特判仍 obj_matview） |
| 21 | 对象删除回显 | DROP TABLE/VIEW/INDEX/SEQUENCE/DOMAIN/FUNCTION/PROCEDURE 等 default 分支对象 | `audit_ddl_drop`（DEBUG1；仅 OBJECT_SCHEMA 输出 ddl_high_risk_drop） | ddl_drop_object（NOTICE；DROP TRIGGER/EVENT TRIGGER/RULE 特判见 #15/16/17） |

### A.1 已确认 NOT 属于静默放行（无需处理）

| 命令族 | 理由（源码位置） |
|--------|-----------------|
| CREATE TABLE/INDEX/VIEW/FUNCTION/PROCEDURE | 条件触发规则族（table_*/idx_*/view_*/prog_*），合规不告警属规则语义，非静默放行 |
| CREATE DATABASE | 条件触发规则族（name_db_name + db_charset_utf8，`audit_ddl_createdb`），合规时无消息属规则语义 |
| DML（SELECT/INSERT/UPDATE/DELETE/MERGE/VALUES/TABLE） | 走 hook_analyze.c 的 dml_* 族，条件触发 |
| 对象类 obj_*（POLICY/PUBLICATION/SUBSCRIPTION/FDW/EXTENSION/LANGUAGE/MATVIEW/ADVANCED/ALTER SYSTEM/REASSIGN OWNED/SECURITY LABEL/CTAS） | 均有可见 `SQL审核[obj_*]:` 输出 |
| 维护类 cmd_*（vacuum/analyze/checkpoint/cluster/reindex/refresh_matview/lock/load/discard/explain/copy/prepare/cursor/notify） | 均有可见 `SQL审核[cmd_*]:` 输出 |
| 高危删除（DROP DATABASE/TABLESPACE/SCHEMA） | `audit_ddl_dropdb`/`audit_ddl_droptablespace` 输出 ddl_high_risk_drop WARNING |
| DROP TYPE/COLLATION/CONVERSION/OPERATOR 等高级对象 | T_DropStmt 特判（hook_utility.c）输出 obj_advanced WARNING |
| ALTER EVENT TRIGGER / ALTER ROUTINE / ALTER AGGREGATE / ALTER COLLATION 等归入上述族 | 分别由 prog_event_trigger / ddl_alter_object / obj_advanced 覆盖 |

### A.2 消除后预期（任务组 14 验收基准，已达成）

- `g_rule_defs[]` 条目数 80→102；新增 22 条 default_level 全部为 AUDIT_NOTICE（14 条）或 AUDIT_WARNING（8 条），无 AUDIT_ERROR（决策 D10/D11）。
- 事务控制/会话设置/过程调用/匿名块/权限/角色管理/注释/重命名/属主/模式迁移/触发器/事件触发器/规则/所有权/对象创建变更删除回显全部产生可见 `SQL审核[rule_id]:` 输出；`g_cmd_map[]` 中 `echo` 条目仅剩 ALTER LARGE OBJECT、ALTER TABLESPACE/DATABASE ... SET/RESET 等真正无专门规则命令。
- 既有 80 条规则行为逐字不变（决策 D9）；级别阻断语义不变（ERROR 拦截 / WARNING、NOTICE 不拦截）。
