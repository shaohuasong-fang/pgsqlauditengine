/* -------------------------------------------------------------------------
 *
 * hook_utility.c
 *
 * ProcessUtility 钩子：拦截 DDL/DCL/TCL 等 utility 语句，按 nodeTag 分发
 * 到各规则模块实施审核。
 *
 * -------------------------------------------------------------------------
 */
#include "postgres.h"

#include "commands/extension.h"
#include "tcop/utility.h"
#include "tcop/tcopprot.h"

#include "compat.h"
#include "pgsqlauditengine.h"
#include "hook_utility.h"
#include "rule_ddl.h"
#include "rule_program.h"
#include "rule_dcl.h"
#include "rule_tcl.h"
#include "rule_cmd.h"
#include "rule_object.h"
#include "rule_exists.h"

static se_ProcessUtility_hook_type prev_ProcessUtility_hook = NULL;

/*
 * CREATE 家族两级流水线第一级入口：存在性检查 → 内容审核。
 * 存在性检查命中（EXISTS_CHECK_BLOCK/WARN）时跳过全部内容规则。
 */
static void
se_audit_create_stmt(Node *stmt)
{
	ExistsCheckResult r;

	r = audit_obj_exists_check(stmt);
	if (r != EXISTS_CHECK_PASS)
		return;

	switch (nodeTag(stmt))
	{
		case T_CreateStmt:
		case T_CreateForeignTableStmt:
			audit_ddl_create_table((CreateStmt *) stmt);
			break;
		case T_IndexStmt:
			audit_ddl_index((IndexStmt *) stmt);
			break;
		case T_ViewStmt:
			audit_ddl_view((ViewStmt *) stmt);
			break;
		case T_CreateSeqStmt:
			audit_ddl_create_seq((CreateSeqStmt *) stmt);
			break;
		case T_CreateSchemaStmt:
			audit_ddl_schema((CreateSchemaStmt *) stmt);
			break;
		case T_CreateDomainStmt:
			audit_ddl_create_domain((CreateDomainStmt *) stmt);
			break;
		case T_CompositeTypeStmt:
			audit_ddl_create_composite((CompositeTypeStmt *) stmt);
			break;
		case T_CreateEnumStmt:
			audit_ddl_create_enum((CreateEnumStmt *) stmt);
			break;
		case T_CreateRangeStmt:
			audit_ddl_create_range((CreateRangeStmt *) stmt);
			break;
		case T_CreatedbStmt:
			audit_ddl_createdb((CreatedbStmt *) stmt);
			break;
		case T_CreateTableSpaceStmt:
			audit_ddl_tablespace((CreateTableSpaceStmt *) stmt);
			break;
		case T_DefineStmt:
			{
				DefineStmt *def = (DefineStmt *) stmt;

				if (def->kind == OBJECT_TYPE)
					audit_ddl_define(def);
				else
					audit_obj_advanced(stmt);
			}
			break;
		case T_CreateExtensionStmt:
			audit_obj_extension(stmt);
			break;
		case T_CreatePLangStmt:
			audit_obj_language(stmt);
			break;
		case T_CreateTableAsStmt:
			{
				CreateTableAsStmt *ctas = (CreateTableAsStmt *) stmt;

				if (SE_CTAS_OBJTYPE(ctas) == OBJECT_MATVIEW)
				{
					audit_ddl_matview(ctas);
					audit_obj_matview(stmt);
				}
				else
					audit_obj_ctas(ctas);
			}
			break;
		default:
			break;
	}
}

/*
 * 命令兜底回显：未映射 case 的命令输出 NOTICE 识别回显并写审核记录。
 */
static void
se_audit_echo(PlannedStmt *pstmt)
{
	const char *cmdname;

	if (pstmt == NULL)
		return;

	cmdname = SE_CMDNAME(pstmt);

	ereport(NOTICE,
			(errmsg("SQL审核[echo]: 未映射命令识别: %s", cmdname)));
	audit_record_write(cmdname, NULL, "echo", AUDIT_NOTICE);
}

static void
se_process_utility(PlannedStmt *pstmt, const char *queryString,
#if PG_VERSION_NUM >= 140000
				   bool readOnlyTree,
#endif
				   ProcessUtilityContext context,
				   ParamListInfo params, QueryEnvironment *queryEnv,
				   DestReceiver *dest,
#if PG_VERSION_NUM >= 130000
				   QueryCompletion *qc)
#else
				   char *completionTag)
#endif
{
	Node	   *parsetree;
#if PG_VERSION_NUM < 130000
	char	   *qc = completionTag;		/* PG11-12 的 completionTag 别名，供统一链式调用 */
#endif

	if (pstmt != NULL)
		parsetree = pstmt->utilityStmt;
	else
		parsetree = NULL;

	if (pgsql_audit_engine_enabled && parsetree != NULL &&
		!creating_extension)
	{
		switch (nodeTag(parsetree))
		{
				/* ---- DDL: CREATE（两级流水线：存在性检查 → 内容审核） ---- */
			case T_CreateStmt:
			case T_CreateForeignTableStmt:
			case T_IndexStmt:
			case T_ViewStmt:
			case T_CreateSeqStmt:
			case T_CreateDomainStmt:
			case T_CompositeTypeStmt:
			case T_CreateEnumStmt:
			case T_CreateRangeStmt:
			case T_CreatedbStmt:
			case T_CreateTableSpaceStmt:
			case T_CreateSchemaStmt:
			case T_DefineStmt:
			case T_CreateExtensionStmt:
			case T_CreatePLangStmt:
			case T_CreateTableAsStmt:
				se_audit_create_stmt(parsetree);
				audit_tcl_note_ddl();
				break;

			case T_CreateRoleStmt:
				audit_ddl_create_role((CreateRoleStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

				/* ---- DDL: ALTER ---- */
			case T_AlterTableStmt:
				{
					AlterTableStmt *alt = (AlterTableStmt *) parsetree;

					if (SE_ALT_OBJTYPE(alt) == OBJECT_MATVIEW)
						audit_obj_matview((Node *) alt);
					else
						audit_ddl_alter_table(alt);
					audit_tcl_note_ddl();
				}
				break;

			case T_AlterRoleStmt:
				audit_ddl_alter_role((AlterRoleStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

			case T_AlterRoleSetStmt:
				audit_ddl_alter_role_set((AlterRoleSetStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

			case T_AlterSeqStmt:
				audit_ddl_alter_seq((AlterSeqStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

			case T_AlterDomainStmt:
				audit_ddl_alter_domain((AlterDomainStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

			case T_AlterOwnerStmt:
				audit_ddl_alter_owner((AlterOwnerStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

			case T_AlterObjectSchemaStmt:
				audit_ddl_alter_object_schema((AlterObjectSchemaStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

#if PG_VERSION_NUM >= 130000
			case T_AlterTypeStmt:
				audit_ddl_alter_type((AlterTypeStmt *) parsetree);
				audit_tcl_note_ddl();
				break;
#endif

			case T_AlterEnumStmt:
				audit_ddl_alter_enum((AlterEnumStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

			case T_AlterDatabaseStmt:
				audit_ddl_alter_database((AlterDatabaseStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

			case T_AlterFunctionStmt:
				audit_program_alter_function((AlterFunctionStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

			case T_AlterEventTrigStmt:
				audit_program_alter_event_trigger((AlterEventTrigStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

				/* ---- DDL: DROP / RENAME / COMMENT / TRUNCATE ---- */
			case T_DropStmt:
				{
					DropStmt   *drop = (DropStmt *) parsetree;

					switch (drop->removeType)
					{
						case OBJECT_POLICY:
							audit_obj_policy((Node *) drop);
							break;
						case OBJECT_PUBLICATION:
							audit_obj_publication((Node *) drop);
							break;
						case OBJECT_SUBSCRIPTION:
							audit_obj_subscription((Node *) drop);
							break;
						case OBJECT_FDW:
						case OBJECT_FOREIGN_SERVER:
						case OBJECT_USER_MAPPING:
							audit_obj_fdw((Node *) drop);
							break;
						case OBJECT_EXTENSION:
							audit_obj_extension((Node *) drop);
							break;
						case OBJECT_LANGUAGE:
							audit_obj_language((Node *) drop);
							break;
						case OBJECT_MATVIEW:
							audit_obj_matview((Node *) drop);
							break;
						case OBJECT_AGGREGATE:
						case OBJECT_OPERATOR:
						case OBJECT_TYPE:
						case OBJECT_COLLATION:
						case OBJECT_CONVERSION:
						case OBJECT_TSCONFIGURATION:
						case OBJECT_TSDICTIONARY:
						case OBJECT_TSPARSER:
						case OBJECT_TSTEMPLATE:
							audit_obj_advanced((Node *) drop);
							break;
						default:
							audit_ddl_drop(drop);
							break;
					}
					audit_tcl_note_ddl();
				}
				break;

			case T_DropdbStmt:
				audit_ddl_dropdb((DropdbStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

			case T_DropTableSpaceStmt:
				audit_ddl_droptablespace((DropTableSpaceStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

			case T_DropRoleStmt:
				audit_ddl_droprole((DropRoleStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

			case T_DropOwnedStmt:
				audit_ddl_dropowned((DropOwnedStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

			case T_RenameStmt:
				audit_ddl_rename((RenameStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

			case T_CommentStmt:
				audit_ddl_comment((CommentStmt *) parsetree);
				break;

			case T_SecLabelStmt:
				audit_obj_security_label((SecLabelStmt *) parsetree);
				break;

			case T_TruncateStmt:
				audit_ddl_truncate((TruncateStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

				/* ---- 可编程对象 ---- */
			case T_CreateFunctionStmt:
				audit_program_function((CreateFunctionStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

			case T_CreateTrigStmt:
				audit_program_trigger((CreateTrigStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

			case T_CreateEventTrigStmt:
				audit_program_event_trigger((CreateEventTrigStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

			case T_RuleStmt:
				audit_program_rule((RuleStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

			case T_DoStmt:
				audit_program_do((DoStmt *) parsetree);
				break;

				/* ---- DCL ---- */
			case T_GrantStmt:
				audit_dcl_grant((GrantStmt *) parsetree);
				break;

			case T_GrantRoleStmt:
				audit_dcl_grant_role((GrantRoleStmt *) parsetree);
				break;

			case T_AlterDefaultPrivilegesStmt:
				audit_dcl_default_privileges((AlterDefaultPrivilegesStmt *) parsetree);
				break;

				/* ---- TCL ---- */
			case T_TransactionStmt:
				audit_tcl_transaction((TransactionStmt *) parsetree);
				break;

			case T_ConstraintsSetStmt:
				audit_tcl_set_transaction(parsetree);
				break;

				/* ---- 维护/性能/数据操作类命令 (5.11 cmd_*) ---- */
			case T_VacuumStmt:
				{
					VacuumStmt *vac = (VacuumStmt *) parsetree;

				if (SE_IS_VACUUMCMD(vac))
					audit_cmd_vacuum(vac);
					else
						audit_cmd_analyze(vac);
				}
				break;

			case T_CheckPointStmt:
				audit_cmd_checkpoint((CheckPointStmt *) parsetree);
				break;

			case T_ClusterStmt:
				audit_cmd_cluster((ClusterStmt *) parsetree);
				break;

			case T_ReindexStmt:
				audit_cmd_reindex((ReindexStmt *) parsetree);
				break;

			case T_RefreshMatViewStmt:
				audit_cmd_refresh_matview((RefreshMatViewStmt *) parsetree);
				break;

			case T_LockStmt:
				audit_cmd_lock((LockStmt *) parsetree);
				break;

			case T_LoadStmt:
				audit_cmd_load((LoadStmt *) parsetree);
				break;

			case T_DiscardStmt:
				audit_cmd_discard((DiscardStmt *) parsetree);
				break;

			case T_ExplainStmt:
				audit_cmd_explain((ExplainStmt *) parsetree);
				break;

			case T_PrepareStmt:
			case T_ExecuteStmt:
			case T_DeallocateStmt:
				audit_cmd_prepare(parsetree);
				break;

			case T_DeclareCursorStmt:
			case T_FetchStmt:
			case T_ClosePortalStmt:
				audit_cmd_cursor(parsetree);
				break;

			case T_ListenStmt:
			case T_NotifyStmt:
			case T_UnlistenStmt:
				audit_cmd_notify(parsetree);
				break;

				/* ---- 对象类命令 (5.11 obj_*) ---- */
			case T_CreatePolicyStmt:
			case T_AlterPolicyStmt:
				audit_obj_policy(parsetree);
				audit_tcl_note_ddl();
				break;

			case T_CreatePublicationStmt:
			case T_AlterPublicationStmt:
				audit_obj_publication(parsetree);
				audit_tcl_note_ddl();
				break;

			case T_CreateSubscriptionStmt:
			case T_AlterSubscriptionStmt:
			case T_DropSubscriptionStmt:
				audit_obj_subscription(parsetree);
				audit_tcl_note_ddl();
				break;

			case T_CreateFdwStmt:
			case T_AlterFdwStmt:
			case T_CreateForeignServerStmt:
			case T_AlterForeignServerStmt:
			case T_CreateUserMappingStmt:
			case T_AlterUserMappingStmt:
			case T_DropUserMappingStmt:
			case T_ImportForeignSchemaStmt:
				audit_obj_fdw(parsetree);
				audit_tcl_note_ddl();
				break;

			case T_AlterExtensionStmt:
				audit_obj_extension(parsetree);
				audit_tcl_note_ddl();
				break;

			case T_CreateAmStmt:
			case T_CreateCastStmt:
			case T_CreateOpClassStmt:
			case T_AlterOpFamilyStmt:
			case T_CreateConversionStmt:
			case T_CreateTransformStmt:
			case T_CreateStatsStmt:
#if PG_VERSION_NUM >= 130000
			case T_AlterStatsStmt:
#endif
			case T_AlterTSDictionaryStmt:
			case T_AlterTSConfigurationStmt:
				audit_obj_advanced(parsetree);
				audit_tcl_note_ddl();
				break;

			case T_AlterSystemStmt:
				audit_obj_alter_system((AlterSystemStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

			case T_ReassignOwnedStmt:
				audit_obj_reassign_owned((ReassignOwnedStmt *) parsetree);
				audit_tcl_note_ddl();
				break;

				/* ---- COPY / CALL / SET ---- */
			case T_CopyStmt:
				audit_cmd_copy((CopyStmt *) parsetree);
				break;

			case T_CallStmt:
				audit_cmd_call((CallStmt *) parsetree);
				break;

			case T_VariableSetStmt:
				{
					VariableSetStmt *vs = (VariableSetStmt *) parsetree;

					if (vs->kind == VAR_SET_MULTI)
						audit_tcl_set_transaction((Node *) vs);
					else if (vs->name != NULL &&
							 (strcmp(vs->name, "role") == 0 ||
							  strcmp(vs->name, "session_authorization") == 0))
						audit_dcl_set_role(vs);
					else
						audit_cmd_set(vs);
				}
				break;

			case T_VariableShowStmt:
				audit_cmd_show((VariableShowStmt *) parsetree);
				break;

			default:
				se_audit_echo(pstmt);
				break;
		}
	}

	if (prev_ProcessUtility_hook)
		prev_ProcessUtility_hook SE_PU_HOOK_PARAMS(pstmt, queryString, readOnlyTree,
												   context, params, queryEnv, dest, qc);
	else
		standard_ProcessUtility SE_PU_HOOK_PARAMS(pstmt, queryString, readOnlyTree,
												  context, params, queryEnv, dest, qc);
}

void
se_install_utility_hook(void)
{
	prev_ProcessUtility_hook = ProcessUtility_hook;
	ProcessUtility_hook = se_process_utility;
}

void
se_uninstall_utility_hook(void)
{
	ProcessUtility_hook = prev_ProcessUtility_hook;
}