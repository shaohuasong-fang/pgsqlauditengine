#ifndef PGSQLAUDITENGINE_HOOK_ANALYZE_H
#define PGSQLAUDITENGINE_HOOK_ANALYZE_H

#include "postgres.h"
#include "parser/analyze.h"

extern void se_install_analyze_hook(void);
extern void se_uninstall_analyze_hook(void);

#endif							/* PGSQLAUDITENGINE_HOOK_ANALYZE_H */