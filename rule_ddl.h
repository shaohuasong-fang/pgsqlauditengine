#ifndef PGSQLAUDITENGINE_RULE_DDL_H
#define PGSQLAUDITENGINE_RULE_DDL_H

#include "pgsqlauditengine.h"

void audit_ddl_create_table(CreateStmt *stmt);
void audit_ddl_alter_table(AlterTableStmt *stmt);
void audit_ddl_index(IndexStmt *stmt);
void audit_ddl_view(ViewStmt *stmt);
void audit_ddl_matview(CreateTableAsStmt *stmt);
void audit_ddl_create_seq(CreateSeqStmt *stmt);
void audit_ddl_create_domain(CreateDomainStmt *stmt);
void audit_ddl_create_composite(CompositeTypeStmt *stmt);
void audit_ddl_create_enum(CreateEnumStmt *stmt);
void audit_ddl_create_range(CreateRangeStmt *stmt);
void audit_ddl_createdb(CreatedbStmt *stmt);
void audit_ddl_tablespace(CreateTableSpaceStmt *stmt);
void audit_ddl_schema(CreateSchemaStmt *stmt);
void audit_ddl_define(DefineStmt *stmt);
void audit_ddl_create_role(CreateRoleStmt *stmt);
void audit_ddl_alter_role(AlterRoleStmt *stmt);
void audit_ddl_alter_role_set(AlterRoleSetStmt *stmt);
void audit_ddl_alter_seq(AlterSeqStmt *stmt);
void audit_ddl_alter_domain(AlterDomainStmt *stmt);
void audit_ddl_alter_owner(AlterOwnerStmt *stmt);
void audit_ddl_alter_object_schema(AlterObjectSchemaStmt *stmt);
#if PG_VERSION_NUM >= 130000
void audit_ddl_alter_type(AlterTypeStmt *stmt);
#endif
void audit_ddl_alter_enum(AlterEnumStmt *stmt);
void audit_ddl_alter_database(AlterDatabaseStmt *stmt);
void audit_ddl_rename(RenameStmt *stmt);
void audit_ddl_comment(CommentStmt *stmt);

void audit_ddl_drop(DropStmt *stmt);
void audit_ddl_dropdb(DropdbStmt *stmt);
void audit_ddl_droptablespace(DropTableSpaceStmt *stmt);
void audit_ddl_droprole(DropRoleStmt *stmt);
void audit_ddl_dropowned(DropOwnedStmt *stmt);
void audit_ddl_truncate(TruncateStmt *stmt);


#endif							/* PGSQLAUDITENGINE_RULE_DDL_H */