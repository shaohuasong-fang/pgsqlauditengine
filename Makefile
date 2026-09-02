# contrib/pgsqlauditengine/Makefile
#
# 跨版本（PostgreSQL 11-18）构建说明：
#   本扩展同时兼容 PostgreSQL 11 ~ 18，所有版本差异均在源码层通过
#   PG_VERSION_NUM 条件编译（集中封装于 compat.h）处理，Makefile 不做任何
#   版本判断。编译命令（对每个目标版本执行）：
#     make USE_PGXS=1 PG_CONFIG=<版本安装目录>/bin/pg_config
#   例：
#     make USE_PGXS=1 PG_CONFIG=/usr/local/postgresql-11.22/bin/pg_config
#     make USE_PGXS=1 PG_CONFIG=/usr/local/postgresql-18.4/bin/pg_config
#   注意：构建产物需在目标版本服务器上完成（PGXS 依赖 pg_config）。
#

MODULE_big = pgsqlauditengine
OBJS = pgsqlauditengine.o rule_registry.o rule_common.o rule_ddl.o rule_program.o \
	rule_dcl.o rule_tcl.o rule_cmd.o rule_object.o rule_exists.o \
	hook_utility.o hook_analyze.o audit_record.o \
	api_server.o api_route.o

EXTENSION = pgsqlauditengine
DATA = pgsqlauditengine--1.0.sql
PGFILEDESC = "pgsqlauditengine - PostgreSQL SQL Audit Tools"

NO_INSTALLCHECK = 1

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/pgsqlauditengine
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
