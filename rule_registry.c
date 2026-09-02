/* -------------------------------------------------------------------------
 *
 * rule_registry.c
 *
 * 规则引擎：内置规则注册表、运行期规则配置（共享内存）与统一审核输出。
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "storage/ipc.h"
#include "storage/shmem.h"
#include "utils/elog.h"
#include "utils/guc.h"

#include "pgsqlauditengine.h"
#include "audit_record.h"
#include "rule_registry.h"

/*
 * 内置规则定义（编译期静态登记）。
 *  - 强制（【强制】）默认 ERROR
 *  - 建议（【建议】）默认 WARNING
 *  - 推荐（【推荐】）默认 NOTICE
 */
static const AuditRuleDef g_rule_defs[] = {
	/* --- 数据库对象命名规范 (5.2) --- */
	{"name_charset",			"对象名称仅由小写字母/数字/下划线构成",		"强制", AUDIT_ERROR},
	{"name_start",				"对象名称必须以字母开头",						"强制", AUDIT_ERROR},
	{"name_len",				"对象名称长度不超过32个字符",					"强制", AUDIT_ERROR},
	{"name_reserved",			"对象名称禁止使用PostgreSQL保留字",				"强制", AUDIT_ERROR},
	{"name_pk_prefix",			"主键约束名必须以pk_为前缀",					"强制", AUDIT_ERROR},
	{"name_uk_prefix",			"唯一约束名必须以uk_为前缀",					"强制", AUDIT_ERROR},
	{"name_ck_prefix",			"检查约束名必须以ck_为前缀",					"强制", AUDIT_ERROR},
	{"name_idx_prefix",			"普通索引名必须以idx_为前缀",					"强制", AUDIT_ERROR},
	{"name_vw_prefix",			"视图名必须以vw_为前缀",						"强制", AUDIT_ERROR},
	{"name_mv_prefix",			"物化视图名必须以mv_为前缀",					"强制", AUDIT_ERROR},
	{"name_seq_prefix",			"序列名必须以seq_为前缀",						"强制", AUDIT_ERROR},
	{"name_trg_prefix",			"触发器名必须以trg_为前缀",					"强制", AUDIT_ERROR},
	{"name_func_prefix",		"函数/过程名必须以fn_为前缀",					"强制", AUDIT_ERROR},
	{"name_rule_prefix",		"规则名必须以rule_为前缀",						"强制", AUDIT_ERROR},
	{"name_type_prefix",		"类型/域名必须以type_为前缀",					"强制", AUDIT_ERROR},
	{"name_tmp_prefix",			"临时对象名必须以tmp为前缀并以日期为后缀",		"强制", AUDIT_ERROR},
	{"name_bak_prefix",			"备份对象名必须以bak为前缀并以日期为后缀",		"建议", AUDIT_WARNING},
	{"name_db_name",			"库名以应用系统缩写为前缀并以环境类型为后缀",	"建议", AUDIT_WARNING},

	/* --- 字段数据类型规范 (5.3 类型部分) --- */
	{"type_varchar",			"字符串类型使用varchar/text",					"建议", AUDIT_WARNING},
	{"type_numeric",			"货币与精确计算字段使用numeric",				"建议", AUDIT_WARNING},
	{"type_date_time",			"日期使用date、时间使用time/timestamp",			"建议", AUDIT_WARNING},
	{"type_jsonb",				"JSON数据使用jsonb类型",						"建议", AUDIT_WARNING},
	{"type_min_size",			"优先选择符合存储需要的最小数据类型",				"强制", AUDIT_ERROR},

	/* --- 表结构与字段设计规范 (5.3) --- */
	{"table_pk_required",		"每张表必须定义主键",							"强制", AUDIT_ERROR},
	{"table_pk_name_id",		"主键列名必须为id",								"强制", AUDIT_ERROR},
	{"table_pk_type_serial",	"主键id使用序列(serial/int)类型",				"强制", AUDIT_ERROR},
	{"table_column_not_null",	"表中所有字段必须为NOT NULL",					"强制", AUDIT_ERROR},
	{"table_comment_required",	"每张表必须有表级和字段级注释",					"强制", AUDIT_ERROR},
	{"table_no_foreign_key",	"禁止在表中定义外键",							"强制", AUDIT_ERROR},
	{"table_no_reserved_column", "禁止建立预留字段",							"建议", AUDIT_WARNING},
	{"db_charset_utf8",			"数据库字符集使用UTF8、排序规则使用C",			"强制", AUDIT_ERROR},

	/* --- 索引规范 (5.4) --- */
	{"idx_concurrently",		"创建索引必须使用CONCURRENTLY",					"强制", AUDIT_ERROR},
	{"idx_field_count",			"单个索引字段数不超过5个",						"建议", AUDIT_WARNING},
	{"idx_table_count",			"单表索引数量不超过5个",						"建议", AUDIT_WARNING},
	{"idx_method",				"根据场景合理选择索引方法(btree/hash/gin/gist/brin)","强制", AUDIT_ERROR},
	{"idx_selectivity",			"索引必须创建在选择性较高的列上",					"强制", AUDIT_ERROR},
	{"idx_redundant",			"避免冗余或重复索引",							"强制", AUDIT_ERROR},

	/* --- 视图设计规范 (5.4) --- */
	{"view_select_star",		"视图禁止使用select *",						"强制", AUDIT_ERROR},
	{"view_order_by",			"视图中禁止使用order by",						"强制", AUDIT_ERROR},
	{"view_nested",				"视图禁止嵌套其他视图",							"建议", AUDIT_WARNING},

	/* --- SQL 编写规范 (5.7) --- */
	{"dml_select_star",			"select只获取必要字段, 禁止select *",			"强制", AUDIT_ERROR},
	{"dml_no_where",			"UPDATE/DELETE缺少WHERE条件将影响全部行",			"强制", AUDIT_ERROR},
	{"dml_delete_truncate",		"清空表建议使用TRUNCATE而非无WHERE的DELETE",		"强制", AUDIT_ERROR},
	{"dml_count_star",			"统计行数使用count(*)",							"强制", AUDIT_ERROR},
	{"dml_left_fuzzy",			"避免左模糊查询(LIKE '%x')",					"强制", AUDIT_ERROR},
	{"dml_batch_copy",			"插入大量数据时建议使用COPY",					"推荐", AUDIT_NOTICE},
	{"dml_in_to_exists",		"使用EXISTS子句代替IN操作符",					"推荐", AUDIT_NOTICE},
	{"dml_array_vs_tmp",		"使用数组代替临时表",							"推荐", AUDIT_NOTICE},

	/* --- 可编程对象审核 (5.5) --- */
	{"prog_name",				"函数/过程/触发器命名应符合命名规范",			"强制", AUDIT_ERROR},
	{"prog_ddl_forbidden",		"函数体内禁止执行DDL",							"建议", AUDIT_WARNING},
	{"prog_business_logic",		"不建议在数据库中存放业务逻辑",					"建议", AUDIT_WARNING},
	{"prog_close_cursor",		"游标使用后必须及时关闭",						"建议", AUDIT_WARNING},

	/* --- 对象变更与删除 (5.6) --- */
	{"ddl_high_risk_drop",		"高危删除操作(DATABASE/TABLESPACE/SCHEMA)",		"建议", AUDIT_WARNING},
	{"ddl_truncate_warn",		"生产环境谨慎使用TRUNCATE",						"建议", AUDIT_WARNING},

	/* --- 事务控制规范 (5.9) --- */
	{"tcl_multi_ddl_in_txn",	"禁止在显式事务中执行多条DDL",					"强制", AUDIT_ERROR},

	/* --- SQL 命令全覆盖审核 (5.11) --- */
	/* cmd_*：维护/性能/数据操作类命令 */
	{"cmd_vacuum",			"VACUUM 操作需谨慎(FULL 获取排他锁并重写表)",		"建议", AUDIT_WARNING},
	{"cmd_analyze",			"ANALYZE 统计信息采集应在业务低峰期进行",			"推荐", AUDIT_NOTICE},
	{"cmd_checkpoint",		"手动 CHECKPOINT 仅建议排障场景使用",				"推荐", AUDIT_NOTICE},
	{"cmd_cluster",			"CLUSTER 需排他锁并重写表, 生产需谨慎",				"建议", AUDIT_WARNING},
	{"cmd_reindex",			"REINDEX 建议使用 CONCURRENTLY",					"建议", AUDIT_WARNING},
	{"cmd_refresh_matview",	"REFRESH MATVIEW 建议使用 CONCURRENTLY",			"建议", AUDIT_WARNING},
	{"cmd_lock",			"LOCK TABLE 需注意锁模式与持锁时长",				"建议", AUDIT_WARNING},
	{"cmd_load",			"LOAD 加载共享库属安全敏感操作",					"建议", AUDIT_WARNING},
	{"cmd_discard",			"DISCARD 将重置会话全部状态",						"推荐", AUDIT_NOTICE},
	{"cmd_explain",			"EXPLAIN ANALYZE 将真实执行被分析语句",				"推荐", AUDIT_NOTICE},
	{"cmd_copy",			"COPY 批量导入导出需确认数据源/格式/敏感范围",		"建议", AUDIT_WARNING},
	{"cmd_prepare",			"预编译语句(PREPARE/EXECUTE/DEALLOCATE)识别回显",	"推荐", AUDIT_NOTICE},
	{"cmd_cursor",			"游标使用后必须 CLOSE, 避免长事务持有",				"建议", AUDIT_WARNING},
	{"cmd_notify",			"通知通道需评估性能开销与使用范围",					"建议", AUDIT_WARNING},

	/* obj_*：对象类命令 */
	{"obj_policy",			"行级安全策略变更需评估既有访问路径",				"建议", AUDIT_WARNING},
	{"obj_publication",		"发布变更影响逻辑复制链路",							"建议", AUDIT_WARNING},
	{"obj_subscription",	"订阅变更影响逻辑复制链路",							"建议", AUDIT_WARNING},
	{"obj_fdw",				"外部数据对象涉及跨系统访问与凭据配置",				"建议", AUDIT_WARNING},
	{"obj_extension",		"扩展需评估来源/版本与影响面",						"建议", AUDIT_WARNING},
	{"obj_language",		"过程语言(尤其非受信语言)需评估执行风险",			"建议", AUDIT_WARNING},
	{"obj_advanced",		"高级对象定义/变更需由资深 DBA 实施",				"建议", AUDIT_WARNING},
	{"obj_alter_system",	"ALTER SYSTEM 修改服务器级配置影响全局",				"建议", AUDIT_WARNING},
	{"obj_reassign_owned",	"对象所有权批量转移需评估影响",						"建议", AUDIT_WARNING},
	{"obj_ctas",			"评估是否确需创建实体表(CTAS/SELECT INTO)",		"建议", AUDIT_WARNING},
	{"obj_matview",			"物化视图需评估数据新鲜度与刷新策略",				"建议", AUDIT_WARNING},
	{"obj_security_label",	"SECURITY LABEL 识别回显",							"推荐", AUDIT_NOTICE},

	/* dcl_* / tcl_* / dml_* */
	{"dcl_set_role",		"会话身份切换需确认权限边界",						"建议", AUDIT_WARNING},
	{"tcl_set_transaction",	"SET TRANSACTION/CONSTRAINTS 识别回显",				"推荐", AUDIT_NOTICE},
	{"dml_merge_check",		"MERGE 需仔细评估影响行范围",						"建议", AUDIT_WARNING},

	/* --- 对象已存在冲突 (5.12) --- */
	{"obj_exists_check",	"CREATE 对象已存在冲突(无INE=ERROR/有INE=WARNING)","强制", AUDIT_ERROR},

	/* --- 命令兜底回显 (5.11) --- */
	{"echo",				"未映射规则的命令识别回显",							"推荐", AUDIT_NOTICE},

	/* --- 阶段二新增：命令全集覆盖补齐 (spec 5.4 需求 23；决策 D9/D10/D11) --- */
	{"tcl_begin",			"显式事务开启(BEGIN/START TRANSACTION)识别回显",			"推荐", AUDIT_NOTICE},
	{"tcl_commit",			"显式事务提交(COMMIT/END)识别回显",					"推荐", AUDIT_NOTICE},
	{"tcl_rollback",		"显式事务回滚(ROLLBACK/ABORT)识别回显",					"推荐", AUDIT_NOTICE},
	{"tcl_savepoint",		"保存点操作(SAVEPOINT/RELEASE/ROLLBACK TO)识别回显",		"推荐", AUDIT_NOTICE},
	{"tcl_prepared",		"两阶段事务(PREPARE/COMMIT/ROLLBACK PREPARED)需评估分布式一致性","建议", AUDIT_WARNING},
	{"cmd_set",				"会话参数设置(SET/RESET)识别回显",						"推荐", AUDIT_NOTICE},
	{"cmd_show",			"会话参数查询(SHOW)识别回显",							"推荐", AUDIT_NOTICE},
	{"cmd_call",			"存储过程调用(CALL)识别回显",							"推荐", AUDIT_NOTICE},
	{"cmd_do",				"匿名块执行(DO)识别回显",								"推荐", AUDIT_NOTICE},
	{"dcl_grant",			"对象权限授予/回收(GRANT/REVOKE)需确认权限边界",			"建议", AUDIT_WARNING},
	{"dcl_grant_role",		"角色成员授予/回收(GRANT/REVOKE)需确认角色边界",			"建议", AUDIT_WARNING},
	{"dcl_default_privileges","默认权限变更(ALTER DEFAULT PRIVILEGES)影响后续创建对象",	"建议", AUDIT_WARNING},
	{"dcl_role",			"角色创建/修改/删除需评估权限与归属",					"建议", AUDIT_WARNING},
	{"obj_comment",			"对象注释(COMMENT)识别回显",							"推荐", AUDIT_NOTICE},
	{"obj_rename_owner",	"对象重命名/属主变更/模式迁移识别回显",					"推荐", AUDIT_NOTICE},
	{"prog_trigger",		"触发器创建/变更/删除需评估执行时机与开销",				"建议", AUDIT_WARNING},
	{"prog_event_trigger",	"事件触发器创建/变更/删除影响全局事件处理",				"建议", AUDIT_WARNING},
	{"prog_rule",			"重写规则(RULE)创建/变更/删除需评估查询重写影响",			"建议", AUDIT_WARNING},
	{"obj_drop_owned",		"DROP OWNED 批量回收对象需评估影响范围",					"推荐", AUDIT_NOTICE},
	{"ddl_create_object",	"对象创建回显(SCHEMA/SEQUENCE/DOMAIN/TYPE/TABLESPACE)",		"推荐", AUDIT_NOTICE},
	{"ddl_alter_object",	"对象结构变更回显(ALTER TABLE/INDEX/VIEW/SEQUENCE/TYPE/DATABASE/FUNCTION)","推荐", AUDIT_NOTICE},
	{"ddl_drop_object",		"对象删除回显(DROP TABLE/VIEW/INDEX/SEQUENCE/DOMAIN/FUNCTION)","推荐", AUDIT_NOTICE},
};

#define NUM_RULES	((int) (sizeof(g_rule_defs) / sizeof(g_rule_defs[0])))

AuditRegistry *pgsql_audit_engine_registry = NULL;

/*
 * 共享内存中的配置数组句柄。
 */
static AuditRuleConfig *g_rule_config_shmem = NULL;

Size
audit_registry_shmem_size(void)
{
	return NUM_RULES * sizeof(AuditRuleConfig);
}

void
audit_registry_shmem_startup(void)
{
	bool		found;

	g_rule_config_shmem = (AuditRuleConfig *)
		ShmemInitStruct("PGSQLAuditEngine Rule Configs",
						NUM_RULES * sizeof(AuditRuleConfig),
						&found);

	if (!found)
	{
		int			i;

		for (i = 0; i < NUM_RULES; i++)
		{
			g_rule_config_shmem[i].enabled = true;
			g_rule_config_shmem[i].level = g_rule_defs[i].default_level;
		}
	}
}

void
audit_registry_init(AuditRegistry *reg)
{
	reg->num_rules = NUM_RULES;
	reg->rules = g_rule_defs;
	reg->configs = g_rule_config_shmem;
	pgsql_audit_engine_registry = reg;
}

int
audit_num_rules(void)
{
	return NUM_RULES;
}

const AuditRuleDef *
audit_rule_at(int i)
{
	if (i < 0 || i >= NUM_RULES)
		return NULL;
	return &g_rule_defs[i];
}

AuditRuleConfig
audit_rule_config_at(int i)
{
	if (i < 0 || i >= NUM_RULES)
	{
		AuditRuleConfig def;

		def.enabled = true;
		def.level = AUDIT_ERROR;
		return def;
	}
	if (g_rule_config_shmem == NULL)
	{
		AuditRuleConfig def;

		def.enabled = true;
		def.level = g_rule_defs[i].default_level;
		return def;
	}
	return g_rule_config_shmem[i];
}

void
audit_rule_set_config(int i, bool enabled, AuditLevel level)
{
	if (i < 0 || i >= NUM_RULES || g_rule_config_shmem == NULL)
		return;
	g_rule_config_shmem[i].enabled = enabled;
	g_rule_config_shmem[i].level = level;
}

int
audit_find_rule(const char *rule_id)
{
	int			i;

	if (rule_id == NULL)
		return -1;

	for (i = 0; i < NUM_RULES; i++)
	{
		if (strcmp(g_rule_defs[i].rule_id, rule_id) == 0)
			return i;
	}
	return -1;
}

AuditLevel
audit_effective_level(const char *rule_id, AuditLevel def_level)
{
	int			idx = audit_find_rule(rule_id);

	if (idx < 0)
		return def_level;

	if (g_rule_config_shmem != NULL)
		return g_rule_config_shmem[idx].level;
	return g_rule_defs[idx].default_level;
}

AuditConclusion
audit_level_to_conclusion(AuditLevel level)
{
	switch (level)
	{
		case AUDIT_ERROR:
			return AUDIT_CONC_BLOCK;
		case AUDIT_WARNING:
			return AUDIT_CONC_BLOCK;
		case AUDIT_NOTICE:
		default:
			return AUDIT_CONC_PASS;
	}
}

/*
 * 统一审核输出。消息统一添加 "SQL审核:" 前缀，并携带 rule_id 便于定位。
 */
void
audit_emit(AuditLevel def_level, const char *rule_id,
		   const char *fmt, ...)
{
	int			idx;
	AuditLevel	level = def_level;
	char		msg[1024];
	va_list		args;

	/*
	 * 查找规则配置：若规则被关闭，则静默跳过；否则用配置级别覆盖默认级别。
	 */
	idx = audit_find_rule(rule_id);
	if (idx >= 0 && g_rule_config_shmem != NULL)
	{
		if (!g_rule_config_shmem[idx].enabled)
			return;
		level = g_rule_config_shmem[idx].level;
	}

	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);

	audit_record_write("VIOLATION", "", rule_id, level);

	switch (level)
	{
		case AUDIT_ERROR:
			ereport(ERROR,
					(errcode(ERRCODE_CHECK_VIOLATION),
					 errmsg("SQL审核[%s]: %s", rule_id, msg)));
			break;

		case AUDIT_WARNING:
			ereport(ERROR,
					(errcode(ERRCODE_CHECK_VIOLATION),
					 errmsg("SQL审核[%s]: %s", rule_id, msg),
					 errdetail("SQL审核级别: WARNING（语句被拦截）")));
			break;

		case AUDIT_NOTICE:
		default:
			ereport(NOTICE,
					(errmsg("SQL审核[%s]: %s", rule_id, msg)));
			break;
	}
}

/*
 * 固定级别审核输出。
 * 传入的 level 即最终输出级别，不被共享内存配置覆盖（区别于 audit_emit）；
 * 规则被关闭时静默返回。
 */
void
audit_emit_fixed(AuditLevel level, const char *rule_id,
				 const char *fmt, ...)
{
	int			idx;
	char		msg[1024];
	va_list		args;

	idx = audit_find_rule(rule_id);
	if (idx >= 0 && g_rule_config_shmem != NULL)
	{
		if (!g_rule_config_shmem[idx].enabled)
			return;
	}

	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);

	audit_record_write("VIOLATION", "", rule_id, level);

	switch (level)
	{
		case AUDIT_ERROR:
			ereport(ERROR,
					(errcode(ERRCODE_CHECK_VIOLATION),
					 errmsg("SQL审核[%s]: %s", rule_id, msg)));
			break;

		case AUDIT_WARNING:
			ereport(ERROR,
					(errcode(ERRCODE_CHECK_VIOLATION),
					 errmsg("SQL审核[%s]: %s", rule_id, msg),
					 errdetail("SQL审核级别: WARNING（语句被拦截）")));
			break;

		case AUDIT_NOTICE:
		default:
			ereport(NOTICE,
					(errmsg("SQL审核[%s]: %s", rule_id, msg)));
			break;
	}
}

/* -------------------------------------------------------------------------
 * 命令→规则映射表（spec 5.11.2 命令与规则映射清单）
 * 支撑命令覆盖可追踪核对与兜底机制；编译期静态数据。
 * -------------------------------------------------------------------------
 */
typedef struct CommandMapEntry
{
	const char *command_family;
	const char *rule_id;		/* NULL 表示仅识别回显(echo) */
	const char *status;			/* "已有" / "新增" / "已有+新增" */
} CommandMapEntry;

static const CommandMapEntry g_cmd_map[] = {
	{"事务控制", "tcl_begin/tcl_commit/tcl_rollback/tcl_savepoint/tcl_prepared", "新增"},
	{"两阶段提交", "tcl_prepared", "新增"},
	{"保存点", "tcl_savepoint", "新增"},
	{"事务特征", "tcl_set_transaction", "新增"},
	{"权限授予/收回", "dcl_grant", "已有+新增"},
	{"默认权限", "dcl_default_privileges", "新增"},
	{"会话身份", "dcl_set_role", "新增"},
	{"角色/用户/组", "dcl_role", "新增"},
	{"建表", "obj_exists_check+table_*", "已有"},
	{"查询建表", "obj_ctas", "新增"},
	{"外部表", "table_*+obj_fdw", "已有+新增"},
	{"索引", "idx_*+cmd_reindex", "已有+新增"},
	{"视图", "view_*", "已有"},
	{"物化视图", "obj_matview+cmd_refresh_matview", "新增"},
	{"序列", "ddl_create_object", "已有+新增"},
	{"模式", "ddl_create_object", "已有+新增"},
	{"数据库", "name_db_name+db_charset_utf8", "已有"},
	{"表空间", "ddl_create_object", "已有+新增"},
	{"函数/过程/例程", "prog_*+ddl_alter_object+ddl_drop_object", "已有+新增"},
	{"触发器", "prog_trigger", "新增"},
	{"事件触发器", "prog_event_trigger", "新增"},
	{"规则", "prog_rule", "新增"},
	{"类型/域", "ddl_create_object+ddl_alter_object+ddl_drop_object", "已有+新增"},
	{"匿名块", "cmd_do", "新增"},
	{"行级安全", "obj_policy", "新增"},
	{"逻辑复制", "obj_publication/obj_subscription", "新增"},
	{"外部数据", "obj_fdw", "新增"},
	{"扩展", "obj_extension", "新增"},
	{"语言", "obj_language", "新增"},
	{"高级对象", "obj_advanced", "新增"},
	{"服务器配置", "obj_alter_system", "新增"},
	{"所有权", "obj_reassign_owned+obj_drop_owned", "已有+新增"},
	{"注释/标签", "obj_security_label+obj_comment", "已有+新增"},
	{"重命名", "obj_rename_owner", "已有+新增"},
	{"配置", "cmd_set", "已有+新增"},
	{"数据查询", "dml_*", "已有"},
	{"数据修改", "dml_no_where等", "已有"},
	{"合并", "dml_merge_check", "新增"},
	{"数据复制", "cmd_copy", "已有+新增"},
	{"游标", "cmd_cursor", "新增"},
	{"预编译", "cmd_prepare", "新增"},
	{"过程调用", "cmd_call", "新增"},
	{"通知", "cmd_notify", "新增"},
	{"维护类", "cmd_vacuum等10条", "新增"},
	{"清空", "ddl_truncate_warn", "已有"},
	{"对象存在性", "obj_exists_check", "新增"},
	{"对象创建", "ddl_create_object", "新增"},
	{"对象结构变更", "ddl_alter_object", "新增"},
	{"对象删除", "ddl_drop_object", "新增"},
	{"对象注释", "obj_comment", "新增"},
	{"会话参数查询", "cmd_show", "新增"},
	{"所有权回收", "obj_drop_owned", "新增"},
};

/*
 * 命令映射查询：按命令族返回映射规则标识；未登记返回 NULL。
 * 供兜底分支（se_audit_echo）与测试核对使用。
 */
const char *
cmd_map_lookup(const char *command_family)
{
	int			i;

	if (command_family == NULL)
		return NULL;

	for (i = 0; i < (int) (sizeof(g_cmd_map) / sizeof(g_cmd_map[0])); i++)
	{
		if (strcmp(g_cmd_map[i].command_family, command_family) == 0)
			return g_cmd_map[i].rule_id;
	}
	return NULL;
}