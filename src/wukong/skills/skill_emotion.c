#include "wukong_ai_skills.h"
#include "skill_emotion.h"
#include "tal_memory.h"
#include "tal_log.h"

WUKONG_AI_EMO_T g_emotions[] = {
    {"U+1F636", "neutral"},      // 😶
    {"U+1F642", "happy"},        // 🙂
    {"U+1F606", "laughing"},     // 😆
    {"U+1F602", "funny"},        // 😂
    {"U+1F614", "sad"},          // 😔
    {"U+1F620", "angry"},        // 😠
    {"U+1F62D", "crying"},       // 😭
    {"U+1F60D", "loving"},       // 😍
    {"U+1F633", "embarrassed"},  // 😳
    {"U+1F62F", "surprise"},     // 😯
    {"U+1F631", "shocked"},      // 😱
    {"U+1F914", "thinking"},     // 🤔
    {"U+1F609", "winking"},      // 😉
    {"U+1F60E", "cool"},         // 😎
    {"U+1F60C", "relaxed"},      // 😌
    {"U+1F924", "delicious"},    // 🤤
    {"U+1F618", "kissy"},        // 😘
    {"U+1F60F", "confident"},    // 😏
    {"U+1F634", "sleepy"},       // 😴
    {"U+1F61C", "silly"},        // 😜
    {"U+1F644", "confused"}      // 🙄
};

/**
 * @brief Convert Unicode code point string "U+XXXX" to UTF-8 bytes
 * @param[in] unicode_str Unicode string in format "U+XXXX" or "u+xxxx" (e.g., "U+1F636")
 * @param[out] utf8_buf Buffer to store UTF-8 bytes (at least 5 bytes)
 * @param[in] buf_size Size of the buffer
 * @return Number of bytes written, or -1 on error
 */
INT_T wukong_emoji_unicode_to_utf8(CONST CHAR_T* unicode_str, CHAR_T* utf8_buf, size_t buf_size)
{
    TUYA_CHECK_NULL_RETURN(unicode_str, -1);
    TUYA_CHECK_NULL_RETURN(utf8_buf, -1);
    
    if (buf_size < 5 || (unicode_str[0] != 'U' && unicode_str[0] != 'u') || unicode_str[1] != '+') {
        return -1;
    }
    
    // Parse hex string after "U+"
    UINT_T codepoint = 0;
    for (CONST CHAR_T* p = unicode_str + 2; *p != '\0'; p++) {
        CHAR_T c = *p;
        if (c >= '0' && c <= '9') {
            codepoint = (codepoint << 4) | (c - '0');
        } else if (c >= 'A' && c <= 'F') {
            codepoint = (codepoint << 4) | (c - 'A' + 10);
        } else if (c >= 'a' && c <= 'f') {
            codepoint = (codepoint << 4) | (c - 'a' + 10);
        } else {
            break;
        }
    }
    
    if (codepoint > 0x10FFFF) {
        return -1;
    }
    
    // Convert to UTF-8
    INT_T len = 0;
    if (codepoint <= 0x7F) {
        utf8_buf[0] = (CHAR_T)codepoint;
        len = 1;
    } else if (codepoint <= 0x7FF) {
        utf8_buf[0] = (CHAR_T)(0xC0 | (codepoint >> 6));
        utf8_buf[1] = (CHAR_T)(0x80 | (codepoint & 0x3F));
        len = 2;
    } else if (codepoint <= 0xFFFF) {
        utf8_buf[0] = (CHAR_T)(0xE0 | (codepoint >> 12));
        utf8_buf[1] = (CHAR_T)(0x80 | ((codepoint >> 6) & 0x3F));
        utf8_buf[2] = (CHAR_T)(0x80 | (codepoint & 0x3F));
        len = 3;
    } else {
        utf8_buf[0] = (CHAR_T)(0xF0 | (codepoint >> 18));
        utf8_buf[1] = (CHAR_T)(0x80 | ((codepoint >> 12) & 0x3F));
        utf8_buf[2] = (CHAR_T)(0x80 | ((codepoint >> 6) & 0x3F));
        utf8_buf[3] = (CHAR_T)(0x80 | (codepoint & 0x3F));
        len = 4;
    }
    
    utf8_buf[len] = '\0';
    return len;
}


CONST CHAR_T* wukong_emoji_get_name(CONST CHAR_T* emoji) 
{
    TUYA_CHECK_NULL_RETURN(emoji, NULL);
    
    for (size_t i = 0; i < CNTSOF(g_emotions); i++) {
        if (strcmp(g_emotions[i].emoji, emoji) == 0) {
            TAL_PR_NOTICE("wukong_emoji_get_name: %s", g_emotions[i].name);
            return g_emotions[i].name;
        }
    }
    
    return g_emotions[0].name; // 未找到匹配的emoji, use neutral as default
}


CONST CHAR_T* wukong_emoji_get_by_name(CONST CHAR_T* name) 
{
    TUYA_CHECK_NULL_RETURN(name, NULL);
    
    for (size_t i = 0; i < CNTSOF(g_emotions); i++) {
        if (strcmp(g_emotions[i].name, name) == 0) {
            return g_emotions[i].emoji;
        }
    }
    
    return g_emotions[0].emoji; // 未找到匹配的emoji, use neutral as default
}

