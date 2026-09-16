/**
 * @file wukong_compact.c
 * @brief End-of-turn conversation compaction driver: dump the oldest session
 *        turns (wukong_session primitives), summarize them through the claw
 *        LLM runtime passed by the caller, apply the rolling summary back,
 *        and announce the two phases on the active channel. The compress
 *        prompt template lives here — nowhere else.
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */
#include "wukong_session.h"
#include "wukong_llm.h"
#include "ty_cJSON.h"
#include "tal_memory.h"
#include "uni_log.h"
#include <string.h>
#include <stdio.h>

/* Two-phase compaction notices — the first is spoken before the summarize
 * call (present tense), the second only after apply succeeded, so everything
 * said to the user is true. */
#define WK_REPLY_COMPACTING_ZH "🗜️ 对话有点长，我先整理一下…"
#define WK_REPLY_COMPACTING_EN "🗜️ Our chat is getting long — tidying it up…"
#define WK_REPLY_COMPACTED_ZH  "🗜️ 整理好了，继续聊～"
#define WK_REPLY_COMPACTED_EN  "🗜️ All tidied up — let's continue."

/* Usage note prefixed when the summary is injected into the system prompt:
 * keeps the model from reciting the summary to the user. */
#define WK_SUMMARY_NOTE \
    "(Background from the earlier part of this conversation, already known " \
    "to both sides. Use it silently for continuity; never recite or " \
    "mention it.)"

/* Summarize-call instructions (transient input of that one call: never
 * persisted, never part of the main system prompt). Five buckets keep
 * actionable items from being averaged away; no persona/user context on
 * purpose — the summary must stay pure dialogue fact.
 * Density is bought reliability (lab-measured on glm-4.7, see ledger
 * 2026-08-18): the in-bucket persisted-fact exceptions, section 5's full
 * wording and the RIGHT/WRONG pair are load-bearing — trimming them
 * collapsed rule adherence. Don't compress this text without re-running
 * the prompt lab. */
STATIC CONST CHAR_T *WK_COMPRESS_SYSTEM =
    "You are a conversation summarizer for a smart-device voice assistant.";
STATIC CONST CHAR_T *WK_COMPRESS_PROMPT =
    "You are compressing the earlier conversation between a user and their\n"
    "smart-device voice assistant into a background summary, so the assistant can\n"
    "seamlessly continue serving the user without the raw history.\n"
    "\n"
    "Write a concise structured summary covering, in order and only when present:\n"
    "1. User context — how the user asked to be addressed; lasting facts or\n"
    "   preferences about the user, including corrections the user gave about\n"
    "   the assistant's behavior. Facts already persisted (mem_ id / profile):\n"
    "   the id or a pointer only, never the text.\n"
    "2. Ongoing tasks — anything set up or requested that is still active or\n"
    "   unfinished (alarms/reminders set, a promised action, a request in progress).\n"
    "3. Key facts & decisions — important info the user gave or agreements made\n"
    "   that later turns may depend on. Same persisted-fact exception: id only.\n"
    "4. Current topic — what the latest exchange was about, so a follow-up like\n"
    "   \"the other one\" / \"do it again\" still resolves.\n"
    "5. Saved memories — every mem_ id seen in the conversation, ids only\n"
    "   (e.g. \"saved memories: mem_0007, mem_0008\"). Their content is\n"
    "   retrievable by id on demand and must not appear anywhere in the\n"
    "   summary — this line is where remembered facts belong, as bare ids.\n"
    "   RIGHT: \"saved memories: mem_0007\"; WRONG: \"allergic to peanuts\n"
    "   (mem_0007)\".\n"
    "\n"
    "Rules:\n"
    "- Write the whole summary, labels included, in the user's language (a\n"
    "  Chinese conversation gets a Chinese summary); third person; factual,\n"
    "  no invention.\n"
    "- Be brief, but never drop actionable items; keep the user's explicit\n"
    "  requests close to their original wording.\n"
    "- If the conversation begins with a previous summary (a system line), carry\n"
    "  its items forward unless the later conversation resolved them.\n"
    "- Omit greetings, small talk, and fully-resolved one-off Q&A.\n"
    "- The summary must be fully self-contained: never write relative wording\n"
    "  like \"no change\" — each version replaces the previous one entirely, so\n"
    "  restate whatever still matters, in full. The one exception is facts the\n"
    "  conversation shows were persisted (memory_save -> mem_ id, or a\n"
    "  successful profile_update): the assistant re-reads those from storage\n"
    "  every turn, so give only the mem_ id (section 5) or note that the\n"
    "  profile holds it — never the persisted text, even when it looks\n"
    "  important; importance is exactly why it was persisted.\n"
    "- Never include transient device state (current time, volume levels,\n"
    "  query snapshots) — you may say the user checked something, but never\n"
    "  the value it had at the time.\n"
    "\n"
    "Conversation:\n";

/* One independent summarize call on the claw LLM runtime (same transport as
 * the dialogue; no tools, no history). Returns a heap summary (caller
 * tal_free) or NULL on any failure — the caller just skips, the next
 * successful turn retries. */
STATIC CHAR_T *__summarize_via_llm(WUKONG_LLM_RT_T *rt, CONST CHAR_T *old_text)
{
    CHAR_T *joined = (CHAR_T *)tal_malloc(strlen(WK_COMPRESS_PROMPT) + strlen(old_text) + 1);
    if (joined == NULL) {
        return NULL;
    }
    sprintf(joined, "%s%s", WK_COMPRESS_PROMPT, old_text);

    ty_cJSON *msgs = ty_cJSON_CreateArray();
    ty_cJSON *um = ty_cJSON_CreateObject();
    ty_cJSON_AddStringToObject(um, "role", "user");
    ty_cJSON_AddStringToObject(um, "content", joined);
    ty_cJSON_AddItemToArray(msgs, um);
    tal_free(joined);

    WUKONG_LLM_REQ_T req = { .system_prompt = WK_COMPRESS_SYSTEM,
                             .messages = msgs, .tools_json = NULL };
    WUKONG_LLM_RESP_T resp = {0};
    CHAR_T *err = NULL;
    OPERATE_RET ir = wukong_llm_runtime_chat(rt, &req, &resp, &err);
    ty_cJSON_Delete(msgs);

    if (ir != OPRT_OK) {
        PR_ERR("compact: summarize infer rt=%d err=%s", ir, err ? err : "");
    }
    CHAR_T *out = NULL;
    if (ir == OPRT_OK && resp.text) {
        CONST CHAR_T *t = resp.text;
        /* glm may leak its thinking draft into content ("draft</think>real");
         * the self-contained summary is whatever follows the marker */
        CONST CHAR_T *close = strstr(t, "</think>");
        if (close) t = close + strlen("</think>");
        while (*t == ' ' || *t == '\n' || *t == '\r' || *t == '\t') t++;
        if (*t != '\0') {
            out = mm_strdup(t);           /* resp.text is owned by resp_free */
        }
    }
    tal_free(err);
    wukong_llm_resp_free(&resp);
    return out;
}

VOID_T wukong_session_compact_run(CONST CHAR_T *chat_id, WUKONG_LLM_RT_T *rt, BOOL_T zh,
                                  VOID_T (*notify)(CONST CHAR_T *text))
{
    if (chat_id == NULL || rt == NULL) {
        return;
    }
    UINT_T covered = 0;
    CHAR_T *old = wukong_session_dump_oldest_for_summary(chat_id, &covered);
    if (old == NULL) {
        return;
    }
    PR_NOTICE("compact: dump covered=%u input_len=%u", covered, (UINT_T)strlen(old));
    /* announce only when there is actually a batch to compact — a dump that
     * finds no turn boundary must not nag every turn */
    if (notify) notify(zh ? WK_REPLY_COMPACTING_ZH : WK_REPLY_COMPACTING_EN);
    CHAR_T *summary = __summarize_via_llm(rt, old);
    if (summary == NULL) {
        PR_ERR("compact: summarize failed, window unchanged (see llm error above)");
    }
    if (summary) {
        if (wukong_session_apply_summary(chat_id, covered, summary) == OPRT_OK) {
            if (notify) notify(zh ? WK_REPLY_COMPACTED_ZH : WK_REPLY_COMPACTED_EN);
        }
        tal_free(summary);
    }
    tal_free(old);
}

CHAR_T *wukong_session_build_summary(CONST CHAR_T *chat_id)
{
    CHAR_T *summary = wukong_session_get_summary(chat_id);
    if (summary == NULL) {
        return NULL;
    }
    CHAR_T *body = (CHAR_T *)tal_malloc(strlen(WK_SUMMARY_NOTE) + strlen(summary) + 2);
    if (body == NULL) {
        return summary;   /* degrade: raw summary without the note */
    }
    sprintf(body, "%s\n%s", WK_SUMMARY_NOTE, summary);
    tal_free(summary);
    return body;
}
