/* -------------------------------------------------------------------------
 *
 * audit_record.c
 *
 * 审核记录共享内存环形缓冲区。backend 写入、RESTful worker 读取，
 * 定长记录、覆盖写、不持久化。
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "storage/ipc.h"
#include "storage/shmem.h"
#include "utils/timestamp.h"

#include "pgsqlauditengine.h"
#include "audit_record.h"

typedef struct AuditRecordBuf
{
	uint32		head;			/* 下一个写入位置 */
	uint32		count;			/* 当前有效条数 (<= cap) */
	AuditRecord	records[AUDIT_RECORD_BUF_CAP];
} AuditRecordBuf;

static AuditRecordBuf *g_record_buf = NULL;

Size
audit_record_shmem_size(void)
{
	return sizeof(AuditRecordBuf);
}

void
audit_record_shmem_startup(void)
{
	bool		found;

	g_record_buf = (AuditRecordBuf *)
		ShmemInitStruct("PGSQLAuditEngine Record Buffer",
						sizeof(AuditRecordBuf),
						&found);
	if (!found)
	{
		g_record_buf->head = 0;
		g_record_buf->count = 0;
	}
}

static void
copy_str(char *dst, const char *src, int maxlen)
{
	if (src == NULL)
	{
		dst[0] = '\0';
		return;
	}
	strlcpy(dst, src, maxlen);
}

void
audit_record_write(const char *stmt_type, const char *object_name,
				   const char *rule_id, AuditLevel level)
{
	AuditRecord *rec;
	uint32		pos;

	if (g_record_buf == NULL)
		return;

	pos = g_record_buf->head;
	rec = &g_record_buf->records[pos];

	rec->ts = GetCurrentTimestamp();
	copy_str(rec->stmt_type, stmt_type, AUDIT_RECORD_STMT_LEN);
	copy_str(rec->object_name, object_name, AUDIT_RECORD_OBJ_LEN);
	copy_str(rec->rule_id, rule_id, AUDIT_RECORD_RULE_LEN);
	rec->level = level;
	rec->conclusion = audit_level_to_conclusion(level);

	/* 环形推进，容量满时覆盖最旧记录 */
	g_record_buf->head = (pos + 1) % AUDIT_RECORD_BUF_CAP;
	if (g_record_buf->count < AUDIT_RECORD_BUF_CAP)
		g_record_buf->count++;
}

int
audit_record_query(AuditRecord *result, int cap)
{
	int			n;
	int			start;
	uint32		count;
	uint32		head;

	if (g_record_buf == NULL)
		return 0;

	count = g_record_buf->count;
	head = g_record_buf->head;

	if (count > (uint32) cap)
		count = (uint32) cap;

	if ((int) count > AUDIT_RECORD_BUF_CAP)
		count = AUDIT_RECORD_BUF_CAP;

	/*
	 * 从最旧记录开始。当 count < cap 时，记录从 0..count-1 顺序填充。
	 * 当缓冲区已满时，最旧记录位于 head（下一个将被覆盖的位置）。
	 */
	if (g_record_buf->count < AUDIT_RECORD_BUF_CAP)
		start = 0;
	else
		start = (int) head;

	for (n = 0; n < (int) count; n++)
	{
		int			idx = (start + n) % AUDIT_RECORD_BUF_CAP;

		result[n] = g_record_buf->records[idx];
	}

	return (int) count;
}