#include <stdio.h>
#include <string.h>
#include "wukong_test.h"
#include "wukong_session.h"
#include "wukong_storage_test.h"
#include "tal_memory.h"

/* Fixed-time stub: wukong_session.c stamps a "ts" field on every line. */
TIME_T tal_time_get_posix(VOID) { return 1772829000; }

/* Evict "A" from the RAM slots (WK_SESSION_MAX = 4) by touching 4 other
 * chat_ids; the next access to "A" must restore from its file = a reboot. */
static void evict_A(void)
{
    wukong_session_append("B", "user", "x");
    wukong_session_append("C", "user", "x");
    wukong_session_append("D", "user", "x");
    wukong_session_append("E", "user", "x");
}

int main(void)
{
    wukong_storage_test_reset();

    /* 1. plain text round-trip; "ts" is stripped on replay */
    wukong_session_append("A", "user", "hello");
    wukong_session_append("A", "assistant", "hi!");
    {
        ty_cJSON *msgs = wukong_session_get_messages("A");
        EXPECT_NOT_NULL(msgs, "messages after append");
        EXPECT_EQ(ty_cJSON_GetArraySize(msgs), 2, "two messages");
        ty_cJSON *m0 = ty_cJSON_GetArrayItem(msgs, 0);
        EXPECT_STR_EQ(ty_cJSON_GetObjectItem(m0, "role")->valuestring, "user", "role user");
        EXPECT_STR_EQ(ty_cJSON_GetObjectItem(m0, "content")->valuestring, "hello", "content kept");
        EXPECT_NULL(ty_cJSON_GetObjectItem(m0, "ts"), "ts stripped on replay");
        ty_cJSON_Delete(msgs);
    }

    /* 2. tool round via append_msg: call + result replay in order */
    {
        ty_cJSON *am = ty_cJSON_Parse(
            "{\"role\":\"assistant\",\"content\":\"\",\"tool_calls\":[{\"id\":\"c1\","
            "\"type\":\"function\",\"function\":{\"name\":\"memory_save\",\"arguments\":\"{}\"}}]}");
        ty_cJSON *tm = ty_cJSON_Parse(
            "{\"role\":\"tool\",\"tool_call_id\":\"c1\",\"content\":\"{\\\"success\\\":true}\"}");
        EXPECT_OK(wukong_session_append_msg("A", am), "append tool-call msg");
        EXPECT_OK(wukong_session_append_msg("A", tm), "append tool result");
        ty_cJSON_Delete(am);
        ty_cJSON_Delete(tm);
        wukong_session_append("A", "assistant", "done!");

        ty_cJSON *msgs = wukong_session_get_messages("A");
        EXPECT_EQ(ty_cJSON_GetArraySize(msgs), 5, "text(2) + tool round(2) + final(1)");
        ty_cJSON *m2 = ty_cJSON_GetArrayItem(msgs, 2);
        EXPECT_NOT_NULL(ty_cJSON_GetObjectItem(m2, "tool_calls"), "tool_calls preserved");
        ty_cJSON *m3 = ty_cJSON_GetArrayItem(msgs, 3);
        EXPECT_STR_EQ(ty_cJSON_GetObjectItem(m3, "role")->valuestring, "tool", "tool result kept");
        EXPECT_NULL(ty_cJSON_GetObjectItem(m3, "ts"), "ts stripped from tool msg");
        ty_cJSON_Delete(msgs);
    }

    /* 3. persistence: evict A's RAM slot, next access restores from file */
    evict_A();
    {
        ty_cJSON *msgs = wukong_session_get_messages("A");
        EXPECT_NOT_NULL(msgs, "history restored from file");
        EXPECT_EQ(ty_cJSON_GetArraySize(msgs), 5, "all five lines back");
        ty_cJSON *m2 = ty_cJSON_GetArrayItem(msgs, 2);
        EXPECT_NOT_NULL(ty_cJSON_GetObjectItem(m2, "tool_calls"), "tool_calls survive reboot");
        ty_cJSON_Delete(msgs);
    }

    /* 4. RAM window: 120 appends keep the newest 100 */
    wukong_session_clear("W");
    for (int i = 1; i <= 120; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "m#%d", i);
        wukong_session_append("W", "user", buf);
    }
    {
        UINT_T count = 0, bytes = 0;
        wukong_session_stat("W", &count, &bytes);
        EXPECT_EQ(count, 100, "RAM window capped at 100");
        ty_cJSON *msgs = wukong_session_get_messages("W");
        ty_cJSON *m0 = ty_cJSON_GetArrayItem(msgs, 0);
        EXPECT_STR_EQ(ty_cJSON_GetObjectItem(m0, "content")->valuestring, "m#21", "oldest 20 dropped");
        ty_cJSON_Delete(msgs);
    }

    /* 5. orphan tool: when the tool_calls message falls out of the window,
     * replay must not start on the dangling tool result */
    wukong_session_clear("O");
    {
        ty_cJSON *am = ty_cJSON_Parse(
            "{\"role\":\"assistant\",\"content\":\"\",\"tool_calls\":[{\"id\":\"c9\","
            "\"type\":\"function\",\"function\":{\"name\":\"t\",\"arguments\":\"{}\"}}]}");
        ty_cJSON *tm = ty_cJSON_Parse(
            "{\"role\":\"tool\",\"tool_call_id\":\"c9\",\"content\":\"ok\"}");
        wukong_session_append_msg("O", am);
        wukong_session_append_msg("O", tm);
        ty_cJSON_Delete(am);
        ty_cJSON_Delete(tm);
        for (int i = 0; i < 99; i++) {
            wukong_session_append("O", "user", "pad");
        }
        /* window is now [tool result, 99 pads] — the tool_calls msg is gone */
        ty_cJSON *msgs = wukong_session_get_messages("O");
        EXPECT_EQ(ty_cJSON_GetArraySize(msgs), 99, "orphan tool skipped");
        ty_cJSON *m0 = ty_cJSON_GetArrayItem(msgs, 0);
        EXPECT_STR_EQ(ty_cJSON_GetObjectItem(m0, "role")->valuestring, "user", "starts on user");
        ty_cJSON_Delete(msgs);
    }

    /* 6. partial tail line (power cut) is skipped on restore */
    wukong_session_clear("P");
    wukong_session_append("P", "user", "good line");
    wukong_storage_append("claw/session", "P.jsonl",
                      (CONST BYTE_T *)"{\"role\":\"user\",\"conte", 22);   /* no newline */
    evict_A();   /* churn slots; P may survive, clear it from RAM via eviction too */
    wukong_session_append("F", "user", "x");   /* 5th distinct id evicts P */
    {
        ty_cJSON *msgs = wukong_session_get_messages("P");
        EXPECT_NOT_NULL(msgs, "restore with partial tail");
        EXPECT_EQ(ty_cJSON_GetArraySize(msgs), 1, "partial tail skipped");
        ty_cJSON_Delete(msgs);
    }

    /* 7. clear drops RAM + file */
    wukong_session_clear("A");
    EXPECT_NULL(wukong_session_get_messages("A"), "cleared session empty");
    evict_A();
    EXPECT_NULL(wukong_session_get_messages("A"), "no file left after clear");

    /* ==================== compaction primitives ==================== */
    /* Fresh chat "K": 45 turns of user/assistant = 90 msgs. Uses its own
     * chat_id so earlier cases are untouched (slot LRU holds 4). */
    {
        CHAR_T ubuf[32], abuf[32];
        for (int t = 0; t < 45; t++) {
            snprintf(ubuf, sizeof(ubuf), "q%03d", t);
            snprintf(abuf, sizeof(abuf), "a%03d", t);
            if (t == 44) {
                EXPECT(!wukong_session_needs_compact("K"),
                       "88 msgs: below trigger");
            }
            wukong_session_append("K", "user", ubuf);
            if (t == 44) break;                      /* stop at 89 msgs */
            wukong_session_append("K", "assistant", abuf);
        }
        EXPECT(!wukong_session_needs_compact("K"), "89 msgs: still below trigger");
        wukong_session_append("K", "assistant", "a044");   /* 90th msg */
        EXPECT(wukong_session_needs_compact("K"), "90 msgs: trigger");

        /* dump: covered lands on a turn boundary (keep region starts on user) */
        UINT_T covered = 0;
        CHAR_T *old = wukong_session_dump_oldest_for_summary("K", &covered);
        EXPECT_NOT_NULL(old, "dump text produced");
        EXPECT(covered == 60, "count-KEEP=60 is msgs[60]=q030(user): exact boundary");
        EXPECT_NOT_NULL(strstr(old, "user: q000"), "oldest turn in dump");
        EXPECT_NOT_NULL(strstr(old, "assistant: a029"), "last covered turn in dump");
        EXPECT_NULL(strstr(old, "q030"), "kept turns NOT in dump");
        tal_free(old);

        /* apply: covered dropped, summary inserted at front, file rewritten */
        EXPECT_OK(wukong_session_apply_summary("K", covered, "EARLY-SUMMARY"),
                  "apply ok");
        UINT_T cnt = 0;
        wukong_session_stat("K", &cnt, NULL);
        EXPECT(cnt == 31, "90 - 60 + 1 = 31 msgs after apply");
        CHAR_T *sum = wukong_session_get_summary("K");
        EXPECT_NOT_NULL(sum, "summary readable");
        EXPECT_STR_EQ(sum, "EARLY-SUMMARY", "summary content");
        tal_free(sum);

        /* get_messages skips the summary line; window starts on a user msg */
        ty_cJSON *msgs = wukong_session_get_messages("K");
        EXPECT_EQ(ty_cJSON_GetArraySize(msgs), 30, "summary line not sent");
        ty_cJSON *first = ty_cJSON_GetArrayItem(msgs, 0);
        EXPECT_STR_EQ(ty_cJSON_GetObjectItem(first, "role")->valuestring, "user",
                      "kept region starts on user");
        ty_cJSON_Delete(msgs);
    }

    /* rolling: a second compaction absorbs the old summary */
    {
        CHAR_T ubuf[32];
        UINT_T cnt = 0;
        wukong_session_stat("K", &cnt, NULL);          /* 31 */
        for (int i = 0; (UINT_T)i < 90 - cnt; i += 2) {
            snprintf(ubuf, sizeof(ubuf), "n%03d", i);
            wukong_session_append("K", "user", ubuf);
            wukong_session_append("K", "assistant", "ok");
        }
        wukong_session_stat("K", &cnt, NULL);
        EXPECT(cnt >= 90, "window grown past trigger again");
        UINT_T covered = 0;
        CHAR_T *old = wukong_session_dump_oldest_for_summary("K", &covered);
        EXPECT_NOT_NULL(old, "second dump ok");
        EXPECT_NOT_NULL(strstr(old, "system: EARLY-SUMMARY"),
                        "old summary feeds the new compaction");
        EXPECT_OK(wukong_session_apply_summary("K", covered, "SUMMARY-V2"), "re-apply ok");
        CHAR_T *sum = wukong_session_get_summary("K");
        EXPECT_STR_EQ(sum, "SUMMARY-V2", "summary rolled, still exactly one");
        tal_free(sum);
        tal_free(old);
    }

    /* reboot: the summary line survives restore and is recognized */
    {
        evict_A();   /* touches B/C/D/E: evicts K from the 4 LRU slots */
        CHAR_T *sum = wukong_session_get_summary("K");
        EXPECT_NOT_NULL(sum, "summary restored from file after eviction");
        EXPECT_STR_EQ(sum, "SUMMARY-V2", "restored content");
        tal_free(sum);
    }

    /* hard-cap eviction skips the summary (drop msgs[1] instead) */
    {
        UINT_T cnt = 0;
        wukong_session_stat("K", &cnt, NULL);
        while (cnt < 100) {                       /* fill to the hard cap */
            wukong_session_append("K", "user", "fill");
            wukong_session_stat("K", &cnt, NULL);
        }
        wukong_session_append("K", "user", "one more");   /* forces a drop */
        CHAR_T *sum = wukong_session_get_summary("K");
        EXPECT_NOT_NULL(sum, "summary survives hard-cap eviction");
        tal_free(sum);
    }

    /* input caps: long content clipped per-message; total capped at a turn */
    {
        CHAR_T big[600];
        memset(big, 'x', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';
        for (int t = 0; t < 45; t++) {            /* fresh chat "L", long msgs */
            wukong_session_append("L", "user", big);
            wukong_session_append("L", "assistant", big);
        }
        UINT_T covered = 0;
        CHAR_T *old = wukong_session_dump_oldest_for_summary("L", &covered);
        EXPECT_NOT_NULL(old, "dump with long msgs ok");
        EXPECT(strlen(old) <= 8192, "total input capped at 8KB");
        EXPECT(covered > 0 && covered < 60, "covered backed off below count-KEEP");
        EXPECT_OK(wukong_session_apply_summary("L", covered, "L-SUM"), "apply capped batch");
        ty_cJSON *msgs = wukong_session_get_messages("L");
        ty_cJSON *first = ty_cJSON_GetArrayItem(msgs, 0);
        EXPECT_STR_EQ(ty_cJSON_GetObjectItem(first, "role")->valuestring, "user",
                      "backed-off boundary still lands on a user msg");
        ty_cJSON_Delete(msgs);
        tal_free(old);
    }

    /* orphans BEHIND the summary: hard-cap eviction can eat a turn-opening
     * user right after the summary line — get_messages must step over the
     * summary AND the orphaned tool round, never emit a tool-first sequence */
    {
        wukong_session_append("Q", "user", "seed");
        EXPECT_OK(wukong_session_apply_summary("Q", 1, "Q-SUM"), "seed summary");
        ty_cJSON *am = ty_cJSON_Parse(
            "{\"role\":\"assistant\",\"content\":\"\",\"tool_calls\":[{\"id\":\"c9\","
            "\"type\":\"function\",\"function\":{\"name\":\"x\",\"arguments\":\"{}\"}}]}");
        ty_cJSON *tm = ty_cJSON_Parse(
            "{\"role\":\"tool\",\"tool_call_id\":\"c9\",\"content\":\"r\"}");
        EXPECT_OK(wukong_session_append_msg("Q", am), "orphan tool_call behind summary");
        EXPECT_OK(wukong_session_append_msg("Q", tm), "orphan tool result behind summary");
        ty_cJSON_Delete(am);
        ty_cJSON_Delete(tm);
        wukong_session_append("Q", "user", "q-new");
        wukong_session_append("Q", "assistant", "a-new");

        ty_cJSON *msgs = wukong_session_get_messages("Q");
        EXPECT_NOT_NULL(msgs, "messages produced");
        EXPECT_EQ(ty_cJSON_GetArraySize(msgs), 2, "summary + orphan round all skipped");
        ty_cJSON *first = ty_cJSON_GetArrayItem(msgs, 0);
        EXPECT_STR_EQ(ty_cJSON_GetObjectItem(first, "role")->valuestring, "user",
                      "window starts on the user turn, not the orphan tool round");
        ty_cJSON_Delete(msgs);
    }

    /* per-message clip must never cut a multi-byte char in half: the lone
     * lead byte plus the escaped "\n" after it made the LLM endpoint reject
     * the whole summarize request as invalid UTF-8 */
    {
        CHAR_T big[600];
        memset(big, 'x', 510);
        strcpy(big + 510, "\xe4\xb8\xad\xe6\x96\x87\xe6\x95\xb0");  /* 中文数: clip lands mid-char */
        for (int t = 0; t < 45; t++) {
            wukong_session_append("U", "user", big);
            wukong_session_append("U", "assistant", "ok");
        }
        UINT_T covered = 0;
        CHAR_T *old = wukong_session_dump_oldest_for_summary("U", &covered);
        EXPECT_NOT_NULL(old, "dump with clip-point multi-byte ok");
        BOOL_T utf8_ok = TRUE;
        for (CONST CHAR_T *q = old; *q && utf8_ok; q++) {
            BYTE_T b = (BYTE_T)*q;
            if ((b & 0x80) == 0) continue;
            UINT_T need = 0;
            if ((b & 0xE0) == 0xC0) need = 1;
            else if ((b & 0xF0) == 0xE0) need = 2;
            else if ((b & 0xF8) == 0xF0) need = 3;
            else { utf8_ok = FALSE; break; }          /* stray continuation byte */
            while (need--) {
                q++;
                if (((BYTE_T)*q & 0xC0) != 0x80) { utf8_ok = FALSE; break; }
            }
        }
        EXPECT(utf8_ok, "dump text stays valid UTF-8 after clipping");
        tal_free(old);
    }

    /* the rolling summary line dumps with a wider clip: a real summary runs
     * ~600B and the 512B message clip cut off its tail sections — current
     * topic and saved-memory ids */
    {
        CHAR_T big[602];
        memset(big, 'S', 592);
        strcpy(big + 592, "TAIL-MARK");                 /* 601 bytes total */
        CHAR_T ubuf[32];
        for (int t = 0; t < 45; t++) {
            snprintf(ubuf, sizeof(ubuf), "v%03d", t);
            wukong_session_append("V", "user", ubuf);
            wukong_session_append("V", "assistant", "ok");
        }
        UINT_T covered = 0;
        CHAR_T *old = wukong_session_dump_oldest_for_summary("V", &covered);
        EXPECT_NOT_NULL(old, "first dump ok");
        tal_free(old);
        EXPECT_OK(wukong_session_apply_summary("V", covered, big), "apply 601B summary");
        UINT_T cnt = 0;
        wukong_session_stat("V", &cnt, NULL);
        for (int i = 0; (UINT_T)i < 90 - cnt; i += 2) {
            snprintf(ubuf, sizeof(ubuf), "w%03d", i);
            wukong_session_append("V", "user", ubuf);
            wukong_session_append("V", "assistant", "ok");
        }
        covered = 0;
        old = wukong_session_dump_oldest_for_summary("V", &covered);
        EXPECT_NOT_NULL(old, "second dump ok");
        EXPECT_NOT_NULL(strstr(old, "TAIL-MARK"), "summary tail survives the dump clip");
        tal_free(old);
    }

    /* 8. namespace migration lock: session falls in claw/session namespace.
     * This case directly reads the session file from storage to verify the
     * WK_SESSION_NS macro is set to "claw/session". If the macro reverts to
     * the old "claw/memory/session", read from claw/session will fail (red). */
    {
        BYTE_T *buf = NULL; UINT_T len = 0;
        wukong_session_clear("NS");
        wukong_session_append("NS", "user", "ns-marker");
        EXPECT_OK(wukong_storage_read("claw/session", "NS.jsonl", &buf, &len),
                  "session file readable from claw/session namespace");
        EXPECT_NOT_NULL(buf, "buffer allocated from storage read");
        EXPECT(strstr((CHAR_T *)buf, "ns-marker"), "ns-marker content found in claw/session");
        wukong_storage_free(buf); buf = NULL;
    }

    /* 9. init/deinit lifecycle + reentry: deinit frees the whole handle (all
     * slots), so the next call must lazily re-ensure a fresh empty one
     * instead of crashing on a dangling pointer. Checked via wukong_session_stat
     * (a bare __find, no __alloc/__restore) so this stays a pure handle-
     * lifecycle check: get_messages would legitimately restore "Z" from the
     * file the append below already wrote through, which is the *separate*
     * restore-after-reboot behavior already covered by evict_A() above, not
     * what this case is testing. */
    {
        wukong_storage_test_reset();
        wukong_session_init();
        wukong_session_append("Z", "user", "hi");
        wukong_session_deinit();                    /* releases the handle */
        UINT_T cnt = 99;                             /* poison: must come back 0 */
        wukong_session_stat("Z", &cnt, NULL);        /* lazy re-ensure: fresh, empty slots */
        EXPECT_EQ(cnt, 0, "after deinit, re-ensured handle has no RAM history for Z");
        wukong_session_init();                       /* idempotent re-init: no crash */
        wukong_session_append("Z2", "user", "ok");
        ty_cJSON *m2 = wukong_session_get_messages("Z2");
        EXPECT_NOT_NULL(m2, "usable after re-init");
        ty_cJSON_Delete(m2);
    }

    TEST_END();
}
