#include "postgres.h"
#include "nodes/parsenodes.h"
#include "common/keywords.h"
#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "parser/parse_type.h"
#include "parser/parse_utilcmd.h"
#include "nodes/pg_list.h"
#include "tcop/utility.h"
#include "utils/guc.h"
#include "utils/elog.h"
#include "utils/syscache.h"
#include "pg_config.h"

#include "ruleparse.h"
#include "tools.h"

static bool isValidChar(const char ch);

// 使用 timestampz 替换 timestamp
void replaceTimestampToTimestamptz(ColumnDef *colDef) 
{
    Oid    typOid;
    int32  atttypmod;

    typenameTypeIdAndMod(NULL, colDef->typeName, &typOid, &atttypmod);
    if ( typOid == 1114 ) 
    {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                    errmsg("SQL审核: 将替換 \"timestatmp\" to \"timestamptz\" ")));
    }
}

// 使用 jsonb 替换 json
void replaceJsonToJsonb(ColumnDef *colDef) 
{
    Oid    typOid;
    int32  atttypmod;

    typenameTypeIdAndMod(NULL, colDef->typeName, &typOid, &atttypmod);
    // Data Type json and jsonb's oid define in src/include/catalog/pg_type.h file
#if PG_VERSION_NUM == 100001
#endif
    if ( typOid == 114 ) 
    {

        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                    errmsg("SQL审核: 将替换 \"json\" to \"jsonb\"")));
    }
}

// 定义有效名称规则检查
bool isValidName(const char *objName) 
{
    int i = 1;

    if ( objName == NULL || strlen(objName) <= 0 )
    {
        return false;
    }

    if ( objName[0] >= '0' && objName[0] <= '9' )
    {
        return false;
    }

    while ( objName[i] ) 
    {
        if ( !isValidChar(objName[i]) ) 
        {
            return false;
        }
        ++i;
    }

    return true;
}

static bool isValidChar(const char ch) 
{
    if (  (ch >= 'a' && ch <= 'z')
        ||
          ch == '_'
        ||
          (ch >= '0' && ch <= '9')
    ) 
    {

        return true;
    }

    return false;
}

//自定义检查规则
void checkRule(CreateStmt *stmt) 
{
    ListCell        *listptr;
    char            *typname = NULL;
    Form_pg_type    typeForm;
    HeapTuple       tuple;
    Oid             atttypid;
    int32           atttypmod;
    ConstrList      constrList;

    if ( !stmt && !(stmt->relation)) 
    {
        return ;
    }

    // check table name wether contains PostgreSQL keywords or not
    if ( isKeyword(stmt->relation->relname) ) 
    {
        ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                        errmsg("SQL审核: PostgreSQL 关键词 \"%s\" 不可以定义表名",
                                stmt->relation->relname)));
    }

    // rule:
    if ( !isValidName(stmt->relation->relname) ) 
    {
        ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                        errmsg("SQL审核: 表名只能由小写字母(a-z)，数字(0-9)，下划线(_) 构成")));
    }

    // check column name whether Contains PostgreSQL keywords or not
    foreach(listptr, stmt->tableElts)
    {
        ColumnDef *colDef = lfirst(listptr);

        initConstrList( &constrList );

        // rule: PostgreSQL keywords can't be as  column name
        if ( isKeyword(colDef->colname) ) 
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                        errmsg("SQL审核: PostgreSQL 关键词 \"%s\" 不能是列名",
                               colDef->colname)));
        }

        if ( !isValidName(colDef->colname) ) 
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                        errmsg("SQL审核: 列名只能由 小写字母(a-z)，数字(0-9)，下划线(_) 构成")));
        }

        typenameTypeIdAndMod(NULL, colDef->typeName, &atttypid, &atttypmod);

        // rule: 建议用 timestamptz 替代 timestamp
        replaceTimestampToTimestamptz(colDef);

        // rule: 建议用 jsonb 替代 json
        replaceJsonToJsonb(colDef);

        // 获取 type name
        tuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(atttypid));
        if ( HeapTupleIsValid(tuple) ) 
        {
            typeForm = (Form_pg_type) GETSTRUCT(tuple);
            typname = typeForm->typname.data;
        }
        ReleaseSysCache(tuple);

        // rule: 必须有一个名为 "id" 的 primary key
        getConstrList( &constrList, colDef->constraints );
        if ( !strcmp(colDef->colname, "id") && !constrList.is_primary_key ) 
        {
            // 名字为 "id" ，但不为主键
            ereport(ERROR,
                        (errcode(ERRCODE_INTERNAL_ERROR),
                            errmsg("SQL审核: \"id\" 建议 id 以序列类型作为主键")));
        }
        else if ( strcmp(colDef->colname, "id") && constrList.is_primary_key ) 
        {
            // 名字不为 "id" ，但是为主键
            ereport(ERROR,
                        (errcode(ERRCODE_INTERNAL_ERROR),
                            errmsg("SQL审核: 名字为 \"id\" 但并不是主键约束")));
        }
        else if ( !strcmp(colDef->colname, "id")
                &&
                  constrList.is_primary_key
                &&
                  (
                   strcmp("int2", typname) &&
                   strcmp("int4", typname) &&
                   strcmp("int8", typname)
                  )
                ) 
        {

            ereport(ERROR,
                        (errcode(ERRCODE_INTERNAL_ERROR),
                            errmsg("SQL审核: 主键类型建议为序列类型")));
        }
    }
}

//检查数据库对象名称是否含有关键字
void checkDBObjName(const char *name, NodeTag nodeTag) 
{
    AuditErrCode auditerrcode = AUDIT_OK;
    char     mymsg[MYMSG_SIZE] = { 0 };

    // 检查对象名称是否包含 PostgreSQL 关键字
    if ( isKeyword(name) ) 
    {
        auditerrcode = AUDIT_IS_KEYWORD;
    }
    // rule:
    else if ( !isValidName(name) ) 
    {
        auditerrcode = AUDIT_INVALID_CHAR;
    }

    if ( auditerrcode != AUDIT_OK ) 
    {
        switch ( auditerrcode ) 
        {
            case AUDIT_IS_KEYWORD:
                snprintf(mymsg,
                         MYMSG_SIZE,
                         "%s \"%s\" %s ",
                         "\b\b\b\b\b\b\b\bSQL审核: 表名中含有 PostgreSQL 关键字",
                         name,
                         "不能做");

                switch ( nodeTag ) 
                {
                        /* CREATE TABLESPACE */
                        case T_CreateTableSpaceStmt:
                            strncat(mymsg, "表空间名称", MYMSG_SIZE-strlen(mymsg)-1);
                            break;

                        /* CREATE DATABASE */
                        case T_CreatedbStmt:
                            strncat(mymsg, "数据库名称", MYMSG_SIZE-strlen(mymsg)-1);
                            break;

                        /* CREATE SCHEMA */
                        case T_CreateSchemaStmt:
                            strncat(mymsg, "模式名称", MYMSG_SIZE-strlen(mymsg)-1);
                            break;

                        /* CREATE INDEX */
                        case T_IndexStmt:
                            strncat(mymsg, "索引名称", MYMSG_SIZE-strlen(mymsg)-1);
                            break;

                        /* CREATE VIEW */
                        case T_ViewStmt:
                            strncat(mymsg, "视图名称", MYMSG_SIZE-strlen(mymsg)-1);
                            break;

                        default:
                            strncat(mymsg, "数据库对象名称", MYMSG_SIZE-strlen(mymsg)-1);
                            break;
                }

                break;

            case AUDIT_INVALID_CHAR:
                snprintf(mymsg,
                         MYMSG_SIZE,
                         "%s%s",
                         "\b\b\b\b\b\b\b\b",
                         "SQL审核: 表名只能由 小写字母(a-z)，数字(0-9)，下划线(_) 构成");
                break;

            default:
                snprintf(mymsg,
                         MYMSG_SIZE,
                         "\b\b\b\b\b\b\b\bSQL审核: 无效的数据库对象名称 ");
                break;
        }

        ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                        errmsg("%s", mymsg)));
    }
}

/************************************************************** 
*getCreateName - 从 Create Stmt 结构体中取出所创建对象的名字*
***************************************************************/
const char *getCreateName(Node *parsetree, NodeTag stmtTag) 
{
    const char *unknown = "Unknown";

    switch ( stmtTag ) 
    {
        /* Tablespace */
        case T_CreateTableSpaceStmt:
            return ((CreateTableSpaceStmt *)parsetree)->tablespacename;
            break;

        /* Database */
        case T_CreatedbStmt:
            return ((CreatedbStmt *)parsetree)->dbname;
            break;

        /* Schema */
        case T_CreateSchemaStmt:
            return ((CreateSchemaStmt *)parsetree)->schemaname;
            break;
        /* View */
        case T_ViewStmt:
            return ((ViewStmt *)parsetree)->view->relname;
            break;

        /* Index */
        case T_IndexStmt:
            return ((IndexStmt *)parsetree)->idxname;
            break;
        /* Other Unknown */
        default:
            return unknown;
            break;
    }
}
