#ifndef PGSQLAUDITENGINE_RULE_TCL_H
#define PGSQLAUDITENGINE_RULE_TCL_H

#include "pgsqlauditengine.h"

void audit_tcl_transaction(TransactionStmt *stmt);

/* 在事务内执行 DDL 时调用，用于显式事务内多条 DDL 计数 */
void audit_tcl_note_ddl(void);

/* 事务特征设置：SET TRANSACTION / SET CONSTRAINTS */
void audit_tcl_set_transaction(Node *stmt);

#endif							/* PGSQLAUDITENGINE_RULE_TCL_H */