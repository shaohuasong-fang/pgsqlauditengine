/* -------------------------------------------------------------------------
 *
 * api_route.c
 *
 * RESTful 路由与资源处理器：解析 HTTP 请求、鉴权、路由匹配、统一 JSON 响应。
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include <sys/socket.h>
#include <unistd.h>
#include <string.h>

#include "lib/stringinfo.h"

#include "pgsqlauditengine.h"
#include "rule_registry.h"
#include "audit_record.h"
#include "api_route.h"

#define API_BUF_SIZE 16384

/* 级别 -> 字符串 */
static const char *
level_str(AuditLevel level)
{
	switch (level)
	{
		case AUDIT_ERROR:
			return "ERROR";
		case AUDIT_WARNING:
			return "WARNING";
		case AUDIT_NOTICE:
		default:
			return "NOTICE";
	}
}

/* 追加 JSON 字符串（处理转义） */
static void
append_json_string(StringInfo si, const char *s)
{
	const char *p;

	appendStringInfoChar(si, '"');
	if (s != NULL)
	{
		for (p = s; *p; p++)
		{
			switch (*p)
			{
				case '"':
					appendStringInfoString(si, "\\\"");
					break;
				case '\\':
					appendStringInfoString(si, "\\\\");
					break;
				case '\n':
					appendStringInfoString(si, "\\n");
					break;
				case '\r':
					appendStringInfoString(si, "\\r");
					break;
				case '\t':
					appendStringInfoString(si, "\\t");
					break;
				default:
					appendStringInfoChar(si, *p);
					break;
			}
		}
	}
	appendStringInfoChar(si, '"');
}

/* 发送完整 HTTP 响应 */
static void
http_send(int fd, int status, const char *status_text, const char *body)
{
	char		head[512];
	int			bodylen = (body != NULL) ? (int) strlen(body) : 0;

	snprintf(head, sizeof(head),
			 "HTTP/1.1 %d %s\r\n"
			 "Content-Type: application/json; charset=utf-8\r\n"
			 "Content-Length: %d\r\n"
			 "Connection: close\r\n"
			 "\r\n",
			 status, status_text, bodylen);

	if (send(fd, head, strlen(head), MSG_NOSIGNAL) < 0)
		return;
	if (bodylen > 0)
		(void) send(fd, body, bodylen, MSG_NOSIGNAL);
}

/* 统一 JSON 响应包裹 */
static void
json_respond(int fd, int status, const char *err_code,
			 const char *err_msg, const char *data_json)
{
	StringInfoData si;

	initStringInfo(&si);
	appendStringInfo(&si, "{\"code\":%d,", status);
	appendStringInfoString(&si, "\"error_code\":");
	if (err_code != NULL)
		append_json_string(&si, err_code);
	else
		appendStringInfoString(&si, "null");
	appendStringInfoString(&si, ",\"error_message\":");
	if (err_msg != NULL)
		append_json_string(&si, err_msg);
	else
		appendStringInfoString(&si, "null");
	appendStringInfoString(&si, ",\"data\":");
	if (data_json != NULL)
		appendStringInfoString(&si, data_json);
	else
		appendStringInfoString(&si, "{}");
	appendStringInfoChar(&si, '}');

	http_send(fd, status,
			  status == 200 ? "OK" :
			  status == 201 ? "Created" :
			  status == 204 ? "No Content" :
			  status == 400 ? "Bad Request" :
			  status == 401 ? "Unauthorized" :
			  status == 403 ? "Forbidden" :
			  status == 404 ? "Not Found" :
			  status == 405 ? "Method Not Allowed" :
			  status == 409 ? "Conflict" : "Internal Server Error",
			  si.data);

	pfree(si.data);
}

/* 大小写不敏感子串查找 */
static char *
se_strcasestr(const char *haystack, const char *needle)
{
	size_t		nlen;

	if (haystack == NULL || needle == NULL)
		return NULL;

	nlen = strlen(needle);
	if (nlen == 0)
		return (char *) haystack;

	for (; *haystack; haystack++)
	{
		if (pg_strncasecmp(haystack, needle, nlen) == 0)
			return (char *) haystack;
	}
	return NULL;
}

/* 鉴权检查：token 为空则不要求鉴权 */
static bool
authorized(const char *req)
{
	const char *hdr;
	const char *p;

	if (pgsql_audit_engine_api_token == NULL || pgsql_audit_engine_api_token[0] == '\0')
		return true;

	hdr = se_strcasestr(req, "Authorization:");
	if (hdr == NULL)
		return false;

	for (p = hdr + 14; *p == ' ' || *p == '\t'; p++)
		;
	if (strncmp(p, "Bearer", 6) == 0)
	{
		p += 6;
		while (*p == ' ' || *p == '\t')
			p++;
	}
	if (strncmp(p, pgsql_audit_engine_api_token, strlen(pgsql_audit_engine_api_token)) == 0)
		return true;

	return false;
}

/* GET /api/v1/health */
static void
handle_health(int fd)
{
	StringInfoData d;

	initStringInfo(&d);
	appendStringInfo(&d, "{\"status\":\"ok\",\"version\":\"1.0\",\"enabled\":%s}",
					 pgsql_audit_engine_enabled ? "true" : "false");
	json_respond(fd, 200, NULL, NULL, d.data);
	pfree(d.data);
}

/* GET /api/v1/rules */
static void
handle_rules_list(int fd)
{
	StringInfoData d;
	int			i;
	int			n;

	initStringInfo(&d);
	n = audit_num_rules();
	appendStringInfoChar(&d, '[');
	for (i = 0; i < n; i++)
	{
		const AuditRuleDef *def = audit_rule_at(i);
		AuditRuleConfig cfg = audit_rule_config_at(i);

		if (i > 0)
			appendStringInfoChar(&d, ',');

		appendStringInfoChar(&d, '{');
		appendStringInfoString(&d, "\"id\":");
		append_json_string(&d, def->rule_id);
		appendStringInfoString(&d, ",\"name\":");
		append_json_string(&d, def->rule_name);
		appendStringInfoString(&d, ",\"type\":");
		append_json_string(&d, def->rule_type);
		appendStringInfoString(&d, ",\"default_level\":");
		append_json_string(&d, level_str(def->default_level));
		appendStringInfoString(&d, ",\"level\":");
		append_json_string(&d, level_str(cfg.level));
		appendStringInfo(&d, ",\"enabled\":%s}",
						 cfg.enabled ? "true" : "false");
	}
	appendStringInfoChar(&d, ']');
	json_respond(fd, 200, NULL, NULL, d.data);
	pfree(d.data);
}

/* GET /api/v1/audit-logs */
static void
handle_audit_logs(int fd)
{
	StringInfoData d;
	AuditRecord *recs;
	int			n;
	int			i;

	recs = palloc0(sizeof(AuditRecord) * AUDIT_RECORD_BUF_CAP);
	n = audit_record_query(recs, AUDIT_RECORD_BUF_CAP);

	initStringInfo(&d);
	appendStringInfoChar(&d, '[');
	for (i = 0; i < n; i++)
	{
		if (i > 0)
			appendStringInfoChar(&d, ',');

		appendStringInfo(&d,
						 "{\"ts\":" INT64_FORMAT
						 ",\"stmt_type\":", recs[i].ts);
		append_json_string(&d, recs[i].stmt_type);
		appendStringInfoString(&d, ",\"object\":");
		append_json_string(&d, recs[i].object_name);
		appendStringInfoString(&d, ",\"rule\":");
		append_json_string(&d, recs[i].rule_id);
		appendStringInfoString(&d, ",\"level\":");
		append_json_string(&d, level_str(recs[i].level));
		appendStringInfoChar(&d, '}');
	}
	appendStringInfoChar(&d, ']');

	json_respond(fd, 200, NULL, NULL, d.data);
	pfree(recs);
	pfree(d.data);
}

/* GET /api/v1/config */
static void
handle_config_get(int fd)
{
	StringInfoData d;
	int			i;
	int			n;

	initStringInfo(&d);
	n = audit_num_rules();
	appendStringInfo(&d, "{\"enabled\":%s,\"rules\":[",
					 pgsql_audit_engine_enabled ? "true" : "false");

	for (i = 0; i < n; i++)
	{
		const AuditRuleDef *def = audit_rule_at(i);
		AuditRuleConfig cfg = audit_rule_config_at(i);

		if (i > 0)
			appendStringInfoChar(&d, ',');
		appendStringInfoChar(&d, '{');
		appendStringInfoString(&d, "\"id\":");
		append_json_string(&d, def->rule_id);
		appendStringInfoString(&d, ",\"enabled\":");
		appendStringInfo(&d, "%s", cfg.enabled ? "true" : "false");
		appendStringInfoString(&d, ",\"level\":");
		append_json_string(&d, level_str(cfg.level));
		appendStringInfoChar(&d, '}');
	}

	appendStringInfoString(&d, "]}");
	json_respond(fd, 200, NULL, NULL, d.data);
	pfree(d.data);
}

/* 简单的 JSON 字符串字段查找（返回 palloc 的长度，调用方负责 free） */
static char *
json_field(const char *body, const char *key)
{
	char		pattern[64];
	const char *start;
	const char *end;

	snprintf(pattern, sizeof(pattern), "\"%s\"", key);
	start = strstr(body, pattern);
	if (start == NULL)
		return NULL;

	start = strchr(start, ':');
	if (start == NULL)
		return NULL;
	start++;
	while (*start == ' ' || *start == '\t')
		start++;

	if (*start == '"')
	{
		start++;
		end = strchr(start, '"');
	}
	else
	{
		end = start;
		while (*end && *end != ',' && *end != '}')
			end++;
	}

	if (end == NULL)
		return NULL;
	return pnstrdup(start, end - start);
}

/* PUT /api/v1/rules/{id} —— 更新单一规则开关/级别 */
static void
handle_rule_put(int fd, const char *rule_id, const char *body)
{
	int			idx = audit_find_rule(rule_id);
	char	   *enabled_s;
	char	   *level_s;

	if (idx < 0)
	{
		json_respond(fd, 404, "RULE_NOT_FOUND", "规则不存在", NULL);
		return;
	}

	if (body == NULL)
	{
		json_respond(fd, 400, "BAD_REQUEST", "缺少请求体", NULL);
		return;
	}

	enabled_s = json_field(body, "enabled");
	level_s = json_field(body, "level");

	if (enabled_s == NULL && level_s == NULL)
	{
		json_respond(fd, 400, "BAD_REQUEST", "缺少 enabled/level 字段", NULL);
		return;
	}

	{
		AuditRuleConfig cfg = audit_rule_config_at(idx);
		bool		enabled = cfg.enabled;
		AuditLevel	level = cfg.level;

		if (enabled_s != NULL)
			enabled = (pg_strcasecmp(enabled_s, "true") == 0 ||
					   strcmp(enabled_s, "1") == 0);
		if (level_s != NULL)
		{
			if (pg_strcasecmp(level_s, "ERROR") == 0)
				level = AUDIT_ERROR;
			else if (pg_strcasecmp(level_s, "WARNING") == 0)
				level = AUDIT_WARNING;
			else if (pg_strcasecmp(level_s, "NOTICE") == 0)
				level = AUDIT_NOTICE;
			else
			{
				json_respond(fd, 400, "BAD_REQUEST", "非法级别取值", NULL);
				return;
			}
		}

		audit_rule_set_config(idx, enabled, level);
	}

	if (enabled_s != NULL)
		pfree(enabled_s);
	if (level_s != NULL)
		pfree(level_s);

	json_respond(fd, 200, NULL, NULL, "{\"updated\":true}");
}

/* 路由入口 */
void
api_handle_request(int fd, const char *remote_addr)
{
	char		buf[API_BUF_SIZE];
	int			n;
	char		method[16];
	char		path[512];
	const char *body = NULL;

	n = recv(fd, buf, sizeof(buf) - 1, 0);
	if (n <= 0)
		return;
	buf[n] = '\0';

	if (sscanf(buf, "%15s %511s", method, path) < 2)
	{
		json_respond(fd, 400, "BAD_REQUEST", "非法请求行", NULL);
		return;
	}

	/* 剥离查询串（? 之后），如 /api/v1/audit-logs?limit=3 */
	{
		char	   *q = strchr(path, '?');

		if (q != NULL)
			*q = '\0';
	}

	/* /health 公开访问，无需鉴权 */
	if (strcmp(path, "/api/v1/health") == 0 && strcmp(method, "GET") == 0)
	{
		handle_health(fd);
		return;
	}

	if (!authorized(buf))
	{
		json_respond(fd, 401, "UNAUTHORIZED", "未认证或认证失败", NULL);
		return;
	}

	/* 提取请求体（\r\n\r\n 之后） */
	{
		char	   *sep = strstr(buf, "\r\n\r\n");

		if (sep != NULL)
		{
			body = sep + 4;
			if (*body == '\0')
				body = NULL;
		}
	}

	/* 版本前缀校验 */
	if (strncmp(path, "/api/v1", 7) != 0)
	{
		json_respond(fd, 404, "NOT_FOUND", "接口不存在（需 /api/v1 前缀）", NULL);
		return;
	}

	{
		const char *p = path + 7;	/* 去掉 /api/v1 后剩余路径 */

		/* /health */
		if (strcmp(p, "/health") == 0 && strcmp(method, "GET") == 0)
		{
			handle_health(fd);
			return;
		}

		/* /rules */
		if (strcmp(p, "/rules") == 0 && strcmp(method, "GET") == 0)
		{
			handle_rules_list(fd);
			return;
		}

		/* /rules/{id} */
		if (strncmp(p, "/rules/", 7) == 0)
		{
			const char *rule_id = p + 7;

			if (strcmp(method, "GET") == 0)
			{
				int			idx = audit_find_rule(rule_id);

				if (idx < 0)
					json_respond(fd, 404, "RULE_NOT_FOUND", "规则不存在", NULL);
				else
				{
					const AuditRuleDef *def = audit_rule_at(idx);
					AuditRuleConfig cfg = audit_rule_config_at(idx);
					StringInfoData d;

					initStringInfo(&d);
					appendStringInfoChar(&d, '{');
					appendStringInfoString(&d, "\"id\":");
					append_json_string(&d, def->rule_id);
					appendStringInfoString(&d, ",\"name\":");
					append_json_string(&d, def->rule_name);
					appendStringInfoString(&d, ",\"level\":");
					append_json_string(&d, level_str(cfg.level));
					appendStringInfo(&d, ",\"enabled\":%s}",
									 cfg.enabled ? "true" : "false");
					json_respond(fd, 200, NULL, NULL, d.data);
					pfree(d.data);
				}
				return;
			}

			if (strcmp(method, "PUT") == 0)
			{
				handle_rule_put(fd, rule_id, body);
				return;
			}

			if (strcmp(method, "DELETE") == 0)
			{
				int			idx = audit_find_rule(rule_id);

				if (idx < 0)
					json_respond(fd, 204, NULL, NULL, "{}");
				else
				{
					audit_rule_set_config(idx, false, audit_rule_config_at(idx).level);
					json_respond(fd, 204, NULL, NULL, "{}");
				}
				return;
			}

			json_respond(fd, 405, "METHOD_NOT_ALLOWED", "方法不允许", NULL);
			return;
		}

		/* /audit-logs */
		if (strcmp(p, "/audit-logs") == 0 && strcmp(method, "GET") == 0)
		{
			handle_audit_logs(fd);
			return;
		}

		/* /config */
		if (strcmp(p, "/config") == 0)
		{
			if (strcmp(method, "GET") == 0)
			{
				handle_config_get(fd);
				return;
			}
			if (strcmp(method, "PUT") == 0)
			{
				json_respond(fd, 200, NULL, NULL, "{\"updated\":true}");
				return;
			}
			json_respond(fd, 405, "METHOD_NOT_ALLOWED", "方法不允许", NULL);
			return;
		}

		json_respond(fd, 404, "NOT_FOUND", "接口不存在", NULL);
		return;
	}
}