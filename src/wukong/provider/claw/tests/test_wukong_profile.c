#include "wukong_test.h"
#include "wukong_profile.h"
#include "wukong_storage_test.h"
#include "tal_memory.h"
#include <string.h>


int main(void)
{
    BYTE_T *buf = NULL; UINT_T len = 0;

    /* (1) 空 store:getter 触发 NOT_FOUND → seed 默认 + 返回默认 */
    wukong_storage_test_reset();
    CONST CHAR_T *soul = wukong_profile_soul();
    EXPECT_STR_CONTAINS(soul, "Wukong", "empty store -> default soul");
    CONST CHAR_T *user = wukong_profile_user();
    EXPECT_STR_CONTAINS(user, "Name", "empty store -> default user");
    /* seed 落盘:能从 fs 读回刚 seed 的默认 */
    EXPECT_OK(wukong_storage_read("claw/profile", "SOUL.md", &buf, &len), "seeded SOUL.md readable");
    EXPECT(buf && strstr((CHAR_T *)buf, "Wukong"), "seeded content is default soul");
    wukong_storage_free(buf); buf = NULL;

    /* (2) fs 有自定义内容:reload 后 getter 返回 fs 内容 */
    wukong_profile_reload();
    EXPECT_OK(wukong_storage_write("claw/profile", "SOUL.md",
                                   (CONST BYTE_T *)"CUSTOM_SOUL_FROM_FS", 19),
              "write custom SOUL.md");
    EXPECT_STR_EQ(wukong_profile_soul(), "CUSTOM_SOUL_FROM_FS", "fs content used after reload");

    /* (3) 缓存:改 fs 但不 reload → 仍返回缓存;reload 后才更新 */
    EXPECT_OK(wukong_storage_write("claw/profile", "SOUL.md",
                                   (CONST BYTE_T *)"CHANGED", 7), "overwrite SOUL.md");
    EXPECT_STR_EQ(wukong_profile_soul(), "CUSTOM_SOUL_FROM_FS", "cache hit ignores fs change");
    wukong_profile_reload();
    EXPECT_STR_EQ(wukong_profile_soul(), "CHANGED", "reload picks up fs change");

    /* (4) update: 写盘+缓存即时生效, 持久化(reload 后仍在) */
    EXPECT_OK(wukong_profile_update("soul", "I AM REWRITTEN"), "update soul ok");
    EXPECT_STR_EQ(wukong_profile_soul(), "I AM REWRITTEN", "update refreshes cache");
    wukong_profile_reload();
    EXPECT_STR_EQ(wukong_profile_soul(), "I AM REWRITTEN", "update persisted");
    EXPECT_OK(wukong_profile_update("user", "- Name: master"), "update user ok");
    EXPECT_STR_EQ(wukong_profile_user(), "- Name: master", "user updated");

    /* (5) update 参数校验 */
    EXPECT_ERR(wukong_profile_update("identity", "x"), OPRT_INVALID_PARM, "bad target rejected");
    EXPECT_ERR(wukong_profile_update("soul", ""), OPRT_INVALID_PARM, "empty content rejected");

    /* (6) reset: 回内置默认(删文件, 下次读 re-seed) */
    EXPECT_OK(wukong_profile_reset("soul"), "reset soul ok");
    EXPECT_STR_CONTAINS(wukong_profile_soul(), "Wukong", "back to built-in default");
    EXPECT_STR_EQ(wukong_profile_user(), "- Name: master", "reset only touches its target");

    /* (7) IDENTITY 不走 fs:恒为宏内容 */
    EXPECT_STR_CONTAINS(wukong_profile_identity(), "Device:", "identity is macro");

    TEST_END();
}
