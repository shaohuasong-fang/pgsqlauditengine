#include "postgres.h"
#include "common/keywords.h"
#include "catalog/namespace.h"
#include "catalog/pg_type.h"    
#include "access/htup_details.h"
#include "parser/parse_type.h"  
#include "utils/syscache.h"     
#include "utils/elog.h"         
#include "nodes/plannodes.h"
#include "nodes/nodes.h"
#include "nodes/parsenodes.h"

#include "tools.h"

//check keyword
int isKeyword(const char *str) 
{
    const ScanKeyword *scanKeywords = NULL;

    if ( str ) 
	{
        scanKeywords = ScanKeywordLookup(str,
                                            ScanKeywords,
                                            NumScanKeywords);

        if ( scanKeywords ) 
		{
            return 1;
        }
    }

    return 0;
}

//initial contraint list 
void initConstrList(ConstrList *clist) 
{
    clist->is_primary_key = false;
    clist->is_unique   = false;
    clist->is_not_null = false;
    clist->has_default = false;
    clist->default_str = NULL;
}

//get constraint list
void getConstrList(ConstrList *cListStruct, List *cons) 
{
    ListCell *clist;

    foreach(clist, cons)
    {
        Constraint *con = (Constraint *) lfirst(clist);

        switch ( con->contype ) 
		{
            case CONSTR_PRIMARY:
                cListStruct->is_primary_key = true;
                break;
            case CONSTR_UNIQUE:
                cListStruct->is_unique = true;
                break;
            case CONSTR_NOTNULL:
                cListStruct->is_not_null = true;
                break;
            case CONSTR_DEFAULT:
                cListStruct->has_default = true;
                break;
            default:
                break;
        }
    }
}
//get column define
ColInfo getColName (ColumnDef *colDef) 
{
    Oid            atttypid;
    int32          atttypmod;
    HeapTuple      tuple;
    Form_pg_type   typForm;
    const char    *typName = NULL;
    ColInfo        colInfo;

    typenameTypeIdAndMod(NULL, colDef->typeName, &atttypid, &atttypmod);
    colInfo.atttypid = atttypid;
    colInfo.atttypmod = atttypmod;

    tuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(atttypid));
    if ( HeapTupleIsValid(tuple) ) 
	{
        typForm = (Form_pg_type) GETSTRUCT(tuple);
        colInfo.atttypname = typName = typForm->typname.data;
    }
    ReleaseSysCache(tuple);

    return colInfo;
}

// audit finish output
void finishAudit() 
{
    char mymsg[512] = { 0 };

    snprintf(mymsg, 512, "\b\b\b\b\b\b\b\b%s",
             "SQL审核:  审核完成");

    ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                    errmsg("%s", mymsg)));
}

void disp_VariableSetStmt(VariableSetStmt *stmt, char *mymsg) 
{
    snprintf(mymsg,
             MYMSG_SIZE,
             "%s %s, is_local = %s",
             "\b\b\b\b\b\b\b\b\b SQL审核: SET",
             stmt->name,
             stmt->is_local ? "True" : "False");

    ereport(NOTICE,
            (errcode(ERRCODE_SUCCESSFUL_COMPLETION),
             errmsg("%s", mymsg)));
}

/* dispStmt - 打印出关注的 Stmt 的信息
 *
 * PlannedStmt 结构体: src/include/nodes/plannodes.h
 * Node 结构体:        src/include/nodes/nodes.h
 * nodeTag 函数:       src/include/nodes/parsenodes.h
 * T_CreateStmt 常量:  src/include/nodes/nodes.h
 * CreatedbStmt 常量:  src/include/nodes/parsenodes.h\
 *
 */

void dispStmt(PlannedStmt *pstmt) 
{
    Node *parsetree = pstmt->utilityStmt;
    char  mymsg[MYMSG_SIZE] = { 0 };

    switch ( nodeTag(parsetree) ) 
	{
        case T_CreatedbStmt:
            snprintf(mymsg, MYMSG_SIZE, "%s\"%s\"",
                    "\b\b\b\b\b\b\b\b\b 审核数据库名称 -> 数据库名称=",
                    ((CreatedbStmt *)parsetree)->dbname);
            ereport(NOTICE,
                        (errcode(ERRCODE_SUCCESSFUL_COMPLETION),
                            errmsg("%s", mymsg)));
            break;

        case T_CreateSchemaStmt:
            snprintf(mymsg, MYMSG_SIZE, "%s\"%s\"",
                    "\b\b\b\b\b\b\b\b\b 审核对象名称 -> 对象名称=",
                    ((CreateSchemaStmt *)parsetree)->schemaname);
            ereport(NOTICE,
                        (errcode(ERRCODE_SUCCESSFUL_COMPLETION),
                            errmsg("%s", mymsg)));
            break;

        case T_CreateStmt:
            dispCreateStmt((CreateStmt *)parsetree);
            break;

        case T_VariableSetStmt:
            disp_VariableSetStmt((VariableSetStmt *)parsetree, mymsg);
            break;

        case T_DeleteStmt:
            snprintf(mymsg, MYMSG_SIZE,
                     "\b\b\b\b\b\b\b\b\b 审核删除语句 -> 删除语句:");
            ereport(NOTICE,
                (errcode(ERRCODE_SUCCESSFUL_COMPLETION),
                    errmsg("%s", mymsg)));
            break;

        default:
            break;
    }
}

/*
 *
 * CreateStmt struct:
 * ListCell struct:
 * Form_pg_type struct:
 * HeapTuple struct:
 * List struct:
 * ereport func:
 * NOTICE macro:
 * errcode func:
 * errmsg func:
 * ERRCODE_SUCCESSFUL_COMPLETION macro:
 * ColumnDef struct:
 * Oid type:
 * int32 type:
 * typenameTypeIdAndMod func:
 * SearchSysCache1 func:
 * TYPEOID macro:
 * ObjectIdGetDatum func:
 * HeapTupleIsValid func:
 * GETSTRUCT macro:
 * ReleaseSysCache func:
 * NameListToString func:
 * FuncCall struct:
 * TypeCase struct:
 */
void dispCreateStmt(CreateStmt *stmt) 
{
    ListCell    *listptr;
    ListCell    *lc;
    char        *typname = NULL;
    Form_pg_type typeForm;
    HeapTuple    tuple;
    char         msg[1024] = { 0 };
    char         default_info[512] = { 0 };
    int          msg_pos = 0;
    int          msgNBytes = 0;
    ConstrList   constrList;
    List        *argList = NULL;

    if ( !stmt && !(stmt->relation)) 
	{
        return ;
    }

    if ( stmt->relation->relname ) 
	{
        snprintf(msg, 512, "详细信息:表名 = \"%s\"", stmt->relation->relname);

        ereport(NOTICE,
                    (errcode(ERRCODE_SUCCESSFUL_COMPLETION),
                        errmsg("%s", msg)));
    }

    foreach(listptr, stmt->tableElts)
    {
        ColumnDef *colDef = lfirst(listptr);

        Oid atttypid;
        int32 atttypmod;
        typenameTypeIdAndMod(NULL, colDef->typeName, &atttypid, &atttypmod);

        tuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(atttypid));
        if ( HeapTupleIsValid(tuple) ) 
		{
            typeForm = (Form_pg_type) GETSTRUCT(tuple);
            typname = typeForm->typname.data;
        }
        ReleaseSysCache(tuple);

        // 这个循环尝试从 colDef 中取出 column 的约束
        initConstrList( &constrList );
        if ( colDef->constraints ) 
		{
            getConstrList( &constrList, colDef->constraints );
        }

        if ( constrList.has_default ) 
		{
            snprintf(default_info, 512, "DEFAULT");
            if ( colDef->raw_default != NULL ) 
			{
                snprintf(default_info + 7,
                         505,
                         "(raw, %s)",
                         NameListToString(((FuncCall *)colDef->raw_default)->funcname)
                        );
            }

            argList = ((FuncCall *)colDef->raw_default)->args;
            foreach(lc, argList)
            {
                Node *aNode = lfirst(lc);
                snprintf(default_info + strlen(default_info),
                         512 - strlen(default_info),
                         "args %d typ %d",
                         nodeTag(aNode),
                         nodeTag(((TypeCast *)aNode)->arg)
                        );
            }
        }

        msgNBytes = snprintf(msg + msg_pos,
                                1024 - msg_pos,
                                "列名 \"%s\", 数据类型 \"%s\", 类型OID %d %s %s %s %s\n",
                                colDef->colname,
                                typname == NULL ? "unkown" : typname,
                                atttypid,
                                constrList.is_primary_key ? "PRIMARY KEY" : "",
                                constrList.is_unique ? "UNIQUE" : "",
                                constrList.is_not_null ? "NOT NULL" : "",
                                constrList.has_default ? default_info : "");

        msg_pos += msgNBytes;

    }

    ereport(NOTICE,
                (errcode(ERRCODE_SUCCESSFUL_COMPLETION),
                    errmsg("%s", msg)));
}
