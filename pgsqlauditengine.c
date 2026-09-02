/* -------------------------------------------------------------------------
 *
 * pgsqlauditengine.c
 *
 * 扩展入口：定义 GUC 配置、申请共享内存、注册共享内存启动回调、
 * 安装/卸载审核钩子、注册 RESTful 后台 Worker。
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "miscadmin.h"
#include "postmaster/bgworker.h"
#include "storage/ipc.h"
#include "storage/shmem.h"
#include "utils/guc.h"

#include "compat.h"
#include "pgsqlauditengine.h"
#include "rule_registry.h"
#include "audit_record.h"
#include "hook_utility.h"
#include "hook_analyze.h"
#include "api_server.h"

PG_MODULE_MAGIC;

void		_PG_init(void);
void		_PG_fini(void);

/* GUC 变量定义 */
bool		pgsql_audit_engine_enabled = false;
bool		pgsql_audit_engine_check_dml = true;
char	   *pgsql_audit_engine_api_listen = NULL;
int			pgsql_audit_engine_api_port = 8900;
char	   *pgsql_audit_engine_api_token = NULL;

/* 共享内存请求回调（仅 PG15+ 存在 shmem_request_hook 机制） */
#if PG_VERSION_NUM >= 150000
static void
pgsql_audit_engine_shmem_request(void)
{
	if (!process_shared_preload_libraries_in_progress)
		return;

	RequestAddinShmemSpace(audit_registry_shmem_size() +
						   audit_record_shmem_size());
}
#endif							/* PG_VERSION_NUM >= 150000 */

/* 共享内存启动回调 */
static void
pgsql_audit_engine_shmem_startup(void)
{

	audit_registry_shmem_startup();
	audit_record_shmem_startup();

	if (pgsql_audit_engine_registry == NULL)
	{
		static AuditRegistry reg;

		audit_registry_init(&reg);
	}
}

void
_PG_init(void)
{

	DefineCustomBoolVariable("PGSAUDAUDITENGINE.enabled",
							 "是否开启 SQL 审核功能",
							 NULL,
							 &pgsql_audit_engine_enabled,
							 false,
							 PGC_SUSET,
							 0,
							 NULL, NULL, NULL);

	DefineCustomBoolVariable("PGSAUDAUDITENGINE.check_dml",
							 "是否开启 DML 语句审核",
							 NULL,
							 &pgsql_audit_engine_check_dml,
							 true,
							 PGC_SUSET,
							 0,
							 NULL, NULL, NULL);

	DefineCustomStringVariable("PGSAUDAUDITENGINE.api_listen",
							   "RESTful 服务监听地址",
							   NULL,
							   &pgsql_audit_engine_api_listen,
							   "127.0.0.1",
							   PGC_POSTMASTER,
							   0,
							   NULL, NULL, NULL);

	DefineCustomIntVariable("PGSAUDAUDITENGINE.api_port",
							"RESTful 服务监听端口",
							NULL,
							&pgsql_audit_engine_api_port,
							8900,
							0,
							65535,
							PGC_POSTMASTER,
							0,
							NULL, NULL, NULL);

	DefineCustomStringVariable("PGSAUDAUDITENGINE.api_token",
							   "RESTful 服务鉴权 Token",
							   NULL,
							   &pgsql_audit_engine_api_token,
							   "",
							   PGC_POSTMASTER,
							   0,
							   NULL, NULL, NULL);

	if (process_shared_preload_libraries_in_progress)
	{
		/* 注册共享内存启动钩子（PG11-18 均存在） */
		shmem_startup_hook = pgsql_audit_engine_shmem_startup;

#if PG_VERSION_NUM >= 150000
		/* PG15+ 通过 shmem_request_hook 延迟申请共享内存 */
		shmem_request_hook = pgsql_audit_engine_shmem_request;
#else
		/* PG11-14 无 shmem_request_hook 机制，在 _PG_init 中直接申请 */
		RequestAddinShmemSpace(audit_registry_shmem_size() +
							   audit_record_shmem_size());
#endif

		/* 注册 RESTful 后台 Worker */
		se_api_register_bgworker();
	}

	/* 安装审核钩子（preload 与 session load 均生效） */
	se_install_utility_hook();
	se_install_analyze_hook();
}

void
_PG_fini(void)
{
	se_uninstall_utility_hook();
	se_uninstall_analyze_hook();
}