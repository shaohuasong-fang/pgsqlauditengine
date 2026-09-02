#ifndef PGSQLAUDITENGINE_RULE_COMMON_H
#define PGSQLAUDITENGINE_RULE_COMMON_H

#include "pgsqlauditengine.h"

/*
 * 校验数据库对象名称是否符合命名规范（字符集/首字符/长度/保留字）。
 * what 用于描述对象类型（如表名/列名/视图名），仅用于消息文案。
 * 返回 true 表示名称合规。
 */
extern bool audit_check_name(const char *name, const char *what);

/*
 * 校验对象名称是否以指定前缀开头（如 pk_/uk_/ck_/idx_/vw_）。
 * 不满足时通过 audit_emit 输出指定 rule_id 的消息。返回 true 表示满足。
 */
extern bool audit_check_prefix(const char *name, const char *prefix,
							   const char *rule_id, const char *what);

/*
 * 校验字段数据类型是否符合规范（varchar/text、numeric、date/time、jsonb）。
 */
extern void audit_check_type(TypeName *typeName, const char *colname);

/* 从 TypeName 提取类型名称（不含 schema 限定），失败返回 NULL */
extern const char *audit_typename(TypeName *typeName);

/* 名称是否为 PostgreSQL 保留字 */
extern bool audit_is_reserved_keyword(const char *name);

/* 名称是否由小写字母/数字/下划线构成且以字母开头、长度<=32 */
extern bool audit_name_basic_ok(const char *name);

#endif							/* PGSQLAUDITENGINE_RULE_COMMON_H */