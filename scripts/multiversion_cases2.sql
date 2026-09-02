\set VERBOSITY terse
DROP TABLE IF EXISTS se_v_dml;
CREATE TABLE se_v_dml(id int NOT NULL, name text NOT NULL, PRIMARY KEY(id));
DELETE FROM se_v_dml;
UPDATE se_v_dml SET name = 'x';
SELECT count(name) FROM se_v_dml;
SELECT * FROM se_v_dml WHERE name LIKE '%abc';
SELECT * FROM se_v_dml WHERE id IN (SELECT id FROM se_v_dml);
INSERT INTO se_v_dml VALUES (1, 'a'), (2, 'b');
REINDEX TABLE se_v_dml;
REINDEX INDEX CONCURRENTLY se_v_dml_pkey;
CREATE STATISTICS se_v_stats ON id, name FROM se_v_dml;
ALTER STATISTICS se_v_stats SET STATISTICS 100;
MERGE INTO se_v_dml t USING (SELECT 3 AS id) s ON (t.id = s.id) WHEN NOT MATCHED THEN INSERT (id) VALUES (s.id);
