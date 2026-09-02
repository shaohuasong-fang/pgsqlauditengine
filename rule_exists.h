/* -------------------------------------------------------------------------
 *
 * rule_exists.h
 *
 * 对象已存在冲突检查（spec 5.12 obj_exists_check）与两级流水线第一级。
 * 存在性判定只走 syscache/目录便捷函数（禁止 SPI，避免 CIC 段错误）。
 *
 * -------------------------------------------------------------------------
 */
#ifndef PGSQLAUDITENGINE_RULE_EXISTS_H
#define PGSQLAUDITENGINE_RULE_EXISTS_H

#include "nodes/parsenodes.h"

typedef enum ExistsCheckResult
{
	EXISTS_CHECK_PASS = 0,		/* 对象不存在 → 调用方继续内容审核 */
	EXISTS_CHECK_BLOCK = 1,		/* 已存在且无 IF NOT EXISTS → 已阻断 */
	EXISTS_CHECK_WARN = 2		/* 已存在且有 IF NOT EXISTS → 已告警, 放行 */
} ExistsCheckResult;

/*
 * 对象类型标识（与 ObjectType 对齐并补充 relation 变体）。
 */
typedef enum AuditObjType
{
	AOT_RELATION, AOT_INDEX, AOT_SEQUENCE, AOT_SCHEMA, AOT_DATABASE,
	AOT_TABLESPACE, AOT_TYPE, AOT_EXTENSION, AOT_LANGUAGE, AOT_AM,
	AOT_PUBLICATION, AOT_SUBSCRIPTION, AOT_POLICY, AOT_CONVERSION,
	AOT_COLLATION, AOT_STATISTICS, AOT_VIEW, AOT_MATVIEW
} AuditObjType;

/*
 * 对 CREATE 家族语句执行对象存在性判定与动态级别输出。
 * 返回结果供调用方决定是否执行第二级内容审核。
 * 目录查询失败时降级返回 EXISTS_CHECK_PASS（交由内核处理）。
 */
extern ExistsCheckResult audit_obj_exists_check(Node *stmt);

/*
 * 类型化对象存在性判定（内部接口）。
 * objtype: 对象类型标识（AuditObjType）
 * name/nspname: 对象名与可空命名空间
 * 返回 InvalidOid 表示不存在。
 */
extern Oid audit_object_exists(int objtype, const char *name, const char *nspname);

#endif							/* PGSQLAUDITENGINE_RULE_EXISTS_H */