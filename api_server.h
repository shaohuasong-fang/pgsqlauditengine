#ifndef PGSQLAUDITENGINE_API_SERVER_H
#define PGSQLAUDITENGINE_API_SERVER_H

#include "postgres.h"
#include "postmaster/bgworker.h"

extern void se_api_register_bgworker(void);
extern PGDLLEXPORT void se_api_server_main(Datum main_arg);

#endif							/* PGSQLAUDITENGINE_API_SERVER_H */