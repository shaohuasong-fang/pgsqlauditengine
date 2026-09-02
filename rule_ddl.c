/* -------------------------------------------------------------------------
 *
 * rule_ddl.c
 *
 * DDL 审核规则：建表/改表/删表、索引、视图、序列、类型/域、数据库/表空间/
 * 模式、ROLE、RENAME/COMMENT/SET、DROP/TRUNCATE 等语句的识别与审核。
 *
 * 说明：本模块基于 ProcessUtility 钩子传入的原始解析树（raw parse tree），
 *       不使用类型 OID 解析，避免 serial 等伪类型无法解析的问题。
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/genam.h"
#include "catalog/index.h"
#include "catalog/pg_class.h"
#include "catalog/pg_statistic.h"
#include "nodes/pg_list.h"
#include "nodes/value.h"
#include "parser/parse_node.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"
#include "compat.h"
#include "pgsqlauditengine.h"
#include "rule_common.h"
#include "rule_ddl.h"

#define AUDIT_PK_PREFIX		"pk_"
#define AUDIT_UK_PREFIX		"uk_"
#define AUDIT_CK_PREFIX		"ck_"
#define AUDIT_IDX_PREFIX	"idx_"
#define AUDIT_VW_PREFIX		"vw_"
#define AUDIT_MV_PREFIX		"mv_"

/* 预留字段特征子串 */
static const char *reserved_col_tokens[] = {
	"reserve1", "reserve2", "reserve3", "reserved",
	"ext_col", "ext_field", "spare", "future", NULL
};

/* 获取 RangeVar 的关系名 */
static const char *
rv_name(RangeVar *rv)
{
	return rv ? rv->relname : NULL;
}

/* 主键列类型名是否属于序列/整数等价类型 */
static bool
pk_type_serial_ok(const char *typename)
{
	static const char *ok_types[] = {
		"serial", "bigserial", "smallserial",
		"int2", "int4", "int8",
		"smallint", "integer", "bigint",
		NULL
	};
	int			i;

	if (typename == NULL)
		return false;

	for (i = 0; ok_types[i] != NULL; i++)
	{
		if (strcmp(typename, ok_types[i]) == 0)
			return true;
	}
	return false;
}

/* 列是否为 NOT NULL（列级 constraints + is_not_null 属性） */
static bool
col_is_not_null(ColumnDef *col)
{
	ListCell   *lc;

	if (col->is_not_null)
		return true;

	foreach(lc, col->constraints)
	{
		Constraint *con = (Constraint *) lfirst(lc);

		if (con->contype == CONSTR_NOTNULL)
			return true;
	}
	return false;
}

/* tableElts 元素是否为表级约束节点（PG16+ 表级约束在 tableElts 中） */
static bool is_table_constraint(Node *n);

/* 列是否属于主键（列级 CONSTR_PRIMARY 或表级 PRIMARY KEY 含该列） */
static bool
col_is_in_pk(CreateStmt *stmt, ColumnDef *col)
{
	ListCell   *lc;

	foreach(lc, stmt->constraints)
	{
		Constraint *con = (Constraint *) lfirst(lc);
		ListCell   *kc;

		if (con->contype == CONSTR_PRIMARY && con->keys != NIL)
		{
			foreach(kc, con->keys)
			{
				if (strcmp(strVal(lfirst(kc)), col->colname) == 0)
					return true;
			}
		}
	}

	foreach(lc, stmt->tableElts)
	{
		Node	   *node = (Node *) lfirst(lc);
		ListCell   *kc;

		if (!is_table_constraint(node))
			continue;

		{
			Constraint *con = (Constraint *) node;

			if (con->contype == CONSTR_PRIMARY && con->keys != NIL)
			{
				foreach(kc, con->keys)
				{
					if (strcmp(strVal(lfirst(kc)), col->colname) == 0)
						return true;
				}
			}
		}
	}

	foreach(lc, col->constraints)
	{
		Constraint *con = (Constraint *) lfirst(lc);

		if (con->contype == CONSTR_PRIMARY)
			return true;
	}
	return false;
}

/* 列名是否具有预留字段特征 */
static bool
is_reserved_column_name(const char *colname)
{
	int			i;

	if (colname == NULL)
		return false;

	for (i = 0; reserved_col_tokens[i] != NULL; i++)
	{
		if (strstr(colname, reserved_col_tokens[i]) != NULL)
			return true;
	}
	return false;
}

/* tableElts 元素是否为表级约束节点（PG16+ 表级约束在 tableElts 中） */
static bool
is_table_constraint(Node *n)
{
	return (n != NULL && nodeTag(n) == T_Constraint);
}

/* 提取 CREATE TABLE 的主键列名（表级/列级 CONSTR_PRIMARY），找不到返回 NULL */
static char *
find_pk_column(CreateStmt *stmt)
{
	ListCell   *lc;

	foreach(lc, stmt->constraints)
	{
		Constraint *con = (Constraint *) lfirst(lc);

		if (con->contype == CONSTR_PRIMARY && con->keys != NIL)
			return strVal(linitial(con->keys));
	}

	foreach(lc, stmt->tableElts)
	{
		Node	   *node = (Node *) lfirst(lc);

		if (is_table_constraint(node))
		{
			Constraint *con = (Constraint *) node;

			if (con->contype == CONSTR_PRIMARY && con->keys != NIL)
				return strVal(linitial(con->keys));
			continue;
		}

		{
			ColumnDef  *col = (ColumnDef *) node;
			ListCell   *c2;

			foreach(c2, col->constraints)
			{
				Constraint *con = (Constraint *) lfirst(c2);

				if (con->contype == CONSTR_PRIMARY)
					return col->colname;
			}
		}
	}
	return NULL;
}

/* 在 CREATE TABLE 各列/约束中查找外键约束，返回 true 表示存在 */
static bool
has_foreign_key(CreateStmt *stmt)
{
	ListCell   *lc;

	foreach(lc, stmt->constraints)
	{
		Constraint *con = (Constraint *) lfirst(lc);

		if (con->contype == CONSTR_FOREIGN)
			return true;
	}

	foreach(lc, stmt->tableElts)
	{
		Node	   *node = (Node *) lfirst(lc);

		if (is_table_constraint(node))
		{
			Constraint *con = (Constraint *) node;

			if (con->contype == CONSTR_FOREIGN)
				return true;
			continue;
		}

		{
			ColumnDef  *col = (ColumnDef *) node;
			ListCell   *c2;

			foreach(c2, col->constraints)
			{
				Constraint *con = (Constraint *) lfirst(c2);

				if (con->contype == CONSTR_FOREIGN)
					return true;
			}
		}
	}
	return false;
}

static void
check_constraint_prefixes(CreateStmt *stmt)
{
	ListCell   *lc;

	foreach(lc, stmt->constraints)
	{
		Constraint *con = (Constraint *) lfirst(lc);

		if (con->conname == NULL)
			continue;

		switch (con->contype)
		{
			case CONSTR_PRIMARY:
				audit_check_prefix(con->conname, AUDIT_PK_PREFIX,
								   "name_pk_prefix", "主键约束");
				break;
			case CONSTR_UNIQUE:
				audit_check_prefix(con->conname, AUDIT_UK_PREFIX,
								   "name_uk_prefix", "唯一约束");
				break;
			case CONSTR_CHECK:
				audit_check_prefix(con->conname, AUDIT_CK_PREFIX,
								   "name_ck_prefix", "检查约束");
				break;
			default:
				break;
		}
	}

	foreach(lc, stmt->tableElts)
	{
		Node	   *node = (Node *) lfirst(lc);

		if (is_table_constraint(node))
		{
			Constraint *con = (Constraint *) node;

			if (con->conname == NULL)
				continue;

			switch (con->contype)
			{
				case CONSTR_PRIMARY:
					audit_check_prefix(con->conname, AUDIT_PK_PREFIX,
									   "name_pk_prefix", "主键约束");
					break;
				case CONSTR_UNIQUE:
					audit_check_prefix(con->conname, AUDIT_UK_PREFIX,
									   "name_uk_prefix", "唯一约束");
					break;
				case CONSTR_CHECK:
					audit_check_prefix(con->conname, AUDIT_CK_PREFIX,
									   "name_ck_prefix", "检查约束");
					break;
				default:
					break;
			}
			continue;
		}

		{
			ColumnDef  *col = (ColumnDef *) node;
			ListCell   *c2;

			foreach(c2, col->constraints)
			{
				Constraint *con = (Constraint *) lfirst(c2);

				if (con->conname == NULL)
					continue;

				switch (con->contype)
				{
					case CONSTR_PRIMARY:
						audit_check_prefix(con->conname, AUDIT_PK_PREFIX,
										   "name_pk_prefix", "主键约束");
						break;
					case CONSTR_UNIQUE:
						audit_check_prefix(con->conname, AUDIT_UK_PREFIX,
										   "name_uk_prefix", "唯一约束");
						break;
					case CONSTR_CHECK:
						audit_check_prefix(con->conname, AUDIT_CK_PREFIX,
										   "name_ck_prefix", "检查约束");
						break;
					default:
						break;
				}
			}
		}
	}
}

/* =====================================================================
 * CREATE TABLE
 * ===================================================================== */
void
audit_ddl_create_table(CreateStmt *stmt)
{
	const char *tblname;
	char	   *pk_col;
	bool		is_temp;
	ListCell   *lc;


	if (stmt == NULL || stmt->relation == NULL)
		return;

	tblname = rv_name(stmt->relation);
	is_temp = (stmt->relation->relpersistence == RELPERSISTENCE_TEMP);

	/* 表名回显 */
	ereport(DEBUG1,
			(errmsg("SQL审核: 创建表语句，表名 = \"%s\"",
					tblname ? tblname : "?")));

	/* 表名命名规范 */
	audit_check_name(tblname, "表");

	/* 临时表命名规范（tmp_ 前缀 + 日期后缀） */
	if (is_temp)
	{
		if (tblname == NULL ||
			strncmp(tblname, "tmp", 3) != 0)
			audit_emit(AUDIT_ERROR, "name_tmp_prefix",
					   "临时表名 \"%s\" 必须以 tmp 为前缀并以日期为后缀",
					   tblname ? tblname : "");

		/* 规则: 中间数据处理建议用数组替代临时表（推荐） */
		audit_emit(AUDIT_NOTICE, "dml_array_vs_tmp",
				   "临时表 \"%s\" 若用于中间数据计算，建议使用数组替代",
				   tblname ? tblname : "");
	}

	/* 主键必填 */
	pk_col = find_pk_column(stmt);
	if (pk_col == NULL)
	{
		audit_emit(AUDIT_ERROR, "table_pk_required",
				   "表 \"%s\" 必须定义主键", tblname);
	}
	else
	{
		/* 主键列名必须为 id */
		if (strcmp(pk_col, "id") != 0)
		{

			audit_emit(AUDIT_ERROR, "table_pk_name_id",
					   "表 \"%s\" 的主键列名必须为 \"id\"，当前为 \"%s\"",
					   tblname, pk_col);
		}
	}

	/* 外键禁止 */
	if (has_foreign_key(stmt))
	{
		audit_emit(AUDIT_ERROR, "table_no_foreign_key",
				   "表 \"%s\" 禁止使用外键，数据完整性应由代码层实现", tblname);
	}

	/* 约束命名前缀 */
	check_constraint_prefixes(stmt);

	/* 逐列检查 */
	foreach(lc, stmt->tableElts)
	{
		ColumnDef  *col;

		if (is_table_constraint((Node *) lfirst(lc)))
			continue;

		col = (ColumnDef *) lfirst(lc);

		/* 列名命名规范 */
		audit_check_name(col->colname, "列");

		/* 字段类型规范 */
		audit_check_type(col->typeName, col->colname);

		/* 规则: 优先选择最小数据类型（主键列由 table_pk_type_serial 处理） */
		if (pk_col == NULL || strcmp(col->colname, pk_col) != 0)
		{
			const char *tn = audit_typename(col->typeName);

			if (tn != NULL &&
				(strcmp(tn, "bigint") == 0 || strcmp(tn, "int8") == 0))
				audit_emit(AUDIT_ERROR, "type_min_size",
						   "列 \"%s\" 建议使用 int 而非 bigint，除非确需大范围数值",
						   col->colname);
			if (tn != NULL && strcmp(tn, "numeric") == 0 &&
				col->typeName->typmods == NIL)
				audit_emit(AUDIT_ERROR, "type_min_size",
						   "列 \"%s\" 的 numeric 类型必须指定 precision 与 scale",
						   col->colname);
		}

		/* 主键类型为序列类型 */
		if (pk_col != NULL && strcmp(col->colname, pk_col) == 0)
		{
			const char *tn = audit_typename(col->typeName);

			if (!pk_type_serial_ok(tn))
			{
				audit_emit(AUDIT_ERROR, "table_pk_type_serial",
						   "表 \"%s\" 主键 \"%s\" 建议使用序列(int/serial)类型",
						   tblname, col->colname);
			}
		}

		/* 字段 NOT NULL（主键列隐含 NOT NULL，跳过） */
		if (!col_is_not_null(col) && !col_is_in_pk(stmt, col))
		{
			audit_emit(AUDIT_ERROR, "table_column_not_null",
					   "表 \"%s\" 的字段 \"%s\" 必须显式设置为 NOT NULL",
					   tblname, col->colname);
		}

		/* 预留字段禁止 */
		if (is_reserved_column_name(col->colname))
		{
			audit_emit(AUDIT_WARNING, "table_no_reserved_column",
					   "表 \"%s\" 存在预留性质的字段 \"%s\"，建议移除",
					   tblname, col->colname);
		}
	}

	audit_record_write("CREATE TABLE", tblname, "table", AUDIT_NOTICE);
}

/* =====================================================================
 * ALTER TABLE
 * ===================================================================== */

/* 决策 D13：ALTER TABLE ENABLE/DISABLE TRIGGER 归 prog_trigger */
static bool
audit_alter_has_trigger_cmd(AlterTableStmt *stmt)
{
	ListCell   *lc;

	if (stmt == NULL || stmt->cmds == NIL)
		return false;

	foreach(lc, stmt->cmds)
	{
		AlterTableCmd *cmd = (AlterTableCmd *) lfirst(lc);

		if (cmd->subtype == AT_EnableTrig ||
			cmd->subtype == AT_EnableTrigAll ||
			cmd->subtype == AT_EnableTrigUser ||
			cmd->subtype == AT_DisableTrig ||
			cmd->subtype == AT_DisableTrigAll ||
			cmd->subtype == AT_DisableTrigUser)
			return true;
	}
	return false;
}

void
audit_ddl_alter_table(AlterTableStmt *stmt)
{
	const char *tblname;

	if (stmt == NULL)
		return;

	tblname = rv_name(stmt->relation);

	if (audit_alter_has_trigger_cmd(stmt))
	{
		audit_emit(AUDIT_WARNING, "prog_trigger",
				   "触发器启用/禁用需评估执行时机与开销 (表: %s)",
				   tblname ? tblname : "?");
	}
	else
	{
		audit_emit(AUDIT_NOTICE, "ddl_alter_object",
				   "对象结构变更回显 (对象类型: 表, 名称: %s)",
				   tblname ? tblname : "?");
	}

	audit_record_write("ALTER TABLE", tblname, "alter_table", AUDIT_NOTICE);
}

/* =====================================================================
 * CREATE INDEX
 * ===================================================================== */

/* List of String 是否包含指定字符串 */
static bool
string_list_contains(List *list, const char *s)
{
	ListCell   *lc;

	foreach(lc, list)
	{
		if (strcmp(strVal(lfirst(lc)), s) == 0)
			return true;
	}
	return false;
}

/* 提取 IndexStmt 的索引列名列表 */
static List *
stmt_index_colnames(IndexStmt *stmt)
{
	List	   *cols = NIL;
	ListCell   *lc;

	foreach(lc, stmt->indexParams)
	{
		IndexElem  *elem = (IndexElem *) lfirst(lc);

		if (elem->name != NULL)
			cols = lappend(cols, makeString(elem->name));
	}
	return cols;
}

/* 冗余索引检查：新索引列集与表上已有索引列集重复 */
static void
check_redundant_index(Relation rel, List *indexlist, List *newcols,
					  const char *idxname)
{
	ListCell   *lc;

	if (newcols == NIL)
		return;

	foreach(lc, indexlist)
	{
		Oid			idxid = lfirst_oid(lc);
		Relation	idxrel;
		IndexInfo  *idxinfo;
		bool		redundant;
		int			i;

		idxrel = index_open(idxid, AccessShareLock);
		idxinfo = BuildIndexInfo(idxrel);
		redundant = (idxinfo->ii_NumIndexAttrs == list_length(newcols));

		if (redundant)
		{
			for (i = 0; i < idxinfo->ii_NumIndexAttrs; i++)
			{
				AttrNumber	attno = idxinfo->ii_IndexAttrNumbers[i];
				char	   *attname;

				if (attno <= 0)
				{
					redundant = false;
					break;
				}
				attname = get_attname(RelationGetRelid(rel), attno, false);
				if (!string_list_contains(newcols, attname))
				{
					redundant = false;
					break;
				}
			}
		}

		index_close(idxrel, AccessShareLock);

		if (redundant)
		{
			audit_emit(AUDIT_ERROR, "idx_redundant",
					   "索引 \"%s\" 与表上已有索引列集重复，存在冗余索引",
					   idxname ? idxname : "(自动命名)");
			break;
		}
	}
}

/* 选择性检查：索引必须创建在选择性较高的列上（依赖 pg_stats 统计信息） */
static void
check_index_selectivity(Relation rel, IndexStmt *stmt, const char *idxname)
{
	ListCell   *lc;
	List	   *low_sel_cols = NIL;
	Oid			relid = RelationGetRelid(rel);

	foreach(lc, stmt->indexParams)
	{
		IndexElem  *elem = (IndexElem *) lfirst(lc);
		AttrNumber	attno;
		HeapTuple	tup;
		bool		isnull;
		float4		nd;

		if (elem->name == NULL)
			continue;

		attno = get_attnum(relid, elem->name);
		if (attno == InvalidAttrNumber)
			continue;

		tup = SearchSysCache3(STATRELATTINH,
							  ObjectIdGetDatum(relid),
							  Int16GetDatum(attno),
							  BoolGetDatum(false));
		if (!HeapTupleIsValid(tup))
			continue;

		nd = DatumGetFloat4(SysCacheGetAttr(STATRELATTINH, tup,
											Anum_pg_statistic_stadistinct,
											&isnull));
		ReleaseSysCache(tup);

		if (!isnull && ((nd < 0 && nd > -0.1) || (nd >= 0 && nd <= 2)))
			low_sel_cols = lappend(low_sel_cols, makeString(elem->name));
	}

	foreach(lc, low_sel_cols)
	{
		audit_emit(AUDIT_ERROR, "idx_selectivity",
				   "索引 \"%s\" 的列 \"%s\" 选择性过低，不建议建立索引",
				   idxname ? idxname : "(自动命名)", strVal(lfirst(lc)));
	}
}

void
audit_ddl_index(IndexStmt *stmt)
{
	const char *idxname;
	const char *tblname;
	int			ncols;
	int			total;

	if (stmt == NULL)
		return;

	idxname = stmt->idxname;
	tblname = rv_name(stmt->relation);
	ncols = list_length(stmt->indexParams);

	/* 约束自动生成的索引（PK/UNIQUE）跳过命名与 CONCURRENTLY 检查 */
	if (stmt->isconstraint)
	{
		audit_record_write("CREATE INDEX (constraint)", idxname, "index", AUDIT_NOTICE);
		return;
	}

	ereport(DEBUG1,
			(errmsg("SQL审核: 创建索引语句，索引名 = \"%s\"，目标表 = \"%s\"",
					idxname ? idxname : "(自动命名)", tblname ? tblname : "?")));

	/* 索引命名 + idx_ 前缀 */
	if (idxname != NULL)
	{
		audit_check_name(idxname, "索引");
		audit_check_prefix(idxname, AUDIT_IDX_PREFIX, "name_idx_prefix", "索引");
	}

	/* 必须使用 CONCURRENTLY */
	if (!stmt->concurrent)
	{
		audit_emit(AUDIT_ERROR, "idx_concurrently",
				   "创建索引 \"%s\" 必须使用 CONCURRENTLY，避免锁住写表操作",
				   idxname ? idxname : "(自动命名)");
	}

	/* 索引方法选择（根据场景合理选择 btree/hash/gin/gist/brin） */
	if (stmt->accessMethod != NULL &&
		(strcmp(stmt->accessMethod, "hash") == 0 && ncols > 1))
	{
		audit_emit(AUDIT_ERROR, "idx_method",
				   "索引 \"%s\" 使用 hash 方法不支持多列，请选择 btree 或其他方法",
				   idxname ? idxname : "(自动命名)");
	}

	/* 单索引字段数不超过 5 */
	if (ncols > 5)
	{
		audit_emit(AUDIT_WARNING, "idx_field_count",
				   "索引 \"%s\" 字段数 %d 超过 5 个",
				   idxname ? idxname : "(自动命名)", ncols);
	}

	/* CIC 特判（决策 D20）：CREATE INDEX CONCURRENTLY 使用独立的两阶段事务/快照
	 * 机制，ProcessUtility 钩子内对其执行关系/系统目录元数据访问（relation_openrv
	 * /RelationGetIndexList/BuildIndexInfo/SearchSysCache*）会与内核 CIC 事务管理
	 * 冲突，触发断言失败与服务端崩溃（实测 PG17 TRAP: SysCache nkeys assert）。
	 * 故 CIC 场景仅做上述静态解析树检查，跳过目录元数据区（idx_table_count 及
	 * 目录型规则 idx_redundant/idx_selectivity 降级为"不可判定"），只写底层记录。
	 * 非 CIC 场景保持完整既有检查路径，行为不变。 */
	if (stmt->concurrent)
	{
		audit_record_write("CREATE INDEX (concurrent)", idxname ? idxname : tblname,
						   "index", AUDIT_NOTICE);
		return;
	}

	/* 单表索引数量不超过 5（查询 pg_index 系统目录） */
	total = 0;
	if (tblname != NULL)
	{
		Relation	rel = relation_openrv(stmt->relation, AccessShareLock);
		List	   *indexlist = RelationGetIndexList(rel);

		total = list_length(indexlist);

		/* 冗余/重复索引检查 */
		check_redundant_index(rel, indexlist, stmt_index_colnames(stmt),
							  idxname);

		/* 索引选择性检查（需统计信息） */
		check_index_selectivity(rel, stmt, idxname);

		list_free(indexlist);
		relation_close(rel, AccessShareLock);
	}
	if (total > 5)
	{
		audit_emit(AUDIT_WARNING, "idx_table_count",
				   "表 \"%s\" 索引数量 %d 超过 5 个",
				   tblname ? tblname : "?", total);
	}

	audit_record_write("CREATE INDEX", idxname ? idxname : tblname,
					   "index", AUDIT_NOTICE);
}

/* =====================================================================
 * CREATE VIEW
 * ===================================================================== */
static bool
select_stmt_has_star(SelectStmt *ss);

static bool
targetlist_has_star(List *targetList)
{
	ListCell   *lc;

	foreach(lc, targetList)
	{
		Node	   *n = (Node *) lfirst(lc);
		ResTarget  *res = (ResTarget *) n;

		if (res->val == NULL)
			continue;

		if (IsA(res->val, ColumnRef))
		{
			ColumnRef  *cr = (ColumnRef *) res->val;
			Node	   *last = (Node *) llast(cr->fields);

			if (IsA(last, A_Star))
				return true;
		}
		else if (IsA(res->val, A_Star))
			return true;
	}
	return false;
}

static bool
fromclause_has_subselect(List *fromClause)
{
	ListCell   *lc;

	foreach(lc, fromClause)
	{
		Node	   *n = (Node *) lfirst(lc);

		if (IsA(n, RangeSubselect))
			return true;
		if (IsA(n, JoinExpr))
			return true;
	}
	return false;
}

static bool
select_stmt_has_star(SelectStmt *ss)
{
	if (ss == NULL)
		return false;

	if (ss->targetList != NIL && targetlist_has_star(ss->targetList))
		return true;

	/* 递归检查 set 操作与子查询 */
	if (ss->larg != NULL && IsA(ss->larg, SelectStmt))
	{
		if (select_stmt_has_star((SelectStmt *) ss->larg))
			return true;
	}
	if (ss->rarg != NULL && IsA(ss->rarg, SelectStmt))
	{
		if (select_stmt_has_star((SelectStmt *) ss->rarg))
			return true;
	}
	return false;
}

void
audit_ddl_view(ViewStmt *stmt)
{
	const char *viewname;
	SelectStmt *ss = NULL;

	if (stmt == NULL || stmt->view == NULL)
		return;

	viewname = rv_name(stmt->view);
	ereport(DEBUG1,
			(errmsg("SQL审核: 创建视图语句，视图名 = \"%s\"",
					viewname ? viewname : "?")));

	audit_check_name(viewname, "视图");
	audit_check_prefix(viewname, AUDIT_VW_PREFIX, "name_vw_prefix", "视图");

	if (IsA(stmt->query, SelectStmt))
		ss = (SelectStmt *) stmt->query;

	/* 禁止 select * */
	if (select_stmt_has_star(ss))
	{
		audit_emit(AUDIT_ERROR, "view_select_star",
				   "视图 \"%s\" 禁止使用 select *，应显式写出所有字段",
				   viewname);
	}

	/* 禁止 order by */
	if (ss != NULL && ss->sortClause != NIL)
	{
		audit_emit(AUDIT_ERROR, "view_order_by",
				   "视图 \"%s\" 中禁止使用 order by", viewname);
	}

	/* 禁止嵌套视图/复杂子查询 */
	if (ss != NULL && fromclause_has_subselect(ss->fromClause))
	{
		audit_emit(AUDIT_WARNING, "view_nested",
				   "视图 \"%s\" 建议避免嵌套其他视图或复杂子查询", viewname);
	}

	audit_record_write("CREATE VIEW", viewname, "view", AUDIT_NOTICE);
}

void
audit_ddl_matview(CreateTableAsStmt *stmt)
{
	const char *mvname;

	if (stmt == NULL || stmt->into == NULL || stmt->into->rel == NULL)
		return;

	mvname = rv_name(stmt->into->rel);
	audit_check_prefix(mvname, AUDIT_MV_PREFIX, "name_mv_prefix", "物化视图");
}


/* =====================================================================
 * CREATE SEQUENCE / DOMAIN / TYPE（复合/枚举/范围）
 * ===================================================================== */
void
audit_ddl_create_seq(CreateSeqStmt *stmt)
{
	const char *seqname;

	if (stmt == NULL || stmt->sequence == NULL)
		return;

	seqname = rv_name(stmt->sequence);
	audit_check_name(seqname, "序列");
	audit_emit(AUDIT_NOTICE, "ddl_create_object",
			   "对象创建回显 (对象类型: 序列, 名称: %s)",
			   seqname ? seqname : "?");
	audit_record_write("CREATE SEQUENCE", seqname, "sequence", AUDIT_NOTICE);
}

void
audit_ddl_create_domain(CreateDomainStmt *stmt)
{
	const char *name;

	if (stmt == NULL || stmt->domainname == NIL)
		return;

	name = strVal(llast(stmt->domainname));
	audit_check_name(name, "域");
	audit_emit(AUDIT_NOTICE, "ddl_create_object",
			   "对象创建回显 (对象类型: 域, 名称: %s)",
			   name ? name : "?");
	audit_record_write("CREATE DOMAIN", name, "domain", AUDIT_NOTICE);
}

void
audit_ddl_create_composite(CompositeTypeStmt *stmt)
{
	const char *name;

	if (stmt == NULL || stmt->typevar == NULL)
		return;

	name = rv_name(stmt->typevar);
	audit_check_name(name, "类型");
	audit_emit(AUDIT_NOTICE, "ddl_create_object",
			   "对象创建回显 (对象类型: 复合类型, 名称: %s)",
			   name ? name : "?");
	audit_record_write("CREATE TYPE", name, "type", AUDIT_NOTICE);
}

void
audit_ddl_create_enum(CreateEnumStmt *stmt)
{
	const char *name;

	if (stmt == NULL || stmt->typeName == NIL)
		return;

	name = strVal(llast(stmt->typeName));
	audit_check_name(name, "类型");
	audit_emit(AUDIT_NOTICE, "ddl_create_object",
			   "对象创建回显 (对象类型: 枚举类型, 名称: %s)",
			   name ? name : "?");
	audit_record_write("CREATE TYPE", name, "type", AUDIT_NOTICE);
}

void
audit_ddl_create_range(CreateRangeStmt *stmt)
{
	const char *name;

	if (stmt == NULL || stmt->typeName == NIL)
		return;

	name = strVal(llast(stmt->typeName));
	audit_check_name(name, "类型");
	audit_emit(AUDIT_NOTICE, "ddl_create_object",
			   "对象创建回显 (对象类型: 范围类型, 名称: %s)",
			   name ? name : "?");
	audit_record_write("CREATE TYPE", name, "type", AUDIT_NOTICE);
}

/* =====================================================================
 * CREATE DATABASE / TABLESPACE / SCHEMA / 其他 Define（聚合/操作符）
 * ===================================================================== */
void
audit_ddl_createdb(CreatedbStmt *stmt)
{
	ListCell   *lc;

	if (stmt == NULL)
		return;

	ereport(DEBUG1,
			(errmsg("SQL审核: 创建数据库语句，数据库名 = \"%s\"",
					stmt->dbname ? stmt->dbname : "?")));
	audit_check_name(stmt->dbname, "数据库");

	/* 数据库命名：应用缩写前缀 + 环境后缀（告警级提示） */
	audit_emit(AUDIT_WARNING, "name_db_name",
			   "数据库名 \"%s\" 建议以应用系统缩写为前缀、环境类型为后缀",
			   stmt->dbname ? stmt->dbname : "");

	/* 字符集 UTF8 / 排序规则 C 检查 */
	foreach(lc, stmt->options)
	{
		DefElem    *def = (DefElem *) lfirst(lc);
		const char *val = NULL;

		if (def->arg != NULL && IsA(def->arg, String))
			val = strVal(def->arg);

		if (strcmp(def->defname, "encoding") == 0 && val != NULL)
		{
			if (pg_strcasecmp(val, "UTF8") != 0 &&
				pg_strcasecmp(val, "UTF-8") != 0)
				audit_emit(AUDIT_ERROR, "db_charset_utf8",
						   "数据库字符集必须使用 UTF8，当前为 \"%s\"", val);
		}
		else if (strcmp(def->defname, "lc_collate") == 0 && val != NULL)
		{
			if (pg_strcasecmp(val, "C") != 0 &&
				pg_strcasecmp(val, "C.UTF-8") != 0 &&
				pg_strcasecmp(val, "POSIX") != 0)
				audit_emit(AUDIT_ERROR, "db_charset_utf8",
						   "数据库排序规则建议使用 C，当前为 \"%s\"", val);
		}
	}

	audit_record_write("CREATE DATABASE", stmt->dbname, "database", AUDIT_NOTICE);
}

void
audit_ddl_tablespace(CreateTableSpaceStmt *stmt)
{
	if (stmt == NULL)
		return;

	audit_check_name(stmt->tablespacename, "表空间");
	audit_emit(AUDIT_NOTICE, "ddl_create_object",
			   "对象创建回显 (对象类型: 表空间, 名称: %s)",
			   stmt->tablespacename ? stmt->tablespacename : "?");
	audit_record_write("CREATE TABLESPACE", stmt->tablespacename, "tablespace", AUDIT_NOTICE);
}
void
audit_ddl_schema(CreateSchemaStmt *stmt)
{
	if (stmt == NULL)
		return;

	audit_check_name(stmt->schemaname, "模式");
	audit_emit(AUDIT_NOTICE, "ddl_create_object",
			   "对象创建回显 (对象类型: 模式, 名称: %s)",
			   stmt->schemaname ? stmt->schemaname : "?");
	audit_record_write("CREATE SCHEMA", stmt->schemaname, "schema", AUDIT_NOTICE);
}

/* CREATE AGGREGATE / OPERATOR / TYPE (DefineStmt) 等 */
void
audit_ddl_define(DefineStmt *stmt)
{
	const char *name;

	if (stmt == NULL || stmt->defnames == NIL)
		return;

	name = strVal(llast(stmt->defnames));
	audit_check_name(name, "对象");
	audit_emit(AUDIT_NOTICE, "ddl_create_object",
			   "对象创建回显 (对象类型: 高级对象, 名称: %s)",
			   name ? name : "?");
	audit_record_write("CREATE", name, "define", AUDIT_NOTICE);
}

void
audit_ddl_create_role(CreateRoleStmt *stmt)
{
	if (stmt == NULL)
		return;

	audit_check_name(stmt->role, "角色");
	audit_emit(AUDIT_WARNING, "dcl_role",
			   "角色创建需评估权限与归属 (角色: %s)",
			   stmt->role ? stmt->role : "?");
	audit_record_write("CREATE ROLE", stmt->role, "role", AUDIT_NOTICE);
}

void
audit_ddl_alter_role(AlterRoleStmt *stmt)
{
	const char *name = NULL;

	if (stmt == NULL)
		return;

	if (stmt->role != NULL)
		name = stmt->role->rolename;

	audit_emit(AUDIT_WARNING, "dcl_role",
			   "角色修改需评估权限与归属 (角色: %s)",
			   name ? name : "?");
	audit_record_write("ALTER ROLE", name, "role", AUDIT_NOTICE);
}

void
audit_ddl_alter_role_set(AlterRoleSetStmt *stmt)
{
	const char *name = NULL;

	if (stmt == NULL)
		return;

	if (stmt->role != NULL)
		name = stmt->role->rolename;

	audit_emit(AUDIT_WARNING, "dcl_role",
			   "角色配置修改需评估权限与归属 (角色: %s)",
			   name ? name : "?");
	audit_record_write("ALTER ROLE SET", name, "role", AUDIT_NOTICE);
}

void
audit_ddl_alter_seq(AlterSeqStmt *stmt)
{
	const char *name;

	if (stmt == NULL || stmt->sequence == NULL)
		return;

	name = rv_name(stmt->sequence);
	audit_emit(AUDIT_NOTICE, "ddl_alter_object",
			   "对象结构变更回显 (对象类型: 序列, 名称: %s)",
			   name ? name : "?");
	audit_record_write("ALTER SEQUENCE", name, "sequence", AUDIT_NOTICE);
}

void
audit_ddl_alter_domain(AlterDomainStmt *stmt)
{
	const char *name;

	if (stmt == NULL || stmt->typeName == NIL)
		return;

	name = strVal(llast(stmt->typeName));
	audit_emit(AUDIT_NOTICE, "ddl_alter_object",
			   "对象结构变更回显 (对象类型: 域, 名称: %s)",
			   name ? name : "?");
	audit_record_write("ALTER DOMAIN", name, "domain", AUDIT_NOTICE);
}

void
audit_ddl_alter_owner(AlterOwnerStmt *stmt)
{
	const char *name = NULL;

	if (stmt == NULL)
		return;

	if (stmt->object != NULL && IsA(stmt->object, List))
	{
		List	   *names = (List *) stmt->object;

		if (names != NIL)
			name = strVal(llast(names));
	}
	else if (stmt->relation != NULL)
		name = rv_name(stmt->relation);


	audit_emit(AUDIT_NOTICE, "obj_rename_owner",
			   "对象属主变更识别回显 (对象: %s)",
			   name ? name : "?");
	audit_record_write("ALTER OWNER", name, "owner", AUDIT_NOTICE);
}

void
audit_ddl_alter_object_schema(AlterObjectSchemaStmt *stmt)
{
	const char *name = NULL;

	if (stmt == NULL)
		return;

	if (stmt->object != NULL && IsA(stmt->object, List))
	{
		List	   *names = (List *) stmt->object;

		if (names != NIL)
			name = strVal(llast(names));
	}
	else if (stmt->relation != NULL)
		name = rv_name(stmt->relation);


	audit_emit(AUDIT_NOTICE, "obj_rename_owner",
			   "对象模式迁移识别回显 (对象: %s)",
			   name ? name : "?");
	audit_record_write("ALTER SCHEMA", name, "alter_schema", AUDIT_NOTICE);
}

#if PG_VERSION_NUM >= 130000
void
audit_ddl_alter_type(AlterTypeStmt *stmt)
{
	const char *name;

	if (stmt == NULL || stmt->typeName == NIL)
		return;

	name = strVal(llast(stmt->typeName));
	audit_emit(AUDIT_NOTICE, "ddl_alter_object",
			   "对象结构变更回显 (对象类型: 类型, 名称: %s)",
			   name ? name : "?");
	audit_record_write("ALTER TYPE", name, "type", AUDIT_NOTICE);
}
#endif

void
audit_ddl_alter_enum(AlterEnumStmt *stmt)
{
	const char *name;

	if (stmt == NULL || stmt->typeName == NIL)
		return;

	name = strVal(llast(stmt->typeName));
	audit_emit(AUDIT_NOTICE, "ddl_alter_object",
			   "对象结构变更回显 (对象类型: 枚举类型, 名称: %s)",
			   name ? name : "?");
	audit_record_write("ALTER TYPE", name, "type", AUDIT_NOTICE);
}

void
audit_ddl_alter_database(AlterDatabaseStmt *stmt)
{
	if (stmt == NULL)
		return;

	audit_emit(AUDIT_NOTICE, "ddl_alter_object",
			   "对象结构变更回显 (对象类型: 数据库, 名称: %s)",
			   stmt->dbname ? stmt->dbname : "?");
	audit_record_write("ALTER DATABASE", stmt->dbname, "database", AUDIT_NOTICE);
}

void
audit_ddl_rename(RenameStmt *stmt)
{
	const char *oldname = NULL;

	if (stmt == NULL)
		return;

	if (stmt->relation != NULL)
		oldname = rv_name(stmt->relation);
	else if (stmt->object != NULL && IsA(stmt->object, List))
	{
		List	   *names = (List *) stmt->object;

		if (names != NIL)
			oldname = strVal(llast(names));
	}

	/* 新名称必须通过命名规范校验（名字违规走 name_* ERROR，不输出回显） */
	audit_check_name(stmt->newname, "对象");

	/* 决策 D13：RENAME 触发器/规则归 prog_* 族，不落入 obj_rename_owner */
	if (stmt->renameType == OBJECT_TRIGGER)
	{
		audit_emit(AUDIT_WARNING, "prog_trigger",
				   "触发器重命名需评估执行时机与开销 (旧名称: %s, 新名称: %s)",
				   oldname ? oldname : "?", stmt->newname ? stmt->newname : "?");
	}
	else if (stmt->renameType == OBJECT_RULE)
	{
		audit_emit(AUDIT_WARNING, "prog_rule",
				   "重写规则重命名需评估查询重写影响 (旧名称: %s, 新名称: %s)",
				   oldname ? oldname : "?", stmt->newname ? stmt->newname : "?");
	}
	else
	{
		audit_emit(AUDIT_NOTICE, "obj_rename_owner",
				   "对象重命名识别回显 (旧名称: %s, 新名称: %s)",
				   oldname ? oldname : "?", stmt->newname ? stmt->newname : "?");
	}
	audit_record_write("RENAME", oldname, "rename", AUDIT_NOTICE);
}

void
audit_ddl_comment(CommentStmt *stmt)
{
	const char *comment;

	if (stmt == NULL)
		return;

	comment = stmt->comment ? stmt->comment : "(删除注释)";

	audit_emit(AUDIT_NOTICE, "obj_comment",
			   "对象注释识别回显 (注释: %s)",
			   comment ? comment : "");
	audit_record_write("COMMENT", NULL, "comment", AUDIT_NOTICE);
}


/* DROP 家族对象名提取
 *
 * 注意：DROP TABLE/VIEW/INDEX 等 objects 元素是多部分名称 List，
 * 而 DROP EXTENSION 等 objects 元素是裸 String 节点，必须区分处理，
 * 否则把 String 当 List 解引用会段错误。
 */
static const char *
drop_first_name(DropStmt *stmt)
{
	Node	   *first;

	if (stmt == NULL || stmt->objects == NIL)
		return NULL;

	first = (Node *) linitial(stmt->objects);

	if (IsA(first, List))
	{
		List	   *names = (List *) first;

		if (names == NIL)
			return NULL;
		first = (Node *) linitial(names);
	}

	if (IsA(first, String))
		return strVal(first);

	return NULL;
}

void
audit_ddl_drop(DropStmt *stmt)
{
	const char *name;
	const char *objtype = "对象";

	if (stmt == NULL)
		return;

	name = drop_first_name(stmt);

	/* 决策 D13：DROP 触发器/事件触发器/规则归 prog_* 族，不落入 ddl_drop_object */
	switch (stmt->removeType)
	{
		case OBJECT_TRIGGER:
			audit_emit(AUDIT_WARNING, "prog_trigger",
					   "触发器删除需评估执行时机与开销 (触发器: %s)",
					   name ? name : "?");
			audit_record_write("DROP", name, "drop", AUDIT_NOTICE);
			return;
		case OBJECT_EVENT_TRIGGER:
			audit_emit(AUDIT_WARNING, "prog_event_trigger",
					   "事件触发器删除影响全局事件处理 (触发器: %s)",
					   name ? name : "?");
			audit_record_write("DROP", name, "drop", AUDIT_NOTICE);
			return;
		case OBJECT_RULE:
			audit_emit(AUDIT_WARNING, "prog_rule",
					   "重写规则删除需评估查询重写影响 (规则: %s)",
					   name ? name : "?");
			audit_record_write("DROP", name, "drop", AUDIT_NOTICE);
			return;
		default:
			break;
	}

	switch (stmt->removeType)
	{
		case OBJECT_TABLE:
			objtype = "表";
			break;
		case OBJECT_INDEX:
			objtype = "索引";
			break;
		case OBJECT_VIEW:
			objtype = "视图";
			break;
		case OBJECT_SCHEMA:
			objtype = "模式";
			break;
		case OBJECT_SEQUENCE:
			objtype = "序列";
			break;
		case OBJECT_TYPE:
			objtype = "类型";
			break;
		case OBJECT_DOMAIN:
			objtype = "域";
			break;
		case OBJECT_EXTENSION:
			objtype = "扩展";
			break;
		default:
			break;
	}

	/* 高危删除：DROP SCHEMA 告警（既有，不变） */
	if (stmt->removeType == OBJECT_SCHEMA)
	{
		audit_emit(AUDIT_WARNING, "ddl_high_risk_drop",
				   "删除模式 \"%s\" 属于高危操作，请谨慎", name ? name : "");
	}

	/* 对象删除回显：default 分支对象（阶段二新增，决策 D13） */
	audit_emit(AUDIT_NOTICE, "ddl_drop_object",
			   "对象删除回显 (对象类型: %s, 名称: %s)",
			   objtype, name ? name : "?");
	audit_record_write("DROP", name, "drop", AUDIT_NOTICE);
}

void
audit_ddl_dropdb(DropdbStmt *stmt)
{
	if (stmt == NULL)
		return;

	audit_emit(AUDIT_WARNING, "ddl_high_risk_drop",
			   "删除数据库 \"%s\" 属于高危操作，请谨慎",
			   stmt->dbname ? stmt->dbname : "?");
	audit_record_write("DROP DATABASE", stmt->dbname, "drop", AUDIT_WARNING);
}

void
audit_ddl_droptablespace(DropTableSpaceStmt *stmt)
{
	if (stmt == NULL)
		return;

	audit_emit(AUDIT_WARNING, "ddl_high_risk_drop",
			   "删除表空间 \"%s\" 属于高危操作，请谨慎",
			   stmt->tablespacename ? stmt->tablespacename : "?");
	audit_record_write("DROP TABLESPACE", stmt->tablespacename, "drop", AUDIT_WARNING);
}

void
audit_ddl_droprole(DropRoleStmt *stmt)
{
	const char *name = NULL;

	if (stmt == NULL)
		return;

	if (stmt->roles != NIL && IsA(linitial(stmt->roles), RoleSpec))
		name = ((RoleSpec *) linitial(stmt->roles))->rolename;
	else if (stmt->roles != NIL)
		name = strVal(linitial(stmt->roles));

	audit_emit(AUDIT_WARNING, "dcl_role",
			   "角色删除需评估权限与归属 (角色: %s)",
			   name ? name : "?");
	audit_record_write("DROP ROLE", name, "drop", AUDIT_NOTICE);
}

void
audit_ddl_dropowned(DropOwnedStmt *stmt)
{
	const char *name = NULL;

	if (stmt == NULL)
		return;

	if (stmt->roles != NIL && IsA(linitial(stmt->roles), RoleSpec))
		name = ((RoleSpec *) linitial(stmt->roles))->rolename;
	else if (stmt->roles != NIL)
		name = strVal(linitial(stmt->roles));

	audit_emit(AUDIT_NOTICE, "obj_drop_owned",
			   "DROP OWNED 批量回收对象需评估影响范围 (角色: %s)",
			   name ? name : "?");
	audit_record_write("DROP OWNED", name, "drop", AUDIT_NOTICE);
}

void
audit_ddl_truncate(TruncateStmt *stmt)
{
	const char *name = NULL;

	if (stmt == NULL)
		return;

	if (stmt->relations != NIL)
	{
		RangeVar  *rv = (RangeVar *) linitial(stmt->relations);

		name = rv_name(rv);
	}

	audit_emit(AUDIT_WARNING, "ddl_truncate_warn",
			   "生产环境中不建议使用 TRUNCATE 语句，目标表 = \"%s\"",
			   name ? name : "?");
	audit_record_write("TRUNCATE", name, "truncate", AUDIT_WARNING);
}

