#ifndef PGSQLAUDITENGINE_RULE_REGISTRY_H
#define PGSQLAUDITENGINE_RULE_REGISTRY_H

#include "pgsqlauditengine.h"

/* 共享内存中规则配置所需的额外空间 */
extern Size audit_registry_shmem_size(void);

/* 在共享内存启动阶段创建规则配置数组 */
extern void audit_registry_shmem_startup(void);

/* 获取规则总数 */
extern int	audit_num_rules(void);

/* 按索引取得规则定义（供 RESTful 枚举） */
extern const AuditRuleDef *audit_rule_at(int i);

/* 按索引取得/设置运行期配置（供 RESTful 读写） */
extern AuditRuleConfig audit_rule_config_at(int i);
extern void	audit_rule_set_config(int i, bool enabled, AuditLevel level);

/* 命令→规则映射查询（spec 5.11.2），按命令族返回映射规则标识；未登记返回 NULL */
extern const char *cmd_map_lookup(const char *command_family);

#endif							/* PGSQLAUDITENGINE_RULE_REGISTRY_H */