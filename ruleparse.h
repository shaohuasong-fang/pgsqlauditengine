#ifndef _Sungsasong_PGSQL_Audit_H
#define _Sungsasong_PGSQL_Audit_H
#endif // _Sungsasong_PGSQL_Audit_H
#include "postgres.h"
#include "catalog/pg_type.h"

typedef enum 
{
    AUDIT_OK = 0,
    AUDIT_IS_KEYWORD,
    AUDIT_INVALID_CHAR
} AuditErrCode;

bool isValidName(const char *objName);
void replaceTimestampToTimestamptz(ColumnDef *colDef);
void replaceJsonToJsonb(ColumnDef *colDef);
void checkRule(CreateStmt *stmt);
const char *getCreateName(Node *parsetree, NodeTag stmtTag);
void checkDBObjName(const char *name, NodeTag nodeTag);


