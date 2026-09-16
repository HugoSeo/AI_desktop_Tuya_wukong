/**
 * @file joyinside_parse.c
 * @brief joyinside parse module — decoupled from svc_ai_agent.
 *
 * Output callbacks are registered by the JD provider via
 * joyinside_parse_set_output_cbs() instead of pulling them
 * from tuya_ai_agent_get_recv_cb / tuya_ai_agent_get_evt_cb.
 */

#include <stdio.h>
#include "joyinside_parse.h"
#include "joyinside_biz.h"
#include "uni_log.h"
#include "tal_memory.h"
#include "uni_base64.h"
#include "joyinside_client.h"

STATIC JD_OUTPUT_CBS_T s_cbs = {0};

VOID_T joyinside_parse_set_output_cbs(CONST JD_OUTPUT_CBS_T *cbs)
{
    if (cbs) {
        s_cbs = *cbs;
    } else {
        memset(&s_cbs, 0, sizeof(s_cbs));
    }
}

STATIC OPERATE_RET __jd_handle_tts(BYTE_T *data, UINT_T len, BOOL_T is_final)
{
    if (!s_cbs.on_tts) return OPRT_OK;

    STATIC BOOL_T first_packet = TRUE;
    BOOL_T is_start = first_packet;
    if (first_packet) first_packet = FALSE;
    if (is_final) first_packet = TRUE;

    return s_cbs.on_tts(data, len, is_start, is_final);
}

STATIC OPERATE_RET __jd_recv_output_to_text(ty_cJSON *root)
{
    if (!s_cbs.on_text) return OPRT_OK;

    CHAR_T *root_str = ty_cJSON_PrintUnformatted(root);
    JD_PR_D("text root: %s", root_str ? root_str : "null");
    if (root_str) Free(root_str);

    ty_cJSON *eof_item = ty_cJSON_GetObjectItem(root, "eof");
    BOOL_T eof = eof_item ? (ty_cJSON_IsTrue(eof_item) ||
                 (eof_item->type == ty_cJSON_Number && eof_item->valueint)) : FALSE;

    return s_cbs.on_text(root, eof);
}

STATIC OPERATE_RET __jd_handle_asr(CHAR_T *asr, CHAR_T *lang)
{
    OPERATE_RET rt = OPRT_OK;

    ty_cJSON *root = ty_cJSON_CreateObject();
    TUYA_CHECK_NULL_RETURN(root, OPRT_MALLOC_FAILED);

    ty_cJSON_AddStringToObject(root, "bizType", "ASR");
    ty_cJSON_AddNumberToObject(root, "eof", 1);
    ty_cJSON *data = ty_cJSON_CreateObject();
    if (data == NULL) {
        ty_cJSON_Delete(root);
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddItemToObject(root, "data", data);

    if (asr) {
        ty_cJSON_AddStringToObject(data, "text", asr);
    } else {
        ty_cJSON_AddStringToObject(data, "text", "");
    }
    if (lang) {
        if (strcmp(lang, "普通话") == 0) {
            ty_cJSON_AddStringToObject(data, "lang", "zh");
        } else if (strcmp(lang, "英文") == 0) {
            ty_cJSON_AddStringToObject(data, "lang", "en");
        }
    }

    rt = __jd_recv_output_to_text(root);
    ty_cJSON_Delete(root);
    return rt;
}

STATIC OPERATE_RET __jd_handle_nlg(CHAR_T *nlg, BOOL_T eof)
{
    OPERATE_RET rt = OPRT_OK;

    ty_cJSON *root = ty_cJSON_CreateObject();
    TUYA_CHECK_NULL_RETURN(root, OPRT_MALLOC_FAILED);

    ty_cJSON_AddStringToObject(root, "bizType", "NLG");
    ty_cJSON_AddBoolToObject(root, "eof", eof);
    ty_cJSON *data = ty_cJSON_CreateObject();
    if (data == NULL) {
        ty_cJSON_Delete(root);
        return OPRT_MALLOC_FAILED;
    }
    ty_cJSON_AddItemToObject(root, "data", data);

    if (nlg) {
        ty_cJSON_AddStringToObject(data, "content", nlg);
    } else {
        ty_cJSON_AddStringToObject(data, "content", "");
    }

    rt = __jd_recv_output_to_text(root);
    ty_cJSON_Delete(root);
    return rt;
}

STATIC OPERATE_RET __jd_handle_event(JD_EVENT_TYPE_E event)
{
    if (!s_cbs.on_event) return OPRT_OK;
    return s_cbs.on_event(event);
}

STATIC OPERATE_RET __jd_parse_asr(ty_cJSON *root)
{
    ty_cJSON *content = ty_cJSON_GetObjectItem(root, "content");
    if (content) {
        ty_cJSON *textType = ty_cJSON_GetObjectItem(content, "textType");
        if (textType && !strcmp(textType->valuestring, "IS_FINAL")) {
            ty_cJSON *text = ty_cJSON_GetObjectItem(content, "text");
            if ((text) && (strlen(text->valuestring) > 0)) {
                ty_cJSON *lang = ty_cJSON_GetObjectItem(content, "lang");
                JD_PR_D("asr: %s, lang: %s", text->valuestring, lang ? lang->valuestring : "null");
                __jd_handle_asr(text->valuestring, lang ? lang->valuestring : NULL);
            } else {
                PR_ERR("asr text null");
            }
        }
    }
    return OPRT_OK;
}

STATIC OPERATE_RET __jd_handle_intent_end(ty_cJSON *content)
{
    return __jd_recv_output_to_text(content);
}

STATIC OPERATE_RET __jd_handle_skill_event(ty_cJSON *content)
{
    return __jd_recv_output_to_text(content);
}

STATIC OPERATE_RET __jd_parse_event(ty_cJSON *root)
{
    ty_cJSON *content = ty_cJSON_GetObjectItem(root, "content");
    if (!content) {
        return OPRT_OK;
    }

    ty_cJSON *eventType = ty_cJSON_GetObjectItem(content, "eventType");
    if (!eventType) {
        return OPRT_OK;
    }
    JD_PR_D("jd recv event type: %s", eventType->valuestring);
    ty_cJSON *eventData = NULL, *startTime = NULL;

    if (!strcmp(eventType->valuestring, "CALL_AGENT_START_EVENT")) {
        eventData = ty_cJSON_GetObjectItem(content, "eventData");
        if (eventData) {
            ty_cJSON *input = ty_cJSON_GetObjectItem(eventData, "input");
            startTime = ty_cJSON_GetObjectItem(eventData, "startTime");
            if (input) {
                JD_PR_D("call start time: %lld", startTime ? (long long)startTime->valuedouble : 0);
                JD_PR_D("receive call agent start event, input: %s", input->valuestring);
            }
        }
    } else if (!strcmp(eventType->valuestring, "EMPTY_CONTENT")) {
        eventData = ty_cJSON_GetObjectItem(content, "eventData");
        if (eventData) {
            startTime = ty_cJSON_GetObjectItem(eventData, "startTime");
            PR_DEBUG("call start time: %lld", startTime ? (long long)startTime->valuedouble : 0);
        }
        __jd_handle_asr(NULL, NULL);
    } else if (!strcmp(eventType->valuestring, "TTS_SENTENCE_START")) {
        eventData = ty_cJSON_GetObjectItem(content, "eventData");
        if (eventData) {
            ty_cJSON *text = ty_cJSON_GetObjectItem(eventData, "text");
            ty_cJSON *textTime = ty_cJSON_GetObjectItem(eventData, "time");
            JD_PR_D("nlg: %s", text ? text->valuestring : "null");
            JD_PR_D("nlg time: %lld", textTime ? (long long)textTime->valuedouble : 0);
        }
    } else if (!strcmp(eventType->valuestring, "CFG_BOT_EVENT")) {
    } else if (!strcmp(eventType->valuestring, "SERVER_VOICE_CHAT_UPDATED")) {
    } else if (strcmp(eventType->valuestring, "CALL_AGENT_INTERRUPTED") == 0) {
        eventData = ty_cJSON_GetObjectItem(content, "eventData");
        __jd_handle_event(JD_EVENT_INTERRUPT);
        if (eventData) {
            startTime = ty_cJSON_GetObjectItem(eventData, "startTime");
            PR_DEBUG("call start time: %lld", startTime ? (long long)startTime->valuedouble : 0);
            if (s_cbs.on_output_stop) s_cbs.on_output_stop(TRUE);
        }
    } else if (strcmp(eventType->valuestring, "TTS_COMPLETE") == 0) {
        __jd_handle_tts(NULL, 0, TRUE);
    } else if (strcmp(eventType->valuestring, "CALL_INTENT_END_EVENT") == 0) {
        __jd_handle_intent_end(content);
    } else if (strcmp(eventType->valuestring, "CALL_SKILL_EVENT") == 0) {
        __jd_handle_skill_event(content);
    } else if (strcmp(eventType->valuestring, "COMPLETE") == 0) {
    } else if (strcmp(eventType->valuestring, "INTERRUPT") == 0) {
    }

    return OPRT_OK;
}

STATIC OPERATE_RET __jd_parse_agent(ty_cJSON *root)
{
    BOOL_T eof = FALSE;
    ty_cJSON *content = ty_cJSON_GetObjectItem(root, "content");
    if (content) {
        ty_cJSON *textReply = ty_cJSON_GetObjectItem(content, "content");
        ty_cJSON *finishReason = ty_cJSON_GetObjectItem(content, "finishReason");
        if ((finishReason) && (finishReason->valuestring)) {
            if (!strcmp(finishReason->valuestring, "stop")) {
                PR_DEBUG("nlg eof");
                eof = TRUE;
            } else if (!strcmp(finishReason->valuestring, "error")) {
                PR_DEBUG("nlg has error");
            } else if (!strcmp(finishReason->valuestring, "audit")) {
                PR_DEBUG("security audit filtering");
            } else {
                JD_PR_D("nlg finish reason: %s", finishReason->valuestring);
            }
        }
        JD_PR_D("nlg content: %s", textReply ? textReply->valuestring : "null");
        __jd_handle_nlg(textReply ? textReply->valuestring : "", eof);
    }
    return OPRT_OK;
}

STATIC OPERATE_RET __jd_parse_activity(ty_cJSON *root)
{
    ty_cJSON *content = ty_cJSON_GetObjectItem(root, "content");
    if (content) {
        ty_cJSON *active = ty_cJSON_GetObjectItem(content, "content");
        ty_cJSON *role = ty_cJSON_GetObjectItem(content, "role");
        PR_DEBUG("activity content: %s", active ? active->valuestring : "null");
        if (role && !strcmp(role->valuestring, "assistant")) {
            PR_DEBUG("reply by robot");
        }
    }
    return OPRT_OK;
}

STATIC OPERATE_RET __jd_parse_tts(ty_cJSON *root)
{
    ty_cJSON *content = ty_cJSON_GetObjectItem(root, "content");
    if (content) {
        ty_cJSON *audioBase64 = ty_cJSON_GetObjectItem(content, "audioBase64");
        ty_cJSON *audioAue = ty_cJSON_GetObjectItem(content, "audioAue");
        ty_cJSON *finish = ty_cJSON_GetObjectItem(content, "finish");
        JD_PR_D("receive tts audio aue: %s", audioAue ? audioAue->valuestring : "null");
        if (audioBase64) {
            CHAR_T *tts_audio_buf = Malloc(strlen(audioBase64->valuestring) + 1);
            TUYA_CHECK_NULL_RETURN(tts_audio_buf, OPRT_MALLOC_FAILED);
            memset(tts_audio_buf, 0, strlen(audioBase64->valuestring) + 1);
            UINT_T decode_len = tuya_base64_decode(audioBase64->valuestring, (BYTE_T *)tts_audio_buf);
            JD_PR_D("receive tts audio len: %d", decode_len);
            BOOL_T is_final = FALSE;
            JD_PR_D("tts finish: %d", ty_cJSON_IsTrue(finish));
            if (finish && ty_cJSON_IsTrue(finish)) {
                is_final = TRUE;
            }
            JD_PR_D("handle audio data len:%u, is_final:%d", decode_len, is_final);
            __jd_handle_tts((BYTE_T *)tts_audio_buf, decode_len, FALSE);
            Free(tts_audio_buf);
        } else {
            PR_ERR("audioBase64 is null");
        }
    }
    return OPRT_OK;
}

STATIC OPERATE_RET __jd_parse_pong(ty_cJSON *root)
{
    joyinside_handle_pong();
    return OPRT_OK;
}

STATIC OPERATE_RET __jd_parse_audio_book(ty_cJSON *root)
{
    ty_cJSON *content = ty_cJSON_GetObjectItem(root, "content");
    if (!content) {
        PR_ERR("audio book content is null");
        return OPRT_INVALID_PARM;
    }
    return __jd_recv_output_to_text(content);
}

OPERATE_RET joyinside_parse_content(ty_cJSON *root)
{
    ty_cJSON *contentType = ty_cJSON_GetObjectItem(root, "contentType");
    if (!contentType || !contentType->valuestring) {
        PR_ERR("content type is null");
        return OPRT_INVALID_PARM;
    }

    JD_PR_D("jd recv content type: %s", contentType->valuestring);
    if (!strcmp(contentType->valuestring, "ASR")) {
        return __jd_parse_asr(root);
    } else if (!strcmp(contentType->valuestring, "EVENT")) {
        return __jd_parse_event(root);
    } else if (!strcmp(contentType->valuestring, "AGENT")) {
        return __jd_parse_agent(root);
    } else if (!strcmp(contentType->valuestring, "ACTIVITY")) {
        return __jd_parse_activity(root);
    } else if (!strcmp(contentType->valuestring, "TTS")) {
        return __jd_parse_tts(root);
    } else if (!strcmp(contentType->valuestring, "PONG")) {
        return __jd_parse_pong(root);
    } else if (!strcmp(contentType->valuestring, "AUDIO_BOOK")) {
        return __jd_parse_audio_book(root);
    } else {
        PR_ERR("unknown content type: %s", contentType->valuestring);
    }

    return OPRT_OK;
}
