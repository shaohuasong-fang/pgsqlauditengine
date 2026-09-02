#ifndef PGSQLAUDITENGINE_API_ROUTE_H
#define PGSQLAUDITENGINE_API_ROUTE_H

#include "postgres.h"

/*
 * 处理单个已 accept 的客户端连接：读取 HTTP 请求、鉴权、路由、返回 JSON 响应。
 * 处理完后由调用方关闭 fd。
 */
extern void api_handle_request(int fd, const char *remote_addr);

#endif							/* PGSQLAUDITENGINE_API_ROUTE_H */