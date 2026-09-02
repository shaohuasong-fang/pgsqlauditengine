#ifndef PGSQLAUDITENGINE_RULE_PROGRAM_H
#define PGSQLAUDITENGINE_RULE_PROGRAM_H

#include "pgsqlauditengine.h"

void audit_program_function(CreateFunctionStmt *stmt);
void audit_program_trigger(CreateTrigStmt *stmt);
void audit_program_event_trigger(CreateEventTrigStmt *stmt);
void audit_program_rule(RuleStmt *stmt);
void audit_program_do(DoStmt *stmt);
void audit_program_alter_function(AlterFunctionStmt *stmt);
void audit_program_alter_event_trigger(AlterEventTrigStmt *stmt);

#endif							/* PGSQLAUDITENGINE_RULE_PROGRAM_H */