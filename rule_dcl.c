/* -------------------------------------------------------------------------
 *
 * rule_dcl.c
 *
 * 权限控制（DCL）审核：GRANT/REVOKE 对象权限、GRANT/REVOKE 角色成员、
 * ALTER DEFAULT PRIVILEGES。
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "nodes/pg_list.h"
#include "nodes/value.h"
#include "compat.h"

#include "pgsqlauditengine.h"
#include "rule_dcl.h"

static const char *
rolespec_name(RoleSpec *rs)
{
	if (rs == NULL)
		return NULL;
	return rs->rolename;
}

/* 从 GrantStmt 的 objects 列表取首个对象名 */
static const char *
grant_object_name(GrantStmt *stmt)
{
	ListCell   *lc;

	if (stmt->objects == NIL)
		return NULL;

	lc = list_head(stmt->objects);
	{
		Node	   *n = (Node *) lfirst(lc);

		if (IsA(n, RangeVar))
			return ((RangeVar *) n)->relname;
		if (IsA(n, String))
			return strVal(n);
		if (IsA(n, ObjectWithArgs))
		{
			ObjectWithArgs *owa = (ObjectWithArgs *) n;

			if (owa->objname != NIL)
				return strVal(llast(owa->objname));
		}
	}
	return NULL;
}

void
audit_dcl_grant(GrantStmt *stmt)
{
	const char *objname;
	const char *grantee = NULL;
	ListCell   *lc;

	if (stmt == NULL)
		return;

	objname = grant_object_name(stmt);

	/* 取首个被授权/收回者 */
	foreach(lc, stmt->grantees)
	{
		grantee = rolespec_name((RoleSpec *) lfirst(lc));
		if (grantee != NULL)
			break;
	}

	audit_emit(AUDIT_WARNING, "dcl_grant",
			   "对象权限授予/回收需确认权限边界 (%s对象: %s, 目标角色: %s)",
			   stmt->is_grant ? "授予" : "回收",
			   objname ? objname : "?",
			   grantee ? grantee : "?");

	audit_record_write(stmt->is_grant ? "GRANT" : "REVOKE",
					   objname, "dcl", AUDIT_NOTICE);
}

void
audit_dcl_grant_role(GrantRoleStmt *stmt)
{
	const char *granted = NULL;
	const char *grantee = NULL;

	if (stmt == NULL)
		return;

	if (stmt->granted_roles != NIL)
	{
		Node	   *n = (Node *) linitial(stmt->granted_roles);

		if (IsA(n, AccessPriv))
			granted = ((AccessPriv *) n)->priv_name;
		else if (IsA(n, String))
			granted = strVal(n);
	}

	if (stmt->grantee_roles != NIL)
	{
		Node	   *n = (Node *) linitial(stmt->grantee_roles);

		if (IsA(n, RoleSpec))
			grantee = rolespec_name((RoleSpec *) n);
		else if (IsA(n, String))
			grantee = strVal(n);
	}

	audit_emit(AUDIT_WARNING, "dcl_grant_role",
			   "角色成员授予/回收需确认角色边界 (%s角色: %s, 目标成员: %s)",
			   stmt->is_grant ? "授予" : "回收",
			   granted ? granted : "?",
			   grantee ? grantee : "?");

	audit_record_write(stmt->is_grant ? "GRANT ROLE" : "REVOKE ROLE",
					   granted ? granted : grantee, "dcl", AUDIT_NOTICE);
}

void
audit_dcl_default_privileges(AlterDefaultPrivilegesStmt *stmt)
{
	if (stmt == NULL)
		return;

	audit_emit(AUDIT_WARNING, "dcl_default_privileges",
			   "默认权限变更影响后续创建对象");
	audit_record_write("ALTER DEFAULT PRIVILEGES", NULL, "dcl", AUDIT_NOTICE);
}

/*
 * 会话身份切换：SET ROLE / SET SESSION AUTHORIZATION
 * （由 hook_utility.c 在 T_VariableSetStmt case 内按 name 分派调用）
 */
void
audit_dcl_set_role(VariableSetStmt *stmt)
{
	const char *rolename = NULL;
	const char *cmd = "SET ROLE";

	if (stmt == NULL)
		return;

	if (stmt->name != NULL &&
		strcmp(stmt->name, "session_authorization") == 0)
		cmd = "SET SESSION AUTHORIZATION";

	if (stmt->args != NIL)
	{
		Node	   *arg = (Node *) linitial(stmt->args);

		if (IsA(arg, A_Const))
		{
			A_Const    *c = (A_Const *) arg;

			if (SE_A_CONST_STR(c) != NULL)
				rolename = SE_A_CONST_STR(c);
		}
	}

	audit_emit(AUDIT_WARNING, "dcl_set_role",
			   "会话身份切换需确认权限边界 (%s 目标角色: %s)",
			   cmd, rolename ? rolename : "?");
	audit_record_write(cmd, rolename, "dcl_set_role", AUDIT_WARNING);
}