/* -------------------------------------------------------------------------
 *
 * pgsqlauditengine.h
 *
 * 公共类型/接口定义。供所有审核模块引用。
 *
 * -------------------------------------------------------------------------
 */
#ifndef PGSQLAUDITENGINE_H
#define PGSQLAUDITENGINE_H

#include "postgres.h"
#include "nodes/parsenodes.h"

typedef enum AuditLevel
{
	AUDIT_NOTICE = 0,
	AUDIT_WARNING = 1,
	AUDIT_ERROR = 2
} AuditLevel;

typedef enum AuditConclusion
{
	AUDIT_CONC_PASS = 0,
	AUDIT_CONC_WARN = 1,
	AUDIT_CONC_BLOCK = 2
} AuditConclusion;

/* 审核总开关 GUC */
extern bool pgsql_audit_engine_enabled;

/* 是否挂载 DML(post_parse_analyze) 审核 */
extern bool pgsql_audit_engine_check_dml;

/* RESTful 服务配置 */
extern char *pgsql_audit_engine_api_listen;
extern int	pgsql_audit_engine_api_port;
extern char *pgsql_audit_engine_api_token;

/*
 * 规则定义：编译期登记的内置规则元数据。
 */
typedef struct AuditRuleDef
{
	const char *rule_id;
	const char *rule_name;
	const char *rule_type;		/* 强制 / 建议 / 推荐 */
	AuditLevel	default_level;
} AuditRuleDef;

/*
 * 运行期规则配置（共享内存）。enabled 控制规则开关，level 覆盖默认级别。
 */
typedef struct AuditRuleConfig
{
	bool		enabled;
	AuditLevel	level;
} AuditRuleConfig;

/* 规则注册表（静态规则定义 + 共享内存配置 + 计数） */
typedef struct AuditRegistry
{
	int					num_rules;
	const AuditRuleDef *rules;
	AuditRuleConfig    *configs;	/* 共享内存数组 */
} AuditRegistry;

extern AuditRegistry *pgsql_audit_engine_registry;

void audit_registry_init(AuditRegistry *reg);

int	 audit_find_rule(const char *rule_id);

/*
 * 统一审核输出入口。
 *  - rule_id: 全局唯一规则标识
 *  - def_level: 该规则默认级别（【强制】=ERROR，【建议】=WARNING，【推荐】=NOTICE）
 *  - fmt: 消息文案（不含 "SQL审核:" 前缀，由本函数统一添加）
 *
 * 行为：若规则被关闭则直接返回；级别可被共享内存配置覆盖；
 *       ERROR 级别将阻断语句，WARNING/NOTICE 仅提示。
 */
void audit_emit(AuditLevel def_level, const char *rule_id,
				const char *fmt, ...) pg_attribute_printf(3, 4);

/*
 * 固定级别审核输出（与 audit_emit 的区别）：
 *  - 传入的 level 即最终输出级别，不被共享内存配置覆盖；
 *  - 规则被关闭时静默返回；写审核记录并 ereport。
 * 用于 obj_exists_check 的动态级别语义：
 *  - "有 IF NOT EXISTS"分支：固定 AUDIT_WARNING（配置为 ERROR 也不阻断）
 *  - "无 IF NOT EXISTS"分支：调用方传入 audit_effective_level() 的结果
 */
void audit_emit_fixed(AuditLevel level, const char *rule_id,
					  const char *fmt, ...) pg_attribute_printf(3, 4);

/*
 * 规则级别校验：返回经配置覆盖后的生效级别；规则未登记时返回 def_level。
 */
AuditLevel audit_effective_level(const char *rule_id, AuditLevel def_level);

/* 将级别映射为结论 */
AuditConclusion audit_level_to_conclusion(AuditLevel level);

/* 写入一条审核记录（共享内存环形缓冲），不产生任何消息 */
void audit_record_write(const char *stmt_type, const char *object_name,
						const char *rule_id, AuditLevel level);

#endif							/* PGSQLAUDITENGINE_H */