#ifndef _Sungsasong_SQL_Audit_TOOLS_H
#define _Sungsasong_SQL_Audit_TOOLS_H

#include "postgres.h"

#define MYMSG_SIZE 256

typedef struct ConstrList 
{
    bool   is_not_null;
    bool   is_primary_key;
    bool   is_unique;
    bool   has_default;
    char  *default_str;
} ConstrList;

typedef struct colInfo 
{
    Oid          atttypid;
    int32        atttypmod;
    const char  *atttypname;
} ColInfo;

ColInfo  getColName(ColumnDef *colDef);
void     initConstrList(ConstrList *clist);
void     getConstrList(ConstrList *cListStruct, List *cons);
void     finishAudit(void);
void     disp_VariableSetStmt(VariableSetStmt *stmt, char *mymsg);
void     dispCreateStmt(CreateStmt *stmt);
void     dispStmt(PlannedStmt *pstmt);
int      isKeyword(const char *str);

#endif
