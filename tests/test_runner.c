#include "../include/system_agent.h"
#include "../include/memory_store.h"
#include "../include/dpapi_crypt.h"
#include "../include/lua_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <windows.h>

static int s_pass, s_fail;

#define TEST(name) do { \
    printf("  %-48s ", name); \
    fflush(stdout); \
} while(0)

#define PASS() do { s_pass++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { s_fail++; printf("FAIL: %s\n", msg); } while(0)
#define ASSERT(cond, msg) do { \
    if (!(cond)) { FAIL(msg); return; } \
} while(0)
#define ASSERT_STREQ(a, b, msg) do { \
    if (strcmp((a),(b)) != 0) { \
        char _buf[256]; snprintf(_buf, sizeof(_buf), "%s: expected '%s' got '%s'", msg, (b), (a)); \
        FAIL(_buf); return; \
    } \
} while(0)

/* Callback helpers for think test */
static char s_captured_think[256];
static void think_cb(const char *text, void *ud) {
    (void)ud;
    strncpy(s_captured_think, text, 255);
}

/* ===== system_agent tests ===== */
static void test_sa_json_parse(void) {
    AgentAction act;
    memset(&act, 0, sizeof(act));
    const char *input = "{\"action\":\"create_file\",\"path\":\"test.txt\",\"content\":\"hello\"}";
    int ret = system_agent_parse(input, (int)strlen(input), &act);
    ASSERT(ret == 1, "create_file should return 1");
    ASSERT_STREQ(act.type, "create_file", "action type");
    ASSERT_STREQ(act.path, "test.txt", "path");
    ASSERT_STREQ(act.content, "hello", "content");
    PASS();
}

static void test_sa_json_no_content(void) {
    AgentAction act;
    memset(&act, 0, sizeof(act));
    const char *input = "{\"action\":\"delete_file\",\"path\":\"work\\tmp.dat\"}";
    int ret = system_agent_parse(input, (int)strlen(input), &act);
    ASSERT(ret == 1, "delete_file should return 1");
    ASSERT_STREQ(act.type, "delete_file", "action type");
    ASSERT_STREQ(act.path, "work\\tmp.dat", "path");
    ASSERT(act.content[0] == '\0', "content should be empty");
    PASS();
}

static void test_sa_json_malformed(void) {
    AgentAction act;
    memset(&act, 0, sizeof(act));
    const char *input = "{bad json}";
    int ret = system_agent_parse(input, (int)strlen(input), &act);
    ASSERT(ret == 0, "malformed JSON should return 0");
    PASS();
}

static void test_sa_json_no_action(void) {
    AgentAction act;
    memset(&act, 0, sizeof(act));
    const char *input = "{\"foo\":\"bar\"}";
    int ret = system_agent_parse(input, (int)strlen(input), &act);
    ASSERT(ret == 0, "missing action field should return 0");
    PASS();
}

static void test_sa_path_sandbox(void) {
    /* valid paths inside sandbox */
    ASSERT(system_agent_validate_path("work/file.txt") == true, "work/ relative OK");
    ASSERT(system_agent_validate_path("profile/settings.json") == true, "profile/ OK");
    ASSERT(system_agent_validate_path("scripts/foo.lua") == true, "scripts/ OK");
    ASSERT(system_agent_validate_path("prompts/default.txt") == true, "prompts/ OK");
    ASSERT(system_agent_validate_path("plugins/myplugin/") == true, "plugins/ OK");
    /* invalid paths */
    ASSERT(system_agent_validate_path("C:\\Windows\\system32\\cmd.exe") == false, "absolute path rejected");
    ASSERT(system_agent_validate_path("..\\..\\etc\\passwd") == false, "parent traversal rejected");
    ASSERT(system_agent_validate_path("../outside.txt") == false, "dotdot rejected");
    ASSERT(system_agent_validate_path("") == false, "empty rejected");
    ASSERT(system_agent_validate_path("outside/foo.txt") == false, "unknown root rejected");
    ASSERT(system_agent_validate_path("~/.bashrc") == false, "tilde rejected");
    ASSERT(system_agent_validate_path("/etc/passwd") == false, "unix absolute rejected");
    PASS();
}

static void test_sa_think_via_feed(void) {
    int think_count = 0;
    s_captured_think[0] = '\0';

    system_agent_set_reasoning_cb(think_cb, NULL);

    const char *input = "<think>Let me reason about this</think>{\"action\":\"create_file\",\"path\":\"work/test.txt\",\"content\":\"done\"}";
    while (*input) {
        char ch[2] = { *input++, 0 };
        if (system_agent_feed(ch)) think_count++;
    }
    system_agent_flush();

    ASSERT(strlen(s_captured_think) > 0, "think content captured");
    ASSERT(strstr(s_captured_think, "Let me reason") != NULL, "think content match");
    ASSERT(think_count >= 1, "action executed after think");
    system_agent_set_reasoning_cb(NULL, NULL);
    PASS();
}

/* ===== dpapi_crypt tests ===== */
static void test_dpapi_roundtrip(void) {
    const char *plain = "hello winalp secret data 123!";
    BYTE *blob = NULL;
    DWORD blob_len = 0;
    ASSERT(dpapi_encrypt(plain, strlen(plain), &blob, &blob_len), "encrypt");
    ASSERT(blob != NULL && blob_len > 0, "encrypt produced output");

    char *dec = NULL;
    DWORD dec_len = 0;
    ASSERT(dpapi_decrypt(blob, blob_len, &dec, &dec_len), "decrypt");
    ASSERT(dec != NULL && dec_len == strlen(plain), "decrypt length");
    ASSERT(memcmp(dec, plain, dec_len) == 0, "decrypt content matches");
    dpapi_free(blob);
    dpapi_free((BYTE*)dec);
    PASS();
}

static void test_dpapi_empty(void) {
    BYTE *blob = NULL; DWORD blob_len = 0;
    ASSERT(dpapi_encrypt("", 0, &blob, &blob_len), "encrypt empty");
    char *dec = NULL; DWORD dec_len = 0;
    ASSERT(dpapi_decrypt(blob, blob_len, &dec, &dec_len), "decrypt empty");
    ASSERT(dec_len == 0, "decrypt empty length");
    dpapi_free(blob);
    dpapi_free((BYTE*)dec);
    PASS();
}

/* ===== memory_store tests ===== */
static void test_mem_store_profile(void) {
    char buf[256];

    ASSERT(memory_store_set_profile("test_key", "test_value"), "set profile");
    ASSERT(memory_store_get_profile("test_key", buf, sizeof(buf)), "get profile");
    ASSERT_STREQ(buf, "test_value", "profile value");

    ASSERT(memory_store_set_profile("test_key", "new_value"), "overwrite profile");
    ASSERT(memory_store_get_profile("test_key", buf, sizeof(buf)), "get after overwrite");
    ASSERT_STREQ(buf, "new_value", "overwritten value");

    ASSERT(memory_store_get_profile("nonexistent", buf, sizeof(buf)) == false, "get nonexistent");
    PASS();
}

static void test_mem_store_tasks(void) {
    char buf[4096];

    const char *task = "{\"id\":\"test_001\",\"title\":\"unit test task\",\"status\":\"pending\"}";
    ASSERT(memory_store_upsert_task(task), "upsert task");

    ASSERT(memory_store_get_tasks(buf, sizeof(buf)), "get tasks");
    ASSERT(strstr(buf, "test_001") != NULL, "task appears in list");
    PASS();
}

static void test_mem_encryption(void) {
    char buf[256];

    memory_store_set_encryption(true);
    ASSERT(memory_store_set_profile("enc_key", "enc_value"), "set with encryption");
    ASSERT(memory_store_get_profile("enc_key", buf, sizeof(buf)), "get with encryption");
    ASSERT_STREQ(buf, "enc_value", "encrypted value matches");

    FILE *f = fopen("profile/profile.json", "rb");
    ASSERT(f != NULL, "profile file exists");
    unsigned char hdr[4];
    size_t n = fread(hdr, 1, 4, f);
    fclose(f);
    ASSERT(n == 4 && hdr[0] == 0x57 && hdr[1] == 0x49 && hdr[2] == 0x4E && hdr[3] == 0x45,
           "file starts with WINE magic");

    memory_store_set_encryption(false);
    PASS();
}

/* ===== lua_runtime tests ===== */
static void test_lua_basic(void) {
    lua_State *L = lua_runtime_new_state("test_data");
    ASSERT(L != NULL, "lua state created");

    const char *result = lua_runtime_dostring_result(L, "return 'hello from lua'");
    ASSERT(result != NULL, "lua execution returns value");
    ASSERT(strstr(result, "hello from lua") != NULL, "lua return value");
    lua_runtime_close(L);
    PASS();
}

static void test_lua_store_api(void) {
    lua_State *L = lua_runtime_new_state("test_data");
    ASSERT(L != NULL, "lua state created");

    bool ok = lua_runtime_dostring(L,
        "store_set('test_key', 'luaval')");
    ASSERT(ok, "store_set via Lua");

    const char *r = lua_runtime_dostring_result(L, "return store_get('test_key')");
    ASSERT(r != NULL, "store_get returns value");
    ASSERT(strstr(r, "luaval") != NULL, "store roundtrip");
    lua_runtime_close(L);
    PASS();
}

static void test_lua_log_api(void) {
    lua_State *L = lua_runtime_new_state("test_data");
    ASSERT(L != NULL, "lua state created");

    /* log_info just triggers a log — verify it doesn't crash */
    bool ok = lua_runtime_dostring(L, "log_info('test log message')");
    ASSERT(ok, "log_info via Lua");
    lua_runtime_close(L);
    PASS();
}

static void test_lua_sandbox_blocked(void) {
    lua_State *L = lua_runtime_new_state("test_data");
    ASSERT(L != NULL, "lua state created");

    /* os.execute should be nil (removed) — access without crashing */
    bool ok = lua_runtime_dostring(L, "local x = os.execute");
    ASSERT(ok, "os.execute accessed without crash");

    /* Trying to call a nil should error gracefully */
    ok = lua_runtime_dostring(L, "os.execute('dir')");
    ASSERT(!ok, "os.execute call should fail");
    lua_runtime_close(L);
    PASS();
}

static void test_lua_sandbox_file_sandboxed(void) {
    lua_State *L = lua_runtime_new_state("test_data");
    ASSERT(L != NULL, "lua state created");

    /* Write inside data dir should work */
    bool ok = lua_runtime_dostring(L, "file_write('inside.txt', 'content')");
    ASSERT(ok, "file_write inside sandbox");

    const char *r = lua_runtime_dostring_result(L, "return file_read('inside.txt')");
    ASSERT(r != NULL, "file_read after file_write");
    ASSERT(strstr(r, "content") != NULL, "sandboxed file roundtrip");
    lua_runtime_close(L);
    PASS();
}

/* ===== run all ===== */
typedef void (*TestFn)(void);
static void run_suite(const char *name, TestFn *tests, int n) {
    printf("\n--- %s ---\n", name);
    for (int i = 0; i < n; i++) tests[i]();
}

int main(void) {
    printf("WinAlp Test Runner\n");
    printf("==================\n");

    CreateDirectory("test_data", NULL);
    memory_store_init("profile");
    lua_runtime_init();

    {
        TestFn tests[] = {
            test_sa_json_parse, test_sa_json_no_content,
            test_sa_json_malformed, test_sa_json_no_action,
            test_sa_path_sandbox, test_sa_think_via_feed
        };
        run_suite("system_agent", tests, 6);
    }

    {
        TestFn tests[] = { test_dpapi_roundtrip, test_dpapi_empty };
        run_suite("dpapi_crypt", tests, 2);
    }

    {
        TestFn tests[] = { test_mem_store_profile, test_mem_store_tasks,
                          test_mem_encryption };
        run_suite("memory_store", tests, 3);
    }

    {
        TestFn tests[] = { test_lua_basic, test_lua_store_api,
                          test_lua_log_api, test_lua_sandbox_blocked,
                          test_lua_sandbox_file_sandboxed };
        run_suite("lua_runtime", tests, 5);
    }

    lua_runtime_shutdown();
    memory_store_shutdown();

    printf("\n==================\n");
    printf("Results: %d pass, %d fail\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
