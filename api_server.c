/* -------------------------------------------------------------------------
 *
 * api_server.c
 *
 * RESTful 审核管理服务：Background Worker 注册与内嵌轻量 HTTP 服务器主循环。
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "postmaster/bgworker.h"

#include "pgsqlauditengine.h"
#include "api_server.h"
#include "api_route.h"

void
se_api_register_bgworker(void)
{
	BackgroundWorker worker;

	memset(&worker, 0, sizeof(worker));
	snprintf(worker.bgw_name, BGW_MAXLEN, "pgsqlauditengine API server");
	snprintf(worker.bgw_type, BGW_MAXLEN, "pgsqlauditengine_api");
	worker.bgw_flags = BGWORKER_SHMEM_ACCESS;
	worker.bgw_start_time = BgWorkerStart_PostmasterStart;
	worker.bgw_restart_time = 5;
	snprintf(worker.bgw_library_name, sizeof(worker.bgw_library_name),
			 "pgsqlauditengine");
	snprintf(worker.bgw_function_name, BGW_MAXLEN, "se_api_server_main");
	worker.bgw_main_arg = (Datum) 0;
	worker.bgw_notify_pid = 0;

	RegisterBackgroundWorker(&worker);
}

PGDLLEXPORT void
se_api_server_main(Datum main_arg)
{
	int			listen_fd;
	struct sockaddr_in addr;
	int			opt = 1;
	const char *listen_addr;

	BackgroundWorkerUnblockSignals();

	listen_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (listen_fd < 0)
	{
		ereport(WARNING,
				(errmsg("SQL审核: RESTful 服务无法创建 socket")));
		return;
	}

	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(pgsql_audit_engine_api_port);

	listen_addr = (pgsql_audit_engine_api_listen != NULL &&
				   pgsql_audit_engine_api_listen[0] != '\0') ?
		pgsql_audit_engine_api_listen : "127.0.0.1";

	if (strcmp(listen_addr, "0.0.0.0") == 0)
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
	else
		addr.sin_addr.s_addr = inet_addr(listen_addr);

	if (bind(listen_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0)
	{
		ereport(WARNING,
				(errmsg("SQL审核: RESTful 服务 bind %s:%d 失败",
						listen_addr, pgsql_audit_engine_api_port)));
		close(listen_fd);
		return;
	}

	if (listen(listen_fd, 16) < 0)
	{
		close(listen_fd);
		return;
	}

	ereport(LOG,
			(errmsg("SQL审核: RESTful 服务已启动，监听 %s:%d",
					listen_addr, pgsql_audit_engine_api_port)));

	for (;;)
	{
		int			conn;
		struct sockaddr_in cli;
		socklen_t	clen = sizeof(cli);
		char		remote[INET_ADDRSTRLEN] = "";

		conn = accept(listen_fd, (struct sockaddr *) &cli, &clen);
		if (conn < 0)
		{
			if (errno == EINTR)
				continue;
			break;
		}

		if (inet_ntop(AF_INET, &cli.sin_addr, remote, sizeof(remote)) == NULL)
			remote[0] = '\0';

		api_handle_request(conn, remote);
		close(conn);
	}

	close(listen_fd);
}