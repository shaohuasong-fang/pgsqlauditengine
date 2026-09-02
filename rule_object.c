/* -------------------------------------------------------------------------
 *
 * rule_object.c
 *
 * 系统级/对象类命令审核（spec 5.11 obj_* 12 条）。
 * 以"识别 + 风险告警"为主，回显对象名与类型。
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "nodes/parsenodes.h"
#include "nodes/value.h"
#include "catalog/namespace.h"

#include "compat.h"
#include "pgsqlauditengine.h"
#include "rule_object.h"

/*
 * 从 DropStmt 提取首对象名（兼容多部分名称 List 与裸 String 节点）。
 */
static const char *
drop_obj_name(DropStmt *stmt)
{
	Node	   *first;

	if (stmt == NULL || stmt->objects == NIL)
		return NULL;

	first = (Node *) linitial(stmt->objects);

	if (IsA(first, List))
	{
		List	   *names = (List *) first;

		if (names == NIL)
			return NULL;
		first = (Node *) linitial(names);
	}

	if (IsA(first, String))
		return strVal(first);

	return NULL;
}

/*
 * 行级安全策略：CREATE/ALTER/DROP POLICY
 */
void
audit_obj_policy(Node *stmt)
{
	const char *name = NULL;
	const char *table = NULL;

	if (stmt == NULL)
		return;

	switch (nodeTag(stmt))
	{
		case T_CreatePolicyStmt:
			name = ((CreatePolicyStmt *) stmt)->policy_name;
			table = ((CreatePolicyStmt *) stmt)->table->relname;
			break;
		case T_AlterPolicyStmt:
			name = ((AlterPolicyStmt *) stmt)->policy_name;
			table = ((AlterPolicyStmt *) stmt)->table->relname;
			break;
		case T_DropStmt:
			{
				DropStmt   *drop = (DropStmt *) stmt;
				Node	   *first;

				name = drop_obj_name(drop);
				/* DROP POLICY 的 objects 元素为 (表名, 策略名) 二元组 */
				if (drop->objects != NIL)
				{
					first = (Node *) linitial(drop->objects);
					if (IsA(first, List))
					{
						List	   *names = (List *) first;

						if (list_length(names) >= 2)
						{
							name = strVal(list_nth(names, 1));
							table = strVal(linitial(names));
						}
					}
				}
			}
			break;
		default:
			break;
	}

	audit_emit(AUDIT_WARNING, "obj_policy",
			   "行级安全策略变更需评估对既有访问路径的影响 (策略: %s, 表: %s)",
			   name ? name : "?", table ? table : "?");
}

/*
 * 逻辑复制发布：CREATE/ALTER/DROP PUBLICATION
 */
void
audit_obj_publication(Node *stmt)
{
	const char *name = NULL;

	if (stmt == NULL)
		return;

	switch (nodeTag(stmt))
	{
		case T_CreatePublicationStmt:
			name = ((CreatePublicationStmt *) stmt)->pubname;
			break;
		case T_AlterPublicationStmt:
			name = ((AlterPublicationStmt *) stmt)->pubname;
			break;
		case T_DropStmt:
			name = drop_obj_name((DropStmt *) stmt);
			break;
		default:
			break;
	}

	audit_emit(AUDIT_WARNING, "obj_publication",
			   "发布变更影响逻辑复制链路 (发布: %s)",
			   name ? name : "?");
}

/*
 * 逻辑复制订阅：CREATE/ALTER/DROP SUBSCRIPTION
 */
void
audit_obj_subscription(Node *stmt)
{
	const char *name = NULL;

	if (stmt == NULL)
		return;

	switch (nodeTag(stmt))
	{
		case T_CreateSubscriptionStmt:
			name = ((CreateSubscriptionStmt *) stmt)->subname;
			break;
		case T_AlterSubscriptionStmt:
			name = ((AlterSubscriptionStmt *) stmt)->subname;
			break;
		case T_DropSubscriptionStmt:
			name = ((DropSubscriptionStmt *) stmt)->subname;
			break;
		default:
			break;
	}

	audit_emit(AUDIT_WARNING, "obj_subscription",
			   "订阅变更影响逻辑复制链路 (订阅: %s)",
			   name ? name : "?");
}

/*
 * 外部数据对象：FDW / SERVER / USER MAPPING / IMPORT FOREIGN SCHEMA
 */
void
audit_obj_fdw(Node *stmt)
{
	const char *name = NULL;

	if (stmt == NULL)
		return;

	switch (nodeTag(stmt))
	{
		case T_CreateFdwStmt:
			name = ((CreateFdwStmt *) stmt)->fdwname;
			break;
		case T_AlterFdwStmt:
			name = ((AlterFdwStmt *) stmt)->fdwname;
			break;
		case T_CreateForeignServerStmt:
			name = ((CreateForeignServerStmt *) stmt)->servername;
			break;
		case T_AlterForeignServerStmt:
			name = ((AlterForeignServerStmt *) stmt)->servername;
			break;
		case T_CreateUserMappingStmt:
			name = ((CreateUserMappingStmt *) stmt)->servername;
			break;
		case T_AlterUserMappingStmt:
			name = ((AlterUserMappingStmt *) stmt)->servername;
			break;
		case T_DropUserMappingStmt:
			name = ((DropUserMappingStmt *) stmt)->servername;
			break;
		case T_ImportForeignSchemaStmt:
			name = ((ImportForeignSchemaStmt *) stmt)->server_name;
			break;
		case T_DropStmt:
			name = drop_obj_name((DropStmt *) stmt);
			break;
		default:
			break;
	}

	audit_emit(AUDIT_WARNING, "obj_fdw",
			   "外部数据对象涉及跨系统访问与凭据配置 (对象: %s)",
			   name ? name : "?");
}

/*
 * 扩展管理：CREATE/ALTER/DROP EXTENSION
 */
void
audit_obj_extension(Node *stmt)
{
	const char *name = NULL;

	if (stmt == NULL)
		return;

	switch (nodeTag(stmt))
	{
		case T_CreateExtensionStmt:
			name = ((CreateExtensionStmt *) stmt)->extname;
			break;
		case T_AlterExtensionStmt:
			name = ((AlterExtensionStmt *) stmt)->extname;
			break;
		case T_DropStmt:
			name = drop_obj_name((DropStmt *) stmt);
			break;
		default:
			break;
	}

	audit_emit(AUDIT_WARNING, "obj_extension",
			   "扩展需评估来源/版本与影响面 (扩展: %s)",
			   name ? name : "?");
}

/*
 * 过程语言：CREATE/ALTER/DROP LANGUAGE
 */
void
audit_obj_language(Node *stmt)
{
	const char *name = NULL;

	if (stmt == NULL)
		return;

	switch (nodeTag(stmt))
	{
		case T_CreatePLangStmt:
			name = ((CreatePLangStmt *) stmt)->plname;
			break;
		case T_DropStmt:
			name = drop_obj_name((DropStmt *) stmt);
			break;
		default:
			break;
	}

	audit_emit(AUDIT_WARNING, "obj_language",
			   "过程语言(尤其非受信语言)需评估执行风险 (语言: %s)",
			   name ? name : "?");
}

/*
 * 高级对象：ACCESS METHOD/CAST/COLLATION/CONVERSION/TRANSFORM/TEXT SEARCH
 * /STATISTICS/OPERATOR/OPERATOR CLASS/OPERATOR FAMILY/AGGREGATE 等
 */
void
audit_obj_advanced(Node *stmt)
{
	const char *name = NULL;
	const char *kind = "高级对象";

	if (stmt == NULL)
		return;

	switch (nodeTag(stmt))
	{
		case T_CreateAmStmt:
			name = ((CreateAmStmt *) stmt)->amname;
			kind = "ACCESS METHOD";
			break;
		case T_CreateCastStmt:
			kind = "CAST";
			name = "?";
			break;
		case T_DefineStmt:
			{
				DefineStmt *d = (DefineStmt *) stmt;

				if (d->defnames != NIL)
					name = NameListToString(d->defnames);
				switch (d->kind)
				{
					case OBJECT_AGGREGATE:
						kind = "AGGREGATE";
						break;
					case OBJECT_OPERATOR:
						kind = "OPERATOR";
						break;
					case OBJECT_TYPE:
						kind = "TYPE";
						break;
					case OBJECT_COLLATION:
						kind = "COLLATION";
						break;
					case OBJECT_TSCONFIGURATION:
						kind = "TEXT SEARCH CONFIGURATION";
						break;
					case OBJECT_TSDICTIONARY:
						kind = "TEXT SEARCH DICTIONARY";
						break;
					case OBJECT_TSPARSER:
						kind = "TEXT SEARCH PARSER";
						break;
					case OBJECT_TSTEMPLATE:
						kind = "TEXT SEARCH TEMPLATE";
						break;
					default:
						break;
				}
			}
			break;
		case T_CreateOpClassStmt:
			name = NameListToString(((CreateOpClassStmt *) stmt)->opclassname);
			kind = "OPERATOR CLASS";
			break;
		case T_AlterOpFamilyStmt:
			name = NameListToString(((AlterOpFamilyStmt *) stmt)->opfamilyname);
			kind = "OPERATOR FAMILY";
			break;
		case T_CreateConversionStmt:
			name = NameListToString(((CreateConversionStmt *) stmt)->conversion_name);
			kind = "CONVERSION";
			break;
		case T_CreateTransformStmt:
			kind = "TRANSFORM";
			name = "?";
			break;
		case T_CreateStatsStmt:
			name = NameListToString(((CreateStatsStmt *) stmt)->defnames);
			kind = "STATISTICS";
			break;
#if PG_VERSION_NUM >= 130000
		case T_AlterStatsStmt:
			name = NameListToString(((AlterStatsStmt *) stmt)->defnames);
			kind = "STATISTICS";
			break;
#endif
		case T_AlterTSDictionaryStmt:
			name = NameListToString(((AlterTSDictionaryStmt *) stmt)->dictname);
			kind = "TEXT SEARCH DICTIONARY";
			break;
		case T_AlterTSConfigurationStmt:
			name = NameListToString(((AlterTSConfigurationStmt *) stmt)->cfgname);
			kind = "TEXT SEARCH CONFIGURATION";
			break;
		case T_DropStmt:
			name = drop_obj_name((DropStmt *) stmt);
			kind = "高级对象";
			break;
		default:
			break;
	}

	audit_emit(AUDIT_WARNING, "obj_advanced",
			   "%s 定义/变更需由资深 DBA 实施 (对象: %s)",
			   kind, name ? name : "?");
}

/*
 * 服务器配置修改：ALTER SYSTEM
 */
void
audit_obj_alter_system(AlterSystemStmt *stmt)
{
	const char *param;
	const char *val = NULL;

	if (stmt == NULL || stmt->setstmt == NULL)
		return;

	param = stmt->setstmt->name;

	if (stmt->setstmt->args != NIL)
	{
		Node	   *arg = (Node *) linitial(stmt->setstmt->args);

		if (IsA(arg, A_Const))
		{
			A_Const    *c = (A_Const *) arg;

			if (SE_A_CONST_STR(c) != NULL)
				val = SE_A_CONST_STR(c);
		}
	}

	audit_emit(AUDIT_WARNING, "obj_alter_system",
			   "ALTER SYSTEM 修改服务器级配置影响全局 (参数: %s, 值: %s)",
			   param ? param : "?", val ? val : "?");
}

/*
 * 对象所有权转移：REASSIGN OWNED
 */
void
audit_obj_reassign_owned(ReassignOwnedStmt *stmt)
{
	const char *src = NULL;
	const char *dst = NULL;

	if (stmt == NULL)
		return;

	if (stmt->roles != NIL && IsA(linitial(stmt->roles), RoleSpec))
		src = ((RoleSpec *) linitial(stmt->roles))->rolename;

	if (stmt->newrole != NULL)
		dst = stmt->newrole->rolename;

	audit_emit(AUDIT_WARNING, "obj_reassign_owned",
			   "对象所有权批量转移需评估影响 (源角色: %s, 目标角色: %s)",
			   src ? src : "?", dst ? dst : "?");
}

/*
 * 查询建表：CREATE TABLE AS / SELECT INTO
 * 注意：objtype==OBJECT_TABLE 由调用方（hook 分发）保证。
 */
void
audit_obj_ctas(CreateTableAsStmt *stmt)
{
	if (stmt == NULL || stmt->into == NULL || stmt->into->rel == NULL)
		return;

	audit_emit(AUDIT_WARNING, "obj_ctas",
			   "评估是否确需创建实体表(CTAS/SELECT INTO) (新表: %s)",
			   stmt->into->rel->relname);
}

/*
 * 物化视图：CREATE/ALTER/DROP MATERIALIZED VIEW
 */
void
audit_obj_matview(Node *stmt)
{
	const char *name = NULL;

	if (stmt == NULL)
		return;

	switch (nodeTag(stmt))
	{
		case T_CreateTableAsStmt:
			{
				CreateTableAsStmt *ctas = (CreateTableAsStmt *) stmt;

				if (SE_CTAS_OBJTYPE(ctas) == OBJECT_MATVIEW &&
					ctas->into != NULL && ctas->into->rel != NULL)
					name = ctas->into->rel->relname;
			}
			break;
		case T_AlterTableStmt:
			{
				AlterTableStmt *alt = (AlterTableStmt *) stmt;

				if (SE_ALT_OBJTYPE(alt) == OBJECT_MATVIEW && alt->relation != NULL)
					name = alt->relation->relname;
			}
			break;
		case T_DropStmt:
			name = drop_obj_name((DropStmt *) stmt);
			break;
		default:
			break;
	}

	audit_emit(AUDIT_WARNING, "obj_matview",
			   "物化视图需评估数据新鲜度与刷新策略 (物化视图: %s)",
			   name ? name : "?");
}

/*
 * 安全标签：SECURITY LABEL
 */
void
audit_obj_security_label(SecLabelStmt *stmt)
{
	const char *provider = NULL;
	const char *object = NULL;

	if (stmt == NULL)
		return;

	provider = stmt->provider;

	if (stmt->object != NULL)
	{
		Node	   *first = stmt->object;

		if (IsA(first, List))
		{
			List	   *names = (List *) first;

			if (names != NIL)
				first = (Node *) linitial(names);
		}
		if (IsA(first, String))
			object = strVal(first);
	}

	audit_emit(AUDIT_NOTICE, "obj_security_label",
			   "SECURITY LABEL 识别回显 (提供方: %s, 对象: %s)",
			   provider ? provider : "?", object ? object : "?");
}