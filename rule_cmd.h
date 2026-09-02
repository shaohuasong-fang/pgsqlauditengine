/* -------------------------------------------------------------------------
 *
 * rule_cmd.h
 *
 * 维护/性能/数据操作类命令审核（spec 5.11 cmd_* 14 条）。
 *
 * -------------------------------------------------------------------------
 */
#ifndef PGSQLAUDITENGINE_RULE_CMD_H
#define PGSQLAUDITENGINE_RULE_CMD_H

#include "nodes/parsenodes.h"

void audit_cmd_vacuum(VacuumStmt *stmt);
void audit_cmd_analyze(VacuumStmt *stmt);
void audit_cmd_checkpoint(CheckPointStmt *stmt);
void audit_cmd_cluster(ClusterStmt *stmt);
void audit_cmd_reindex(ReindexStmt *stmt);
void audit_cmd_refresh_matview(RefreshMatViewStmt *stmt);
void audit_cmd_lock(LockStmt *stmt);
void audit_cmd_load(LoadStmt *stmt);
void audit_cmd_discard(DiscardStmt *stmt);
void audit_cmd_explain(ExplainStmt *stmt);
void audit_cmd_copy(CopyStmt *stmt);
void audit_cmd_prepare(Node *stmt);
void audit_cmd_cursor(Node *stmt);
void audit_cmd_notify(Node *stmt);
void audit_cmd_call(CallStmt *stmt);
void audit_cmd_set(VariableSetStmt *stmt);
void audit_cmd_show(VariableShowStmt *stmt);

#endif							/* PGSQLAUDITENGINE_RULE_CMD_H */