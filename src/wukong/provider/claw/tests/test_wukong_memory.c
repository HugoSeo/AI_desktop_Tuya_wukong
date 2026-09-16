#include <stdio.h>
#include <string.h>
#include "wukong_test.h"
#include "wukong_storage_test.h"
#include "wukong_memory.h"
#include "claw_config.h"
#include "tal_memory.h"     /* tal_free() for idx cleanup below */
#include "tal_time_service.h"

/* wukong_memory.c calls tal_time_get_posix() (save/update/MEMORY.md timestamp);
 * the shared test stubs don't provide it, so supply a fixed-value stub here
 * (same pattern as tm/tests/stubs_alarm.c) to satisfy the linker. */
TIME_T tal_time_get_posix(VOID) { return 1772829000; }

static void seed(const char *name, const char *json)
{ wukong_storage_write("claw/memory", name, (const BYTE_T *)json, (UINT_T)strlen(json)); }

int main(void)
{
    wukong_storage_test_reset();

    /* 空库: init ok, build_index NULL */
    EXPECT_OK(wukong_memory_init(), "init empty ok");
    { /* empty store still injects the section header (framing + rules) */
      char *ix0 = wukong_memory_build_index();
      EXPECT_NOT_NULL(ix0, "empty -> header-only index");
      EXPECT(ix0 && strstr(ix0, "long-term memory") != NULL, "empty index has framing");
      EXPECT(ix0 && strstr(ix0, "mem_") == NULL, "empty index has no entries");
      if (ix0) tal_free(ix0); }

    /* 预置两条 + 索引, 重载后 build_index 反映 */
    seed("indexes.json",
        "{\"version\":\"1.0\",\"next_id\":3,\"keyword\":{\"咖啡\":[\"mem_0001\"],\"早晨\":[\"mem_0001\",\"mem_0002\"]}}");
    seed("memories.json",
        "{\"version\":\"1.0\",\"count\":2,\"data\":{"
        "\"mem_0001\":{\"id\":\"mem_0001\",\"time\":100,\"tags\":[\"咖啡\",\"早晨\"],\"importance\":8,\"access\":0,\"content\":\"黑咖啡\"},"
        "\"mem_0002\":{\"id\":\"mem_0002\",\"time\":200,\"tags\":[\"早晨\"],\"importance\":7,\"access\":0,\"content\":\"7点起床\"}}}");
    wukong_memory_reload();
    char *idx = wukong_memory_build_index();
    EXPECT_NOT_NULL(idx, "index not NULL");
    EXPECT_STR_CONTAINS(idx, "The user's long-term memory", "framing line");
    EXPECT_STR_CONTAINS(idx, "咖啡: mem_0001", "keyword line 咖啡");
    EXPECT_STR_CONTAINS(idx, "早晨: mem_0001, mem_0002", "keyword line 早晨");
    if (idx) tal_free(idx);

    /* corrupt on-disk file: a non-string tag element makes the load path do
     * strncpy(dst, NULL) and segfault (ferryman !123). The loader must
     * type-check each node and skip the bad one, keeping the valid tags. */
    wukong_storage_test_reset();
    seed("memories.json",
        "{\"version\":\"1.0\",\"count\":1,\"data\":{"
        "\"mem_0009\":{\"id\":\"mem_0009\",\"time\":100,\"tags\":[\"good\",123,\"ok\"],"
        "\"importance\":5,\"access\":0,\"content\":\"c\"}}}");
    wukong_memory_reload();   /* must not crash on the bad tag element */
    { char *ixc = wukong_memory_build_index();
      EXPECT_NOT_NULL(ixc, "load with corrupt tag element survives");
      EXPECT(ixc && strstr(ixc, "good: mem_0009") != NULL, "valid tag before bad kept");
      EXPECT(ixc && strstr(ixc, "ok: mem_0009") != NULL, "valid tag after bad kept");
      if (ixc) tal_free(ixc); }

    /* save creates a new entry -> id allocated + reflected in build_index */
    wukong_storage_test_reset(); wukong_memory_reload();
    { const char *tags[] = {"茶","早晨"}; char *id = NULL;
      EXPECT_OK(wukong_memory_save("早上喝茶", tags, 2, 8, &id), "save ok");
      EXPECT_NOT_NULL(id, "save returns id");
      char *ix = wukong_memory_build_index();
      EXPECT(ix && strstr(ix, "茶: mem_") != NULL, "index has 茶");
      if (ix) tal_free(ix);
      /* get recalls content + bumps access */
      char *gj = NULL; const char *ids[] = { id };
      EXPECT_OK(wukong_memory_get(ids, 1, &gj), "get ok");
      EXPECT(gj && strstr(gj, "早上喝茶") != NULL, "get returns content");
      if (gj) tal_free(gj);
      /* update partially overrides fields */
      ty_cJSON *u = ty_cJSON_Parse("{\"importance\":9,\"content\":\"早上喝绿茶\"}");
      EXPECT_OK(wukong_memory_update(id, u), "update ok"); ty_cJSON_Delete(u);
      char *gj2 = NULL; EXPECT_OK(wukong_memory_get(ids, 1, &gj2), "get after update");
      EXPECT(gj2 && strstr(gj2, "绿茶") != NULL, "update applied");
      if (gj2) tal_free(gj2);
      /* list shows everything without ids */
      { char *lj = NULL;
        EXPECT_OK(wukong_memory_list(&lj), "list ok");
        EXPECT(lj && strstr(lj, id) != NULL, "list contains the id");
        EXPECT(lj && strstr(lj, "绿茶") == NULL, "list is index-only (no content)");
        if (lj) tal_free(lj); }
      /* delete cleans up the index */
      EXPECT_OK(wukong_memory_delete(id), "delete ok");
      { char *ix1 = wukong_memory_build_index();
        EXPECT(ix1 && strstr(ix1, "mem_") == NULL, "no entries after delete");
        if (ix1) tal_free(ix1); }
      { char *lj2 = NULL;
        EXPECT_OK(wukong_memory_list(&lj2), "list after delete ok");
        EXPECT(lj2 && strstr(lj2, "\"count\":0") != NULL, "list empty after delete");
        if (lj2) tal_free(lj2); }
      tal_free(id);
    }
    /* update on a missing id -> NOT_FOUND */
    { ty_cJSON *u = ty_cJSON_Parse("{\"content\":\"x\"}");
      EXPECT_ERR(wukong_memory_update("mem_9999", u), OPRT_NOT_FOUND, "update missing -> NOT_FOUND");
      ty_cJSON_Delete(u); }

    /* write-then-commit: each CRUD must land on flash, not only the cache.
     * reload() drops the cache and re-reads store (= reboot), so these assert
     * the shadow-view serialization (APPEND/UPDATE/DELETE) is correct on disk,
     * which a cache-only get() cannot catch. */
    wukong_storage_test_reset(); wukong_memory_reload();
    { char *id = NULL; const char *tags[] = { "咖啡" };
      EXPECT_OK(wukong_memory_save("喜欢美式", tags, 1, 6, &id), "persist save ok");
      const char *ids[] = { id };
      wukong_memory_reload();                       /* reboot: rebuild cache from flash */
      char *g1 = NULL; EXPECT_OK(wukong_memory_get(ids, 1, &g1), "get after reload (save)");
      EXPECT(g1 && strstr(g1, "喜欢美式") != NULL, "APPEND persisted to flash");
      if (g1) tal_free(g1);
      ty_cJSON *u = ty_cJSON_Parse("{\"content\":\"喜欢拿铁\"}");
      EXPECT_OK(wukong_memory_update(id, u), "persist update ok"); ty_cJSON_Delete(u);
      wukong_memory_reload();
      char *g2 = NULL; EXPECT_OK(wukong_memory_get(ids, 1, &g2), "get after reload (update)");
      EXPECT(g2 && strstr(g2, "喜欢拿铁") != NULL, "UPDATE persisted to flash");
      EXPECT(g2 && strstr(g2, "喜欢美式") == NULL, "old content replaced on flash");
      if (g2) tal_free(g2);
      EXPECT_OK(wukong_memory_delete(id), "persist delete ok");
      wukong_memory_reload();
      { char *ix2 = wukong_memory_build_index();
        EXPECT(ix2 && strstr(ix2, "mem_") == NULL, "DELETE persisted to flash (no entries)");
        if (ix2) tal_free(ix2); }
      tal_free(id);
    }

    /* eviction: fill to CLAW_FIXED_MEM_CEIL (one low-importance + rest high-importance),
     * then save one more -> lowest retention_score entry gets evicted.
     * tal_time_get_posix() is fixed above, so recency ties across all entries
     * and the score is driven purely by importance*2 + access. */
    wukong_storage_test_reset(); wukong_memory_reload();
    { char *low_id = NULL, *high_id = NULL;
      for (UINT_T i = 0; i < CLAW_FIXED_MEM_CEIL; i++) {
          char *id = NULL;
          INT_T importance = (i == 0) ? 1 : 8;
          EXPECT_OK(wukong_memory_save("filler", NULL, 0, importance, &id), "fill save ok");
          if (i == 0) low_id = id;
          else if (i == 1) high_id = id;
          else tal_free(id);
      }
      char *extra_id = NULL;
      EXPECT_OK(wukong_memory_save("overflow entry", NULL, 0, 8, &extra_id), "save at capacity triggers eviction, still ok");
      EXPECT_NOT_NULL(extra_id, "overflow save returns id");
      char *gj = NULL; const char *ids[] = { low_id, high_id };
      EXPECT_OK(wukong_memory_get(ids, 2, &gj), "get after eviction ok");
      EXPECT(gj && strstr(gj, low_id) == NULL, "lowest-importance memory evicted");
      EXPECT(gj && strstr(gj, high_id) != NULL, "high-importance memory survives");
      if (gj) tal_free(gj);
      tal_free(low_id); tal_free(high_id); tal_free(extra_id);
    }

    /* out-of-range importance from update must be clamped like save (ferryman
     * !123): an LLM memory_update with importance=999 must not buy the entry a
     * huge retention score and dodge eviction. Save low (1), update to 999
     * (clamped -> stays 1), fill the rest mid (8); the target must be the
     * eviction victim. If 999 leaked in it would top the scores and survive. */
    wukong_storage_test_reset(); wukong_memory_reload();
    { char *target = NULL;
      EXPECT_OK(wukong_memory_save("clamp target", NULL, 0, 1, &target), "low save ok");
      ty_cJSON *ub = ty_cJSON_Parse("{\"importance\":999}");
      EXPECT_OK(wukong_memory_update(target, ub), "out-of-range update ok");
      ty_cJSON_Delete(ub);
      for (UINT_T i = 0; i < CLAW_FIXED_MEM_CEIL - 1; i++) {
          char *fid = NULL;
          EXPECT_OK(wukong_memory_save("filler", NULL, 0, 8, &fid), "fill save ok");
          tal_free(fid);
      }
      char *extra = NULL;
      EXPECT_OK(wukong_memory_save("overflow", NULL, 0, 8, &extra),
                "overflow save triggers eviction");
      char *gj = NULL; const char *tids[] = { target };
      EXPECT_OK(wukong_memory_get(tids, 1, &gj), "get target after overflow");
      EXPECT(gj && strstr(gj, target) == NULL,
             "clamped-low target evicted (out-of-range 999 did not leak in)");
      if (gj) tal_free(gj);
      tal_free(target); tal_free(extra);
    }

    /* runtime mem_max: config-driven cap below the compile ceiling takes effect.
     * mem_max=10 (clamp floor); saving 11 entries must hold count at 10 because
     * append evicts when count>=mem_max before adding. */
    {
        wukong_storage_test_reset();
        const char *j = "{\"memory\":{\"mem_max\":10}}";
        wukong_storage_write("claw", "wukong.json", (const BYTE_T *)j, (UINT_T)strlen(j));
        claw_config_load();
        wukong_memory_reload();                 /* mem_max=10 reload path: no crash, count<=10 */
        const char *tags[] = {"t"};
        for (UINT_T i = 0; i < 11; i++) {
            char *id = NULL; char content[16];
            snprintf(content, sizeof(content), "m%u", i);
            EXPECT_OK(wukong_memory_save(content, tags, 1, 5, &id), "mem_max fill save ok");
            tal_free(id);
        }
        char *lst = NULL;
        EXPECT_OK(wukong_memory_list(&lst), "list under mem_max ok");
        EXPECT(lst && strstr(lst, "\"count\":10") != NULL, "mem_max effective cap = 10");
        if (lst) tal_free(lst);
    }

    /* real boot path: __claw_storage_catchup calls wukong_memory_reload(),
     * not wukong_memory_init(). reload must enforce mem_max too, or lowering
     * mem_max in wukong.json has no effect until the next save-triggered
     * eviction (ferryman finding). Save 15 at the default cap (100), then
     * shrink mem_max to 10 and reload -> cache must trim to 10 immediately. */
    {
        wukong_storage_test_reset();
        claw_config_load();                     /* reseed default mem_max=100 */
        const char *tags[] = {"t"};
        for (UINT_T i = 0; i < 15; i++) {
            char *id = NULL; char content[16];
            snprintf(content, sizeof(content), "r%u", i);
            EXPECT_OK(wukong_memory_save(content, tags, 1, 5, &id), "reload-cap fill save ok");
            tal_free(id);
        }
        const char *j2 = "{\"memory\":{\"mem_max\":10}}";
        wukong_storage_write("claw", "wukong.json", (const BYTE_T *)j2, (UINT_T)strlen(j2));
        claw_config_load();
        wukong_memory_reload();                 /* must trim cache 15 -> 10, not just cap future saves */
        char *lst2 = NULL;
        EXPECT_OK(wukong_memory_list(&lst2), "list after reload-cap ok");
        EXPECT(lst2 && strstr(lst2, "\"count\":10") != NULL, "reload enforces lowered mem_max");
        if (lst2) tal_free(lst2);
    }

    TEST_END();
}
