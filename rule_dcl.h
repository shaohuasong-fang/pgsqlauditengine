#ifndef PGSQLAUDITENGINE_RULE_DCL_H
#define PGSQLAUDITENGINE_RULE_DCL_H

#include "pgsqlauditengine.h"

void audit_dcl_grant(GrantStmt *stmt);
void audit_dcl_grant_role(GrantRoleStmt *stmt);
void audit_dcl_default_privileges(AlterDefaultPrivilegesStmt *stmt);
void audit_dcl_set_role(VariableSetStmt *stmt);

#endif							/* PGSQLAUDITENGINE_RULE_DCL_H */