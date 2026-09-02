/* -------------------------------------------------------------------------
 *
 * hook_analyze.c
 *
 * post_parse_analyze 钩子：拦截 DML 语句（SELECT/INSERT/UPDATE/DELETE/MERGE），
 * 实现无 WHERE 修改、count(列)、左模糊查询等审核。
 *
 * 说明：
 *  - 语义分析后查询树中 select * 已被展开为具体列，故 select * 的 DML 检测
 *    在此阶段不可靠，本模块聚焦可可靠判定的规则（无 WHERE、count、左模糊）。
 *  - 仅审核顶层查询（parentParseState == NULL），避免子查询重复审核。
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "catalog/pg_type.h"
#include "nodes/nodeFuncs.h"
#include "nodes/pg_list.h"
#include "parser/analyze.h"
#include "parser/parse_node.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"

#include "compat.h"
#include "pgsqlauditengine.h"
#include "hook_analyze.h"

static se_post_parse_analyze_hook_type prev_post_parse_analyze_hook = NULL;

/* 判断聚合是否为 count 且非 count(*)（即 count(列)） */
static bool
is_count_of_column(Aggref *agg)
{
	const char *fname;

	if (agg->aggstar)
		return false;

	fname = get_func_name(agg->aggfnoid);
	if (fname == NULL)
		return false;

	return (strcmp(fname, "count") == 0);
}

/* 遍历 targetList，检查 count(列) 而非 count(*) */
static void
check_count_star(Query *query)
{
	ListCell   *lc;

	foreach(lc, query->targetList)
	{
		TargetEntry *tle = (TargetEntry *) lfirst(lc);
		Aggref	   *agg;

		if (tle->expr == NULL || !IsA(tle->expr, Aggref))
			continue;

		agg = (Aggref *) tle->expr;
		if (is_count_of_column(agg))
		{
			audit_emit(AUDIT_NOTICE, "dml_count_star",
					   "统计行数建议使用 count(*)");
			break;
		}
	}
}

/* 提取 Const 节点的字符串值（仅 text/varchar/bpchar），失败返回 NULL */
static const char *
const_string(Node *n)
{
	Const	   *c;

	if (n == NULL || !IsA(n, Const))
		return NULL;

	c = (Const *) n;
	if (c->constisnull)
		return NULL;

	switch (c->consttype)
	{
		case TEXTOID:
		case VARCHAROID:
		case BPCHAROID:
			return TextDatumGetCString(c->constvalue);
		default:
			return NULL;
	}
}

/*
 * 检查单个 LIKE 模式。pat 为 pattern 字符串节点。
 */
static void
check_like_pattern(Node *patnode)
{
	const char *pat;

	if (patnode == NULL)
		return;

	pat = const_string(patnode);
	if (pat == NULL)
		return;

	if (pat[0] == '%')
	{
		audit_emit(AUDIT_WARNING, "dml_left_fuzzy",
				   "查询使用了左模糊匹配（LIKE 以 %% 开头），建议避免");
	}
}

/*
 * 表达式树回调：检测 LIKE（OpExpr "~~" / FuncExpr like）。
 * 返回 true 表示停止遍历。
 */
static bool
left_fuzzy_walker(Node *node, void *context)
{
	if (node == NULL)
		return false;

	if (IsA(node, OpExpr))
	{
		OpExpr	   *op = (OpExpr *) node;
		const char *opname = get_opname(op->opno);

		if (opname != NULL && strcmp(opname, "~~") == 0 &&
			list_length(op->args) >= 2)
		{
			check_like_pattern((Node *) list_nth(op->args, 1));
		}
	}
	else if (IsA(node, FuncExpr))
	{
		FuncExpr   *fe = (FuncExpr *) node;
		const char *fname = get_func_name(fe->funcid);

		if (fname != NULL &&
			(strcmp(fname, "like") == 0 || strcmp(fname, "textlike") == 0))
		{
			if (list_length(fe->args) >= 2)
				check_like_pattern((Node *) list_nth(fe->args, 1));
		}
	}

	return expression_tree_walker(node, left_fuzzy_walker, context);
}

static void
check_left_fuzzy(Query *query)
{
	if (query->jointree == NULL)
		return;

	expression_tree_walker((Node *) query->jointree,
						   left_fuzzy_walker, NULL);
}

/* 遍历查询树查找 IN (子查询)（ANY_SUBLINK），命中一次即停止 */
static bool
find_in_sublink_walker(Node *node, void *context)
{
	if (node == NULL)
		return false;

	if (IsA(node, SubLink))
	{
		SubLink    *sl = (SubLink *) node;

		if (sl->subLinkType == ANY_SUBLINK)
		{
			audit_emit(AUDIT_NOTICE, "dml_in_to_exists",
					   "查询使用了 IN (子查询)，建议改用 EXISTS 子句");
			return true;
		}
		return false;
	}

	return expression_tree_walker(node, find_in_sublink_walker, context);
}

static void
check_in_sublink(Query *query)
{
	query_tree_walker(query, find_in_sublink_walker, NULL, 0);
}

/* 多行 INSERT VALUES：建议使用 COPY 批量导入 */
static void
check_multi_row_insert(ParseState *pstate, Query *query)
{
	ListCell   *lc;

	if (query->commandType != CMD_INSERT)
		return;

	foreach(lc, pstate->p_rtable)
	{
		RangeTblEntry *rte = (RangeTblEntry *) lfirst(lc);

		if (rte->rtekind == RTE_VALUES && rte->values_lists != NIL)
		{
			int			nrows = list_length(rte->values_lists);

			if (nrows > 1)
				audit_emit(AUDIT_NOTICE, "dml_batch_copy",
						   "INSERT 语句一次写入 %d 行数据，批量导入建议使用 COPY",
						   nrows);
			return;
		}
	}
}

/* MERGE 分支条件审核：任一 WHEN 分支未限定 condition 时告警（PG15+ 才存在） */
#if PG_VERSION_NUM >= 150000
static void
check_merge(Query *query)
{
	const char *tablename = NULL;
	ListCell   *lc;

	if (query->rtable != NIL && query->resultRelation > 0 &&
		query->resultRelation <= list_length(query->rtable))
	{
		RangeTblEntry *rte = (RangeTblEntry *)
			list_nth(query->rtable, query->resultRelation - 1);

		if (rte->eref != NULL)
			tablename = rte->eref->aliasname;
	}

	foreach(lc, query->mergeActionList)
	{
		MergeAction *ma = (MergeAction *) lfirst(lc);

		if (ma->qual == NULL)
		{
			audit_emit(AUDIT_WARNING, "dml_merge_check",
					   "MERGE 存在未限定 WHEN 条件的分支, 请仔细评估影响行范围");
			break;
		}
	}

	audit_record_write("MERGE", tablename, "dml_merge_check", AUDIT_NOTICE);
}
#endif							/* PG_VERSION_NUM >= 150000 */

static void
check_dml(ParseState *pstate, Query *query)
{
	const char *stmt_type = "DML";

	switch (query->commandType)
	{
		case CMD_SELECT:
			stmt_type = "SELECT";
			break;
		case CMD_INSERT:
			stmt_type = "INSERT";
			break;
		case CMD_UPDATE:
			stmt_type = "UPDATE";
			break;
		case CMD_DELETE:
			stmt_type = "DELETE";
			break;
#if PG_VERSION_NUM >= 150000
		case CMD_MERGE:
			stmt_type = "MERGE";
			check_merge(query);
			break;
#endif
		default:
			return;
	}

	ereport(DEBUG1,
			(errmsg("SQL审核: 数据操作语句，类型 = %s", stmt_type)));

	/* 无 WHERE 修改告警（UPDATE/DELETE） */
	if (query->commandType == CMD_UPDATE || query->commandType == CMD_DELETE)
	{
		FromExpr   *jtnode;

		if (query->jointree != NULL && IsA(query->jointree, FromExpr))
		{
			jtnode = (FromExpr *) query->jointree;

			if (jtnode->quals == NULL)
			{
				if (query->commandType == CMD_DELETE)
					audit_emit(AUDIT_ERROR, "dml_no_where",
							   "DELETE 语句缺少 WHERE 条件，清空表请使用 TRUNCATE");
				else
					audit_emit(AUDIT_WARNING, "dml_no_where",
							   "UPDATE 语句缺少 WHERE 条件，将影响全部行");
			}
		}
	}

	/* count(列) 检查 */
	if (query->commandType == CMD_SELECT && query->targetList != NIL)
		check_count_star(query);

	/* 左模糊查询检查 */
	check_left_fuzzy(query);

	/* IN (子查询) 建议用 EXISTS */
	if (query->commandType == CMD_SELECT || query->commandType == CMD_UPDATE ||
		query->commandType == CMD_DELETE)
		check_in_sublink(query);

	/* 多行 INSERT 建议用 COPY */
	check_multi_row_insert(pstate, query);

	audit_record_write(stmt_type, NULL, "dml", AUDIT_NOTICE);
}

static void
se_post_parse_analyze(ParseState *pstate, Query *query
#if PG_VERSION_NUM >= 140000
					  , JumbleState *jstate
#endif
)
{
	/* 仅审核顶层查询 */
	if (pgsql_audit_engine_enabled && pgsql_audit_engine_check_dml &&
		pstate != NULL && pstate->parentParseState == NULL &&
		query != NULL)
	{
		check_dml(pstate, query);
	}

	if (prev_post_parse_analyze_hook)
		prev_post_parse_analyze_hook SE_ANALYZE_HOOK_PARAMS(pstate, query, jstate);
}

void
se_install_analyze_hook(void)
{
	prev_post_parse_analyze_hook = post_parse_analyze_hook;
	post_parse_analyze_hook = se_post_parse_analyze;
}

void
se_uninstall_analyze_hook(void)
{
	post_parse_analyze_hook = prev_post_parse_analyze_hook;
}