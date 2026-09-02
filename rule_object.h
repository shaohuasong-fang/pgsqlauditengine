/* -------------------------------------------------------------------------
 *
 * rule_object.h
 *
 * 系统级/对象类命令审核（spec 5.11 obj_* 12 条）。
 *
 * -------------------------------------------------------------------------
 */
#ifndef PGSQLAUDITENGINE_RULE_OBJECT_H
#define PGSQLAUDITENGINE_RULE_OBJECT_H

#include "nodes/parsenodes.h"

void audit_obj_policy(Node *stmt);
void audit_obj_publication(Node *stmt);
void audit_obj_subscription(Node *stmt);
void audit_obj_fdw(Node *stmt);
void audit_obj_extension(Node *stmt);
void audit_obj_language(Node *stmt);
void audit_obj_advanced(Node *stmt);
void audit_obj_alter_system(AlterSystemStmt *stmt);
void audit_obj_reassign_owned(ReassignOwnedStmt *stmt);
void audit_obj_ctas(CreateTableAsStmt *stmt);
void audit_obj_matview(Node *stmt);
void audit_obj_security_label(SecLabelStmt *stmt);

#endif							/* PGSQLAUDITENGINE_RULE_OBJECT_H */