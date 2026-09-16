/* Env stubs for the agent-loop test: wukong_ai_event_notify (linked as part of
 * wukong_agent_loop.c though loop_run never calls it) and the skill/memory
 * catalog builders that wukong_context.c injects (the agent test exercises
 * neither, so return NULL -> no "Available skills"/"## Memory" section). */
#include "wukong_ai_agent.h"
#include "wukong_skill.h"
#include "wukong_memory.h"

VOID wukong_ai_event_notify(WUKONG_AI_EVENT_TYPE_E type, VOID *data)
{
    (VOID_T)type;
    (VOID_T)data;
}

CHAR_T *wukong_skill_build_summary(VOID_T)
{
    return NULL;
}

CHAR_T *wukong_memory_build_index(VOID_T)
{
    return NULL;
}
