"""claw provider unified test suite (conftest auto-discovered).

All claw host tests live in this one directory / one pytest entry. SUT sources
are added by path relative to apps/.../src/ via add_sdk_source; shared doubles
come from tests_common (unchanged). Suites needing a real SDK header ahead of a
same-named tests_common stub list it in front_includes (agent: wukong_ai_agent.h).

Tuple columns:
  name, description, sdk_sources, common_sources, test_sources,
  extra_includes, front_includes, extra_defines
"""
import pytest

_TESTS = [
    ("context",
     "wukong_context system prompt + history build",
     ["wukong/provider/claw/context/wukong_context.c",
      "wukong/provider/claw/context/wukong_profile.c",
      "wukong/provider/claw/memory/wukong_session.c",
      "wukong/provider/claw/memory/wukong_compact.c",
      "wukong/provider/claw/config/claw_config.c",
      "wukong/storage/wukong_storage_record.c"],
     ["stubs_cjson.c", "stub_tkl_fs.c"],
     ["test_wukong_context.c", "stub_storage_ready.c"],
     ["wukong/storage", "wukong/provider", "wukong/provider/claw",
      "wukong/provider/claw/llm",
      "wukong/provider/claw/context",
      "wukong/toolkits/skill", "wukong/provider/claw/memory",
      "wukong/provider/claw/config"],
     [],
     ["WUKONG_STORAGE_SDCARD=1", "WUKONG_STORAGE_HOST_TEST=1"]),

    ("session",
     "wukong_session JSONL persistence + window + tool rounds",
     ["wukong/provider/claw/memory/wukong_session.c",
      "wukong/provider/claw/config/claw_config.c",
      "wukong/storage/wukong_storage_record.c"],
     ["stubs_cjson.c", "stub_tkl_fs.c"],
     ["test_wukong_session.c", "stub_storage_ready.c"],
     ["wukong/provider", "wukong/provider/claw/memory",
      "wukong/storage", "wukong/provider/claw/config"],
     [],
     ["WUKONG_STORAGE_SDCARD=1", "WUKONG_STORAGE_HOST_TEST=1"]),

    ("profile",
     "wukong_profile fs read + cache + seed + reload",
     ["wukong/provider/claw/context/wukong_profile.c",
      "wukong/storage/wukong_storage_record.c"],
     ["stub_tkl_fs.c", "stubs_cjson.c"],
     ["test_wukong_profile.c", "stub_storage_ready.c"],
     ["wukong/storage", "wukong/provider", "wukong/provider/claw/context"],
     [],
     ["WUKONG_STORAGE_SDCARD=1", "WUKONG_STORAGE_HOST_TEST=1"]),

    ("memory",
     "wukong_memory init/build_index/persistence",
     ["wukong/provider/claw/memory/wukong_memory.c",
      "wukong/provider/claw/config/claw_config.c",
      "wukong/storage/wukong_storage_record.c"],
     ["stub_tkl_fs.c", "stubs_cjson.c"],
     ["test_wukong_memory.c", "stub_storage_ready.c"],
     ["wukong/storage", "wukong/provider/claw/memory", "wukong/provider/claw/config"],
     [],
     ["WUKONG_STORAGE_SDCARD=1", "WUKONG_STORAGE_HOST_TEST=1"]),

    ("skill",
     "wukong_skill directory scan/frontmatter/build_summary/read-strip/"
     "reload/id-safety/self-managed lifecycle (tool + storage.ready)",
     ["wukong/toolkits/skill/wukong_skill.c",
      "wukong/storage/wukong_storage_record.c"],
     ["stub_tkl_fs.c", "stubs_cjson.c"],
     ["test_wukong_skill.c", "stub_storage_ready.c", "stub_base_event.c",
      "stub_skill_tool.c"],
     ["wukong/storage", "wukong/toolkits/skill"],
     ["wukong/provider/claw/tests"],
     ["WUKONG_STORAGE_SDCARD=1", "WUKONG_STORAGE_HOST_TEST=1"]),

    ("agent",
     "wukong_agent_loop ReAct loop (reason/act/observe)",
     ["wukong/provider/claw/agent/wukong_agent_loop.c",
      "wukong/provider/claw/context/wukong_context.c",
      "wukong/provider/claw/context/wukong_profile.c",
      "wukong/provider/claw/memory/wukong_session.c",
      "wukong/provider/claw/memory/wukong_compact.c",
      "wukong/provider/claw/config/claw_config.c",
      "wukong/storage/wukong_storage_record.c"],
     ["stubs_cjson.c", "stub_tkl_fs.c"],
     ["test_wukong_agent_loop.c", "stub_env.c", "stub_storage_ready.c"],
     ["wukong/provider/claw/agent", "wukong/provider/claw/llm", "wukong/provider/claw/context", "wukong/toolkits/skill",
      "wukong/provider/claw/memory", "wukong/provider", "wukong/provider/claw",
      "wukong/channel", "wukong/toolkits", "wukong/storage",
      "wukong/provider/claw/config"],
     ["wukong"],  # front: real wukong_ai_agent.h over the tests_common stub
     ["WUKONG_STORAGE_SDCARD=1", "WUKONG_STORAGE_HOST_TEST=1"]),

    ("llm",
     "wukong_llm request build + response parse + tool calls",
     ["wukong/provider/claw/llm/wukong_llm.c", "wukong/toolkits/wukong_tool.c"],
     ["stubs_cjson.c"],
     ["test_wukong_llm.c", "stub_http.c", "stub_mcp.c"],
     ["wukong/provider", "wukong/provider/claw/llm", "wukong/toolkits"],
     [],
     []),

    ("feishu_proto",
     "feishu protobuf frame parse/encode + ping/ACK build (pure codec)",
     ["wukong/channel/im_feishu/feishu_proto.c"],
     [],
     ["test_feishu_proto.c"],
     ["wukong/channel/im_feishu"],
     [],
     []),

    ("config",
     "claw_config threshold clamp (pure validation)",
     ["wukong/provider/claw/config/claw_config.c",
      "wukong/storage/wukong_storage_record.c"],
     ["stubs_cjson.c", "stub_tkl_fs.c"],
     ["test_claw_config.c", "stub_storage_ready.c"],
     ["wukong/provider/claw/config", "wukong/storage"],
     [],
     ["WUKONG_STORAGE_SDCARD=1", "WUKONG_STORAGE_HOST_TEST=1"]),

    ("cli_config",
     "claw_config CLI dispatch: set feishu (id+secret) + usage errors + masked list",
     ["wukong/provider/claw/cli/claw_cli_config.c"],
     [],
     ["test_wukong_cli_claw_config.c"],
     ["wukong/provider/claw/cli", "wukong/provider/claw", "wukong/channel"],
     [],
     []),
]
_IDS = [t[0] for t in _TESTS]


@pytest.mark.parametrize(
    "name,description,sdk_sources,common_sources,test_sources,"
    "extra_includes,front_includes,extra_defines",
    _TESTS, ids=_IDS,
)
def test_claw(c_test, name, description, sdk_sources, common_sources,
              test_sources, extra_includes, front_includes, extra_defines):
    for s in sdk_sources:
        c_test.add_sdk_source(s)
    for s in common_sources:
        c_test.add_common_source(s)
    for s in test_sources:
        c_test.add_test_source(s)
    for i in extra_includes:
        c_test.include_sdk(i)
    for i in front_includes:
        c_test.include_sdk_front(i)
    for d in extra_defines:
        c_test.define(d)
    c_test.run()
