/* -------------------------------------------------------------------------
 *
 * compat.h —— pgsqlauditengine 跨版本（PG11-18）兼容层
 *
 * 将 PostgreSQL 各版本间存在差异的头文件路径、钩子函数签名、解析树节点
 * 字段统一封装为宏与类型定义。业务规则文件（rule_*.c / hook_*.c /
 * api_*.c / 入口文件）只通过本层提供的兼容宏访问差异字段，不散落裸 #if。
 *
 * 版本分界说明（均经各版本内核源码逐点核实）：
 *  - VACUUM  : PG12 起 VacuumStmt->options 为 List*（DefElem）且新增
 *              is_vacuumcmd 字段；PG11 为 int 位掩码（VACOPT_* 定义于
 *              nodes/parsenodes.h），无 is_vacuumcmd。
 *  - REINDEX : PG14 起 ReindexStmt->params 为 List*（DefElem）；PG12-13
 *              为 int options + bool concurrent；PG11 为 int options 位掩码
 *              （仅 REINDEXOPT_VERBOSE），不支持 CONCURRENTLY 语法。
 *  - 其余分界（ProcessUtility 三态 / post_parse_analyze 两态 /
 *              objtype / CreateCommandName / MERGE / AlterStatsStmt）见各宏注释。
 *
 * -------------------------------------------------------------------------
 */
#ifndef PGSQLAUDITENGINE_COMPAT_H
#define PGSQLAUDITENGINE_COMPAT_H

#include "postgres.h"

/*
 * 头文件路径差异：
 * - access/relation.h 为 PG12 起引入；PG11 中 relation_open 等声明于
 *   access/heapam.h。
 */
#if PG_VERSION_NUM >= 120000
#include "access/relation.h"
#else
#include "access/heapam.h"
#endif

/* 以下内核头文件在 PG11-18 全部存在，可无条件 include */
#include "nodes/parsenodes.h"
#include "nodes/value.h"
#include "nodes/pg_list.h"
#include "postmaster/bgworker.h"
#include "storage/shmem.h"
#include "tcop/utility.h"
#include "parser/analyze.h"

/*
 * ProcessUtility 钩子签名三态（与 tcop/utility.h 的 ProcessUtility_hook_type 对齐）：
 * - PG14+  : (pstmt, queryString, readOnlyTree, context, params, queryEnv, dest,
 *             QueryCompletion *qc)  8 参
 * - PG13   : (pstmt, queryString, context, params, queryEnv, dest,
 *             QueryCompletion *qc)  7 参
 * - PG11-12: (pstmt, queryString, context, params, queryEnv, dest,
 *             char *completionTag)  7 参
 * 说明：queryEnv 参数在 PG11-18 均存在；真实分界为 qc(PG13) 与 readOnlyTree(PG14)。
 * SE_PU_HOOK_PARAMS 用于链式调用展开参数列表；PG11-12 分支末尾传递 qc
 * （钩子函数签名中该形参即 completionTag，统一命名为 qc 以复用宏）。
 */
#if PG_VERSION_NUM >= 140000
typedef void (*se_ProcessUtility_hook_type) (PlannedStmt *pstmt,
											 const char *queryString,
											 bool readOnlyTree,
											 ProcessUtilityContext context,
											 ParamListInfo params,
											 QueryEnvironment *queryEnv,
											 DestReceiver *dest,
											 QueryCompletion *qc);
#define SE_PU_HOOK_PARAMS(pstmt, queryString, readOnlyTree, context, params, queryEnv, dest, qc) \
	((pstmt), (queryString), (readOnlyTree), (context), (params), (queryEnv), (dest), (qc))
#elif PG_VERSION_NUM >= 130000
typedef void (*se_ProcessUtility_hook_type) (PlannedStmt *pstmt,
											 const char *queryString,
											 ProcessUtilityContext context,
											 ParamListInfo params,
											 QueryEnvironment *queryEnv,
											 DestReceiver *dest,
											 QueryCompletion *qc);
#define SE_PU_HOOK_PARAMS(pstmt, queryString, readOnlyTree, context, params, queryEnv, dest, qc) \
	((pstmt), (queryString), (context), (params), (queryEnv), (dest), (qc))
#else
typedef void (*se_ProcessUtility_hook_type) (PlannedStmt *pstmt,
											 const char *queryString,
											 ProcessUtilityContext context,
											 ParamListInfo params,
											 QueryEnvironment *queryEnv,
											 DestReceiver *dest,
											 char *qc);
#define SE_PU_HOOK_PARAMS(pstmt, queryString, readOnlyTree, context, params, queryEnv, dest, qc) \
	((pstmt), (queryString), (context), (params), (queryEnv), (dest), (qc))
#endif

/*
 * post_parse_analyze 钩子签名两态（与 parser/analyze.h 对齐）：
 * - PG14+  : (ParseState *pstate, Query *query, JumbleState *jstate)  3 参
 * - PG11-13: (ParseState *pstate, Query *query)                       2 参
 * 说明：pstate 参数在 PG11-18 均存在；真实分界为 jstate(PG14)。
 */
#if PG_VERSION_NUM >= 140000
typedef void (*se_post_parse_analyze_hook_type) (ParseState *pstate,
												 Query *query,
												 JumbleState *jstate);
#define SE_ANALYZE_HOOK_PARAMS(pstate, query, jstate) ((pstate), (query), (jstate))
#else
typedef void (*se_post_parse_analyze_hook_type) (ParseState *pstate,
												 Query *query);
#define SE_ANALYZE_HOOK_PARAMS(pstate, query, jstate) ((pstate), (query))
#endif

/*
 * CreateTableAsStmt 目标类型字段名差异：
 * PG14+ 为 objtype；PG11-13 为 relkind（同类型 ObjectType）。
 */
#if PG_VERSION_NUM >= 140000
#define SE_CTAS_OBJTYPE(stmt)		((stmt)->objtype)
#else
#define SE_CTAS_OBJTYPE(stmt)		((stmt)->relkind)
#endif

/*
 * AlterTableStmt 目标类型字段名差异：同上（PG14+ 为 objtype，PG11-13 为 relkind）。
 */
#if PG_VERSION_NUM >= 140000
#define SE_ALT_OBJTYPE(stmt)		((stmt)->objtype)
#else
#define SE_ALT_OBJTYPE(stmt)		((stmt)->relkind)
#endif

/*
 * VACUUM/ANALYZE 区分与选项检测（实际源码分界为 PG12，非 PG13）：
 * - PG12+  : VacuumStmt->options 为 List*（DefElem），并有 is_vacuumcmd 字段；
 * - PG11   : VacuumStmt->options 为 int（VacuumOption 位掩码），无 is_vacuumcmd。
 */
#if PG_VERSION_NUM >= 120000
#define SE_IS_VACUUMCMD(stmt)		((stmt)->is_vacuumcmd)
#define SE_VACOPT_PRESENT(stmt, optname) se_vacopt_present((stmt)->options, (optname))
#else
#define SE_IS_VACUUMCMD(stmt)		(!((stmt)->options & VACOPT_ANALYZE))
#define SE_VACOPT_PRESENT(stmt, optname) se_vacopt_bit((stmt)->options, (optname))
#endif

/*
 * REINDEX CONCURRENTLY 检测（实际源码分界为 PG14，非 PG15）：
 * - PG14+ : ReindexStmt->params 为 List*（DefElem）；
 * - PG12-13: ReindexStmt->concurrent 为 bool 字段；
 * - PG11  : int options 位掩码且不支持 CONCURRENTLY 语法，恒为 false。
 */
#if PG_VERSION_NUM >= 140000
#define SE_REINDEX_CONCURRENT(stmt) se_vacopt_present((stmt)->params, "concurrently")
#elif PG_VERSION_NUM >= 120000
#define SE_REINDEX_CONCURRENT(stmt) ((stmt)->concurrent)
#else
#define SE_REINDEX_CONCURRENT(stmt) (false)
#endif

/*
 * MERGE 支持分界：PG15 起引入 CMD_MERGE/MergeAction/Query.mergeActionList。
 * PG11-14 编译期不得引用任何 MERGE 符号。
 */
#if PG_VERSION_NUM >= 150000
#define SE_HAS_MERGE			1
#else
#define SE_HAS_MERGE			0
#endif

/*
 * ALTER STATISTICS（AlterStatsStmt）支持分界：PG13 起引入。
 */
#if PG_VERSION_NUM >= 130000
#define SE_HAS_ALTER_STATS		1
#else
#define SE_HAS_ALTER_STATS		0
#endif

/*
 * CreateCommandName 支持分界：PG13+ 有 CreateCommandName（static inline，
 * 返回 const char *）；PG11-12 用 CreateCommandTag（返回 const char *）。
 */
#if PG_VERSION_NUM >= 130000
#define SE_CMDNAME(p)		CreateCommandName((Node *) (p))
#else
#define SE_CMDNAME(p)		CreateCommandTag((Node *) (p))
#endif

/*
 * 关键字保留字判断辅助函数：
 * - PG12+ : ScanKeywordLookup(name, &ScanKeywords) 返回下标（int），分类存于
 *           ScanKeywordCategories[] 数组（common/keywords.h）；
 * - PG11   : ScanKeywordLookup(name, ScanKeywords, NumScanKeywords) 返回
 *           const ScanKeyword *，分类存于 ScanKeyword->category 字段。
 * 实现在 rule_common.c。
 */
extern bool se_is_reserved_keyword(const char *name);

/*
 * 表扫描 API 差异：
 * - PG12+ : table_open/table_close（access/table.h）；
 * - PG11   : heap_open/heap_close（access/heapam.h）。
 */
#if PG_VERSION_NUM >= 120000
#define SE_TABLE_OPEN(relid, lockmode)		table_open((relid), (lockmode))
#define SE_TABLE_CLOSE(rel, lockmode)		table_close((rel), (lockmode))
#else
#define SE_TABLE_OPEN(relid, lockmode)		heap_open((relid), (lockmode))
#define SE_TABLE_CLOSE(rel, lockmode)		heap_close((rel), (lockmode))
#endif

/*
 * 从系统目录 tuple 取 OID 差异：
 * - PG12+ : catalog 结构体（FormData_*）首成员即为 oid，用 GETSTRUCT 取；
 * - PG11   : 无 oid 成员，用 HeapTupleGetOid(tp) 宏（access/htup_details.h）。
 */
#if PG_VERSION_NUM >= 120000
#define SE_TUPLE_OID(tp)		(((Form_pg_type) GETSTRUCT(tp))->oid)
#else
#define SE_TUPLE_OID(tp)		(HeapTupleGetOid(tp))
#endif

/*
 * VACUUM/REINDEX 选项检测辅助函数：
 * - se_vacopt_present(List *options, const char *optname)：
 *   遍历 List 中 DefElem 的 defname 匹配（PG12+ VACUUM / PG14+ REINDEX 语义）；
 * - se_vacopt_bit(int options, const char *optname)：
 *   按 VACOPT_* 位掩码匹配（PG11 VACUUM 语义；PG11 无 REINDEX CONCURRENTLY）。
 * 实现在 rule_common.c（避免与 rule_cmd.c 产生循环依赖）。
 */
extern bool se_vacopt_present(List *options, const char *optname);
extern bool se_vacopt_bit(int options, const char *optname);

/*
 * A_Const 字符串值访问：
 * - PG15+ : A_Const.val 为 ValUnion 联合体（sval 为 String 结构体值，
 *           字符串在 String.sval，即 c->val.sval.sval）；
 * - PG11-14: A_Const.val 为 Value 结构体，字符串在 val.val.str（type 需为 T_String）。
 */
#if PG_VERSION_NUM >= 150000
#define SE_A_CONST_STR(c)		((c)->val.sval.sval)
#else
#define SE_A_CONST_STR(c)		(((c)->val.type == T_String) ? (c)->val.val.str : NULL)
#endif

#endif							/* PGSQLAUDITENGINE_COMPAT_H */