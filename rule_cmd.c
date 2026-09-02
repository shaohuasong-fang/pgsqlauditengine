/* -------------------------------------------------------------------------
 *
 * rule_cmd.c
 *
 * 维护/性能/数据操作类命令审核（spec 5.11 cmd_* 14 条）。
 * 全部为非阻断级别（WARNING/NOTICE），由 hook_utility.c 对应 case 调用。
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "nodes/parsenodes.h"

#include "compat.h"
#include "pgsqlauditengine.h"
#include "rule_cmd.h"

/*
 * VACUUM / VACUUM FULL / VACUUM ANALYZE
 * 注意：PG12+ 中 VacuumStmt->options 为 DefElem 列表，PG11 为 int 位掩码。
 * 选项检测统一走 compat.h 的 se_vacopt_present/se_vacopt_bit。
 */
void
audit_cmd_vacuum(VacuumStmt *stmt)
{
	const char *relname = NULL;

	if (stmt == NULL)
		return;

	if (stmt->rels != NIL &&
		IsA(linitial(stmt->rels), VacuumRelation))
	{
		VacuumRelation *vr = (VacuumRelation *) linitial(stmt->rels);

		if (vr->relation != NULL)
			relname = vr->relation->relname;
	}

	if (SE_VACOPT_PRESENT(stmt, "full"))
		audit_emit(AUDIT_WARNING, "cmd_vacuum",
				   "VACUUM FULL 获取排他锁并重写表, 生产环境需谨慎 (目标表: %s)",
				   relname ? relname : "?");
	else
		audit_emit(AUDIT_WARNING, "cmd_vacuum",
				   "VACUUM 操作回显 (目标表: %s)",
				   relname ? relname : "?");
}

/*
 * ANALYZE
 */
void
audit_cmd_analyze(VacuumStmt *stmt)
{
	const char *relname = NULL;

	if (stmt == NULL)
		return;

	if (stmt->rels != NIL &&
		IsA(linitial(stmt->rels), VacuumRelation))
	{
		VacuumRelation *vr = (VacuumRelation *) linitial(stmt->rels);

		if (vr->relation != NULL)
			relname = vr->relation->relname;
	}

	audit_emit(AUDIT_NOTICE, "cmd_analyze",
			   "ANALYZE 统计信息采集应在业务低峰期进行 (目标表: %s)",
			   relname ? relname : "?");
}

/*
 * CHECKPOINT
 */
void
audit_cmd_checkpoint(CheckPointStmt *stmt)
{
	if (stmt == NULL)
		return;

	audit_emit(AUDIT_NOTICE, "cmd_checkpoint",
			   "手动 CHECKPOINT 仅建议排障场景使用");
}

/*
 * CLUSTER
 */
void
audit_cmd_cluster(ClusterStmt *stmt)
{
	if (stmt == NULL)
		return;

	audit_emit(AUDIT_WARNING, "cmd_cluster",
			   "CLUSTER 需排他锁并重写表, 生产需谨慎 (目标表: %s)",
			   stmt->relation ? stmt->relation->relname : "?");
}

/*
 * REINDEX：未带 CONCURRENTLY 时告警
 * （PG14+ params 为 DefElem 列表 / PG12-13 为 concurrent 字段 / PG11 不支持）
 */
void
audit_cmd_reindex(ReindexStmt *stmt)
{
	const char *objname;

	if (stmt == NULL)
		return;

	objname = (stmt->relation != NULL) ? stmt->relation->relname : stmt->name;

	if (!SE_REINDEX_CONCURRENT(stmt))
		audit_emit(AUDIT_WARNING, "cmd_reindex",
				   "REINDEX 建议使用 CONCURRENTLY 以避免长时间阻塞 (目标: %s)",
				   objname ? objname : "?");
	else
		audit_emit(AUDIT_WARNING, "cmd_reindex",
				   "REINDEX 操作回显 (目标: %s, 使用 CONCURRENTLY)",
				   objname ? objname : "?");
}

/*
 * REFRESH MATERIALIZED VIEW：未带 CONCURRENTLY 时告警
 */
void
audit_cmd_refresh_matview(RefreshMatViewStmt *stmt)
{
	if (stmt == NULL)
		return;

	if (!stmt->concurrent)
		audit_emit(AUDIT_WARNING, "cmd_refresh_matview",
				   "REFRESH MATERIALIZED VIEW 建议使用 CONCURRENTLY 以避免长时间阻塞 (目标: %s)",
				   stmt->relation ? stmt->relation->relname : "?");
	else
		audit_emit(AUDIT_WARNING, "cmd_refresh_matview",
				   "REFRESH MATERIALIZED VIEW 操作回显 (目标: %s, 使用 CONCURRENTLY)",
				   stmt->relation ? stmt->relation->relname : "?");
}

/*
 * LOCK TABLE：回显表名与锁模式
 */
void
audit_cmd_lock(LockStmt *stmt)
{
	ListCell   *lc;
	StringInfoData objlist;

	if (stmt == NULL)
		return;

	initStringInfo(&objlist);
	foreach(lc, stmt->relations)
	{
		RangeVar  *rv = (RangeVar *) lfirst(lc);

		if (objlist.len > 0)
			appendStringInfoString(&objlist, ",");
		appendStringInfoString(&objlist, rv->relname);
	}

	audit_emit(AUDIT_WARNING, "cmd_lock",
			   "LOCK TABLE 需注意锁模式与持锁时长 (表: %s, 模式: %d, nowait: %s)",
			   objlist.data, stmt->mode, stmt->nowait ? "true" : "false");
	pfree(objlist.data);
}

/*
 * LOAD：回显共享库文件名
 */
void
audit_cmd_load(LoadStmt *stmt)
{
	if (stmt == NULL)
		return;

	audit_emit(AUDIT_WARNING, "cmd_load",
			   "LOAD 加载共享库属安全敏感操作 (文件: %s)",
			   stmt->filename ? stmt->filename : "?");
}

/*
 * DISCARD：回显目标
 */
void
audit_cmd_discard(DiscardStmt *stmt)
{
	const char *target = "?";

	if (stmt == NULL)
		return;

	switch (stmt->target)
	{
		case DISCARD_ALL:
			target = "ALL";
			break;
		case DISCARD_PLANS:
			target = "PLANS";
			break;
		case DISCARD_SEQUENCES:
			target = "SEQUENCES";
			break;
		case DISCARD_TEMP:
			target = "TEMP";
			break;
	}

	audit_emit(AUDIT_NOTICE, "cmd_discard",
			   "DISCARD %s 将重置会话状态 (临时表/准备语句/通知监听等)", target);
}

/*
 * EXPLAIN：options 含 analyze 时提示真实执行；不递归审核被分析语句
 */
void
audit_cmd_explain(ExplainStmt *stmt)
{
	ListCell   *lc;
	bool		has_analyze = false;

	if (stmt == NULL)
		return;

	foreach(lc, stmt->options)
	{
		DefElem    *def = (DefElem *) lfirst(lc);

		if (def != NULL && strcmp(def->defname, "analyze") == 0)
		{
			has_analyze = true;
			break;
		}
	}

	if (has_analyze)
		audit_emit(AUDIT_NOTICE, "cmd_explain",
				   "EXPLAIN ANALYZE 将真实执行被分析语句, 生产大查询需谨慎");
	else
		audit_emit(AUDIT_NOTICE, "cmd_explain",
				   "EXPLAIN 语句识别回显");
}

/*
 * COPY FROM / COPY TO：确认数据源/格式/敏感范围
 */
void
audit_cmd_copy(CopyStmt *stmt)
{
	const char *relname;
	const char *src;

	if (stmt == NULL)
		return;

	relname = (stmt->relation != NULL) ? stmt->relation->relname : "(查询)";
	src = (stmt->filename != NULL) ? stmt->filename :
		(stmt->is_program ? "(程序)" : "(stdin/stdout)");

	if (stmt->is_from)
		audit_emit(AUDIT_WARNING, "cmd_copy",
				   "COPY FROM 批量导入需确认数据源与格式 (表: %s, 来源: %s)",
				   relname, src);
	else
		audit_emit(AUDIT_WARNING, "cmd_copy",
				   "COPY TO 批量导出需确认敏感数据范围 (表: %s, 去向: %s)",
				   relname, src);
}

/*
 * PREPARE / EXECUTE / DEALLOCATE：识别回显预编译语句名
 */
void
audit_cmd_prepare(Node *stmt)
{
	const char *name = NULL;
	const char *op = "预编译语句";

	if (stmt == NULL)
		return;

	switch (nodeTag(stmt))
	{
		case T_PrepareStmt:
			name = ((PrepareStmt *) stmt)->name;
			op = "PREPARE";
			break;
		case T_ExecuteStmt:
			name = ((ExecuteStmt *) stmt)->name;
			op = "EXECUTE";
			break;
		case T_DeallocateStmt:
			name = ((DeallocateStmt *) stmt)->name;
			op = "DEALLOCATE";
			break;
		default:
			break;
	}

	audit_emit(AUDIT_NOTICE, "cmd_prepare",
			   "%s 预编译语句识别回显 (名称: %s)",
			   op, name ? name : "?");
}

/*
 * DECLARE CURSOR / FETCH / MOVE / CLOSE：游标使用后必须 CLOSE
 */
void
audit_cmd_cursor(Node *stmt)
{
	const char *name = NULL;

	if (stmt == NULL)
		return;

	switch (nodeTag(stmt))
	{
		case T_DeclareCursorStmt:
			name = ((DeclareCursorStmt *) stmt)->portalname;
			audit_emit(AUDIT_WARNING, "cmd_cursor",
					   "游标使用后必须 CLOSE, 避免长事务持有 (游标: %s)",
					   name ? name : "?");
			return;
		case T_FetchStmt:
			name = ((FetchStmt *) stmt)->portalname;
			audit_emit(AUDIT_WARNING, "cmd_cursor",
					   "FETCH/MOVE 游标操作回显 (游标: %s)",
					   name ? name : "?");
			return;
		case T_ClosePortalStmt:
			name = ((ClosePortalStmt *) stmt)->portalname;
			audit_emit(AUDIT_WARNING, "cmd_cursor",
					   "CLOSE 游标操作回显 (游标: %s)",
					   name ? name : "?");
			return;
		default:
			break;
	}
}

/*
 * LISTEN / NOTIFY / UNLISTEN：通知通道需评估性能开销
 */
void
audit_cmd_notify(Node *stmt)
{
	const char *name = NULL;
	const char *op = "通知通道";

	if (stmt == NULL)
		return;

	switch (nodeTag(stmt))
	{
		case T_ListenStmt:
			name = ((ListenStmt *) stmt)->conditionname;
			op = "LISTEN";
			break;
		case T_NotifyStmt:
			name = ((NotifyStmt *) stmt)->conditionname;
			op = "NOTIFY";
			break;
		case T_UnlistenStmt:
			name = ((UnlistenStmt *) stmt)->conditionname;
			op = "UNLISTEN";
			break;
		default:
			break;
	}

	audit_emit(AUDIT_WARNING, "cmd_notify",
			   "%s 通知通道需评估性能开销与使用范围 (通道: %s)",
			   op, name ? name : "?");
}

/*
 * CALL：存储过程调用（阶段二新增，spec 5.4 需求 23；决策 D15）
 */
void
audit_cmd_call(CallStmt *stmt)
{
	const char *procname = NULL;

	if (stmt == NULL)
		return;

	if (stmt->funccall != NULL && stmt->funccall->funcname != NIL)
		procname = strVal(llast(stmt->funccall->funcname));

	audit_emit(AUDIT_NOTICE, "cmd_call",
			   "存储过程调用(CALL)识别回显 (过程: %s)",
			   procname ? procname : "?");
}

/*
 * SET / RESET（普通变量）：按 kind 区分（阶段二新增；决策 D15）
 * 说明：VAR_SET_MULTI（SET TRANSACTION）→ tcl_set_transaction、
 * role/session_authorization → dcl_set_role 由 hook_utility.c 分派，不进入本函数。
 */
void
audit_cmd_set(VariableSetStmt *stmt)
{
	const char *op;
	const char *name;

	if (stmt == NULL)
		return;

	switch (stmt->kind)
	{
		case VAR_RESET:
			op = "RESET";
			break;
		case VAR_RESET_ALL:
			op = "RESET ALL";
			break;
		case VAR_SET_VALUE:
		case VAR_SET_DEFAULT:
		case VAR_SET_CURRENT:
		default:
			op = "SET";
			break;
	}

	name = stmt->name;
	audit_emit(AUDIT_NOTICE, "cmd_set",
			   "会话参数设置(%s)识别回显 (参数: %s)",
			   op, name ? name : "?");
}

/*
 * SHOW：会话参数查询（阶段二新增；决策 D15）
 */
void
audit_cmd_show(VariableShowStmt *stmt)
{
	if (stmt == NULL)
		return;

	audit_emit(AUDIT_NOTICE, "cmd_show",
			   "会话参数查询(SHOW)识别回显 (参数: %s)",
			   stmt->name ? stmt->name : "?");
}