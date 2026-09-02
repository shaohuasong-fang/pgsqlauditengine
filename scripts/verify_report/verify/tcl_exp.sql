\set VERBOSITY terse
-- EXP-A: 事务内单条 DROP TABLE IF EXISTS（计数1），COMMIT 预期不触发
BEGIN;
DROP TABLE IF EXISTS se_exp_a;
COMMIT;
-- EXP-B: 事务内单条 CREATE SEQUENCE（计数1），COMMIT 预期不触发
BEGIN;
CREATE SEQUENCE se_exp_sq;
COMMIT;
-- EXP-C: 事务内 DROP + CREATE SEQUENCE（计数2），COMMIT 预期触发 tcl_multi_ddl_in_txn
BEGIN;
DROP TABLE IF EXISTS se_exp_c;
CREATE SEQUENCE se_exp_sq2;
COMMIT;
-- EXP-D: 触发后状态机是否卡死（单条 DROP 事务应仍被阻断）
BEGIN;
DROP TABLE IF EXISTS se_exp_d;
COMMIT;