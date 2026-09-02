#ifndef PGSQLAUDITENGINE_HOOK_UTILITY_H
#define PGSQLAUDITENGINE_HOOK_UTILITY_H

#include "postgres.h"
#include "tcop/utility.h"

extern void se_install_utility_hook(void);
extern void se_uninstall_utility_hook(void);

#endif							/* PGSQLAUDITENGINE_HOOK_UTILITY_H */