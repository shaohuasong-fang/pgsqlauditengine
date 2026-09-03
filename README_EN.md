# pgsqlauditengine

[中文](https://github.com/shaohuasong-fang/pgsqlauditengine/blob/main/README.md) | [英文](https://github.com/shaohuasong-fang/pgsqlauditengine/blob/main/README_EN.md)

PostgreSQL SQL Audit Extension (SQL Audit Engine for PostgreSQL): provides static auditing for DDL / DML / DCL / TCL / command-type statements in PostgreSQL through a combination of a shared library, a Background Worker, and an embedded RESTful API. It supports PostgreSQL versions 11 through 18.

pgsqlauditengine mounts `ProcessUtility_hook` and `post_parse_analyze_hook` to evaluate statements before execution. Based on rule severity, it enforces the semantics of NOTICE allow / WARNING block / ERROR block. The audit process does not modify any statement and does not depend on external services. Rule switches and levels can be adjusted in real time through the embedded RESTful management service.

---

## 1. Project Overview

pgsqlauditengine is a PostgreSQL extension that performs static SQL auditing before statement execution at the database kernel level. It helps DBAs and developers move database object design standards and SQL coding conventions forward to the execution stage:

- Audit scope: Covers DDL (CREATE TABLE/INDEX/VIEW/FUNCTION/MATERIALIZED VIEW, etc.), DML (UPDATE/DELETE/SELECT standards), DCL (permissions/roles), TCL (transactions/savepoints), and command statements (VACUUM/COPY/REINDEX, etc.), with fallback command echoing to guarantee that no command is silently allowed through.
- Implementation model: shared library `pgsqlauditengine.so` (audit logic) + Background Worker (RESTful management service) + shared memory (rule configuration area and circular audit log buffer).
- Zero intrusion: The extension hooks are mounted without modifying PostgreSQL kernel source code. The same source code is compatible with PG11-18; cross-version differences are encapsulated in `compat.h`.

## 2. Core Features

- Three-tier severity semantics (effective across all PG11-18 versions):

  | Rule Level | Output Level | Behavior |
  |----------|----------|------|
  | Mandatory | ERROR (`ERRCODE_CHECK_VIOLATION`) | Blocked: statement does not execute |
  | Recommended | WARNING | Blocked: statement does not execute (output level prefix is ERROR and includes the `SQL Audit Level: WARNING (statement blocked)` errdetail marker) |
  | Preferred | NOTICE | Allowed: statement executes normally |



## 3. Version Support Matrix

| Version | Kernel Source | Remote Install Directory | PostgreSQL Listen Port | RESTful Port | Validation Result |
|------|----------|--------------|-------------|--------------|----------|
| PG11 | `postgresql-11.22` | `/usr/local/pgversion/11/pgsql` | 54311 | 8911 | PASS |
| PG12 | `postgresql-12.22` | `/usr/local/pgversion/12/pgsql` | 54312 | 8912 | PASS |
| PG13 | `postgresql-13.23` | `/usr/local/pgversion/13/pgsql` | 54313 | 8913 | PASS |
| PG14 | `postgresql-14.24` | `/usr/local/pgversion/14/pgsql` | 54314 | 8914 | PASS |
| PG15 | `postgresql-15.19` | `/usr/local/pgversion/15/pgsql` | 54315 | 8915 | PASS |
| PG16 | `postgresql-16.15` | `/usr/local/pgversion/16/pgsql` | 54316 | 8916 | PASS |
| PG17 | `postgresql-17.11` | `/usr/local/pgsql` | 5432 | 8917 | PASS |
| PG18 | `postgresql-18.6` | `/usr/local/pgversion/18/pgsql` | 54318 | 8918 | PASS |

### Version Boundary Capabilities

| Capability | PG11 | PG12 | PG13 | PG14 | PG15 | PG16 | PG17 | PG18 |
|------|------|------|------|------|------|------|------|------|
| `REINDEX CONCURRENTLY` audit | No syntax | Triggered | Triggered | Triggered | Triggered | Triggered | Triggered | Triggered |
| `ALTER STATISTICS` audit | No syntax | No syntax | Triggered | Triggered | Triggered | Triggered | Triggered | Triggered |
| `MERGE` audit | No syntax | No syntax | No syntax | No syntax | Triggered | Triggered | Triggered | Triggered |

## 4. Quick Start

### 4.1 Build and Install (PGXS)

```bash
cd postgresql-${version}/contrib/
git clone https://github.com/shaohuasong-fang/pgsqlauditengine.git
cd pgsqlauditengine
# Single-version build and install (PG18)
rm -f *.o *.so                              # PGXS does not track .h dependencies; after changing compat.h, a full rebuild is required
make -j$(nproc) USE_PGXS=1 PG_CONFIG=/usr/local/pgversion/18/pgsql/bin/pg_config
make -j$(nproc) USE_PGXS=1 PG_CONFIG=/usr/local/pgversion/18/pgsql/bin/pg_config install   # requires root privileges
```

### 4.2 Configuration (`postgresql.conf` or `postgresql.auto.conf`)

Append the following settings and restart the instance (`api_*` is POSTMASTER-level and requires a restart to take effect):

```
shared_preload_libraries = 'pgsqlauditengine'
PGSAUDAUDITENGINE.enabled = on
PGSAUDAUDITENGINE.check_dml = on
PGSAUDAUDITENGINE.api_listen = '127.0.0.1'
PGSAUDAUDITENGINE.api_port = 8918            # Independent per version, such as 8911~8918
PGSAUDAUDITENGINE.api_token = ''
```

```bash
# Restart the instance (PG18)
su - postgres -c '/usr/local/pgversion/18/pgsql/bin/pg_ctl restart -D /data/18/data -o "-p 54318" -l /data/18/data/server.log'
```

### 4.3 Five GUC Parameters

| Parameter Name | Type | Default | Context | Description |
|--------|------|--------|--------|------|
| `PGSAUDAUDITENGINE.enabled` | bool | off | SUSET | Global audit switch |
| `PGSAUDAUDITENGINE.check_dml` | bool | on | SUSET | DML statement audit switch |
| `PGSAUDAUDITENGINE.api_listen` | string | 127.0.0.1 | POSTMASTER | RESTful listen address |
| `PGSAUDAUDITENGINE.api_port` | int | 8900 | POSTMASTER | RESTful listen port |
| `PGSAUDAUDITENGINE.api_token` | string | empty | POSTMASTER | API auth token (empty means no auth required) |

### 4.4 Create the Extension and Verify

```bash
export PGPASSWORD=postgres
PSQL=/usr/local/pgversion/18/pgsql/bin/psql

# Create the extension (metadata registration only; hooks are loaded by shared_preload)
$PSQL -h 127.0.0.1 -p 54318 -U postgres -d postgres -c "CREATE EXTENSION IF NOT EXISTS pgsqlauditengine;"

# Verify GUC registration
$PSQL -h 127.0.0.1 -p 54318 -U postgres -d postgres \
  -tAc "SELECT name, boot_val, context FROM pg_settings WHERE name LIKE 'PGSAUDAUDITENGINE.%' ORDER BY 1;"

# Verify that the audit hook is active (count(column) triggers dml_count_star and should be an ERROR block)
$PSQL -h 127.0.0.1 -p 54318 -U postgres -d postgres \
  -c "SELECT count(name) FROM pg_settings WHERE name LIKE '%audit%';"

# Verify the RESTful service
curl -s http://127.0.0.1:8918/api/v1/health
curl -s http://127.0.0.1:8918/api/v1/rules | grep -o '"id"' | wc -l    # 102
```

## 5. Usage Examples

### 5.1 psql Audit Output (Three Severity Levels)

```sql
-- Trigger ERROR blocking: table name uppercase (name_charset, ERROR level → blocked)
-- Trigger WARNING blocking: materialized view requires refresh strategy evaluation (obj_matview, WARNING level → blocked)
-- Trigger NOTICE allow: object drop echo (ddl_drop_object, NOTICE level → allowed)
```

Actual output (consistent with verify baseline `out_test_pg18.log`):

```text
psql:/tmp/reg_pg18/seg_cases.sql:7: ERROR:  SQL审核[name_charset]: 表名称 "SeTUpper" 只能由小写字母(a-z)、数字(0-9)、下划线(_)构成
psql:/tmp/reg_pg18/seg_p0.sql:46: ERROR:  SQL审核[obj_matview]: 物化视图需评估数据新鲜度与刷新策略 (物化视图: vw_se_mv)
psql:/tmp/reg_pg18/seg_p0.sql:10: NOTICE:  SQL审核[ddl_drop_object]: 对象删除回显 (对象类型: 表, 名称: se_t_base)
```

### 5.2 RESTful API Examples

Base URL: `http://127.0.0.1:8918/api/v1`; auth header: `Authorization: Bearer <api_token>` (if the token is empty, authentication is not required; `/health` is always public). Responses follow a unified envelope: `{"code","error_code","error_message","data"}`.

```bash
# 1) Health check (no authentication required)
curl -s http://127.0.0.1:8918/api/v1/health

# 2) Rule list (102 items)
curl -s -H 'Authorization: Bearer test-token-123' http://127.0.0.1:8918/api/v1/rules

# 3) Single rule detail
curl -s -H 'Authorization: Bearer test-token-123' http://127.0.0.1:8918/api/v1/rules/dml_left_fuzzy

# 4) Modify a rule (takes effect immediately)
curl -s -X PUT -H 'Authorization: Bearer test-token-123' -H 'Content-Type: application/json' \
     -d '{"enabled":false}' http://127.0.0.1:8918/api/v1/rules/dml_left_fuzzy

# 5) Restore default rule settings
curl -s -X DELETE -H 'Authorization: Bearer test-token-123' http://127.0.0.1:8918/api/v1/rules/dml_left_fuzzy

# 6) Audit log (circular buffer)
curl -s -H 'Authorization: Bearer test-token-123' http://127.0.0.1:8918/api/v1/audit-logs

# 7) Export full runtime configuration
curl -s -H 'Authorization: Bearer test-token-123' http://127.0.0.1:8918/api/v1/config
```

| Method | Path | Auth | Description |
|------|------|------|------|
| GET | `/api/v1/health` | No | Health check |
| GET | `/api/v1/rules` | Yes | Rule list |
| GET | `/api/v1/rules/{id}` | Yes | Single rule detail |
| PUT | `/api/v1/rules/{id}` | Yes | Modify rule enablement/severity |
| DELETE | `/api/v1/rules/{id}` | Yes | Restore default rule configuration |
| GET | `/api/v1/audit-logs` | Yes | Audit logs (circular buffer) |
| GET | `/api/v1/config` | Yes | Full runtime configuration export |

## 6. Directory Structure

```
PostgreSQLClass/
├── pgsqlauditengine/               # Extension source and documentation (the only deliverable)
│   ├── rule_registry.c/.h          # Rule registry g_rule_defs[] (102 rules) and command mapping g_cmd_map[]
│   ├── rule_ddl.c / rule_exists.c  # DDL/index audit logic (includes CIC static/directory check separation fix)
│   ├── rule_dml* and hook_analyze.c # DML audit (post_parse_analyze hook)
│   ├── hook_utility.c              # ProcessUtility hook and command fallback
│   ├── api_route.c / api_server.c  # RESTful routes and Background Worker
│   ├── audit_record.c / compat.h   # Audit circular buffer / cross-version compatibility layer
│   ├── pgsqlauditengine--1.0.sql   # Extension SQL metadata
│   ├── Makefile / .control
```

## 7. References

- PostgreSQL official command reference: https://www.postgresql.org/docs/current/sql-commands.html
