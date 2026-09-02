/* -------------------------------------------------------------------------
 *
 * rule_common.c
 *
 * 通用规则：对象命名规范、字段数据类型规范。
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "common/keywords.h"
#include "nodes/pg_list.h"
#include "nodes/value.h"

#include "compat.h"
#include "pgsqlauditengine.h"
#include "rule_common.h"

#define AUDIT_NAME_MAXLEN	32

/* 是否为合法字符：小写字母/数字/下划线 */
static bool
is_valid_char(char ch)
{
	if ((ch >= 'a' && ch <= 'z') ||
		(ch >= '0' && ch <= '9') ||
		ch == '_')
		return true;
	return false;
}

/* 仅字符集合规（小写字母/数字/下划线） */
static bool
charset_ok(const char *name)
{
	const char *p;

	if (name == NULL || *name == '\0')
		return false;

	for (p = name; *p; p++)
	{
		if (!is_valid_char(*p))
			return false;
	}
	return true;
}

/* 名称是否以字母开头 */
static bool
start_ok(const char *name)
{
	if (name == NULL || *name == '\0')
		return false;
	return (name[0] >= 'a' && name[0] <= 'z');
}

bool
audit_name_basic_ok(const char *name)
{
	if (name == NULL)
		return false;
	if (!charset_ok(name))
		return false;
	if (!start_ok(name))
		return false;
	if (strlen(name) > AUDIT_NAME_MAXLEN)
		return false;
	return true;
}

bool
audit_is_reserved_keyword(const char *name)
{
	return se_is_reserved_keyword(name);
}

/*
 * 保留字判断（compat.h 声明）：
 * - PG12+ : ScanKeywordLookup 返回下标，ScanKeywordCategories[] 数组判分类；
 * - PG11   : ScanKeywordLookup 返回指针，ScanKeyword->category 字段判分类。
 */
bool
se_is_reserved_keyword(const char *name)
{
	if (name == NULL || *name == '\0')
		return false;

#if PG_VERSION_NUM >= 120000
	{
		int			kwnum;

		kwnum = ScanKeywordLookup(name, &ScanKeywords);
		if (kwnum < 0)
			return false;

		return ScanKeywordCategories[kwnum] == RESERVED_KEYWORD;
	}
#else
	{
		const ScanKeyword *kw;

		kw = ScanKeywordLookup(name, ScanKeywords, NumScanKeywords);
		if (kw == NULL)
			return false;

		return kw->category == RESERVED_KEYWORD;
	}
#endif
}

bool
audit_check_name(const char *name, const char *what)
{
	if (name == NULL || *name == '\0')
	{
		audit_emit(AUDIT_ERROR, "name_start", "%s名称为空", what ? what : "");
		return false;
	}

	if (!charset_ok(name))
	{
		audit_emit(AUDIT_ERROR, "name_charset",
				   "%s名称 \"%s\" 只能由小写字母(a-z)、数字(0-9)、下划线(_)构成",
				   what ? what : "", name);
		return false;
	}

	if (!start_ok(name))
	{
		audit_emit(AUDIT_ERROR, "name_start",
				   "%s名称 \"%s\" 必须以字母开头，禁止以数字或下划线开头",
				   what ? what : "", name);
		return false;
	}

	if (strlen(name) > AUDIT_NAME_MAXLEN)
	{
		audit_emit(AUDIT_ERROR, "name_len",
				   "%s名称 \"%s\" 超过32个字符", what ? what : "", name);
		return false;
	}

	if (audit_is_reserved_keyword(name))
	{
		audit_emit(AUDIT_ERROR, "name_reserved",
				   "%s名称 \"%s\" 是PostgreSQL保留字，禁止使用",
				   what ? what : "", name);
		return false;
	}

	return true;
}

bool
audit_check_prefix(const char *name, const char *prefix,
				   const char *rule_id, const char *what)
{
	if (name == NULL || strlen(name) < strlen(prefix) ||
		strncmp(name, prefix, strlen(prefix)) != 0)
	{
		audit_emit(AUDIT_ERROR, rule_id,
				   "%s名称 \"%s\" 必须以 \"%s\" 为前缀",
				   what ? what : "", name ? name : "", prefix ? prefix : "");
		return false;
	}
	return true;
}

const char *
audit_typename(TypeName *typeName)
{
	if (typeName == NULL || typeName->names == NIL)
		return NULL;

	return strVal(llast(typeName->names));
}

/*
 * VACUUM/REINDEX 选项检测辅助函数（compat.h 声明）：
 * - se_vacopt_present：遍历 List 中 DefElem 的 defname 匹配
 *   （PG12+ VACUUM options / PG14+ REINDEX params 语义）；
 * - se_vacopt_bit：按 VACOPT_* 位掩码匹配（PG11 VACUUM options 语义）。
 */
bool
se_vacopt_present(List *options, const char *optname)
{
	ListCell   *lc;

	if (options == NIL)
		return false;

	foreach(lc, options)
	{
		DefElem    *def = (DefElem *) lfirst(lc);

		if (def != NULL && strcmp(def->defname, optname) == 0)
			return true;
	}

	return false;
}

bool
se_vacopt_bit(int options, const char *optname)
{
	/* PG12+ 使用 DefElem 列表，位掩码语义不存在，此函数不应被调用 */
#if PG_VERSION_NUM >= 120000
	(void) options;
	(void) optname;
	return false;
#else
	/* PG11: VACOPT_* 位掩码（定义于 nodes/parsenodes.h） */
	if (strcmp(optname, "full") == 0)
		return (options & VACOPT_FULL) != 0;
	if (strcmp(optname, "analyze") == 0)
		return (options & VACOPT_ANALYZE) != 0;
	return false;
#endif
}

void
audit_check_type(TypeName *typeName, const char *colname)
{
	const char *typename;

	typename = audit_typename(typeName);
	if (typename == NULL)
		return;

	/* 规则: 建议用 jsonb 替代 json */
	if (strcmp(typename, "json") == 0)
	{
		audit_emit(AUDIT_WARNING, "type_jsonb",
				   "列 \"%s\" 建议使用 jsonb 类型替代 json", colname);
	}

	/* 规则: 货币/精确计算使用 numeric，避免 float4/float8 */
	if (strcmp(typename, "real") == 0 ||
		strcmp(typename, "float4") == 0 ||
		strcmp(typename, "float8") == 0 ||
		strcmp(typename, "double precision") == 0)
	{
		audit_emit(AUDIT_WARNING, "type_numeric",
				   "列 \"%s\" 若用于货币或精确计算，建议使用 numeric", colname);
	}

	/* 规则: 字符串类型使用 varchar/text，避免定长 char */
	if (strcmp(typename, "char") == 0 ||
		strcmp(typename, "bpchar") == 0 ||
		strcmp(typename, "nchar") == 0)
	{
		audit_emit(AUDIT_WARNING, "type_varchar",
				   "列 \"%s\" 建议使用 varchar/text 替代定长 char", colname);
	}
}