/**
 * @file test_wukong_skill.c
 * @brief wukong_skill scan/parse/build_summary/read/reload/id-safety.
 * Run via pytest test_suite.py -k skill.
 */
#include <stdio.h>
#include <string.h>
#include "wukong_test.h"
#include "wukong_storage_test.h"
#include "wukong_skill.h"
#include "tal_memory.h"


static void seed(const char *id, const char *content)
{
    char ns[64];
    snprintf(ns, sizeof(ns), "claw/skills/%s", id);
    wukong_storage_write(ns, "SKILL.md", (const BYTE_T *)content, (UINT_T)strlen(content));
}

int main(void)
{
    wukong_storage_test_reset();

    seed("morning_routine",
         "---\n{\"name\":\"morning_routine\",\"description\":\"Morning briefing across tools\"}\n---\n"
         "# Morning Routine\n## Steps\n1. device_time_get\n");
    seed("no_fm", "# Plain\nno frontmatter body\n");   /* desc 用 id 兜底 */

    EXPECT_OK(wukong_skill_init(), "skill init ok");

    /* build_summary: 含两条,格式 "- id: desc" */
    char *sum = wukong_skill_build_summary();
    EXPECT(sum && strstr(sum, "If a skill below applies") != NULL,
           "summary carries the section-header rule");
    EXPECT_NOT_NULL(sum, "summary not NULL");
    EXPECT_STR_CONTAINS(sum, "- morning_routine: Morning briefing across tools",
                        "summary line for morning_routine");
    EXPECT_STR_CONTAINS(sum, "- no_fm:", "summary line for no_fm (id fallback)");
    if (sum) tal_free(sum);

    /* read: 剥 frontmatter,返回正文,不含 JSON */
    char *body = NULL;
    EXPECT_OK(wukong_skill_read("morning_routine", &body), "read morning_routine ok");
    EXPECT_NOT_NULL(body, "body not NULL");
    EXPECT_STR_CONTAINS(body, "# Morning Routine", "body has heading");
    EXPECT(body && strstr(body, "\"description\"") == NULL, "body strips frontmatter json");
    if (body) tal_free(body);

    /* read: 无 frontmatter → 返回整文件 */
    body = NULL;
    EXPECT_OK(wukong_skill_read("no_fm", &body), "read no_fm ok");
    EXPECT(body && strstr(body, "no frontmatter body") != NULL, "no_fm body returned");
    if (body) tal_free(body);

    /* read: 不存在 → NOT_FOUND */
    body = NULL;
    EXPECT_ERR(wukong_skill_read("ghost", &body), OPRT_NOT_FOUND, "missing skill -> NOT_FOUND");

    /* read: 非法 id → INVALID_PARM */
    body = NULL;
    EXPECT_ERR(wukong_skill_read("../secret", &body), OPRT_INVALID_PARM, "traversal id rejected");
    EXPECT_ERR(wukong_skill_read("a/b", &body), OPRT_INVALID_PARM, "slash id rejected");

    /* reload: 加一个后重扫,清单增加 */
    seed("added",
         "---\n{\"description\":\"added later\"}\n---\n# Added\n");
    wukong_skill_reload();
    sum = wukong_skill_build_summary();
    EXPECT(sum && strstr(sum, "- added:") != NULL, "reload picks up new skill");
    if (sum) tal_free(sum);

    /* 目录存在但没有 SKILL.md → 不入清单(目录名不是有效 skill id) */
    wukong_storage_write("claw/skills/empty_dir", "notes.txt",
                         (const BYTE_T *)"x", 1);
    wukong_skill_reload();
    sum = wukong_skill_build_summary();
    EXPECT(sum && strstr(sum, "empty_dir") == NULL,
           "dir without SKILL.md excluded from catalog");
    EXPECT(sum && strstr(sum, "- added:") != NULL,
           "unrelated existing skills still present after the rescan");
    if (sum) tal_free(sum);

    /* 空清单 → build_summary NULL */
    wukong_storage_test_reset();
    wukong_skill_reload();
    EXPECT(wukong_skill_build_summary() == NULL, "empty catalog -> NULL summary");

    TEST_END();
}
