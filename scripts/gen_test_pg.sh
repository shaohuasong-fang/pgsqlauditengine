#!/usr/bin/env bash
# =============================================================================
# gen_test_pg.sh — pgsqlauditengine_test.sql 版本裁剪器（design 4.2.4 / 决策 D19）
# -----------------------------------------------------------------------------
# 用法 : bash scripts/gen_test_pg.sh <ver> pgsqlauditengine_test.sql \
#          > pgsqlauditengine_test_pg<ver>.sql
# 输入 : <ver>        目标 PG 主版本号（11~18，整数）
#        <source.sql> 源测试脚本（含 `-- [PGv+]` 行尾分界标记）
# 输出 : stdout 裁剪后的脚本（可重定向为 pgsqlauditengine_test_pg<ver>.sql）
# 裁剪规则（spec 5.5.1 需求 31 / design 4.2.4 第 2 条）:
#   - 可执行语句行行尾标记 `-- [PGv+]`（多行语句标记在首行）:
#       目标版本 < v   → 整行注释（多行语句含续行直至 `;` 结尾行）
#       目标版本 >= v  → 原样保留
#   - `[PG11+]` 等价无分界（PG11 起全版本支持），n<=ver 恒成立，不裁剪
#   - 无标记语句全版本执行；注释行（行首 `--`）原样保留
#   - 被裁剪行前缀 `-- [CUT PG<ver>] `，可追溯裁剪点
#   - 语句结束判定：非注释行内含 `;` 即视为语句结束（兼容行尾带 `-- [PGv+]`
#     注释的语句，修复分号后接注释时 `;[\t ]*$` 不匹配导致的 cut 状态泄漏）
# 验收 : 生成的 8 个版本脚本 `psql -f` 试跑无 syntax error（任务组 21.2/28）
# =============================================================================
set -u

if [ $# -lt 2 ]; then
    echo "usage: $0 <ver> <source.sql>" >&2
    exit 1
fi

VER="$1"
SRC="$2"

case "$VER" in
    1[1-8]) ;;
    *) echo "error: <ver> 必须为 11~18 的整数（当前: $VER）" >&2; exit 1 ;;
esac

if [ ! -f "$SRC" ]; then
    echo "error: 源脚本不存在: $SRC" >&2
    exit 1
fi

awk -v ver="$VER" '
function extract_n(tag,   t) {
    t = tag
    sub(/^\[PG/, "", t)
    gsub(/[^0-9]/, "", t)
    return (t == "" ? 0 : t + 0)
}
BEGIN { cut = 0 }
{
    line = $0
    is_comment = (line ~ /^--/)
    has_semi = (index(line, ";") > 0)

    if (cut) {
        if (!is_comment)
            print "-- [CUT PG" ver "] " line
        else
            print line
        if (!is_comment && has_semi)
            cut = 0
        next
    }

    if (match(line, /\[PG[0-9]+\+\]/)) {
        tag = substr(line, RSTART, RLENGTH)
        n = extract_n(tag)
        if (is_comment || n <= ver) {
            print line
            next
        }
        print "-- [CUT PG" ver "] " line
        if (!has_semi)
            cut = 1
        next
    }

    print line
}
' "$SRC"