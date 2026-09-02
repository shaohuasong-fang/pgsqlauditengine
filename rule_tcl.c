/* -------------------------------------------------------------------------
 *
 * rule_tcl.c
 *
 * 事务控制（TCL）审核：BEGIN/START/COMMIT/ROLLBACK、SAVEPOINT/RELEASE/
 * ROLLBACK TO、PREPARE TRANSACTION 等，以及显式事务内多条 DDL 告警。
 *
 * 说明：显式事务始终在单个 backend 内执行，故使用进程内静态状态机。
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "pgsqlauditengine.h"
#include "rule_tcl.h"

/* 进程内事务状态机 */
static bool	in_explicit_txn = false;
static int	txn_ddl_count = 0;

void
audit_tcl_note_ddl(void)
{
	if (in_explicit_txn)
		txn_ddl_count++;
}

static const char *
txn_kind_str(TransactionStmtKind kind)
{
	switch (kind)
	{
		case TRANS_STMT_BEGIN:
		case TRANS_STMT_START:
			return "BEGIN/START TRANSACTION";
		case TRANS_STMT_COMMIT:
			return "COMMIT";
		case TRANS_STMT_ROLLBACK:
			return "ROLLBACK";
		case TRANS_STMT_SAVEPOINT:
			return "SAVEPOINT";
		case TRANS_STMT_RELEASE:
			return "RELEASE SAVEPOINT";
		case TRANS_STMT_ROLLBACK_TO:
			return "ROLLBACK TO SAVEPOINT";
		case TRANS_STMT_PREPARE:
			return "PREPARE TRANSACTION";
		case TRANS_STMT_COMMIT_PREPARED:
			return "COMMIT PREPARED";
		case TRANS_STMT_ROLLBACK_PREPARED:
			return "ROLLBACK PREPARED";
		default:
			return "?";
	}
}

void
audit_tcl_transaction(TransactionStmt *stmt)
{
	if (stmt == NULL)
		return;

	switch (stmt->kind)
	{
		case TRANS_STMT_BEGIN:
		case TRANS_STMT_START:
			audit_emit(AUDIT_NOTICE, "tcl_begin",
					   "显式事务开启(%s)识别回显",
					   txn_kind_str(stmt->kind));
			in_explicit_txn = true;
			txn_ddl_count = 0;
			break;

		case TRANS_STMT_COMMIT:
			audit_emit(AUDIT_NOTICE, "tcl_commit",
					   "显式事务提交(%s)识别回显",
					   txn_kind_str(stmt->kind));
			if (in_explicit_txn && txn_ddl_count > 1)
			{
				audit_emit(AUDIT_WARNING, "tcl_multi_ddl_in_txn",
						   "显式事务中执行了 %d 条 DDL，禁止在事务中执行多条 DDL",
						   txn_ddl_count);
			}
			in_explicit_txn = false;
			txn_ddl_count = 0;
			break;

		case TRANS_STMT_ROLLBACK:
			audit_emit(AUDIT_NOTICE, "tcl_rollback",
					   "显式事务回滚(%s)识别回显",
					   txn_kind_str(stmt->kind));
			if (in_explicit_txn && txn_ddl_count > 1)
			{
				audit_emit(AUDIT_WARNING, "tcl_multi_ddl_in_txn",
						   "显式事务中执行了 %d 条 DDL，禁止在事务中执行多条 DDL",
						   txn_ddl_count);
			}
			in_explicit_txn = false;
			txn_ddl_count = 0;
			break;

		case TRANS_STMT_SAVEPOINT:
		case TRANS_STMT_RELEASE:
		case TRANS_STMT_ROLLBACK_TO:
			audit_emit(AUDIT_NOTICE, "tcl_savepoint",
					   "保存点操作(%s)识别回显%s%s",
					   txn_kind_str(stmt->kind),
					   stmt->savepoint_name ? " (保存点: " : "",
					   stmt->savepoint_name ? stmt->savepoint_name : "");
			break;

		case TRANS_STMT_PREPARE:
		case TRANS_STMT_COMMIT_PREPARED:
		case TRANS_STMT_ROLLBACK_PREPARED:
			audit_emit(AUDIT_WARNING, "tcl_prepared",
					   "两阶段事务(%s)需评估分布式一致性%s%s",
					   txn_kind_str(stmt->kind),
					   stmt->savepoint_name ? " (标识: " : "",
					   stmt->savepoint_name ? stmt->savepoint_name : "");
			break;

		default:
			break;
	}

	audit_record_write("TCL", txn_kind_str(stmt->kind), "tcl", AUDIT_NOTICE);
}

/*
 * 事务特征设置：SET TRANSACTION（VariableSetStmt kind==VAR_SET_MULTI）
 * / SET CONSTRAINTS（ConstraintsSetStmt）
 */
void
audit_tcl_set_transaction(Node *stmt)
{
	if (stmt == NULL)
		return;

	switch (nodeTag(stmt))
	{
		case T_VariableSetStmt:
			audit_emit(AUDIT_NOTICE, "tcl_set_transaction",
					   "SET TRANSACTION 事务特征识别回显");
			break;

		case T_ConstraintsSetStmt:
			audit_emit(AUDIT_NOTICE, "tcl_set_transaction",
					   "SET CONSTRAINTS 约束检查时机识别回显 (deferred: %s)",
					   ((ConstraintsSetStmt *) stmt)->deferred ? "true" : "false");
			break;

		default:
			break;
	}
}