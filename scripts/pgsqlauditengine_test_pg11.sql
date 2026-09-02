\set VERBOSITY terse
-- =============================================================================
-- pgsqlauditengine 规则边界与有效性测试脚本
-- -----------------------------------------------------------------------------
-- 用途   : 对 pgsqlauditengine 扩展 g_rule_defs[] 全部 102 条规则逐条执行
--          「预期触发 + 预期不触发」边界用例，验证规则触发、级别与消息文案。
-- 执行   : psql -h 127.0.0.1 -p 5439 -U postgres -d postgres \
--            -f pgsqlauditengine_test.sql > out_test_pg18.log 2>&1
-- 前提   : 扩展已 preload 且 PGSAUDAUDITENGINE.enabled=on / check_dml=on；
--          psql 默认 ON_ERROR_STOP=off（ERROR 仅阻断当前语句，后续继续）；
--          权限类命令（CREATE ROLE/ALTER DEFAULT PRIVILEGES/DROP OWNED）以
-- 超级用户执行；PREPARE TRANSACTION 需 max_prepared_transactions>0
--          （见 PART 0 的 SHOW 与兜底说明）。
-- 版本分界标记字典（design 4.2.4 / 决策 D19，裁剪器 gen_test_pg.sh 依赖）:
--   [PG11+] : 等价无分界，PG11 起全版本支持，仅文档标注，不触发裁剪
--   [PG12+] : 语句仅 PG12 起支持（REINDEX INDEX CONCURRENTLY 等）→ PG<12 裁剪
--   [PG13+] : 语句仅 PG13 起支持（ALTER STATISTICS 等）→ PG<13 裁剪
--   [PG15+] : 语句仅 PG15 起支持（MERGE 等）→ PG<15 裁剪
--   标记规范 : 固定位于可执行语句行行尾 `-- [PGv+]`（多行语句标记在首行）；
--             裁剪器对「目标版本 < v」的语句整行注释（多行语句含续行直至 `;` 结尾），
--             `[PG11+]` 无裁剪；无标记语句全版本执行。
--   现有分界语句 : REINDEX INDEX CONCURRENTLY [PG12+]（PART 10）、ALTER STATISTICS
--              [PG13+]（PART 11）、MERGE [PG15+]（PART 12）；ALTER TYPE 无落地用例，
--              其分界由 pgauditrule.md/version_matrix.md 说明（见 PART 15.7 边界注释）。
-- 命名空间: 表 se_t_* / 索引 idx_*（合规）se_i_*（违规）/ 视图 vw_*（合规）
--          se_v_*（违规）/ 函数 se_f_* / 过程 se_p_* / 角色 se_r_* /
--          触发器 trg_se* / 事件触发器 evt_se* / 规则 rl_se* / 其他 se_x_*
--          临时表 tmp_se_*
-- 组织   : PART 0 前置清理 -> PART 1~8/10~13 规则族用例 -> PART 14 后置清理
--          -> PART 15 阶段二新增规则命令族（7 子分区，22 条新增规则用例）
--          -> PART 9 事务控制（置脚本最末，触发后状态机卡死，见段内说明）
--          -> RULE COVERAGE CHECKLIST 覆盖率核对注释段
-- 零改动 : 本脚本为纯测试资产，不修改任何扩展源码与不可变契约。
-- =============================================================================

-- =============================================================================
-- PART 0: 前置清理段（幂等：DROP IF EXISTS CASCADE，autocommit 下执行）
-- -----------------------------------------------------------------------------
-- 规则状态受控（任务组 26.2）：WARNING 拦截语义下，清理段涉及的 DROP 语句可能被
-- ddl_high_risk_drop/obj_publication/obj_subscription/obj_fdw/obj_language 等
-- WARNING 规则拦截导致对象残留。回归编排脚本 run_regression_pg<v>.sh 在执行本段
-- 前通过 RESTful 临时关闭/降级上述规则（enabled=false 或 level=NOTICE），使清理
-- 语句放行；执行用例段（PART 1~13/15）前恢复规则配置。直接 psql 执行本脚本时
-- 上述 DROP 语句将按 WARNING 拦截语义被拦截（对象残留，属预期，非脚本缺陷）。
-- =============================================================================
DROP TABLE IF EXISTS se_t_base CASCADE;
DROP TABLE IF EXISTS se_t_dml CASCADE;
DROP TABLE IF EXISTS se_t_manycols CASCADE;
DROP TABLE IF EXISTS se_t_red CASCADE;
DROP TABLE IF EXISTS se_t_sel CASCADE;
DROP TABLE IF EXISTS se_t_pkbad CASCADE;
DROP TABLE IF EXISTS se_t_ukbad CASCADE;
DROP TABLE IF EXISTS se_t_ckbad CASCADE;
DROP TABLE IF EXISTS se_t_vc CASCADE;
DROP TABLE IF EXISTS se_t_num CASCADE;
DROP TABLE IF EXISTS se_t_dt CASCADE;
DROP TABLE IF EXISTS se_t_js CASCADE;
DROP TABLE IF EXISTS se_t_ms CASCADE;
DROP TABLE IF EXISTS se_t_nopk CASCADE;
DROP TABLE IF EXISTS se_t_pkname CASCADE;
DROP TABLE IF EXISTS se_t_pktype CASCADE;
DROP TABLE IF EXISTS se_t_null CASCADE;
DROP TABLE IF EXISTS se_t_fk CASCADE;
DROP TABLE IF EXISTS se_t_resv CASCADE;
DROP TABLE IF EXISTS se_t_comment CASCADE;
DROP TABLE IF EXISTS se_t_dup CASCADE;
DROP TABLE IF EXISTS se_t_ctas CASCADE;
DROP TABLE IF EXISTS se_t_txndrop CASCADE;
DROP TABLE IF EXISTS bak_se_t_base CASCADE;
DROP TABLE IF EXISTS se_t_tmpbad CASCADE;
DROP TABLE IF EXISTS tmp_se_t_arr CASCADE;
DROP TABLE IF EXISTS "SeTUpper" CASCADE;
DROP TABLE IF EXISTS "1st_table" CASCADE;
DROP TABLE IF EXISTS "select" CASCADE;
DROP TABLE IF EXISTS se_t_len_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz CASCADE;
DROP VIEW IF EXISTS vw_se_t_base CASCADE;
DROP VIEW IF EXISTS vw_se_t_star CASCADE;
DROP VIEW IF EXISTS vw_se_t_order CASCADE;
DROP VIEW IF EXISTS vw_se_t_sub CASCADE;
DROP VIEW IF EXISTS vw_se_t_nest CASCADE;
DROP VIEW IF EXISTS se_v_bad CASCADE;
DROP MATERIALIZED VIEW IF EXISTS vw_se_mv CASCADE;
DROP MATERIALIZED VIEW IF EXISTS vw_se_mv2 CASCADE;
DROP INDEX IF EXISTS idx_se_base CASCADE;
DROP INDEX IF EXISTS idx_noc CASCADE;
DROP INDEX IF EXISTS idx_hash CASCADE;
DROP INDEX IF EXISTS idx_many CASCADE;
DROP INDEX IF EXISTS idx_tc1 CASCADE;
DROP INDEX IF EXISTS idx_tc2 CASCADE;
DROP INDEX IF EXISTS idx_tc3 CASCADE;
DROP INDEX IF EXISTS idx_tc4 CASCADE;
DROP INDEX IF EXISTS idx_tc5 CASCADE;
DROP INDEX IF EXISTS idx_sel CASCADE;
DROP INDEX IF EXISTS idx_red1 CASCADE;
DROP INDEX IF EXISTS idx_red2 CASCADE;
DROP INDEX IF EXISTS se_i_bad CASCADE;
DROP FUNCTION IF EXISTS se_f_base CASCADE;
DROP FUNCTION IF EXISTS se_f_ddl CASCADE;
DROP FUNCTION IF EXISTS se_f_logic CASCADE;
DROP FUNCTION IF EXISTS se_f_cursor CASCADE;
DROP FUNCTION IF EXISTS se_f_badname CASCADE;
DROP SCHEMA IF EXISTS se_x_schema CASCADE;
DROP PUBLICATION IF EXISTS se_x_pub;
DROP SUBSCRIPTION IF EXISTS se_x_sub;
DROP FOREIGN DATA WRAPPER IF EXISTS se_x_fdw CASCADE;
DROP LANGUAGE IF EXISTS se_x_lang CASCADE;
DROP STATISTICS IF EXISTS se_x_stats;
DROP DATABASE IF EXISTS se_db_test;
DROP DATABASE IF EXISTS se_db_latin;
DROP DATABASE IF EXISTS se_db_utf8;
DROP SCHEMA IF EXISTS se_x_p15 CASCADE;
DROP SEQUENCE IF EXISTS se_x_p15_seq;
DROP DOMAIN IF EXISTS se_x_p15_dom;

DROP PROCEDURE IF EXISTS se_p_p15();
DROP FUNCTION IF EXISTS se_f_p15_trg() CASCADE;
DROP FUNCTION IF EXISTS se_f_p15_evt() CASCADE;

-- =============================================================================
-- 合规基表与派生对象（作为 DML/命令/对象类规则的「不触发」载体与执行目标）
-- 满足全部可判定建表规则：表名合规/主键 id serial/全列 NOT NULL/无外键/
-- 无预留列/无 bigint 非主键/numeric 带精度/约束名自动生成（不触发前缀规则）
-- =============================================================================
CREATE TABLE se_t_base (
    id serial PRIMARY KEY,
    name text NOT NULL,
    amount numeric(12,2) NOT NULL,
    created_at timestamp NOT NULL,
    status varchar(20) NOT NULL
);
-- 合规索引（idx_ 前缀 + CONCURRENTLY，避免触发 idx_concurrently/name_idx_prefix）
CREATE INDEX CONCURRENTLY idx_se_base ON se_t_base (status);
-- 合规视图（vw_ 前缀，显式列、无 order by、无嵌套）
CREATE VIEW vw_se_t_base AS SELECT id, name, amount FROM se_t_base;
-- 合规函数（se_f_ 前缀，函数体无 DDL/无游标/长度 <500）
CREATE FUNCTION se_f_base() RETURNS int LANGUAGE sql AS 'SELECT 1';
-- DML 专用表（供 DML 族用例执行，保护基表数据）
CREATE TABLE se_t_dml (
    id serial PRIMARY KEY,
    name text NOT NULL,
    amount numeric(12,2) NOT NULL,
    status varchar(20) NOT NULL
);
INSERT INTO se_t_dml (name, amount, status) VALUES ('a', 1.00, 'ok');
INSERT INTO se_t_dml (name, amount, status) VALUES ('b', 2.00, 'ok');

-- 前置条件核对（PART 15.1 tcl_prepared 用例依赖 max_prepared_transactions>0，
-- 决策 Q4 默认方案：连接级核对/标注跳过，不落库）：
SHOW max_prepared_transactions;
-- 兜底说明：若该参数为 0，PREPARE TRANSACTION 将报错
--  （"PREPARE TRANSACTION not enabled during server startup"），需在
--  postgresql.conf 设置 max_prepared_transactions = 10 并重启实例后重跑；
--  审核拦截（SQL审核[tcl_prepared]）先于该参数检查触发（PART 15 实测确认），
--  故参数为 0 时用例仍输出审核记录，仅 PG 侧报错属预期，不影响其他用例。
-- 注：本 SHOW 语句由 cmd_show（阶段二新增 NOTICE 规则）识别回显，
--     属预期额外输出，不计入 PART 15 cmd_show 用例核对。

-- =============================================================================
-- PART 1: 命名规范规则族（12 条规则）
-- =============================================================================

-- RULE name_charset: 对象名称仅由小写字母/数字/下划线构成 | 强制/ERROR
-- 预期触发: 大写表名建表 → SQL审核[name_charset]: 表名称 "SeTUpper" 只能由小写字母(a-z)、数字(0-9)、下划线(_)构成
-- 预期不触发: 基表 se_t_base 命名合规（见 PART 0），无 name_charset 输出
CREATE TABLE "SeTUpper"(id serial PRIMARY KEY, name text NOT NULL);

-- RULE name_start: 对象名称必须以字母开头 | 强制/ERROR
-- 预期触发: 数字开头表名建表 → SQL审核[name_start]: 表名称 "1st_table" 必须以字母开头，禁止以数字或下划线开头
-- 预期不触发: 基表 se_t_base 以字母开头，无 name_start 输出
CREATE TABLE "1st_table"(id serial PRIMARY KEY, name text NOT NULL);

-- RULE name_len: 对象名称长度不超过32个字符 | 强制/ERROR
-- 预期触发: 超长表名建表 → SQL审核[name_len]: 表名称 "se_t_len_..." 超过32个字符
-- 预期不触发: 基表 se_t_base 长度 9，无 name_len 输出
CREATE TABLE se_t_len_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz(id serial PRIMARY KEY, name text NOT NULL);

-- RULE name_reserved: 对象名称禁止使用PostgreSQL保留字 | 强制/ERROR
-- 预期触发: 保留字 "select" 建表 → SQL审核[name_reserved]: 表名称 "select" 是PostgreSQL保留字，禁止使用
-- 预期不触发: 基表 se_t_base 非保留字，无 name_reserved 输出
CREATE TABLE "select"(id serial PRIMARY KEY, name text NOT NULL);

-- RULE name_pk_prefix: 主键约束名必须以pk_为前缀 | 强制/ERROR
-- 预期触发: 显式主键约束名 id_pk 非 pk_ → SQL审核[name_pk_prefix]: 主键约束名 "id_pk" 必须以 pk_ 为前缀
-- 预期不触发: 基表 se_t_base 主键约束自动命名（无显式约束名），无 name_pk_prefix 输出
CREATE TABLE se_t_pkbad(id int NOT NULL, name text NOT NULL, CONSTRAINT id_pk PRIMARY KEY(id));

-- RULE name_uk_prefix: 唯一约束名必须以uk_为前缀 | 强制/ERROR
-- 预期触发: 唯一约束名 id_uq 非 uk_ → SQL审核[name_uk_prefix]: 唯一约束名 "id_uq" 必须以 uk_ 为前缀
-- 预期不触发: 基表 se_t_base 无唯一约束，无 name_uk_prefix 输出
CREATE TABLE se_t_ukbad(id serial PRIMARY KEY, name text NOT NULL, CONSTRAINT id_uq UNIQUE(name));

-- RULE name_ck_prefix: 检查约束名必须以ck_为前缀 | 强制/ERROR
-- 预期触发: 检查约束名 id_chk 非 ck_ → SQL审核[name_ck_prefix]: 检查约束名 "id_chk" 必须以 ck_ 为前缀
-- 预期不触发: 基表 se_t_base 无检查约束，无 name_ck_prefix 输出
CREATE TABLE se_t_ckbad(id serial PRIMARY KEY, name text NOT NULL, CONSTRAINT id_chk CHECK (length(name) > 0));

-- RULE name_idx_prefix: 普通索引名必须以idx_为前缀 | 强制/ERROR
-- 预期触发: 索引名 se_i_bad 非 idx_（CONCURRENTLY 排除 idx_concurrently）→ SQL审核[name_idx_prefix]: 索引名 "se_i_bad" 必须以 idx_ 为前缀
-- 预期不触发: 合规索引 idx_se_base（PART 0 已建）以 idx_ 开头，无 name_idx_prefix 输出
CREATE INDEX CONCURRENTLY se_i_bad ON se_t_base (name);

-- RULE name_vw_prefix: 视图名必须以vw_为前缀 | 强制/ERROR
-- 预期触发: 视图名 se_v_bad 非 vw_ → SQL审核[name_vw_prefix]: 视图名 "se_v_bad" 必须以 vw_ 为前缀
-- 预期不触发: 合规视图 vw_se_t_base（PART 0 已建）以 vw_ 开头，无 name_vw_prefix 输出
CREATE VIEW se_v_bad AS SELECT id, name FROM se_t_base;

-- RULE name_tmp_prefix: 临时对象名必须以tmp为前缀并以日期为后缀 | 强制/ERROR
-- 预期触发: 临时表名 se_t_tmpbad 非 tmp 前缀 → SQL审核[name_tmp_prefix]: 临时表名 "se_t_tmpbad" 必须以 tmp 为前缀并以日期为后缀
-- 预期不触发: 合规临时表 tmp_se_t_arr（见 PART 6）以 tmp 开头，无 name_tmp_prefix 输出
CREATE TEMP TABLE se_t_tmpbad(id int PRIMARY KEY);

-- RULE name_bak_prefix: 备份对象名必须以bak为前缀并以日期为后缀 | 建议/WARNING | [豁免]
-- 预期触发: bak_ 前缀建表（语义上应触发）→ 该规则在当前扩展版本无判定逻辑（rule_common.c audit_check_prefix
--           仅被 pk_/uk_/ck_/idx_/vw_ 前缀调用），预期不产生 name_bak_prefix 审核消息（本阶段不修改扩展）
-- 预期不触发: 表完全合规，除豁免声明外无任何 SQL审核 输出
CREATE TABLE bak_se_t_base(id serial PRIMARY KEY, name text NOT NULL);

-- RULE name_db_name: 库名以应用系统缩写为前缀并以环境类型为后缀 | 建议/WARNING
-- 预期触发: CREATE DATABASE se_db_test → SQL审核[name_db_name]: 库名 ... 建议以应用系统缩写为前缀并以环境类型为后缀
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、库不创建）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证（见 pgsqlauditengine_api.md PUT /rules/{id} 用例）
CREATE DATABASE se_db_test;

-- =============================================================================
-- PART 2: 字段类型规则族（5 条规则）
-- =============================================================================

-- RULE type_varchar: 字符串类型使用varchar/text | 建议/WARNING
-- 预期触发: char(10) 定长列 → SQL审核[type_varchar]: 列 "code" 建议使用 varchar/text 替代定长 char
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、表 se_t_vc 不创建）
-- 预期不触发: 基表 status varchar(20) 为变长字符串，无 type_varchar 输出
CREATE TABLE se_t_vc(id serial PRIMARY KEY, code char(10) NOT NULL);

-- RULE type_numeric: 货币与精确计算字段使用numeric | 建议/WARNING
-- 预期触发: double precision 金额列 → SQL审核[type_numeric]: 列 "amount" 若用于货币或精确计算，建议使用 numeric
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、表 se_t_num 不创建）
-- 预期不触发: 基表 amount numeric(12,2) 为精确类型，无 type_numeric 输出
CREATE TABLE se_t_num(id serial PRIMARY KEY, amount double precision NOT NULL);

-- RULE type_date_time: 日期使用date、时间使用time/timestamp | 建议/WARNING | [豁免]
-- 预期触发: 含 date/time 列的建表（语义上应触发）→ 该规则在当前扩展版本无判定逻辑
--           （rule_common.c audit_check_type 仅实现 jsonb/numeric/varchar 分支），
--           预期不产生 type_date_time 审核消息（本阶段不修改扩展）
-- 预期不触发: 表其余维度合规，除豁免声明外无任何 SQL审核 输出
CREATE TABLE se_t_dt(id serial PRIMARY KEY, birthday date NOT NULL, start_time time NOT NULL);

-- RULE type_jsonb: JSON数据使用jsonb类型 | 建议/WARNING
-- 预期触发: json 列 → SQL审核[type_jsonb]: 列 "payload" 建议使用 jsonb 类型替代 json
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、表 se_t_js 不创建）
-- 预期不触发: 基表无 json 列，无 type_jsonb 输出
CREATE TABLE se_t_js(id serial PRIMARY KEY, payload json NOT NULL);

-- RULE type_min_size: 优先选择符合存储需要的最小数据类型 | 强制/ERROR
-- 预期触发: 非主键 bigint 列 → SQL审核[type_min_size]: 列 "big_col" 建议使用 int 而非 bigint
-- 预期不触发: 基表 id 为 int 主键（由 table_pk_type_serial 覆盖）、amount numeric 带精度，无 type_min_size 输出
CREATE TABLE se_t_ms(id serial PRIMARY KEY, big_col bigint NOT NULL);

-- =============================================================================
-- PART 3: 表结构规则族（8 条规则）
-- =============================================================================

-- RULE table_pk_required: 每张表必须定义主键 | 强制/ERROR
-- 预期触发: 无主键建表 → SQL审核[table_pk_required]: 表 "se_t_nopk" 必须定义主键
-- 预期不触发: 基表 se_t_base 有主键，无 table_pk_required 输出
CREATE TABLE se_t_nopk(id int NOT NULL, name text NOT NULL);

-- RULE table_pk_name_id: 主键列名必须为id | 强制/ERROR
-- 预期触发: 主键列名 code 非 id → SQL审核[table_pk_name_id]: 表 "se_t_pkname" 的主键列名必须为 "id"
-- 预期不触发: 基表主键列为 id，无 table_pk_name_id 输出
CREATE TABLE se_t_pkname(code serial PRIMARY KEY, name text NOT NULL);

-- RULE table_pk_type_serial: 主键id使用序列(serial/int)类型 | 强制/ERROR
-- 预期触发: 主键 id 为 text → SQL审核[table_pk_type_serial]: 表 "se_t_pktype" 主键 "id" 建议使用序列(int/serial)类型
-- 预期不触发: 基表主键 id serial，无 table_pk_type_serial 输出
CREATE TABLE se_t_pktype(id text PRIMARY KEY, name text NOT NULL);

-- RULE table_column_not_null: 表中所有字段必须为NOT NULL | 强制/ERROR
-- 预期触发: 存在可空列 name → SQL审核[table_column_not_null]: 表 "se_t_null" 的字段 "name" 必须显式设置为 NOT NULL
-- 预期不触发: 基表全列 NOT NULL，无 table_column_not_null 输出
CREATE TABLE se_t_null(id serial PRIMARY KEY, name text);

-- RULE table_no_foreign_key: 禁止在表中定义外键 | 强制/ERROR
-- 预期触发: 外键列 ref_id → SQL审核[table_no_foreign_key]: 表 "se_t_fk" 禁止使用外键，数据完整性应由代码层实现
-- 预期不触发: 基表无外键，无 table_no_foreign_key 输出
CREATE TABLE se_t_fk(id serial PRIMARY KEY, ref_id int REFERENCES se_t_base(id));

-- RULE table_no_reserved_column: 禁止建立预留字段 | 建议/WARNING
-- 预期触发: 预留字段 reserve1 → SQL审核[table_no_reserved_column]: 表 "se_t_resv" 存在预留性质的字段 "reserve1"，建议移除
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、表 se_t_resv 不创建）
-- 预期不触发: 基表无预留字段，无 table_no_reserved_column 输出
CREATE TABLE se_t_resv(id serial PRIMARY KEY, reserve1 text NOT NULL);

-- RULE db_charset_utf8: 数据库字符集使用UTF8、排序规则使用C | 强制/ERROR
-- 预期触发: CREATE DATABASE ... ENCODING 'LATIN1' → SQL审核[db_charset_utf8]: 数据库字符集必须使用 UTF8（审核先于执行，
--           ERROR 阻断后库不创建；本语句同时触发 name_db_name WARNING 为预期额外输出）
-- 预期不触发: CREATE DATABASE ... ENCODING 'UTF8' → 仅输出 name_db_name WARNING，库创建成功（PART 14 清理）
CREATE DATABASE se_db_latin ENCODING 'LATIN1';
CREATE DATABASE se_db_utf8 ENCODING 'UTF8';

-- RULE table_comment_required: 每张表必须有表级和字段级注释 | 强制/ERROR | [豁免]
-- 预期触发: 带完整表/字段注释的建表（语义上应触发）→ 该规则在当前扩展版本无判定逻辑
--           （rule_ddl.c 全文件无该 id 的 audit_emit 调用），预期不产生 table_comment_required 审核消息
-- 预期不触发: 表其余维度合规；COMMENT 语句若触发其他规则输出属预期额外输出（不影响本豁免断言）
CREATE TABLE se_t_comment(id serial PRIMARY KEY, name text NOT NULL);
COMMENT ON TABLE se_t_comment IS '测试表注释';
COMMENT ON COLUMN se_t_comment.id IS '主键注释';
COMMENT ON COLUMN se_t_comment.name IS '名称注释';

-- =============================================================================
-- PART 4: 索引规则族（6 条规则）
-- =============================================================================

-- RULE idx_concurrently: 创建索引必须使用CONCURRENTLY | 强制/ERROR
-- 预期触发: 无 CONCURRENTLY 建索引 → SQL审核[idx_concurrently]: 创建索引 "idx_noc" 必须使用 CONCURRENTLY
-- 预期不触发: 合规索引 idx_se_base 用 CONCURRENTLY 创建（PART 0），无 idx_concurrently 输出
CREATE INDEX idx_noc ON se_t_base (name);

-- RULE idx_field_count: 单个索引字段数不超过5个 | 建议/WARNING
-- 预期触发: 单索引 7 字段 → SQL审核[idx_field_count]: 索引 "idx_many" 字段数 7 超过 5 个
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截；CIC 未启动、无 INVALID 索引残留）
-- 预期不触发: 单字段索引 idx_se_base，无 idx_field_count 输出
CREATE TABLE se_t_manycols(id serial PRIMARY KEY, c1 int NOT NULL, c2 int NOT NULL, c3 int NOT NULL, c4 int NOT NULL, c5 int NOT NULL, c6 int NOT NULL, c7 int NOT NULL);
CREATE INDEX CONCURRENTLY idx_many ON se_t_manycols (c1, c2, c3, c4, c5, c6, c7);

-- RULE idx_table_count: 单表索引数量不超过5个 | 建议/WARNING
-- 预期触发: se_t_base 已有主键索引 + idx_se_base（2 个），建 idx_tc1~idx_tc4 后存量 6 个，
--           再建 idx_tc5 时存量 6 超过 5 → SQL审核[idx_table_count]: 表 "se_t_base" 索引数量 6 超过 5 个
--           （规则在创建前检查 RelationGetIndexList 存量索引数，total>5 才触发）
--           注: CIC 修复（任务组 23）后，CREATE INDEX CONCURRENTLY 走 CIC 特判跳过目录元数据区，
--           idx_table_count 在 CIC 场景不可判定；本用例经「规则状态受控切换」（任务组 26.3）：
--           下方 `\! curl` 临时关闭 idx_concurrently，以非 CIC CREATE INDEX 触发后由
--           idx_redundant 用例段末尾的恢复语句重新开启。
-- 预期不触发: 索引数量 ≤5 时无输出
\! curl -s -X PUT -H 'Content-Type: application/json' -d '{"enabled":false}' "http://127.0.0.1:${SE_API_PORT}/api/v1/rules/idx_concurrently" > /dev/null 2>&1
CREATE INDEX idx_tc1 ON se_t_base (name);
CREATE INDEX idx_tc2 ON se_t_base (amount);
CREATE INDEX idx_tc3 ON se_t_base (created_at);
CREATE INDEX idx_tc4 ON se_t_base (status, created_at);
CREATE INDEX idx_tc5 ON se_t_base (name, amount);

-- RULE idx_method: 根据场景合理选择索引方法(btree/hash/gin/gist/brin) | 强制/ERROR
-- 预期触发: hash 多列索引 → SQL审核[idx_method]: 索引 "idx_hash" 使用 hash 方法不支持多列（PG 本身不支持
--           hash 多列，审核先于执行输出后语句报错）
-- 预期不触发: btree 索引 idx_se_base，无 idx_method 输出
CREATE INDEX CONCURRENTLY idx_hash ON se_t_base USING hash (name, amount);

-- RULE idx_selectivity: 索引必须创建在选择性较高的列上 | 强制/ERROR
-- 预期触发: 低基数列 flag 建索引（先造 100 行重复值 + ANALYZE）→ SQL审核[idx_selectivity]:
--           索引 "idx_sel" 的列 "flag" 选择性过低，不建议建立索引
--           （前置 ANALYZE se_t_sel 触发 cmd_analyze NOTICE 为预期额外输出）
--           注: CIC 修复（任务组 23）后，CREATE INDEX CONCURRENTLY 走 CIC 特判跳过目录元数据区，
--           idx_selectivity 在 CIC 场景不可判定；本用例在用例段内经「规则状态受控切换」
--           （临时关闭 idx_concurrently 后用非 CIC CREATE INDEX 触发，见任务组 26.3）验证。
-- 预期不触发: 高基数列/无统计信息列不触发（基表未 ANALYZE 的列无统计）
CREATE TABLE se_t_sel(id serial PRIMARY KEY, flag int NOT NULL);
INSERT INTO se_t_sel (flag) SELECT 1 FROM generate_series(1, 100);
ANALYZE se_t_sel;
CREATE INDEX idx_sel ON se_t_sel (flag);

-- RULE idx_redundant: 避免冗余或重复索引 | 强制/ERROR
-- 预期触发: 同表同列集二次建索引 → SQL审核[idx_redundant]: 索引 "idx_red2" 与表上已有索引列集重复，存在冗余索引
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截；
--             注: CIC 修复后 concurrent 索引走 CIC 特判，idx_redundant 在 CIC 场景不可判定，
--             本用例在用例段内经「规则状态受控切换」（临时关闭 idx_concurrently 后用
--             非 CIC CREATE INDEX 触发，见任务组 26.3）验证）
-- 预期不触发: 不同列集索引（idx_red1 vs idx_tc*）不触发
CREATE TABLE se_t_red(id serial PRIMARY KEY, name text NOT NULL, status varchar(20) NOT NULL);
CREATE INDEX idx_red1 ON se_t_red (name, status);
CREATE INDEX idx_red2 ON se_t_red (name, status);
-- [规则状态受控恢复（任务组 26.3）] 恢复 idx_concurrently 开启
\! curl -s -X PUT -H 'Content-Type: application/json' -d '{"enabled":true}' "http://127.0.0.1:${SE_API_PORT}/api/v1/rules/idx_concurrently" > /dev/null 2>&1

-- =============================================================================
-- PART 5: 视图规则族（3 条规则）
-- =============================================================================

-- RULE view_select_star: 视图禁止使用select * | 强制/ERROR
-- 预期触发: 视图内 select * → SQL审核[view_select_star]: 视图 ... 禁止使用 select *（视图名 vw_ 前缀合规，
--           不叠加 name_vw_prefix）
-- 预期不触发: 合规视图 vw_se_t_base 显式列，无 view_select_star 输出
CREATE VIEW vw_se_t_star AS SELECT * FROM se_t_base;

-- RULE view_order_by: 视图中禁止使用order by | 强制/ERROR
-- 预期触发: 视图内 order by → SQL审核[view_order_by]: 视图 ... 禁止使用 order by
-- 预期不触发: 合规视图 vw_se_t_base 无 order by，无 view_order_by 输出
CREATE VIEW vw_se_t_order AS SELECT id, name FROM se_t_base ORDER BY id;

-- RULE view_nested: 视图禁止嵌套其他视图 | 建议/WARNING
-- 预期触发: 视图 FROM 子查询 → SQL审核[view_nested]: 视图 "vw_se_t_nest" 建议避免嵌套其他视图或复杂子查询
--           （实现按 fromclause_has_subselect 检测 FROM 中的 RangeSubselect/JoinExpr，
--             而非视图引用嵌套；子查询内用显式列避免叠加 view_select_star；
--             WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、视图 vw_se_t_nest 不创建）
-- 预期不触发: 基于表的视图 vw_se_t_sub（FROM 为 RangeVar，无子查询无 JOIN），无 view_nested 输出
CREATE VIEW vw_se_t_sub AS SELECT id, name FROM se_t_base;
CREATE VIEW vw_se_t_nest AS SELECT id FROM (SELECT id, name FROM se_t_base) AS sub;

-- =============================================================================
-- PART 6: DML 与 SQL 编写规则族（8 条规则）
-- 说明: DML 用例基于专用表 se_t_dml（PART 0 已建并插入 2 行数据），保护基表数据。
-- =============================================================================

-- RULE dml_count_star: 统计行数使用count(*) | 强制/ERROR
-- 预期触发: count(列) → SQL审核[dml_count_star]: 统计行数建议使用 count(*)
-- 预期不触发: count(*) → 无输出
SELECT count(name) FROM se_t_dml;
SELECT count(*) FROM se_t_dml;

-- RULE dml_left_fuzzy: 避免左模糊查询(LIKE '%x') | 强制/ERROR
-- 预期触发: LIKE '%x' → SQL审核[dml_left_fuzzy]: 查询使用了左模糊匹配（LIKE 以 % 开头），建议避免
--           （SELECT * 在本用例中顺带验证 dml_select_star 豁免：不输出 dml_select_star）
-- 预期不触发: LIKE 'x%' → 无输出
SELECT * FROM se_t_dml WHERE name LIKE '%x';
SELECT id FROM se_t_dml WHERE name LIKE 'a%';

-- RULE dml_batch_copy: 插入大量数据时建议使用COPY | 推荐/NOTICE
-- 预期触发: 多行 INSERT VALUES → SQL审核[dml_batch_copy]: INSERT 语句一次写入 2 行数据，批量导入建议使用 COPY
-- 预期不触发: 单行 VALUES（PART 0 基表插入已验）→ 无输出
INSERT INTO se_t_dml (name, amount, status) VALUES ('c', 3.00, 'ok'), ('d', 4.00, 'ok');

-- RULE dml_in_to_exists: 使用EXISTS子句代替IN操作符 | 推荐/NOTICE
-- 预期触发: IN (子查询) → SQL审核[dml_in_to_exists]: 查询使用了 IN (子查询)，建议改用 EXISTS 子句
-- 预期不触发: IN (常量列表) → 无输出
SELECT id FROM se_t_dml WHERE id IN (SELECT id FROM se_t_dml);
SELECT id FROM se_t_dml WHERE id IN (1, 2);

-- RULE dml_array_vs_tmp: 使用数组代替临时表 | 推荐/NOTICE
-- 预期触发: 合规临时表（tmp 前缀，避免叠加 name_tmp_prefix）→ SQL审核[dml_array_vs_tmp]:
--           临时表 "tmp_se_t_arr" 若用于中间数据计算，建议使用数组替代
-- 预期不触发: 常规表（无临时表语义）→ 无输出
CREATE TEMP TABLE tmp_se_t_arr(id int PRIMARY KEY);

-- RULE dml_no_where: UPDATE/DELETE缺少WHERE条件将影响全部行 | 强制/ERROR
-- 预期触发: 无 WHERE DELETE/UPDATE → SQL审核[dml_no_where]: DELETE 语句缺少 WHERE 条件，清空表请使用 TRUNCATE
--           / UPDATE 语句缺少 WHERE 条件，将影响全部行（default_level=ERROR 实测）
-- 预期不触发: 带 WHERE 的 UPDATE → 无输出
DELETE FROM se_t_dml;
UPDATE se_t_dml SET status = 'x';
UPDATE se_t_dml SET status = 'ok' WHERE id = 1;

-- RULE dml_select_star: select只获取必要字段, 禁止select * | 强制/ERROR | [豁免]
-- 预期触发: SELECT * 的 DML（语义上应触发）→ 该规则在当前扩展版本无判定逻辑
--           （hook_analyze.c 明确「DML 的 select * 无法可靠检测」；视图场景输出 view_select_star），
--           预期不产生 dml_select_star 审核消息
-- 预期不触发: 本语句除豁免声明外无 SQL审核 输出
SELECT * FROM se_t_dml;

-- RULE dml_delete_truncate: 清空表建议使用TRUNCATE而非无WHERE的DELETE | 强制/ERROR | [豁免]
-- 预期触发: 无 WHERE DELETE（语义上应触发）→ 该规则在当前扩展版本无独立触发点，实际输出为
--           dml_no_where（hook_analyze.c:297/300，文案含「清空表请使用 TRUNCATE」），
--           预期不产生 dml_delete_truncate 审核消息
-- 预期不触发: 本语句实际输出 dml_no_where ERROR（见 PART 6 dml_no_where 用例）
DELETE FROM se_t_dml;

-- =============================================================================
-- PART 7: 可编程对象规则族（4 条规则）
-- 说明: 函数体审核为文本检查（rule_program.c contains_nocase），函数体含
--       create/drop/alter/truncate 即触发，与执行期行为无关。
-- =============================================================================

-- RULE prog_ddl_forbidden: 函数体内禁止执行DDL | 建议/WARNING
-- 预期触发: 函数体含 CREATE → SQL审核[prog_ddl_forbidden]: 函数 "se_f_ddl" 函数体内不建议执行 DDL 语句
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、函数 se_f_ddl 不创建）
-- 预期不触发: 合规函数 se_f_base（函数体仅 SELECT 1，PART 0 已建）→ 无输出
CREATE FUNCTION se_f_ddl() RETURNS void LANGUAGE plpgsql AS $$
BEGIN
    CREATE TABLE se_t_ddl_tmp(id int);
END
$$;

-- RULE prog_business_logic: 不建议在数据库中存放业务逻辑 | 建议/WARNING
-- 预期触发: 函数体超过 500 字符 → SQL审核[prog_business_logic]: 函数 "se_f_logic" 函数体较长，不建议在数据库中存放复杂业务逻辑
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、函数 se_f_logic 不创建）
-- 预期不触发: 短函数体 se_f_base → 无输出
CREATE FUNCTION se_f_logic() RETURNS int LANGUAGE sql AS $BODY$SELECT 1; -- 0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ$BODY$;

-- RULE prog_close_cursor: 游标使用后必须及时关闭 | 建议/WARNING
-- 预期触发: PL/pgSQL 声明游标未 CLOSE → SQL审核[prog_close_cursor]: 函数 "se_f_cursor" 声明游标后请确保及时 CLOSE
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、函数 se_f_cursor 不创建）
-- 预期不触发: 无游标的函数 se_f_base → 无输出
CREATE FUNCTION se_f_cursor() RETURNS void LANGUAGE plpgsql AS $$
DECLARE
    c CURSOR FOR SELECT id FROM se_t_base;
BEGIN
    OPEN c;
END
$$;

-- RULE prog_name: 函数/过程/触发器命名应符合命名规范 | 强制/ERROR | [豁免]
-- 预期触发: 不合规函数名（大写）→ 该规则在当前扩展版本无独立输出，函数命名走 audit_check_name
--           （实际输出 name_charset，而非 prog_name），预期不产生 prog_name 审核消息
-- 预期不触发: 合规函数名 se_f_base → 无 name_* 输出
CREATE FUNCTION "SeFBadName"() RETURNS int LANGUAGE sql AS 'SELECT 1';

-- =============================================================================
-- PART 8: 对象变更与删除规则族（2 条规则）
-- =============================================================================

-- RULE ddl_high_risk_drop: 高危删除操作(DATABASE/TABLESPACE/SCHEMA) | 建议/WARNING
-- 预期触发: DROP SCHEMA ... CASCADE → SQL审核[ddl_high_risk_drop]: 高危删除操作 ...（WARNING 拦截语义：
--           输出级别前缀为 ERROR，语句被拦截、SCHEMA 残留——清理段经规则状态受控（任务组 26.2）放行）；
--           DROP DATABASE 不存在的库 → 审核先输出 WARNING 后语句报错（属预期，拦截语义下仅输出审核消息）
-- 预期不触发: 普通 DROP TABLE（见 PART 14）→ 无 ddl_high_risk_drop 输出
CREATE SCHEMA se_x_schema;
DROP SCHEMA se_x_schema CASCADE;
DROP DATABASE se_x_nodb;

-- RULE ddl_truncate_warn: 生产环境谨慎使用TRUNCATE | 建议/WARNING
-- 预期触发: TRUNCATE → SQL审核[ddl_truncate_warn]: 生产环境谨慎使用 TRUNCATE
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、表数据不清空）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证（见 pgsqlauditengine_api.md PUT /rules/{id} 用例）
TRUNCATE se_t_dml;

-- =============================================================================

-- PART 10: 命令类规则族（14 条规则）
-- 说明: 命令类多属恒触发类，反例以「规则关闭后不触发」方式验证（见 api.md）。
-- =============================================================================

-- RULE cmd_vacuum: VACUUM 操作需谨慎(FULL 获取排他锁并重写表) | 建议/WARNING
-- 预期触发: VACUUM / VACUUM FULL → SQL审核[cmd_vacuum]: VACUUM 操作回显 / VACUUM FULL 获取排他锁并重写表
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、VACUUM 不执行）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
VACUUM se_t_base;
VACUUM FULL se_t_base;

-- RULE cmd_analyze: ANALYZE 统计信息采集应在业务低峰期进行 | 推荐/NOTICE
-- 预期触发: ANALYZE → SQL审核[cmd_analyze]: ANALYZE 统计信息采集应在业务低峰期进行 (目标表: se_t_base)
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
ANALYZE se_t_base;

-- RULE cmd_checkpoint: 手动 CHECKPOINT 仅建议排障场景使用 | 推荐/NOTICE
-- 预期触发: CHECKPOINT → SQL审核[cmd_checkpoint]: 手动 CHECKPOINT 仅建议排障场景使用
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
CHECKPOINT;

-- RULE cmd_cluster: CLUSTER 需排他锁并重写表, 生产需谨慎 | 建议/WARNING
-- 预期触发: CLUSTER 表 → SQL审核[cmd_cluster]: CLUSTER 需排他锁并重写表, 生产需谨慎 (目标表: se_t_base)
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、CLUSTER 不执行）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
CLUSTER se_t_base;

-- RULE cmd_reindex: REINDEX 建议使用 CONCURRENTLY | 建议/WARNING
-- 预期触发: REINDEX TABLE（无 CONCURRENTLY）→ SQL审核[cmd_reindex]: REINDEX 建议使用 CONCURRENTLY；
--           REINDEX INDEX CONCURRENTLY 子用例 [PG12+] → SQL审核[cmd_reindex]: REINDEX 操作回显 (使用 CONCURRENTLY)
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、REINDEX 不执行）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
REINDEX TABLE se_t_base;
-- [CUT PG11] REINDEX INDEX CONCURRENTLY se_t_base_pkey;  -- [PG12+]

-- RULE cmd_refresh_matview: REFRESH MATVIEW 建议使用 CONCURRENTLY | 建议/WARNING
-- 预期触发: REFRESH MATERIALIZED VIEW → SQL审核[cmd_refresh_matview]: REFRESH MATERIALIZED VIEW 建议使用 CONCURRENTLY
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截；前置 CREATE MATERIALIZED VIEW 若触发
--             obj_matview WARNING 被拦截，则 REFRESH 目标不存在，但 cmd_refresh_matview 审核先于内核执行仍输出）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
CREATE MATERIALIZED VIEW vw_se_mv AS SELECT id, name FROM se_t_base;
REFRESH MATERIALIZED VIEW vw_se_mv;

-- RULE cmd_lock: LOCK TABLE 需注意锁模式与持锁时长 | 建议/WARNING
-- 预期触发: LOCK TABLE（显式事务内，LOCK 须在事务块中使用）→ SQL审核[cmd_lock]: LOCK TABLE 需注意锁模式与持锁时长
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截；后续 COMMIT 正常提交空事务）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
BEGIN;
LOCK TABLE se_t_base IN ACCESS SHARE MODE;
COMMIT;

-- RULE cmd_load: LOAD 加载共享库属安全敏感操作 | 建议/WARNING
-- 预期触发: LOAD → SQL审核[cmd_load]: LOAD 加载共享库属安全敏感操作 (文件: plpgsql)
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、LOAD 不执行）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
LOAD 'plpgsql';

-- RULE cmd_discard: DISCARD 将重置会话全部状态 | 推荐/NOTICE
-- 预期触发: DISCARD ALL → SQL审核[cmd_discard]: DISCARD ALL 将重置会话状态
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
DISCARD ALL;

-- RULE cmd_explain: EXPLAIN ANALYZE 将真实执行被分析语句 | 推荐/NOTICE
-- 预期触发: EXPLAIN ANALYZE → SQL审核[cmd_explain]: EXPLAIN ANALYZE 将真实执行被分析语句
--           （不递归审核被分析语句；用显式列避免 select *）
-- 预期不触发: EXPLAIN（无 ANALYZE）→ SQL审核[cmd_explain]: EXPLAIN 语句识别回显
EXPLAIN ANALYZE SELECT id FROM se_t_base;
EXPLAIN SELECT id FROM se_t_base;

-- RULE cmd_copy: COPY 批量导入导出需确认数据源/格式/敏感范围 | 建议/WARNING
-- 预期触发: COPY TO 文件 → SQL审核[cmd_copy]: COPY TO 批量导出需确认敏感数据范围 (表: se_t_base, 去向: /tmp/...)
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、文件不创建）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
COPY se_t_base TO '/tmp/se_t_copy.csv';

-- RULE cmd_prepare: 预编译语句(PREPARE/EXECUTE/DEALLOCATE)识别回显 | 推荐/NOTICE
-- 预期触发: PREPARE → SQL审核[cmd_prepare]: PREPARE 预编译语句识别回显 (名称: se_f_stmt)
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
PREPARE se_f_stmt(int) AS SELECT id FROM se_t_base WHERE id = $1;

-- RULE cmd_cursor: 游标使用后必须 CLOSE, 避免长事务持有 | 建议/WARNING
-- 预期触发: DECLARE CURSOR（显式事务内）→ SQL审核[cmd_cursor]: 游标使用后必须 CLOSE；
--           CLOSE → SQL审核[cmd_cursor]: CLOSE 游标操作回显
--           （WARNING 拦截语义：输出级别前缀为 ERROR，DECLARE 被拦截后游标不存在，
--             CLOSE 仍输出审核消息；后续 COMMIT 正常提交空事务）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
BEGIN;
DECLARE se_c_cur CURSOR FOR SELECT id FROM se_t_base;
CLOSE se_c_cur;
COMMIT;

-- RULE cmd_notify: 通知通道需评估性能开销与使用范围 | 建议/WARNING
-- 预期触发: NOTIFY → SQL审核[cmd_notify]: NOTIFY 通知通道需评估性能开销与使用范围 (通道: se_x_channel)
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、通知不发送）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
NOTIFY se_x_channel, 'msg';

-- =============================================================================
-- PART 11: 对象类规则族（12 条规则）
-- 说明: 对有执行前置条件的对象语句，利用「审核先于执行」机制（审核消息在
--       语句报错前输出），psql 继续执行后续用例。
-- =============================================================================

-- RULE obj_policy: 行级安全策略变更需评估既有访问路径 | 建议/WARNING
-- 预期触发: CREATE POLICY → SQL审核[obj_policy]: 行级安全策略变更需评估对既有访问路径的影响 (策略: se_x_policy, 表: se_t_base)
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、策略不创建）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
CREATE POLICY se_x_policy ON se_t_base USING (true);

-- RULE obj_publication: 发布变更影响逻辑复制链路 | 建议/WARNING
-- 预期触发: CREATE PUBLICATION → SQL审核[obj_publication]: 发布变更影响逻辑复制链路 (发布: se_x_pub)
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、发布不创建）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
CREATE PUBLICATION se_x_pub FOR TABLE se_t_base;

-- RULE obj_subscription: 订阅变更影响逻辑复制链路 | 建议/WARNING
-- 预期触发: CREATE SUBSCRIPTION → SQL审核[obj_subscription]: 订阅变更影响逻辑复制链路 (订阅: se_x_sub)
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截，无法连接目标库问题不出现）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
CREATE SUBSCRIPTION se_x_sub CONNECTION 'host=127.0.0.1 port=5439 dbname=se_x_nodb user=postgres password=postgres' PUBLICATION se_x_pub;

-- RULE obj_fdw: 外部数据对象涉及跨系统访问与凭据配置 | 建议/WARNING
-- 预期触发: CREATE FOREIGN DATA WRAPPER → SQL审核[obj_fdw]: 外部数据对象涉及跨系统访问与凭据配置 (对象: se_x_fdw)
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、FDW 不创建）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
CREATE FOREIGN DATA WRAPPER se_x_fdw;

-- RULE obj_extension: 扩展需评估来源/版本与影响面 | 建议/WARNING
-- 预期触发: CREATE EXTENSION 不存在的扩展 → SQL审核[obj_extension]: 扩展需评估来源/版本与影响面 (扩展: se_x_nonexistent_ext)
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截，不再出现"审核先输出后语句报错"）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
-- 注: CREATE LANGUAGE <未知语言> 在 PG 中按扩展处理，同样输出 obj_extension（见 obj_language 段说明）
CREATE EXTENSION se_x_nonexistent_ext;

-- RULE obj_language: 过程语言(尤其非受信语言)需评估执行风险 | 建议/WARNING
-- 预期触发: DROP LANGUAGE IF EXISTS（语言不存在，审核先于执行）→
--           SQL审核[obj_language]: 过程语言(尤其非受信语言)需评估执行风险 (语言: se_x_lang)
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
-- 注: CREATE LANGUAGE <未知语言> 在 PG 中被当作扩展处理（报错 extension "..." is not available），
--     触发 obj_extension 而非 obj_language；obj_language 的触发点为 DROP LANGUAGE
--     （PART 0/14 清理段的 DROP LANGUAGE se_x_lang 同款输出）。
DROP LANGUAGE IF EXISTS se_x_lang;

-- RULE obj_advanced: 高级对象定义/变更需由资深 DBA 实施 | 建议/WARNING
-- 预期触发: CREATE STATISTICS / ALTER STATISTICS [PG13+] → SQL审核[obj_advanced]: STATISTICS 定义/变更需由资深 DBA 实施
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
CREATE STATISTICS se_x_stats ON name, status FROM se_t_base;
-- [CUT PG11] ALTER STATISTICS se_x_stats SET STATISTICS 100;  -- [PG13+]

-- RULE obj_alter_system: ALTER SYSTEM 修改服务器级配置影响全局 | 建议/WARNING
-- 预期触发: ALTER SYSTEM SET → SQL审核[obj_alter_system]: ALTER SYSTEM 修改服务器级配置影响全局 (参数: work_mem)
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、参数不落库；用例后 RESET 语句同样
--             被 obj_alter_system 拦截，无残留配置，无需复位）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
ALTER SYSTEM SET work_mem = '16MB';
ALTER SYSTEM RESET work_mem;

-- RULE obj_reassign_owned: 对象所有权批量转移需评估影响 | 建议/WARNING
-- 预期触发: REASSIGN OWNED → SQL审核[obj_reassign_owned]: 对象所有权批量转移需评估影响 (源角色: postgres, 目标角色: postgres)
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、所有权不转移）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
REASSIGN OWNED BY postgres TO postgres;

-- RULE obj_ctas: 评估是否确需创建实体表(CTAS/SELECT INTO) | 建议/WARNING
-- 预期触发: CREATE TABLE AS SELECT → SQL审核[obj_ctas]: 评估是否确需创建实体表(CTAS/SELECT INTO) (新表: se_t_ctas)
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、CTAS 表不创建）
-- 预期不触发: 常规 CREATE TABLE（基表）→ 无 obj_ctas 输出
CREATE TABLE se_t_ctas AS SELECT id, name FROM se_t_base;

-- RULE obj_matview: 物化视图需评估数据新鲜度与刷新策略 | 建议/WARNING
-- 预期触发: CREATE MATERIALIZED VIEW → SQL审核[obj_matview]: 物化视图需评估数据新鲜度与刷新策略 (物化视图: vw_se_mv2)
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、物化视图不创建）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
CREATE MATERIALIZED VIEW vw_se_mv2 AS SELECT id, name FROM se_t_base;

-- RULE obj_security_label: SECURITY LABEL 识别回显 | 推荐/NOTICE
-- 预期触发: SECURITY LABEL 无 provider → SQL审核[obj_security_label]: SECURITY LABEL 识别回显
--           （审核先输出后语句报错，属预期）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
SECURITY LABEL FOR se_x_provider ON TABLE se_t_base IS 'secrecy';

-- =============================================================================
-- PART 12: DCL/TCL/DML 补充规则族（3 条规则）
-- =============================================================================

-- RULE dcl_set_role: 会话身份切换需确认权限边界 | 建议/WARNING
-- 预期触发: SET ROLE → SQL审核[dcl_set_role]: 会话身份切换需确认权限边界 (SET ROLE 目标角色: postgres)
--           （WARNING 拦截语义：输出级别前缀为 ERROR，SET ROLE 被拦截、角色未切换，
--             SET ROLE NONE 复位语句同样被拦截，属预期额外输出）
-- 预期不触发: SET work_mem 非会话身份切换 → 无 dcl_set_role 输出（可能触发 echo 回显，属预期额外输出）
SET ROLE postgres;
SET ROLE NONE;
SET work_mem = '16MB';

-- RULE tcl_set_transaction: SET TRANSACTION/CONSTRAINTS 识别回显 | 推荐/NOTICE
-- 预期触发: SET TRANSACTION READ ONLY / SET CONSTRAINTS ALL DEFERRED（显式事务内）→
--           SQL审核[tcl_set_transaction]: SET TRANSACTION 事务特征识别回显 / SET CONSTRAINTS 约束检查时机识别回显
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
BEGIN;
SET TRANSACTION READ ONLY;
COMMIT;
BEGIN;
SET CONSTRAINTS ALL DEFERRED;
COMMIT;

-- RULE dml_merge_check: MERGE 需仔细评估影响行范围 | 建议/WARNING | [PG15+]
-- 预期触发: MERGE 存在未限定 WHEN 条件分支 → SQL审核[dml_merge_check]: MERGE 存在未限定 WHEN 条件的分支, 请仔细评估影响行范围
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、MERGE 不执行）
-- 预期不触发: MERGE 全部 WHEN 带条件 → 无输出
-- [CUT PG11] MERGE INTO se_t_dml AS t USING (SELECT 3 AS id) AS s ON (t.id = s.id)  -- [PG15+]
-- [CUT PG11] WHEN NOT MATCHED THEN INSERT (id) VALUES (s.id);
-- [CUT PG11] MERGE INTO se_t_dml AS t USING (SELECT 5 AS id) AS s ON (t.id = s.id)  -- [PG15+]
-- [CUT PG11] WHEN NOT MATCHED AND s.id > 0 THEN INSERT (id, name, amount, status) VALUES (s.id, 'merge-ok', 0.00, 'ok');

-- =============================================================================
-- PART 13: 对象存在性与命令兜底规则族（2 条规则）
-- =============================================================================

-- RULE obj_exists_check: CREATE 对象已存在冲突(无INE=ERROR/有INE=WARNING) | 强制/ERROR
-- 预期触发: 同名 CREATE TABLE 无 INE → SQL审核[obj_exists_check]: 对象已存在, 请使用 IF NOT EXISTS 或删除旧对象 (表: se_t_dup)；
--           带 INE → SQL审核[obj_exists_check]: 对象已存在, IF NOT EXISTS 生效, 跳过创建 (表: se_t_dup)
--           （WARNING 拦截语义：带 INE 分支输出级别前缀为 ERROR 且语句被拦截，属预期行为变更 Q10；
--             二次 CREATE 命中存在性检查后跳过后续内容审核）
-- 预期不触发: 全新对象名 → 无 obj_exists_check 输出
CREATE TABLE se_t_dup(id serial PRIMARY KEY, name text NOT NULL);
CREATE TABLE se_t_dup(id serial PRIMARY KEY, name text NOT NULL);
CREATE TABLE IF NOT EXISTS se_t_dup(id serial PRIMARY KEY, name text NOT NULL);

-- RULE echo: 未映射规则的命令识别回显 | 推荐/NOTICE
-- 预期触发: ALTER DATABASE ... SET（未映射命令）→ SQL审核[echo]: 未映射命令识别: ALTER DATABASE ...（恒触发类）
--           （用例后 ALTER DATABASE ... RESET 复位该库级参数，同样输出 echo 属预期）
-- 预期不触发: 恒触发类，以「规则关闭后不触发」方式验证
ALTER DATABASE postgres SET search_path TO public;
ALTER DATABASE postgres RESET search_path;

-- =============================================================================
-- PART 14: 后置清理与复位段
-- 说明：DROP 全部测试对象；规则开关/级别改动复位方式见 pgsqlauditengine_api.md
--       （通过 RESTful PUT /api/v1/rules/{id} 恢复 enabled=true、level=default_level；
--         ALTER SYSTEM 用例改动用 ALTER SYSTEM RESET 复位）
-- 规则状态受控（任务组 26.2）：与 PART 0 相同，本段 DROP 语句在 WARNING 拦截语义下
--       可能被 ddl_high_risk_drop/obj_publication/obj_subscription/obj_fdw/obj_language
--       等 WARNING 规则拦截；回归编排脚本在本段执行前临时关闭/降级上述规则，执行后
--       恢复用例段配置，脚本结束后还原初始配置快照。
-- =============================================================================
DROP TABLE IF EXISTS se_t_base CASCADE;
DROP TABLE IF EXISTS se_t_dml CASCADE;
DROP TABLE IF EXISTS se_t_manycols CASCADE;
DROP TABLE IF EXISTS se_t_red CASCADE;
DROP TABLE IF EXISTS se_t_sel CASCADE;
DROP TABLE IF EXISTS se_t_pkbad CASCADE;
DROP TABLE IF EXISTS se_t_ukbad CASCADE;
DROP TABLE IF EXISTS se_t_ckbad CASCADE;
DROP TABLE IF EXISTS se_t_vc CASCADE;
DROP TABLE IF EXISTS se_t_num CASCADE;
DROP TABLE IF EXISTS se_t_dt CASCADE;
DROP TABLE IF EXISTS se_t_js CASCADE;
DROP TABLE IF EXISTS se_t_ms CASCADE;
DROP TABLE IF EXISTS se_t_nopk CASCADE;
DROP TABLE IF EXISTS se_t_pkname CASCADE;
DROP TABLE IF EXISTS se_t_pktype CASCADE;
DROP TABLE IF EXISTS se_t_null CASCADE;
DROP TABLE IF EXISTS se_t_fk CASCADE;
DROP TABLE IF EXISTS se_t_resv CASCADE;
DROP TABLE IF EXISTS se_t_comment CASCADE;
DROP TABLE IF EXISTS se_t_dup CASCADE;
DROP TABLE IF EXISTS se_t_ctas CASCADE;
DROP TABLE IF EXISTS se_t_txndrop CASCADE;
DROP TABLE IF EXISTS bak_se_t_base CASCADE;
DROP TABLE IF EXISTS se_t_tmpbad CASCADE;
DROP TABLE IF EXISTS tmp_se_t_arr CASCADE;
DROP TABLE IF EXISTS "SeTUpper" CASCADE;
DROP TABLE IF EXISTS "1st_table" CASCADE;
DROP TABLE IF EXISTS "select" CASCADE;
DROP TABLE IF EXISTS se_t_len_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz CASCADE;
DROP VIEW IF EXISTS vw_se_t_base CASCADE;
DROP VIEW IF EXISTS vw_se_t_star CASCADE;
DROP VIEW IF EXISTS vw_se_t_order CASCADE;
DROP VIEW IF EXISTS vw_se_t_sub CASCADE;
DROP VIEW IF EXISTS vw_se_t_nest CASCADE;
DROP VIEW IF EXISTS se_v_bad CASCADE;
DROP MATERIALIZED VIEW IF EXISTS vw_se_mv CASCADE;
DROP MATERIALIZED VIEW IF EXISTS vw_se_mv2 CASCADE;
DROP INDEX IF EXISTS idx_se_base CASCADE;
DROP INDEX IF EXISTS idx_noc CASCADE;
DROP INDEX IF EXISTS idx_hash CASCADE;
DROP INDEX IF EXISTS idx_many CASCADE;
DROP INDEX IF EXISTS idx_tc1 CASCADE;
DROP INDEX IF EXISTS idx_tc2 CASCADE;
DROP INDEX IF EXISTS idx_tc3 CASCADE;
DROP INDEX IF EXISTS idx_tc4 CASCADE;
DROP INDEX IF EXISTS idx_tc5 CASCADE;
DROP INDEX IF EXISTS idx_sel CASCADE;
DROP INDEX IF EXISTS idx_red1 CASCADE;
DROP INDEX IF EXISTS idx_red2 CASCADE;
DROP INDEX IF EXISTS se_i_bad CASCADE;
DROP FUNCTION IF EXISTS se_f_base CASCADE;
DROP FUNCTION IF EXISTS se_f_ddl CASCADE;
DROP FUNCTION IF EXISTS se_f_logic CASCADE;
DROP FUNCTION IF EXISTS se_f_cursor CASCADE;
DROP FUNCTION IF EXISTS se_f_badname CASCADE;
DROP SCHEMA IF EXISTS se_x_schema CASCADE;
DROP PUBLICATION IF EXISTS se_x_pub;
DROP SUBSCRIPTION IF EXISTS se_x_sub;
DROP FOREIGN DATA WRAPPER IF EXISTS se_x_fdw CASCADE;
DROP LANGUAGE IF EXISTS se_x_lang CASCADE;
DROP STATISTICS IF EXISTS se_x_stats;
DROP DATABASE IF EXISTS se_db_test;
DROP DATABASE IF EXISTS se_db_latin;
DROP DATABASE IF EXISTS se_db_utf8;
DROP SCHEMA IF EXISTS se_x_p15 CASCADE;
DROP SEQUENCE IF EXISTS se_x_p15_seq;
DROP DOMAIN IF EXISTS se_x_p15_dom;

DROP PROCEDURE IF EXISTS se_p_p15();
DROP FUNCTION IF EXISTS se_f_p15_trg() CASCADE;
DROP FUNCTION IF EXISTS se_f_p15_evt() CASCADE;

-- =============================================================================
-- PART 15: 阶段二新增规则命令族用例（22 条新增规则，7 子分区）
-- -----------------------------------------------------------------------------
-- 说明：本段覆盖阶段二新增 22 条规则的「预期触发/预期不触发」用例，与既有
-- PART 0-14 同风格；置于 PART 14 之后、PART 9 之前（PART 9 tcl_multi_ddl_in_txn
-- 触发后状态机卡死为既定行为，本段需在状态机正常时执行）。
-- WARNING 拦截语义（任务组 25）：本段 8 条 WARNING 新增规则（tcl_prepared/
-- dcl_grant/dcl_grant_role/dcl_default_privileges/dcl_role/prog_trigger/
-- prog_event_trigger/prog_rule）命中时输出级别前缀为 ERROR + errdetail WARNING
-- 标识，语句被拦截；14 条 NOTICE 规则放行。
-- 幂等性（决策 D17）：事务用例 BEGIN/COMMIT（或 ROLLBACK）严格配对；SET 用例随
-- RESET 复位；DROP OWNED 仅作用于不存在的专用角色（NOTICE 放行后 PG 报错，无
-- 副作用）；被 WARNING 拦截的对象（触发器/规则/角色）不落库、无需清理；可创建
-- 对象（schema/序列/域/枚举/过程/触发器函数）就地 DROP 清理，schema 由
-- PART 0/14 清理段兜底。
-- =============================================================================

-- ---- 15.1 事务控制族（tcl_begin/tcl_commit/tcl_rollback/tcl_savepoint/tcl_prepared）----

-- RULE tcl_begin/tcl_commit: 显式事务开启/提交识别回显 | 推荐/NOTICE
-- 预期触发: BEGIN; COMMIT; → SQL审核[tcl_begin]: 显式事务开启... + SQL审核[tcl_commit]: 显式事务提交...
BEGIN;
COMMIT;

-- RULE tcl_rollback: 显式事务回滚识别回显 | 推荐/NOTICE
-- 预期触发: BEGIN; ROLLBACK; → SQL审核[tcl_rollback]: 显式事务回滚...
BEGIN;
ROLLBACK;

-- RULE tcl_savepoint: 保存点操作识别回显 | 推荐/NOTICE
-- 预期触发: SAVEPOINT/ROLLBACK TO/RELEASE → SQL审核[tcl_savepoint]: 保存点操作...
--           （本块 BEGIN/COMMIT 配对，事务内无 DDL，不触发 tcl_multi_ddl_in_txn）
BEGIN;
SAVEPOINT se_sp1;
ROLLBACK TO se_sp1;
RELEASE se_sp1;
COMMIT;

-- RULE tcl_prepared: 两阶段事务需评估分布式一致性 | 建议/WARNING
-- 预期触发: PREPARE TRANSACTION → SQL审核[tcl_prepared]: 两阶段事务...
--           （WARNING 拦截语义：输出级别前缀为 ERROR，语句被拦截、事务 aborted，
--             随 ROLLBACK 复位；审核拦截先于 max_prepared_transactions 检查）
BEGIN;
PREPARE TRANSACTION 'se_x_p15_2pc';
ROLLBACK;

-- ---- 15.2 会话设置族（cmd_set/cmd_show）----

-- RULE cmd_set: 会话参数设置识别回显 | 推荐/NOTICE
-- 预期触发: SET/RESET → SQL审核[cmd_set]: 会话参数设置...
--           （SET LOCAL 在事务内执行；RESET 复位；不触碰 role/session_authorization，
--             走既有 dcl_set_role）
SET search_path = public;
BEGIN;
SET LOCAL search_path = public;
COMMIT;
RESET search_path;

-- RULE cmd_show: 会话参数查询识别回显 | 推荐/NOTICE
-- 预期触发: SHOW → SQL审核[cmd_show]: 会话参数查询...
SHOW search_path;

-- ---- 15.3 过程调用族（cmd_call/cmd_do）----

-- RULE cmd_do: 匿名块执行识别回显 | 推荐/NOTICE
-- 预期触发: DO → SQL审核[cmd_do]: 匿名块执行...
DO $$ BEGIN NULL; END $$ LANGUAGE plpgsql;

-- RULE cmd_call: 存储过程调用识别回显 | 推荐/NOTICE
-- 预期触发: CALL → SQL审核[cmd_call]: 存储过程调用...
-- 前置: CREATE PROCEDURE（PG11+，触发 ddl_create_object NOTICE 为预期额外输出）
CREATE PROCEDURE se_p_p15() LANGUAGE plpgsql AS $$ BEGIN NULL; END $$;
CALL se_p_p15();
DROP PROCEDURE IF EXISTS se_p_p15();

-- ---- 15.4 权限与角色族（dcl_grant/dcl_grant_role/dcl_default_privileges/dcl_role）----
-- 本族命令（GRANT/REVOKE/ALTER DEFAULT PRIVILEGES/CREATE/ALTER/DROP ROLE/
-- DROP OWNED）需以超级用户执行，见文件头前提说明。

-- RULE dcl_grant: 对象权限授予/回收需确认权限边界 | 建议/WARNING
-- 预期触发: GRANT/REVOKE（对象权限）→ SQL审核[dcl_grant]: ...（WARNING 拦截，语句不执行，无副作用）
GRANT SELECT ON se_t_base TO postgres;
REVOKE SELECT ON se_t_base FROM postgres;

-- RULE dcl_grant_role: 角色成员授予/回收需确认角色边界 | 建议/WARNING
-- 预期触发: GRANT/REVOKE（角色成员）→ SQL审核[dcl_grant_role]: ...（WARNING 拦截，语句不执行，无副作用）
GRANT pg_signal_backend TO postgres;
REVOKE pg_signal_backend FROM postgres;

-- RULE dcl_default_privileges: 默认权限变更需确认权限边界 | 建议/WARNING
-- 预期触发: ALTER DEFAULT PRIVILEGES → SQL审核[dcl_default_privileges]: ...（WARNING 拦截，语句不执行，无副作用）
ALTER DEFAULT PRIVILEGES FOR ROLE postgres IN SCHEMA public GRANT SELECT ON TABLES TO postgres;

-- RULE dcl_role: 角色创建/修改/删除需评估权限与归属 | 建议/WARNING
-- 预期触发: CREATE/ALTER/DROP ROLE → SQL审核[dcl_role]: ...（WARNING 拦截，角色不落库，天然幂等）
CREATE ROLE se_r_p15;
ALTER ROLE se_r_p15 SET work_mem = '16MB';
DROP ROLE IF EXISTS se_r_p15;

-- ---- 15.5 注释/重命名/属主族（obj_comment/obj_rename_owner/obj_drop_owned）----

-- RULE obj_comment: 对象注释识别回显 | 推荐/NOTICE
-- 预期触发: COMMENT ON → SQL审核[obj_comment]: 对象注释识别回显...
COMMENT ON TABLE se_t_base IS 'se p15 comment';
COMMENT ON COLUMN se_t_base.id IS 'se p15 pk comment';
COMMENT ON FUNCTION se_f_base() IS 'se p15 func comment';

-- RULE obj_rename_owner: 对象重命名/属主变更识别回显 | 推荐/NOTICE
-- 预期触发: ALTER TABLE RENAME/OWNER/SET SCHEMA → SQL审核[obj_rename_owner]: ...
--           （RENAME 成对出现保幂等；OWNER/SET SCHEMA 作用于既有对象）
ALTER TABLE se_t_base RENAME TO se_t_base_p15;
ALTER TABLE se_t_base_p15 RENAME TO se_t_base;
ALTER TABLE se_t_base OWNER TO postgres;
ALTER TABLE se_t_base SET SCHEMA public;

-- RULE obj_drop_owned: DROP OWNED 批量回收需评估影响 | 推荐/NOTICE
-- 预期触发: DROP OWNED BY 不存在的角色 → SQL审核[obj_drop_owned]: ...（NOTICE 放行后
--           PG 报 role does not exist，属预期，无副作用）
DROP OWNED BY se_r_p15_nonexist;

-- ---- 15.6 触发器/事件触发器/规则族（prog_trigger/prog_event_trigger/prog_rule）----

-- 前置: 触发器/事件触发器函数（触发 ddl_create_object/prog_name 检查，NOTICE 放行）
CREATE FUNCTION se_f_p15_trg() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN RETURN NEW; END $$;
CREATE FUNCTION se_f_p15_evt() RETURNS event_trigger LANGUAGE plpgsql AS $$ BEGIN NULL; END $$;

-- RULE prog_trigger: 触发器创建/变更/删除需评估执行时机与开销 | 建议/WARNING
-- 预期触发: CREATE/ALTER RENAME/ENABLE/DROP TRIGGER → SQL审核[prog_trigger]: ...
--           （WARNING 拦截；DROP TRIGGER 特判归 prog_trigger 而非 ddl_drop_object；
--             触发器不落库，DROP IF EXISTS 天然幂等）
CREATE TRIGGER trg_se_p15 BEFORE INSERT ON se_t_base FOR EACH ROW EXECUTE FUNCTION se_f_p15_trg();
ALTER TRIGGER trg_se_p15 ON se_t_base RENAME TO trg_se_p15b;
ALTER TABLE se_t_base ENABLE TRIGGER trg_se_p15b;
DROP TRIGGER IF EXISTS trg_se_p15b ON se_t_base;

-- RULE prog_event_trigger: 事件触发器创建/删除影响全局事件处理 | 建议/WARNING
-- 预期触发: CREATE/DROP EVENT TRIGGER → SQL审核[prog_event_trigger]: ...
--           （WARNING 拦截；事件触发器不落库，DROP IF EXISTS 天然幂等）
CREATE EVENT TRIGGER evt_se_p15 ON ddl_command_start EXECUTE FUNCTION se_f_p15_evt();
DROP EVENT TRIGGER IF EXISTS evt_se_p15;

-- RULE prog_rule: 重写规则创建/变更/删除需评估查询重写影响 | 建议/WARNING
-- 预期触发: CREATE/ALTER RENAME/DROP RULE → SQL审核[prog_rule]: ...
--           （WARNING 拦截；DROP RULE 特判归 prog_rule 而非 ddl_drop_object；
--             规则不落库，DROP IF EXISTS 天然幂等）
CREATE RULE rl_se_p15 AS ON INSERT TO se_t_base DO INSTEAD NOTHING;
ALTER RULE rl_se_p15 ON se_t_base RENAME TO rl_se_p15b;
DROP RULE IF EXISTS rl_se_p15b ON se_t_base;

-- ---- 15.7 对象创建/变更/删除回显族（ddl_create_object/ddl_alter_object/ddl_drop_object）----

-- RULE ddl_create_object: 对象创建回显 | 推荐/NOTICE
-- 预期触发: CREATE SCHEMA/SEQUENCE/DOMAIN → SQL审核[ddl_create_object]: ...
CREATE SCHEMA se_x_p15;
CREATE SEQUENCE se_x_p15_seq;
CREATE DOMAIN se_x_p15_dom AS varchar(10);

-- RULE ddl_alter_object: 对象结构变更回显 | 推荐/NOTICE
-- 预期触发: ALTER SEQUENCE/DOMAIN → SQL审核[ddl_alter_object]: ...
ALTER SEQUENCE se_x_p15_seq INCREMENT BY 2;
ALTER DOMAIN se_x_p15_dom SET DEFAULT 'x';

-- RULE ddl_drop_object: 对象删除回显 | 推荐/NOTICE
-- 预期触发: DROP SEQUENCE/DOMAIN/PROCEDURE/FUNCTION → SQL审核[ddl_drop_object]: ...
--           （schema 的 DROP 由 PART 0/14 清理段在规则受控状态下执行，避免
--             ddl_high_risk_drop WARNING 拦截残留）
-- 边界（判定分流）: DROP TYPE 走 obj_advanced ERROR（高级对象变更需资深 DBA 实施，
--           强制拦截）而非 ddl_drop_object，故本段不创建/删除类型对象；
--           CREATE TYPE/ALTER TYPE 用例在 pgauditrule.md 触发示例中说明；
--           ALTER TYPE 的版本分界（[PG13+] 起支持的部分变体）由 pgauditrule.md
--           触发示例与版本矩阵 version_matrix.md 覆盖，本脚本不落地用例。
DROP SEQUENCE IF EXISTS se_x_p15_seq;
DROP DOMAIN IF EXISTS se_x_p15_dom;
DROP FUNCTION IF EXISTS se_f_p15_trg() CASCADE;
DROP FUNCTION IF EXISTS se_f_p15_evt() CASCADE;
-- 边界（判定分流）：名字违规的 CREATE 走 name_* ERROR 而非 ddl_create_object，
--   已在 PART 1 name_charset 用例验证（CREATE TABLE "SeTUpper"），此处不重复。

-- =============================================================================
-- PART 9: 事务控制规则族（1 条规则）
-- -----------------------------------------------------------------------------
-- 本段置于脚本最末（PART 14 之后）：tcl_multi_ddl_in_txn 触发时 COMMIT 处
-- ereport(ERROR) 长跳转，状态复位语句（in_explicit_txn=false; txn_ddl_count=0;）
-- 不执行，会话内状态机永久卡死（实测触发后同连接内任何事务均被阻断）。
-- psql 退出后连接关闭、进程内静态状态清零，故不影响脚本幂等性。
-- 实测计数: CREATE TABLE 内部展开计数≥2（普通 CREATE 计 2、含 serial 计 4），
--           故反例不可用单条 CREATE TABLE；DROP TABLE / DROP SEQUENCE
--           (IF EXISTS) 各计 1，为可靠反例且幂等（IF EXISTS 不落库）。
--           触发用例用 DROP + CREATE SEQUENCE 精确计数 2，避免 CREATE TABLE
--           展开计数的不确定性（CREATE SEQUENCE 随事务回滚不落库）。
-- =============================================================================

-- RULE tcl_multi_ddl_in_txn: 禁止在显式事务中执行多条DDL | 强制/ERROR
-- 预期不触发: 显式事务内单条 DDL（DROP TABLE / DROP SEQUENCE IF EXISTS 各计数1）→ 无输出
BEGIN;
DROP TABLE IF EXISTS se_t_txnone;
COMMIT;
BEGIN;
DROP SEQUENCE IF EXISTS se_x_txseq;
COMMIT;
-- 预期触发: 显式事务内 2 条 DDL（DROP + CREATE SEQUENCE，计数2）后 COMMIT →
--           ERROR: SQL审核[tcl_multi_ddl_in_txn]: 显式事务中执行了 2 条 DDL，禁止在事务中执行多条 DDL
--           （default_level=ERROR 实测，COMMIT 触发 ERROR 使事务回滚，对象不落库；
--             触发后状态机卡死为既定行为，本段置于脚本最末）
BEGIN;
DROP TABLE IF EXISTS se_t_txndrop;
CREATE SEQUENCE se_x_txseq2;
COMMIT;

-- =============================================================================
-- RULE COVERAGE CHECKLIST（覆盖率核对注释段）
-- -----------------------------------------------------------------------------
-- 事实来源 : rule_registry.c g_rule_defs[]（102 条 rule id，唯一权威清单）
-- 核对命令 :
--   清单核对 : grep -oE '^-- RULE [a-z_0-9]+' scripts/pgsqlauditengine_test.sql \
--              | sed 's/^-- RULE //' | sort -u   # 与 g_rule_defs[] 102 条 setdiff 为空
--             （须限定行首 '-- RULE' 前缀，避免误匹配 PART 15 中 CREATE RULE 语句）
--   输出核对 : grep -oE 'SQL审核\[[a-z_0-9]+\]' out_test_pg<v>.log \
--              | sed 's/SQL审核\[//;s/\]//' | sort -u > /tmp/actual_rules.txt
-- 预期     : 实际输出集合 ∪ 5 条白名单豁免 = 102 条全集（setdiff 为空），
--            且无全集之外的规则输出。
-- 覆盖缺口 : 102 条全集中 2 条规则（db_charset_utf8、dml_delete_truncate）为「可触发
--            但本脚本用例未覆盖」，缺其输出时实际输出 ∪ 5 条豁免 = 100 条；缺口原因
--            见本段末「覆盖缺口说明」。
-- 说明     : 「用例行号」列为各规则三段式用例注释在本脚本中的行号（格式 L<行号>），
--            修改用例时需同步维护本清单；`[阶段二新增]` 标注的 22 条规则用例位于
--            PART 15（L825-1002，7 子分区）。
-- WARNING 拦截语义（任务组 25）：WARNING 规则命中输出级别前缀为 ERROR（errdetail 含
--            WARNING 标识），语句被拦截；NOTICE 放行、ERROR 拦截不变。
-- CIC 修复影响（任务组 23）：idx_table_count/idx_redundant/idx_selectivity 在
--            CREATE INDEX CONCURRENTLY 场景经 CIC 特判不可判定，经规则状态受控切换
--            （任务组 26.3）以非 CIC 路径触发验证。
-- =============================================================================

-- ---- 组 1 命名规范（12 条）----
-- RULE name_charset: 对象名称仅由小写字母/数字/下划线构成 | 强制/ERROR | 用例行号: L120
-- RULE name_start: 对象名称必须以字母开头 | 强制/ERROR | 用例行号: L125
-- RULE name_len: 对象名称长度不超过32个字符 | 强制/ERROR | 用例行号: L130
-- RULE name_reserved: 对象名称禁止使用PostgreSQL保留字 | 强制/ERROR | 用例行号: L135
-- RULE name_pk_prefix: 主键约束名必须以pk_为前缀 | 强制/ERROR | 用例行号: L140
-- RULE name_uk_prefix: 唯一约束名必须以uk_为前缀 | 强制/ERROR | 用例行号: L145
-- RULE name_ck_prefix: 检查约束名必须以ck_为前缀 | 强制/ERROR | 用例行号: L150
-- RULE name_idx_prefix: 普通索引名必须以idx_为前缀 | 强制/ERROR | 用例行号: L155
-- RULE name_vw_prefix: 视图名必须以vw_为前缀 | 强制/ERROR | 用例行号: L160
-- RULE name_tmp_prefix: 临时对象名必须以tmp为前缀并以日期为后缀 | 强制/ERROR | 用例行号: L165
-- RULE name_bak_prefix: 备份对象名必须以bak为前缀并以日期为后缀 | 建议/WARNING | 用例行号: L170 [豁免]
-- RULE name_db_name: 库名以应用系统缩写为前缀并以环境类型为后缀 | 建议/WARNING | 用例行号: L176

-- ---- 组 2 字段类型（5 条）----
-- RULE type_varchar: 字符串类型使用varchar/text | 建议/WARNING | 用例行号: L185
-- RULE type_numeric: 货币与精确计算字段使用numeric | 建议/WARNING | 用例行号: L190
-- RULE type_date_time: 日期使用date、时间使用time/timestamp | 建议/WARNING | 用例行号: L195 [豁免]
-- RULE type_jsonb: JSON数据使用jsonb类型 | 建议/WARNING | 用例行号: L202
-- RULE type_min_size: 优先选择符合存储需要的最小数据类型 | 强制/ERROR | 用例行号: L207

-- ---- 组 3 表结构（8 条）----
-- RULE table_pk_required: 每张表必须定义主键 | 强制/ERROR | 用例行号: L216
-- RULE table_pk_name_id: 主键列名必须为id | 强制/ERROR | 用例行号: L221
-- RULE table_pk_type_serial: 主键id使用序列(serial/int)类型 | 强制/ERROR | 用例行号: L226
-- RULE table_column_not_null: 表中所有字段必须为NOT NULL | 强制/ERROR | 用例行号: L231
-- RULE table_comment_required: 每张表必须有表级和字段级注释 | 强制/ERROR | 用例行号: L253 [豁免]
-- RULE table_no_foreign_key: 禁止在表中定义外键 | 强制/ERROR | 用例行号: L236
-- RULE table_no_reserved_column: 禁止建立预留字段 | 建议/WARNING | 用例行号: L241
-- RULE db_charset_utf8: 数据库字符集使用UTF8、排序规则使用C | 强制/ERROR | 用例行号: L246

-- ---- 组 4 索引（6 条）----
-- RULE idx_concurrently: 创建索引必须使用CONCURRENTLY | 强制/ERROR | 用例行号: L266
-- RULE idx_field_count: 单个索引字段数不超过5个 | 建议/WARNING | 用例行号: L271
-- RULE idx_table_count: 单表索引数量不超过5个 | 建议/WARNING | 用例行号: L277
-- RULE idx_method: 根据场景合理选择索引方法(btree/hash/gin/gist/brin) | 强制/ERROR | 用例行号: L288
-- RULE idx_selectivity: 索引必须创建在选择性较高的列上 | 强制/ERROR | 用例行号: L294
-- RULE idx_redundant: 避免冗余或重复索引 | 强制/ERROR | 用例行号: L304

-- ---- 组 5 视图（3 条）----
-- RULE view_select_star: 视图禁止使用select * | 强制/ERROR | 用例行号: L315
-- RULE view_order_by: 视图中禁止使用order by | 强制/ERROR | 用例行号: L321
-- RULE view_nested: 视图禁止嵌套其他视图 | 建议/WARNING | 用例行号: L326

-- ---- 组 6 DML 与 SQL 编写（8 条）----
-- RULE dml_select_star: select只获取必要字段, 禁止select * | 强制/ERROR | 用例行号: L377 [豁免]
-- RULE dml_no_where: UPDATE/DELETE缺少WHERE条件将影响全部行 | 强制/ERROR | 用例行号: L369
-- RULE dml_delete_truncate: 清空表建议使用TRUNCATE而非无WHERE的DELETE | 强制/ERROR | 用例行号: L384 [豁免]
-- RULE dml_count_star: 统计行数使用count(*) | 强制/ERROR | 用例行号: L339
-- RULE dml_left_fuzzy: 避免左模糊查询(LIKE '%x') | 强制/ERROR | 用例行号: L345
-- RULE dml_batch_copy: 插入大量数据时建议使用COPY | 推荐/NOTICE | 用例行号: L352
-- RULE dml_in_to_exists: 使用EXISTS子句代替IN操作符 | 推荐/NOTICE | 用例行号: L357
-- RULE dml_array_vs_tmp: 使用数组代替临时表 | 推荐/NOTICE | 用例行号: L363

-- ---- 组 7 可编程对象（7 条）----
-- RULE prog_name: 函数/过程/触发器命名应符合命名规范 | 强制/ERROR | 用例行号: L422 [豁免]
-- RULE prog_ddl_forbidden: 函数体内禁止执行DDL | 建议/WARNING | 用例行号: L397
-- RULE prog_business_logic: 不建议在数据库中存放业务逻辑 | 建议/WARNING | 用例行号: L406
-- RULE prog_close_cursor: 游标使用后必须及时关闭 | 建议/WARNING | 用例行号: L411
-- RULE prog_trigger: 触发器创建/变更/删除需评估执行时机与开销 | 建议/WARNING | 用例行号: L950 [阶段二新增]
-- RULE prog_event_trigger: 事件触发器创建/删除影响全局事件处理 | 建议/WARNING | 用例行号: L959 [阶段二新增]
-- RULE prog_rule: 重写规则创建/变更/删除需评估查询重写影响 | 建议/WARNING | 用例行号: L965 [阶段二新增]

-- ---- 组 8 对象变更与删除（5 条）----
-- RULE ddl_high_risk_drop: 高危删除操作(DATABASE/TABLESPACE/SCHEMA) | 建议/WARNING | 用例行号: L432
-- RULE ddl_truncate_warn: 生产环境谨慎使用TRUNCATE | 建议/WARNING | 用例行号: L440
-- RULE ddl_create_object: 对象创建回显 | 推荐/NOTICE | 用例行号: L975 [阶段二新增]
-- RULE ddl_alter_object: 对象结构变更回显 | 推荐/NOTICE | 用例行号: L981 [阶段二新增]
-- RULE ddl_drop_object: 对象删除回显 | 推荐/NOTICE | 用例行号: L986 [阶段二新增]

-- ---- 组 9 事务控制（6 条）----
-- RULE tcl_multi_ddl_in_txn: 禁止在显式事务中执行多条DDL | 强制/ERROR | 用例行号: L751
-- 注: PART 9 段已移至脚本最末（PART 14 后），因触发后状态机卡死，见段内说明。
-- RULE tcl_begin: 显式事务开启识别回显 | 推荐/NOTICE | 用例行号: L843 [阶段二新增]
-- RULE tcl_commit: 显式事务提交识别回显 | 推荐/NOTICE | 用例行号: L843 [阶段二新增]
-- RULE tcl_rollback: 显式事务回滚识别回显 | 推荐/NOTICE | 用例行号: L848 [阶段二新增]
-- RULE tcl_savepoint: 保存点操作识别回显 | 推荐/NOTICE | 用例行号: L853 [阶段二新增]
-- RULE tcl_prepared: 两阶段事务需评估分布式一致性 | 建议/WARNING | 用例行号: L862 [阶段二新增]

-- ---- 组 10 命令类（18 条）----
-- RULE cmd_vacuum: VACUUM 操作需谨慎(FULL 获取排他锁并重写表) | 建议/WARNING | 用例行号: L451
-- RULE cmd_analyze: ANALYZE 统计信息采集应在业务低峰期进行 | 推荐/NOTICE | 用例行号: L457
-- RULE cmd_checkpoint: 手动 CHECKPOINT 仅建议排障场景使用 | 推荐/NOTICE | 用例行号: L462
-- RULE cmd_cluster: CLUSTER 需排他锁并重写表, 生产需谨慎 | 建议/WARNING | 用例行号: L467
-- RULE cmd_reindex: REINDEX 建议使用 CONCURRENTLY | 建议/WARNING | 用例行号: L472
-- RULE cmd_refresh_matview: REFRESH MATVIEW 建议使用 CONCURRENTLY | 建议/WARNING | 用例行号: L479
-- RULE cmd_lock: LOCK TABLE 需注意锁模式与持锁时长 | 建议/WARNING | 用例行号: L486
-- RULE cmd_load: LOAD 加载共享库属安全敏感操作 | 建议/WARNING | 用例行号: L493
-- RULE cmd_discard: DISCARD 将重置会话全部状态 | 推荐/NOTICE | 用例行号: L499
-- RULE cmd_explain: EXPLAIN ANALYZE 将真实执行被分析语句 | 推荐/NOTICE | 用例行号: L504
-- RULE cmd_copy: COPY 批量导入导出需确认数据源/格式/敏感范围 | 建议/WARNING | 用例行号: L511
-- RULE cmd_prepare: 预编译语句(PREPARE/EXECUTE/DEALLOCATE)识别回显 | 推荐/NOTICE | 用例行号: L517
-- RULE cmd_cursor: 游标使用后必须 CLOSE, 避免长事务持有 | 建议/WARNING | 用例行号: L522
-- RULE cmd_notify: 通知通道需评估性能开销与使用范围 | 建议/WARNING | 用例行号: L531
-- RULE cmd_set: 会话参数设置识别回显 | 推荐/NOTICE | 用例行号: L872 [阶段二新增]
-- RULE cmd_show: 会话参数查询识别回显 | 推荐/NOTICE | 用例行号: L882 [阶段二新增]
-- RULE cmd_do: 匿名块执行识别回显 | 推荐/NOTICE | 用例行号: L888 [阶段二新增]
-- RULE cmd_call: 存储过程调用识别回显 | 推荐/NOTICE | 用例行号: L892 [阶段二新增]

-- ---- 组 11 对象类（15 条）----
-- RULE obj_policy: 行级安全策略变更需评估既有访问路径 | 建议/WARNING | 用例行号: L542
-- RULE obj_publication: 发布变更影响逻辑复制链路 | 建议/WARNING | 用例行号: L547
-- RULE obj_subscription: 订阅变更影响逻辑复制链路 | 建议/WARNING | 用例行号: L552
-- RULE obj_fdw: 外部数据对象涉及跨系统访问与凭据配置 | 建议/WARNING | 用例行号: L558
-- RULE obj_extension: 扩展需评估来源/版本与影响面 | 建议/WARNING | 用例行号: L563
-- RULE obj_language: 过程语言(尤其非受信语言)需评估执行风险 | 建议/WARNING | 用例行号: L570
-- RULE obj_advanced: 高级对象定义/变更需由资深 DBA 实施 | 建议/WARNING | 用例行号: L579
-- RULE obj_alter_system: ALTER SYSTEM 修改服务器级配置影响全局 | 建议/WARNING | 用例行号: L585
-- RULE obj_reassign_owned: 对象所有权批量转移需评估影响 | 建议/WARNING | 用例行号: L592
-- RULE obj_ctas: 评估是否确需创建实体表(CTAS/SELECT INTO) | 建议/WARNING | 用例行号: L597
-- RULE obj_matview: 物化视图需评估数据新鲜度与刷新策略 | 建议/WARNING | 用例行号: L602
-- RULE obj_security_label: SECURITY LABEL 识别回显 | 推荐/NOTICE | 用例行号: L608
-- RULE obj_comment: 对象注释识别回显 | 推荐/NOTICE | 用例行号: L925 [阶段二新增]
-- RULE obj_rename_owner: 对象重命名/属主变更识别回显 | 推荐/NOTICE | 用例行号: L931 [阶段二新增]
-- RULE obj_drop_owned: DROP OWNED 批量回收需评估影响 | 推荐/NOTICE | 用例行号: L939 [阶段二新增]

-- ---- 组 12 DCL/TCL/DML 补充（7 条）----
-- RULE dcl_set_role: 会话身份切换需确认权限边界 | 建议/WARNING | 用例行号: L618
-- RULE tcl_set_transaction: SET TRANSACTION/CONSTRAINTS 识别回显 | 推荐/NOTICE | 用例行号: L626
-- RULE dml_merge_check: MERGE 需仔细评估影响行范围 | 建议/WARNING | 用例行号: L637 [PG15+]
-- RULE dcl_grant: 对象权限授予/回收需确认权限边界 | 建议/WARNING | 用例行号: L903 [阶段二新增]
-- RULE dcl_grant_role: 角色成员授予/回收需确认角色边界 | 建议/WARNING | 用例行号: L908 [阶段二新增]
-- RULE dcl_default_privileges: 默认权限变更需确认权限边界 | 建议/WARNING | 用例行号: L913 [阶段二新增]
-- RULE dcl_role: 角色创建/修改/删除需评估权限与归属 | 建议/WARNING | 用例行号: L917 [阶段二新增]

-- ---- 组 13 对象已存在冲突（1 条）----
-- RULE obj_exists_check: CREATE 对象已存在冲突(无INE=ERROR/有INE=WARNING) | 强制/ERROR | 用例行号: L649

-- ---- 组 14 命令兜底回显（1 条）----
-- RULE echo: 未映射规则的命令识别回显 | 推荐/NOTICE | 用例行号: L658

-- ---- 白名单豁免清单（5 条「注册但无判定」规则，覆盖率核对的豁免项）----
-- 1. table_comment_required: rule_ddl.c 全文件无该 id 的 audit_emit 调用，无判定逻辑
-- 2. type_date_time: rule_common.c audit_check_type 仅实现 jsonb/numeric/varchar 分支，无 date/time 分支
-- 3. name_bak_prefix: audit_check_prefix 仅被 pk_/uk_/ck_/idx_/vw_ 前缀调用，无 bak 前缀调用
-- 4. dml_select_star: hook_analyze.c 明确「DML 的 select * 无法可靠检测」；视图场景输出 view_select_star
-- 5. prog_name: 函数命名走 audit_check_name（输出 name_* 规则），无 prog_name 独立输出
-- 上述 5 条构造语义上应触发的输入，预期不产生对应 rule id 的 SQL审核 输出；
-- 注册表存在性与配置读写由 pgsqlauditengine_api.md 覆盖。

-- ---- 覆盖缺口说明（2 条「可触发但本脚本用例未覆盖」规则）----
-- 1. db_charset_utf8: PART 1 CREATE DATABASE 用例（L176 附近 name_db_name 用例）的库名
--    违反 name_db_name（建议/WARNING），被 WARNING 拦截语义先行拦截（语句不执行），
--    未触达 ENCODING 检查；构造合规库名 + 非 UTF8 编码（如 ENCODING 'LATIN1'）的用例
--    可触发（ERROR 拦截，库不落库，幂等），留待后续补充。
-- 2. dml_delete_truncate: 无 WHERE DELETE 在 hook_analyze.c:297/300 先输出 dml_no_where
--    （强制/ERROR 拦截），语句被拦截后该规则判定不可达（无独立输出点）；其触发需在
--    dml_no_where 关闭场景下构造，非本脚本默认执行路径。
-- =============================================================================
