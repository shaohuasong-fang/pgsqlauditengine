/* -------------------------------------------------------------------------
 *
 * rule_program.c
 *
 * 可编程对象审核规则：函数、存储过程、触发器、事件触发器、规则、DO 块。
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "catalog/pg_trigger.h"
#include "nodes/pg_list.h"
#include "nodes/value.h"

#include "pgsqlauditengine.h"
#include "rule_common.h"
#include "rule_program.h"

/* 从函数定义 options 中取语言名 */
static const char *
func_language(CreateFunctionStmt *stmt)
{
	ListCell   *lc;

	foreach(lc, stmt->options)
	{
		DefElem    *def = (DefElem *) lfirst(lc);

		if (strcmp(def->defname, "language") == 0 &&
			def->arg != NULL && IsA(def->arg, String))
			return strVal(def->arg);
	}
	return NULL;
}

/* 从函数定义 options 中取函数体文本（AS '...'） */
static void
func_body(CreateFunctionStmt *stmt, char *buf, int buflen)
{
	ListCell   *lc;

	buf[0] = '\0';

	foreach(lc, stmt->options)
	{
		DefElem    *def = (DefElem *) lfirst(lc);

		if (strcmp(def->defname, "as") != 0 || def->arg == NULL)
			continue;

		if (IsA(def->arg, List))
		{
			ListCell   *c2;

			foreach(c2, (List *) def->arg)
			{
				const char *s;

				if (!IsA(lfirst(c2), String))
					continue;
				s = strVal(lfirst(c2));
				if (strlen(buf) + strlen(s) + 1 < (size_t) buflen)
					strlcat(buf, s, buflen);
			}
		}
		else if (IsA(def->arg, String))
		{
			strlcpy(buf, strVal(def->arg), buflen);
		}
	}
}

/* 轻量忽略大小写子串查找 */
static bool
contains_nocase(const char *haystack, const char *needle)
{
	size_t		hlen;
	size_t		nlen;
	size_t		i;

	if (haystack == NULL || needle == NULL)
		return false;

	hlen = strlen(haystack);
	nlen = strlen(needle);
	if (nlen == 0 || nlen > hlen)
		return false;

	for (i = 0; i + nlen <= hlen; i++)
	{
		if (pg_strncasecmp(haystack + i, needle, nlen) == 0)
			return true;
	}
	return false;
}

void
audit_program_function(CreateFunctionStmt *stmt)
{
	const char *fname;
	const char *language;
	char		body[8192];
	ListCell   *lc;
	bool		is_proc;

	if (stmt == NULL || stmt->funcname == NIL)
		return;

	fname = strVal(llast(stmt->funcname));
	is_proc = stmt->is_procedure;
	language = func_language(stmt);

	ereport(DEBUG1,
			(errmsg("SQL审核: 创建%s语句，对象名 = \"%s\"，语言 = %s",
					is_proc ? "存储过程" : "函数",
					fname ? fname : "?",
					language ? language : "sql")));

	audit_check_name(fname, is_proc ? "存储过程" : "函数");

	/* 参数名/类型审核 */
	foreach(lc, stmt->parameters)
	{
		FunctionParameter *fp = (FunctionParameter *) lfirst(lc);

		if (fp->name != NULL)
			audit_check_name(fp->name, "参数");
		if (fp->argType != NULL)
			audit_check_type(fp->argType, fp->name ? fp->name : "(参数)");
	}

	func_body(stmt, body, sizeof(body));

	/* 函数体内禁止 DDL */
	if (contains_nocase(body, "create ") ||
		contains_nocase(body, "alter ") ||
		contains_nocase(body, "drop ") ||
		contains_nocase(body, "truncate"))
	{
		audit_emit(AUDIT_WARNING, "prog_ddl_forbidden",
				   "%s \"%s\" 函数体内不建议执行 DDL 语句",
				   is_proc ? "存储过程" : "函数", fname);
	}

	/* 游标关闭检查 */
	if (contains_nocase(body, "declare") && !contains_nocase(body, "close"))
	{
		audit_emit(AUDIT_WARNING, "prog_close_cursor",
				   "%s \"%s\" 声明游标后请确保及时 CLOSE",
				   is_proc ? "存储过程" : "函数", fname);
	}

	/* 业务逻辑存放告警 */
	if (strlen(body) > 500)
	{
		audit_emit(AUDIT_WARNING, "prog_business_logic",
				   "%s \"%s\" 函数体较长，不建议在数据库中存放复杂业务逻辑",
				   is_proc ? "存储过程" : "函数", fname);
	}

	audit_record_write(is_proc ? "CREATE PROCEDURE" : "CREATE FUNCTION",
					   fname, "program", AUDIT_NOTICE);
}

static const char *
trig_timing_str(int16 timing)
{
	switch (timing)
	{
		case TRIGGER_TYPE_BEFORE:
			return "BEFORE";
		case TRIGGER_TYPE_AFTER:
			return "AFTER";
		case TRIGGER_TYPE_INSTEAD:
			return "INSTEAD OF";
		default:
			return "";
	}
}

void
audit_program_trigger(CreateTrigStmt *stmt)
{
	const char *trigname;
	const char *tblname;
	int16		events;
	const char *events_str;

	if (stmt == NULL)
		return;

	trigname = stmt->trigname;
	tblname = stmt->relation ? stmt->relation->relname : NULL;
	events = stmt->events;

	if (events & TRIGGER_TYPE_UPDATE)
		events_str = "UPDATE";
	else if (events & TRIGGER_TYPE_INSERT)
		events_str = "INSERT";
	else if (events & TRIGGER_TYPE_DELETE)
		events_str = "DELETE";
	else if (events & TRIGGER_TYPE_TRUNCATE)
		events_str = "TRUNCATE";
	else
		events_str = "?";

	audit_check_name(trigname, "触发器");
	audit_emit(AUDIT_WARNING, "prog_trigger",
			   "触发器创建需评估执行时机与开销 (触发器: %s, 目标表: %s, 时机: %s, 事件: %s)",
			   trigname ? trigname : "?",
			   tblname ? tblname : "?",
			   trig_timing_str(stmt->timing),
			   events_str);
	audit_record_write("CREATE TRIGGER", trigname, "trigger", AUDIT_NOTICE);
}

void
audit_program_event_trigger(CreateEventTrigStmt *stmt)
{
	if (stmt == NULL)
		return;

	audit_check_name(stmt->trigname, "事件触发器");
	audit_emit(AUDIT_WARNING, "prog_event_trigger",
			   "事件触发器创建影响全局事件处理 (触发器: %s, 事件: %s)",
			   stmt->trigname ? stmt->trigname : "?",
			   stmt->eventname ? stmt->eventname : "?");
	audit_record_write("CREATE EVENT TRIGGER", stmt->trigname, "event_trigger", AUDIT_NOTICE);
}

void
audit_program_rule(RuleStmt *stmt)
{
	const char *rulename;
	const char *tblname;
	char		eventbuf[16];

	if (stmt == NULL)
		return;

	rulename = stmt->rulename;
	tblname = stmt->relation ? stmt->relation->relname : NULL;

	switch (stmt->event)
	{
		case CMD_SELECT:
			strlcpy(eventbuf, "SELECT", sizeof(eventbuf));
			break;
		case CMD_INSERT:
			strlcpy(eventbuf, "INSERT", sizeof(eventbuf));
			break;
		case CMD_UPDATE:
			strlcpy(eventbuf, "UPDATE", sizeof(eventbuf));
			break;
		case CMD_DELETE:
			strlcpy(eventbuf, "DELETE", sizeof(eventbuf));
			break;
		default:
			strlcpy(eventbuf, "?", sizeof(eventbuf));
			break;
	}

	audit_check_name(rulename, "规则");
	audit_emit(AUDIT_WARNING, "prog_rule",
			   "重写规则创建需评估查询重写影响 (规则: %s, 目标表: %s, 事件: %s)",
			   rulename ? rulename : "?",
			   tblname ? tblname : "?",
			   eventbuf);
	audit_record_write("CREATE RULE", rulename, "rule", AUDIT_NOTICE);
}

void
audit_program_do(DoStmt *stmt)
{
	if (stmt == NULL)
		return;

	audit_emit(AUDIT_NOTICE, "cmd_do",
			   "匿名块执行(DO)识别回显");
	audit_record_write("DO", NULL, "do", AUDIT_NOTICE);
}

void
audit_program_alter_function(AlterFunctionStmt *stmt)
{
	const char *fname = NULL;

	if (stmt == NULL)
		return;

	if (stmt->func != NULL && stmt->func->objname != NIL)
		fname = strVal(llast(stmt->func->objname));

	audit_emit(AUDIT_NOTICE, "ddl_alter_object",
			   "对象结构变更回显 (对象类型: 函数, 名称: %s)",
			   fname ? fname : "?");
	audit_record_write("ALTER FUNCTION", fname, "program", AUDIT_NOTICE);
}

void
audit_program_alter_event_trigger(AlterEventTrigStmt *stmt)
{
	if (stmt == NULL)
		return;

	audit_emit(AUDIT_WARNING, "prog_event_trigger",
			   "事件触发器变更影响全局事件处理 (触发器: %s)",
			   stmt->trigname ? stmt->trigname : "?");
	audit_record_write("ALTER EVENT TRIGGER", stmt->trigname, "event_trigger", AUDIT_NOTICE);
}