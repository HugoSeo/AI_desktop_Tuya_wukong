#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wukong_test.h"
#include "wukong_storage_test.h"
#include "claw_config.h"

int main(void) {
    /* 合法用之 */
    assert(claw_config_clamp(50, 1, 100, 90) == 50);
    /* 越界(高/低)回落默认 */
    assert(claw_config_clamp(200, 1, 100, 90) == 90);
    assert(claw_config_clamp(0,   1, 100, 90) == 90);
    /* 边界含端点 */
    assert(claw_config_clamp(1,   1, 100, 90) == 1);
    assert(claw_config_clamp(100, 1, 100, 90) == 100);

    /* 指针化单例:未 load 时 get() 懒分配并返回编译默认(memory_init 早读也安全) */
    EXPECT_EQ(claw_config_get()->mem_max, CLAW_CFG_MEM_MAX, "defaults before any load");
    EXPECT_EQ(claw_config_get()->compact_trigger, CLAW_CFG_COMPACT_TRIGGER, "defaults before any load 2");

    /* 分配缝:host(ENABLE_EXT_RAM 关)等价 tal_malloc,可读写、free 不崩 */
    {
        char *p = (char *)wukong_claw_malloc(32);
        EXPECT_NOT_NULL(p, "claw_malloc non-null");
        p[0] = 'x'; p[31] = 'y';
        EXPECT_EQ(p[0], 'x', "claw_malloc writable");
        wukong_claw_free(p);
    }
    /* 新运行时键:写含 memory.mem_max / session.session_msg_chars 的 wukong.json,load 后生效 */
    {
        wukong_storage_test_reset();
        const char *j =
          "{\"session\":{\"session_msg_chars\":1000,\"session_file_max\":20000},"
          "\"memory\":{\"mem_max\":40}}";
        wukong_storage_write("claw", "wukong.json", (const BYTE_T *)j, (UINT_T)strlen(j));
        claw_config_load();
        EXPECT_EQ(claw_config_get()->session_msg_chars, 1000, "msg_chars parsed");
        EXPECT_EQ(claw_config_get()->session_file_max, 20000, "file_max parsed");
        EXPECT_EQ(claw_config_get()->mem_max, 40, "mem_max parsed");
    }
    /* 越界 clamp 回默认;缺键回默认 */
    {
        wukong_storage_test_reset();
        const char *j = "{\"memory\":{\"mem_max\":9999}}";   /* > CLAW_FIXED_MEM_CEIL */
        wukong_storage_write("claw", "wukong.json", (const BYTE_T *)j, (UINT_T)strlen(j));
        claw_config_load();
        EXPECT_EQ(claw_config_get()->mem_max, CLAW_CFG_MEM_MAX, "mem_max clamp->default");
        EXPECT_EQ(claw_config_get()->session_msg_chars, CLAW_CFG_SESSION_MSG_CHARS, "missing->default");
    }

    /* 增量合并迁移:旧文件缺新键 → load 后补齐回写,已有键原值保留(越界值也不改写文件) */
    {
        wukong_storage_test_reset();
        const char *old =   /* spec3 旧 4 键;compact_trigger 用越界 9999 验原值保留 */
          "{\"session\":{\"compact_trigger\":9999,\"compact_keep_ratio\":3,\"send_tokens\":3000},"
          "\"agent\":{\"max_tool_iterations\":10}}";
        wukong_storage_write("claw", "wukong.json", (const BYTE_T *)old, (UINT_T)strlen(old));
        claw_config_load();
        BYTE_T *buf = NULL; UINT_T len = 0;
        wukong_storage_read("claw", "wukong.json", &buf, &len);
        EXPECT_NOT_NULL(buf, "file present after migrate");
        EXPECT_NOT_NULL(strstr((char *)buf, "session_msg_chars"), "msg_chars key added");
        EXPECT_NOT_NULL(strstr((char *)buf, "session_file_max"), "file_max key added");
        EXPECT_NOT_NULL(strstr((char *)buf, "mem_max"), "mem_max key added");
        EXPECT_NOT_NULL(strstr((char *)buf, "9999"), "present value preserved verbatim in file");
        char migrated[512];
        snprintf(migrated, sizeof(migrated), "%s", (char *)buf);
        wukong_storage_free(buf);
        /* 内存单例:越界 compact_trigger 仍 clamp 回默认(文件保留原值,运行时用校验值) */
        EXPECT_EQ(claw_config_get()->compact_trigger, CLAW_CFG_COMPACT_TRIGGER, "in-memory clamps out-of-range");
        /* 二次加载幂等:已补齐 → dirty=FALSE → 不再回写,文件逐字不变 */
        claw_config_load();
        BYTE_T *buf2 = NULL; UINT_T len2 = 0;
        wukong_storage_read("claw", "wukong.json", &buf2, &len2);
        EXPECT_NOT_NULL(buf2, "file present on second load");
        EXPECT_EQ(strcmp((char *)buf2, migrated), 0, "second load idempotent (no re-write)");
        wukong_storage_free(buf2);
    }
    /* 幂等:已完整文件 load 后不回写(dirty=FALSE),内容逐字不变 */
    {
        wukong_storage_test_reset();
        const char *full =
          "{\"session\":{\"compact_trigger\":90,\"compact_keep_ratio\":3,\"send_tokens\":3000,"
          "\"session_msg_chars\":4096,\"session_file_max\":131072},"
          "\"memory\":{\"mem_max\":100},\"agent\":{\"max_tool_iterations\":10}}";
        wukong_storage_write("claw", "wukong.json", (const BYTE_T *)full, (UINT_T)strlen(full));
        claw_config_load();
        BYTE_T *buf = NULL; UINT_T len = 0;
        wukong_storage_read("claw", "wukong.json", &buf, &len);
        EXPECT_NOT_NULL(buf, "file present");
        EXPECT_EQ(strcmp((char *)buf, full), 0, "complete file byte-identical (no rewrite)");
        wukong_storage_free(buf);
    }

    TEST_END();
}
