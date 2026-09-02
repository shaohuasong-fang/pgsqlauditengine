/* -------------------------------------------------------------------------
 *
 * rule_exists.c
 *
 * 对象已存在冲突检查（spec 5.12 obj_exists_check）与两级流水线第一级。
 *
 * 存在性判定只走 syscache/目录便捷函数（禁止 SPI，避免在 ProcessUtility
 * 钩子中执行查询导致 CIC 段错误）；目录访问异常降级返回 InvalidOid。
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/genam.h"
#include "access/htup_details.h"
#include "access/skey.h"
#if PG_VERSION_NUM >= 120000
#include "access/table.h"
#else
#include "access/heapam.h"
#endif
#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "catalog/pg_am.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_conversion.h"
#include "catalog/pg_database.h"
#include "catalog/pg_extension.h"
#include "catalog/pg_language.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_policy.h"
#include "catalog/pg_publication.h"
#include "catalog/pg_statistic_ext.h"
#include "catalog/pg_subscription.h"
#include "catalog/pg_tablespace.h"
#include "catalog/pg_type.h"
#include "catalog/pg_class.h"
#include "commands/dbcommands.h"
#include "commands/defrem.h"
#include "commands/extension.h"
#include "commands/proclang.h"
#include "commands/tablespace.h"
#include "nodes/parsenodes.h"
#include "nodes/value.h"
#include "utils/catcache.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"

#include "pgsqlauditengine.h"
#include "rule_exists.h"
#include "compat.h"

/* 默认命名空间回退（提取器未提供 schemaname 时） */
#define EXISTS_DEFAULT_NSP "public"

/* -------------------------------------------------------------------------
 * 名称/命名空间/INE 标志提取器（按节点类型）
 * -------------------------------------------------------------------------
 */

static const char *
name_from_rangavar(RangeVar *rv)
{
	return rv ? rv->relname : NULL;
}

static const char *
nsp_from_rangavar(RangeVar *rv)
{
	return rv ? rv->schemaname : NULL;
}

static const char *
nsp_from_rangavar_force(RangeVar *rv)
{
	/* 表不存在时也返回默认命名空间（策略等场景） */
	return rv ? (rv->schemaname ? rv->schemaname : EXISTS_DEFAULT_NSP)
		: EXISTS_DEFAULT_NSP;
}

static const char *
get_name_create_table(Node *stmt)
{
	return name_from_rangavar(((CreateStmt *) stmt)->relation);
}

static const char *
get_nsp_create_table(Node *stmt)
{
	return nsp_from_rangavar(((CreateStmt *) stmt)->relation);
}

static bool
get_ine_create_table(Node *stmt)
{
	return ((CreateStmt *) stmt)->if_not_exists;
}

static const char *
get_name_view(Node *stmt)
{
	return name_from_rangavar(((ViewStmt *) stmt)->view);
}

static const char *
get_nsp_view(Node *stmt)
{
	return nsp_from_rangavar(((ViewStmt *) stmt)->view);
}

static const char *
get_name_seq(Node *stmt)
{
	return name_from_rangavar(((CreateSeqStmt *) stmt)->sequence);
}

static const char *
get_nsp_seq(Node *stmt)
{
	return nsp_from_rangavar(((CreateSeqStmt *) stmt)->sequence);
}

static bool
get_ine_seq(Node *stmt)
{
	return ((CreateSeqStmt *) stmt)->if_not_exists;
}

static const char *
get_name_index(Node *stmt)
{
	return ((IndexStmt *) stmt)->idxname;
}

static const char *
get_nsp_index(Node *stmt)
{
	IndexStmt  *s = (IndexStmt *) stmt;

	return s->relation ? nsp_from_rangavar(s->relation) : NULL;
}

static bool
get_ine_index(Node *stmt)
{
	return ((IndexStmt *) stmt)->if_not_exists;
}

static const char *
get_name_ctas(Node *stmt)
{
	CreateTableAsStmt *s = (CreateTableAsStmt *) stmt;

	return (s->into && s->into->rel) ? s->into->rel->relname : NULL;
}

static const char *
get_nsp_ctas(Node *stmt)
{
	CreateTableAsStmt *s = (CreateTableAsStmt *) stmt;

	return (s->into && s->into->rel) ? s->into->rel->schemaname : NULL;
}

static bool
get_ine_ctas(Node *stmt)
{
	return ((CreateTableAsStmt *) stmt)->if_not_exists;
}

static const char *
get_name_schema(Node *stmt)
{
	return ((CreateSchemaStmt *) stmt)->schemaname;
}

static bool
get_ine_schema(Node *stmt)
{
	return ((CreateSchemaStmt *) stmt)->if_not_exists;
}

static const char *
get_name_database(Node *stmt)
{
	return ((CreatedbStmt *) stmt)->dbname;
}

static const char *
get_name_tablespace(Node *stmt)
{
	return ((CreateTableSpaceStmt *) stmt)->tablespacename;
}

static const char *
get_name_domain(Node *stmt)
{
	return NameListToString(((CreateDomainStmt *) stmt)->domainname);
}

static const char *
get_name_composite(Node *stmt)
{
	return name_from_rangavar(((CompositeTypeStmt *) stmt)->typevar);
}

static const char *
get_nsp_composite(Node *stmt)
{
	return nsp_from_rangavar(((CompositeTypeStmt *) stmt)->typevar);
}

static const char *
get_name_enum(Node *stmt)
{
	return NameListToString(((CreateEnumStmt *) stmt)->typeName);
}

static const char *
get_name_range(Node *stmt)
{
	return NameListToString(((CreateRangeStmt *) stmt)->typeName);
}

static const char *
get_name_define(Node *stmt)
{
	DefineStmt  *d = (DefineStmt *) stmt;

	return d->defnames ? NameListToString(d->defnames) : NULL;
}

static const char *
get_nsp_define(Node *stmt)
{
	DefineStmt  *d = (DefineStmt *) stmt;

	if (d->defnames != NIL && list_length(d->defnames) == 2)
		return strVal(linitial(d->defnames));
	return NULL;
}

static bool
get_ine_define(Node *stmt)
{
	return ((DefineStmt *) stmt)->if_not_exists;
}

static const char *
get_name_extension(Node *stmt)
{
	return ((CreateExtensionStmt *) stmt)->extname;
}

static bool
get_ine_extension(Node *stmt)
{
	return ((CreateExtensionStmt *) stmt)->if_not_exists;
}

static const char *
get_name_language(Node *stmt)
{
	return ((CreatePLangStmt *) stmt)->plname;
}

static const char *
get_name_am(Node *stmt)
{
	return ((CreateAmStmt *) stmt)->amname;
}

static const char *
get_name_publication(Node *stmt)
{
	return ((CreatePublicationStmt *) stmt)->pubname;
}

static bool
get_ine_publication(Node *stmt)
{
	/* PG18 不支持 CREATE PUBLICATION IF NOT EXISTS */
	return false;
}

static const char *
get_name_subscription(Node *stmt)
{
	return ((CreateSubscriptionStmt *) stmt)->subname;
}

static const char *
get_name_policy(Node *stmt)
{
	return ((CreatePolicyStmt *) stmt)->policy_name;
}

static const char *
get_nsp_policy(Node *stmt)
{
	/* policy 场景复用该字段承载目标表名 */
	return nsp_from_rangavar_force(((CreatePolicyStmt *) stmt)->table);
}

static const char *
get_name_conversion(Node *stmt)
{
	return NameListToString(((CreateConversionStmt *) stmt)->conversion_name);
}

static const char *
get_nsp_conversion(Node *stmt)
{
	CreateConversionStmt *s = (CreateConversionStmt *) stmt;

	if (list_length(s->conversion_name) == 2)
		return strVal(linitial(s->conversion_name));
	return NULL;
}

static bool
get_ine_conversion(Node *stmt)
{
	/* PG18 不支持 CREATE CONVERSION IF NOT EXISTS */
	return false;
}

static const char *
get_name_stats(Node *stmt)
{
	return NameListToString(((CreateStatsStmt *) stmt)->defnames);
}

static const char *
get_nsp_stats(Node *stmt)
{
	CreateStatsStmt *s = (CreateStatsStmt *) stmt;

	if (list_length(s->defnames) == 2)
		return strVal(linitial(s->defnames));
	return NULL;
}

static bool
get_ine_stats(Node *stmt)
{
	return ((CreateStatsStmt *) stmt)->if_not_exists;
}

/* -------------------------------------------------------------------------
 * 存在性判定函数（返回 InvalidOid 表示不存在）
 * -------------------------------------------------------------------------
 */

static Oid
nspoid_of(const char *nspname)
{
	if (nspname == NULL)
		nspname = EXISTS_DEFAULT_NSP;
	return get_namespace_oid(nspname, true);
}

/* relation 类统一判定：校验 relkind */
static Oid
exists_relation_of_relkind(const char *name, const char *nspname, char relkind)
{
	Oid			nspoid;
	Oid			relid;
	HeapTuple	tp;
	Form_pg_class relform;
	bool		ok;

	if (name == NULL)
		return InvalidOid;

	nspoid = nspoid_of(nspname);
	if (nspoid == InvalidOid)
		return InvalidOid;

	relid = get_relname_relid(name, nspoid);
	if (relid == InvalidOid)
		return InvalidOid;

	tp = SearchSysCache1(RELOID, ObjectIdGetDatum(relid));
	if (!HeapTupleIsValid(tp))
		return InvalidOid;
	relform = (Form_pg_class) GETSTRUCT(tp);
	ok = (relform->relkind == relkind);
	ReleaseSysCache(tp);

	return ok ? relid : InvalidOid;
}

static Oid
exists_table(const char *name, const char *nspname)
{
	Oid			relid;

	relid = exists_relation_of_relkind(name, nspname, RELKIND_RELATION);
	if (relid != InvalidOid)
		return relid;
	return exists_relation_of_relkind(name, nspname, RELKIND_PARTITIONED_TABLE);
}

static Oid
exists_view(const char *name, const char *nspname)
{
	return exists_relation_of_relkind(name, nspname, RELKIND_VIEW);
}

static Oid
exists_matview(const char *name, const char *nspname)
{
	return exists_relation_of_relkind(name, nspname, RELKIND_MATVIEW);
}

static Oid
exists_sequence(const char *name, const char *nspname)
{
	return exists_relation_of_relkind(name, nspname, RELKIND_SEQUENCE);
}

static Oid
exists_index(const char *name, const char *nspname)
{
	return exists_relation_of_relkind(name, nspname, RELKIND_INDEX);
}

static Oid
exists_schema(const char *name, const char *nspname)
{
	if (name == NULL)
		return InvalidOid;
	return get_namespace_oid(name, true);
}

static Oid
exists_database(const char *name, const char *nspname)
{
	if (name == NULL)
		return InvalidOid;
	return get_database_oid(name, true);
}

static Oid
exists_tablespace(const char *name, const char *nspname)
{
	if (name == NULL)
		return InvalidOid;
	return get_tablespace_oid(name, true);
}

static Oid
exists_type(const char *name, const char *nspname)
{
	Oid			nspoid;
	HeapTuple	tp;
	Oid			result = InvalidOid;

	if (name == NULL)
		return InvalidOid;

	nspoid = nspoid_of(nspname);
	if (nspoid == InvalidOid)
		return InvalidOid;

	tp = SearchSysCache2(TYPENAMENSP,
						 CStringGetDatum(name),
						 ObjectIdGetDatum(nspoid));
	if (HeapTupleIsValid(tp))
	{
		result = SE_TUPLE_OID(tp);
		ReleaseSysCache(tp);
	}
	return result;
}

static Oid
exists_extension(const char *name, const char *nspname)
{
	if (name == NULL)
		return InvalidOid;
	return get_extension_oid(name, true);
}

static Oid
exists_language(const char *name, const char *nspname)
{
	if (name == NULL)
		return InvalidOid;
	return get_language_oid(name, true);
}

static Oid
exists_am(const char *name, const char *nspname)
{
	if (name == NULL)
		return InvalidOid;
	return get_am_oid(name, true);
}

static Oid
exists_publication(const char *name, const char *nspname)
{
	if (name == NULL)
		return InvalidOid;
	return get_publication_oid(name, true);
}

static Oid
exists_subscription(const char *name, const char *nspname)
{
	if (name == NULL)
		return InvalidOid;
	return get_subscription_oid(name, true);
}

static Oid
exists_policy(const char *name, const char *tablename)
{
	Oid			relid;
	Relation	pg_policy_rel;
	SysScanDesc	scan;
	ScanKeyData	keys[2];
	HeapTuple	tp;
	Oid			result = InvalidOid;

	if (name == NULL || tablename == NULL)
		return InvalidOid;

	relid = get_relname_relid(tablename, nspoid_of(NULL));
	if (relid == InvalidOid)
		return InvalidOid;

	pg_policy_rel = SE_TABLE_OPEN(PolicyRelationId, AccessShareLock);
	ScanKeyInit(&keys[0], Anum_pg_policy_polrelid, BTEqualStrategyNumber,
				F_OIDEQ, ObjectIdGetDatum(relid));
	ScanKeyInit(&keys[1], Anum_pg_policy_polname, BTEqualStrategyNumber,
				F_NAMEEQ, CStringGetDatum(name));
	scan = systable_beginscan(pg_policy_rel, PolicyPolrelidPolnameIndexId,
							  true, NULL, 2, keys);
	tp = systable_getnext(scan);
	if (HeapTupleIsValid(tp))
		result = SE_TUPLE_OID(tp);
	systable_endscan(scan);
	SE_TABLE_CLOSE(pg_policy_rel, AccessShareLock);

	return result;
}

static Oid
exists_conversion(const char *name, const char *nspname)
{
	Oid			nspoid;
	HeapTuple	tp;
	Oid			result = InvalidOid;

	if (name == NULL)
		return InvalidOid;

	nspoid = nspoid_of(nspname);
	if (nspoid == InvalidOid)
		return InvalidOid;

	tp = SearchSysCache2(CONNAMENSP,
						 CStringGetDatum(name),
						 ObjectIdGetDatum(nspoid));
	if (HeapTupleIsValid(tp))
	{
		result = SE_TUPLE_OID(tp);
		ReleaseSysCache(tp);
	}
	return result;
}

static Oid
exists_collation(const char *name, const char *nspname)
{
	Oid			nspoid;
	HeapTuple	tp;
	Oid			result = InvalidOid;

	if (name == NULL)
		return InvalidOid;

	nspoid = nspoid_of(nspname);
	if (nspoid == InvalidOid)
		return InvalidOid;

	/* PG18: collation 名称键含 encoding，默认匹配所有编码(-1) */
	tp = SearchSysCache3(COLLNAMEENCNSP,
						 CStringGetDatum(name),
						 ObjectIdGetDatum(nspoid),
						 Int32GetDatum(-1));
	if (HeapTupleIsValid(tp))
	{
		result = SE_TUPLE_OID(tp);
		ReleaseSysCache(tp);
	}
	return result;
}

static Oid
exists_statistics(const char *name, const char *nspname)
{
	Oid			nspoid;
	HeapTuple	tp;
	Oid			result = InvalidOid;

	if (name == NULL)
		return InvalidOid;

	nspoid = nspoid_of(nspname);
	if (nspoid == InvalidOid)
		return InvalidOid;

	tp = SearchSysCache2(STATEXTNAMENSP,
						 CStringGetDatum(name),
						 ObjectIdGetDatum(nspoid));
	if (HeapTupleIsValid(tp))
	{
		result = SE_TUPLE_OID(tp);
		ReleaseSysCache(tp);
	}
	return result;
}

/* -------------------------------------------------------------------------
 * 存在性判定表（数据驱动）
 * -------------------------------------------------------------------------
 */
typedef struct ObjExistsDesc
{
	AuditObjType	objtype;
	NodeTag			node_tag;	/* 匹配的节点类型 */
	const char	   *objtype_name;	/* 消息文案中的对象类型名 */
	const char *(*get_name) (Node *stmt);
	const char *(*get_nspname) (Node *stmt);
	bool		(*get_if_not_exists) (Node *stmt);	/* NULL = 不支持 INE */
	Oid			(*exists) (const char *name, const char *nspname);
} ObjExistsDesc;

static const ObjExistsDesc g_obj_exists_desc[] = {
	{AOT_RELATION, T_CreateStmt, "表",
	 get_name_create_table, get_nsp_create_table, get_ine_create_table, exists_table},
	{AOT_RELATION, T_CreateForeignTableStmt, "外部表",
	 get_name_create_table, get_nsp_create_table, get_ine_create_table, exists_table},
	{AOT_VIEW, T_ViewStmt, "视图",
	 get_name_view, get_nsp_view, NULL, exists_view},
	{AOT_SEQUENCE, T_CreateSeqStmt, "序列",
	 get_name_seq, get_nsp_seq, get_ine_seq, exists_sequence},
	{AOT_INDEX, T_IndexStmt, "索引",
	 get_name_index, get_nsp_index, get_ine_index, exists_index},
	{AOT_RELATION, T_CreateTableAsStmt, "表(CTAS)",
	 get_name_ctas, get_nsp_ctas, get_ine_ctas, exists_table},
	{AOT_MATVIEW, T_CreateTableAsStmt, "物化视图",
	 get_name_ctas, get_nsp_ctas, get_ine_ctas, exists_matview},
	{AOT_SCHEMA, T_CreateSchemaStmt, "模式",
	 get_name_schema, NULL, get_ine_schema, exists_schema},
	{AOT_DATABASE, T_CreatedbStmt, "数据库",
	 get_name_database, NULL, NULL, exists_database},
	{AOT_TABLESPACE, T_CreateTableSpaceStmt, "表空间",
	 get_name_tablespace, NULL, NULL, exists_tablespace},
	{AOT_TYPE, T_CreateDomainStmt, "域",
	 get_name_domain, NULL, NULL, exists_type},
	{AOT_TYPE, T_CompositeTypeStmt, "复合类型",
	 get_name_composite, get_nsp_composite, NULL, exists_type},
	{AOT_TYPE, T_CreateEnumStmt, "枚举类型",
	 get_name_enum, NULL, NULL, exists_type},
	{AOT_TYPE, T_CreateRangeStmt, "范围类型",
	 get_name_range, NULL, NULL, exists_type},
	{AOT_TYPE, T_DefineStmt, "类型",
	 get_name_define, get_nsp_define, get_ine_define, exists_type},
	{AOT_COLLATION, T_DefineStmt, "排序规则",
	 get_name_define, get_nsp_define, get_ine_define, exists_collation},
	{AOT_EXTENSION, T_CreateExtensionStmt, "扩展",
	 get_name_extension, NULL, get_ine_extension, exists_extension},
	{AOT_LANGUAGE, T_CreatePLangStmt, "过程语言",
	 get_name_language, NULL, NULL, exists_language},
	{AOT_AM, T_CreateAmStmt, "访问方法",
	 get_name_am, NULL, NULL, exists_am},
	{AOT_PUBLICATION, T_CreatePublicationStmt, "发布",
	 get_name_publication, NULL, get_ine_publication, exists_publication},
	{AOT_SUBSCRIPTION, T_CreateSubscriptionStmt, "订阅",
	 get_name_subscription, NULL, NULL, exists_subscription},
	{AOT_POLICY, T_CreatePolicyStmt, "策略",
	 get_name_policy, get_nsp_policy, NULL, exists_policy},
	{AOT_CONVERSION, T_CreateConversionStmt, "转换",
	 get_name_conversion, get_nsp_conversion, get_ine_conversion, exists_conversion},
	{AOT_STATISTICS, T_CreateStatsStmt, "统计对象",
	 get_name_stats, get_nsp_stats, get_ine_stats, exists_statistics},
};

/*
 * 判定表条目与语句节点是否匹配。
 * T_DefineStmt 需按 objtype 区分 TYPE/COLLATION；
 * T_CreateTableAsStmt 需按 objtype 区分 表/物化视图。
 */
static bool
desc_matches_node(const ObjExistsDesc *d, Node *stmt)
{
	if (nodeTag(stmt) != d->node_tag)
		return false;

	if (nodeTag(stmt) == T_DefineStmt)
	{
		DefineStmt *dd = (DefineStmt *) stmt;
		bool		is_coll = (dd->kind == OBJECT_COLLATION);

		return (d->objtype == AOT_COLLATION) == is_coll;
	}

	if (nodeTag(stmt) == T_CreateTableAsStmt)
	{
		CreateTableAsStmt *ctas = (CreateTableAsStmt *) stmt;
		bool		is_mv = (SE_CTAS_OBJTYPE(ctas) == OBJECT_MATVIEW);

		return (d->objtype == AOT_MATVIEW) == is_mv;
	}

	return true;
}

/* -------------------------------------------------------------------------
 * 类型化对象存在性判定（对外接口）
 * -------------------------------------------------------------------------
 */
Oid
audit_object_exists(int objtype, const char *name, const char *nspname)
{
	switch ((AuditObjType) objtype)
	{
		case AOT_RELATION:
			return exists_table(name, nspname);
		case AOT_VIEW:
			return exists_view(name, nspname);
		case AOT_MATVIEW:
			return exists_matview(name, nspname);
		case AOT_SEQUENCE:
			return exists_sequence(name, nspname);
		case AOT_INDEX:
			return exists_index(name, nspname);
		case AOT_SCHEMA:
			return exists_schema(name, nspname);
		case AOT_DATABASE:
			return exists_database(name, nspname);
		case AOT_TABLESPACE:
			return exists_tablespace(name, nspname);
		case AOT_TYPE:
			return exists_type(name, nspname);
		case AOT_EXTENSION:
			return exists_extension(name, nspname);
		case AOT_LANGUAGE:
			return exists_language(name, nspname);
		case AOT_AM:
			return exists_am(name, nspname);
		case AOT_PUBLICATION:
			return exists_publication(name, nspname);
		case AOT_SUBSCRIPTION:
			return exists_subscription(name, nspname);
		case AOT_POLICY:
			return exists_policy(name, nspname);
		case AOT_CONVERSION:
			return exists_conversion(name, nspname);
		case AOT_COLLATION:
			return exists_collation(name, nspname);
		case AOT_STATISTICS:
			return exists_statistics(name, nspname);
		default:
			return InvalidOid;
	}
}

/*
 * 对 CREATE 家族语句执行对象存在性判定与动态级别输出。
 */
ExistsCheckResult
audit_obj_exists_check(Node *stmt)
{
	const ObjExistsDesc *desc = NULL;
	const char *name;
	const char *nspname;
	bool		has_ine = false;
	Oid			oid;
	AuditLevel	level;
	int			i;

	if (stmt == NULL)
		return EXISTS_CHECK_PASS;

	/* CIC 特判（决策 D20）：CREATE INDEX CONCURRENTLY 使用独立的两阶段事务/快照
	 * 机制，ProcessUtility 钩子内对其访问系统目录（本文件下方 exists_index →
	 * exists_relation_of_relkind 的 syscache 查询）会与内核 CIC 事务管理冲突，
	 * 实测 PG17 触发 TRAP: SysCache nkeys assert 服务端崩溃。
	 * 故 CIC 场景跳过对象存在性检查直接放行；非 CIC 场景行为不变。 */
	if (nodeTag(stmt) == T_IndexStmt &&
		((IndexStmt *) stmt)->concurrent)
		return EXISTS_CHECK_PASS;

	for (i = 0; i < (int) (sizeof(g_obj_exists_desc) / sizeof(g_obj_exists_desc[0])); i++)
	{
		if (desc_matches_node(&g_obj_exists_desc[i], stmt))
		{
			desc = &g_obj_exists_desc[i];
			break;
		}
	}
	if (desc == NULL)
		return EXISTS_CHECK_PASS;

	name = desc->get_name ? desc->get_name(stmt) : NULL;
	nspname = desc->get_nspname ? desc->get_nspname(stmt) : NULL;
	if (desc->get_if_not_exists != NULL)
		has_ine = desc->get_if_not_exists(stmt);

	oid = audit_object_exists((int) desc->objtype, name, nspname);

	if (oid == InvalidOid)
		return EXISTS_CHECK_PASS;

	if (has_ine)
	{
		audit_emit_fixed(AUDIT_WARNING, "obj_exists_check",
						 "对象已存在, IF NOT EXISTS 生效, 跳过创建 (%s: %s)",
						 desc->objtype_name, name ? name : "?");
		return EXISTS_CHECK_WARN;
	}

	level = audit_effective_level("obj_exists_check", AUDIT_ERROR);
	audit_emit_fixed(level, "obj_exists_check",
					 "对象已存在, 请使用 IF NOT EXISTS 或删除旧对象 (%s: %s)",
					 desc->objtype_name, name ? name : "?");
	return EXISTS_CHECK_BLOCK;
}