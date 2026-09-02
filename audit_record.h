#ifndef PGSQLAUDITENGINE_AUDIT_RECORD_H
#define PGSQLAUDITENGINE_AUDIT_RECORD_H

#include "datatype/timestamp.h"
#include "pgsqlauditengine.h"

#define AUDIT_RECORD_BUF_CAP	4096
#define AUDIT_RECORD_STMT_LEN	32
#define AUDIT_RECORD_OBJ_LEN	128
#define AUDIT_RECORD_RULE_LEN	64

typedef struct AuditRecord
{
	TimestampTz	ts;
	char		stmt_type[AUDIT_RECORD_STMT_LEN];
	char		object_name[AUDIT_RECORD_OBJ_LEN];
	char		rule_id[AUDIT_RECORD_RULE_LEN];
	AuditLevel	level;
	AuditConclusion conclusion;
} AuditRecord;

/* 共享内存空间占用 */
extern Size audit_record_shmem_size(void);
/* 在共享内存启动阶段创建环形缓冲 */
extern void audit_record_shmem_startup(void);

/*
 * 写入一条审核记录（backend 侧调用，容量满时覆盖最旧记录）。
 * 若共享内存尚未初始化则在无缓冲模式下静默返回。
 */
extern void audit_record_write(const char *stmt_type, const char *object_name,
							   const char *rule_id, AuditLevel level);

/*
 * 读取当前缓冲中全部记录（worker/RESTful 侧调用）。
 * 调用方传入 result 数组（由调用方分配，容量为 cap），返回实际填充条数。
 */
extern int	audit_record_query(AuditRecord *result, int cap);

#endif							/* PGSQLAUDITENGINE_AUDIT_RECORD_H */