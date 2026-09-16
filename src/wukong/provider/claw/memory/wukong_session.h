#pragma once
#include "tuya_cloud_types.h"
#include "ty_cJSON.h"

/*
 * Per-chat_id conversation history: RAM working window + JSONL persistence.
 *
 * Each appended message is one line of memory/session/<chat_id>.jsonl (a
 * standard OpenAI message, plus a "ts" field stripped on replay). The RAM
 * window (most recent messages) serves reads; writes go through to the file
 * (write-through). With no medium the module degrades to RAM-only; once
 * storage comes up the file window is restored on first access, so history
 * survives reboots. Tool rounds (assistant tool_calls + tool results) are
 * part of the history — the model can see what it actually called.
 *
 * All calls run on the single provider processing thread; not thread-safe.
 */

/* Allocate the session handle up front (idempotent). Every public entry
 * below also lazily ensures it, so calling this is optional but avoids the
 * first-call allocation latency. */
OPERATE_RET wukong_session_init(VOID_T);

/* Free the session handle and all RAM history (files are untouched). After
 * this, the next call lazily re-allocates an empty handle. */
VOID_T wukong_session_deinit(VOID_T);

/* Append one plain-text message (role = "user"/"assistant"). */
OPERATE_RET wukong_session_append(CONST CHAR_T *chat_id, CONST CHAR_T *role,
                                  CONST CHAR_T *content);

/* Append one structured message verbatim (e.g. assistant tool_calls / tool
 * result, already in OpenAI wire shape). @p msg is borrowed, not consumed. */
OPERATE_RET wukong_session_append_msg(CONST CHAR_T *chat_id, CONST ty_cJSON *msg);

/* New ty_cJSON array of chat_id's recent history as OpenAI messages. The send
 * window is token-budgeted (newest backwards) and always starts on a user
 * message (assistant/tool_call/tool orphans at the cut are skipped); the
 * rolling summary line is excluded — it travels via the system prompt.
 * Caller owns the array (ty_cJSON_Delete). NULL if none. */
ty_cJSON *wukong_session_get_messages(CONST CHAR_T *chat_id);

/* Drop a session's history: RAM window + the backing file. */
VOID_T wukong_session_clear(CONST CHAR_T *chat_id);

/* Report a chat_id's current RAM window usage (0/0 if no such session).
 * out_count/out_bytes may be NULL. For logging/diagnostics. */
VOID_T wukong_session_stat(CONST CHAR_T *chat_id, UINT_T *out_count, UINT_T *out_bytes);

/* ==== Compaction primitives (storage only; the LLM-driven flow is
 *      wukong_session_compact_run, in wukong_compact.c) ==== */

/* True if the RAM window has reached the soft compaction trigger (90). */
BOOL_T wukong_session_needs_compact(CONST CHAR_T *chat_id);

/* Text of the oldest batch to summarize: any existing summary line plus whole
 * turns (turn = a user message through its final reply); the kept region
 * always starts on a user message (~30 kept, floats to the turn boundary).
 * "role: content" per line, content clipped to 512 chars, total capped at 8KB
 * (backs off to the last complete turn when hit). *out_covered = number of
 * oldest messages covered — pass it to apply so both share ONE boundary.
 * Caller frees. NULL if compaction isn't needed / no turn boundary found. */
CHAR_T *wukong_session_dump_oldest_for_summary(CONST CHAR_T *chat_id, UINT_T *out_covered);

/* Drop the oldest @p covered messages (the dump boundary), insert @p summary
 * as one role=system line at the front, rewrite the backing file. */
OPERATE_RET wukong_session_apply_summary(CONST CHAR_T *chat_id, UINT_T covered,
                                         CONST CHAR_T *summary);

/* Raw stored summary text, or NULL if the chat has none. Returns a heap
 * string — caller owns it (tal_free): the RAM window stores serialized JSON
 * lines, extracting content requires parse+copy. Prompt injection goes
 * through wukong_session_build_summary instead. */
CHAR_T *wukong_session_get_summary(CONST CHAR_T *chat_id);

/* Note-prefixed summary body for the system prompt's "## Conversation
 * summary" section (wukong_compact.c): heap, caller tal_free; NULL when the
 * chat has no summary. */
CHAR_T *wukong_session_build_summary(CONST CHAR_T *chat_id);

/* End-of-turn compaction driver (wukong_compact.c): summarize the oldest
 * turns via the claw LLM runtime @p rt and shrink the window. Call when
 * needs_compact says so, after the turn's reply is delivered — never on the
 * answer path. @p notify (optional) speaks the two compaction phases to the
 * user, localized by @p zh. On failure the window stays unchanged — the
 * next successful turn retries. */
struct wukong_llm_runtime;
VOID_T wukong_session_compact_run(CONST CHAR_T *chat_id,
                                  struct wukong_llm_runtime *rt, BOOL_T zh,
                                  VOID_T (*notify)(CONST CHAR_T *text));
