#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "devpkg.h"
#include "editor.h"
#include "eosvm.h"
#include "runtime_iface.h"
#include "sha256.h"
#include "hmac_sha256.h"

static int failures;
#define CHECK(c, name) do { if (c) printf("PASS %s\n", name); else { printf("FAIL %s\n", name); failures++; } } while (0)

#define BLOCKS 512u
static uint8_t disk[BLOCKS][OS_STORAGE_BLOCK_SIZE];
static int fail_writes_after = -1;
static int dev_read(void *ctx, u32 b, u8 d[OS_STORAGE_BLOCK_SIZE]) { (void)ctx; if (b >= BLOCKS) return 0; memcpy(d, disk[b], OS_STORAGE_BLOCK_SIZE); return 1; }
static int dev_write(void *ctx, u32 b, const u8 d[OS_STORAGE_BLOCK_SIZE])
{
    (void)ctx;
    if (b >= BLOCKS) return 0;
    if (fail_writes_after == 0) return 0;
    if (fail_writes_after > 0) fail_writes_after--;
    memcpy(disk[b], d, OS_STORAGE_BLOCK_SIZE);
    return 1;
}
static os_storage_device device = { 0, BLOCKS, dev_read, dev_write };

static void fresh_fs(os_storage_fs *fs)
{
    memset(disk, 0, sizeof disk);
    fail_writes_after = -1;
    if (!os_storage_format(&device) || !os_storage_mount(fs, &device)) { printf("FAIL fixture storage\n"); exit(1); }
}

static const uint8_t KEY1[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
static const uint8_t KEY2[16] = { 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf };

static char root[512] = ".";
static char *slurp(const char *rel, size_t *n)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", root, rel);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = malloc(65536);
    *n = fread(buf, 1, 65535, f);
    buf[*n] = 0;
    fclose(f);
    return buf;
}

static int run_text(const char *src, os_eosvm_result *r, uint32_t fuel)
{
    uint8_t code[OS_EOSVM_CODE_MAX];
    size_t n;
    unsigned line;
    int a = os_eosvm_assemble(src, strlen(src), code, sizeof code, &n, &line);
    if (a) return a;
    return os_eosvm_run(code, n, fuel, r);
}

static size_t make_pkg(uint8_t *out, size_t cap, const char *id, uint32_t version, const char *arch, int kind,
                       const char *dep, const uint8_t *payload, size_t plen, int slot, const uint8_t *key)
{
    os_pkg_manifest m;
    size_t n = 0;
    memset(&m, 0, sizeof m);
    strcpy(m.id, id); m.version = version; strcpy(m.arch, arch); m.kind = kind; m.min_abi = 1;
    strcpy(m.dependency, dep);
    if (os_pkg_build(&m, payload, plen, out, cap, &n)) return 0;
    if (slot && os_pkg_sign(out, n, slot, key, 16)) return 0;
    return n;
}

int main(int argc, char **argv)
{
    os_eosvm_result r;
    static uint8_t code[OS_EOSVM_CODE_MAX];
    size_t code_len;
    unsigned line;
    if (argc > 1) { strncpy(root, argv[1], sizeof root - 1); root[sizeof root - 1] = 0; }

    /* ---- EOSVM runtime ---- */
    CHECK(run_text("push 6\npush 7\nmul\nprint\nhalt\n", &r, 100) == 0 && strcmp(r.output, "42\n") == 0, "G17-VM-ARITHMETIC-PROGRAM-EXECUTES");
    CHECK(run_text("push -7\npush 2\ndiv\nprint\npush -7\npush 2\nmod\nprint\nhalt\n", &r, 100) == 0 && strcmp(r.output, "-3\n-1\n") == 0, "G17-VM-SIGNED-DIVISION-AND-MODULO");
    CHECK(run_text("push 2147483647\npush 1\nadd\nprint\nhalt\n", &r, 100) == 0 && strcmp(r.output, "-2147483648\n") == 0, "G17-VM-WRAPS-WITHOUT-UB");
    CHECK(run_text("push 1\npush 0\ndiv\nhalt\n", &r, 100) == OS_EVM_ERR_DIVIDE, "G17-VM-DIVIDE-BY-ZERO-TRAPS");
    CHECK(run_text("push -2147483648\npush -1\ndiv\nhalt\n", &r, 100) == OS_EVM_ERR_DIVIDE, "G17-VM-INT-MIN-DIV-MINUS-ONE-TRAPS");
    CHECK(run_text("pop\nhalt\n", &r, 100) == OS_EVM_ERR_STACK, "G17-VM-STACK-UNDERFLOW-TRAPS");
    {
        char big[2048] = "";
        for (int i = 0; i < 70; i++) strcat(big, "push 1\n");
        strcat(big, "halt\n");
        CHECK(run_text(big, &r, 1000) == OS_EVM_ERR_STACK, "G17-VM-STACK-OVERFLOW-TRAPS");
    }
    CHECK(run_text("top: jmp top\n", &r, 500) == OS_EVM_ERR_FUEL && r.steps == 500, "G17-VM-FUEL-BOUNDS-INFINITE-LOOP");
    CHECK(run_text("push 1\nhalt\n", &r, 0) == OS_EVM_ERR_ARGUMENT, "G17-VM-ZERO-FUEL-REJECTED");
    {
        char prog[3000] = "top: push 65\nputc\njmp top\n";
        CHECK(run_text(prog, &r, 100000) == OS_EVM_ERR_OUTPUT && r.output_len == OS_EOSVM_OUTPUT_MAX, "G17-VM-OUTPUT-BOUNDED");
    }
    {
        const uint8_t bad_op[] = { 0xee };
        const uint8_t trunc[] = { OS_EVM_PUSH, 1, 2 };
        const uint8_t jump_out[] = { OS_EVM_JMP, 99, 0, OS_EVM_HALT };
        const uint8_t jump_mid[] = { OS_EVM_PUSH, 0, 0, 0, 0, OS_EVM_JMP, 2, 0 };
        const uint8_t bad_local[] = { OS_EVM_LOAD, 16, OS_EVM_HALT };
        CHECK(os_eosvm_validate(bad_op, 1) != 0 && os_eosvm_validate(trunc, 3) != 0 && os_eosvm_validate(jump_out, 4) != 0 &&
              os_eosvm_validate(jump_mid, 8) != 0 && os_eosvm_validate(bad_local, 3) != 0 && os_eosvm_validate(NULL, 0) != 0,
              "G17-VM-MALFORMED-BYTECODE-REJECTED-BEFORE-RUN");
        CHECK(os_eosvm_run(jump_mid, 8, 100, &r) != 0 && r.steps == 0, "G17-VM-INVALID-PROGRAM-NEVER-EXECUTES");
    }
    {
        const uint8_t fall[] = { OS_EVM_PUSH, 1, 0, 0, 0 };
        CHECK(os_eosvm_run(fall, 5, 100, &r) == OS_EVM_ERR_CODE, "G17-VM-RUNNING-OFF-END-TRAPS");
    }

    /* ---- Assembler (build tool) ---- */
    {
        const char *bad[] = { "frobnicate\n", "push\n", "push x\n", "push 99999999999\n", "jmp nowhere\n", "a:\na:\nhalt\n",
                              "load 16\n", "pop 1\n", "push 1 2\n", "\x01\n", "", "; only comment\n" };
        int all = 1;
        for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
            if (os_eosvm_assemble(bad[i], strlen(bad[i]), code, sizeof code, &code_len, &line) == 0) all = 0;
        CHECK(all, "G17-ASM-REJECTS-MALFORMED-SOURCE");
        os_eosvm_assemble("halt\nbogus\n", 11, code, sizeof code, &code_len, &line);
        CHECK(line == 2, "G17-ASM-REPORTS-ERROR-LINE");
        CHECK(os_eosvm_assemble("Push 5\nPRINT\nHALT", 17, code, sizeof code, &code_len, &line) == 0 && code_len == 7, "G17-ASM-CASE-INSENSITIVE-AND-NO-TRAILING-NEWLINE");
        CHECK(os_eosvm_assemble("push 1\nhalt\n", 12, code, 3, &code_len, &line) == OS_EVM_ERR_SIZE, "G17-ASM-OUTPUT-CAPACITY-ENFORCED");
    }
    {
        char src[OS_EOSVM_CODE_MAX * 8];
        src[0] = 0;
        for (int i = 0; i < 400; i++) strcat(src, "push 1\n");
        CHECK(os_eosvm_assemble(src, strlen(src), code, sizeof code, &code_len, &line) == OS_EVM_ERR_SIZE, "G17-ASM-PROGRAM-SIZE-LIMIT");
    }
    {
        size_t n;
        char *fib = slurp("sdk/examples/fib.eos", &n);
        CHECK(fib && os_eosvm_assemble(fib, n, code, sizeof code, &code_len, &line) == 0 && os_eosvm_run(code, code_len, OS_EOSVM_FUEL_DEFAULT, &r) == 0 &&
              strcmp(r.output, "0\n1\n1\n2\n3\n5\n8\n13\n21\n34\n") == 0, "G17-SDK-FIB-EXAMPLE-EXECUTES");
        free(fib);
        char *hello = slurp("sdk/examples/hello.eos", &n);
        CHECK(hello && os_eosvm_assemble(hello, n, code, sizeof code, &code_len, &line) == 0 && os_eosvm_run(code, code_len, OS_EOSVM_FUEL_DEFAULT, &r) == 0 &&
              strcmp(r.output, "Hello\n5050\n") == 0, "G17-SDK-HELLO-EXAMPLE-EXECUTES");
        free(hello);
    }

    /* ---- Runtime interface table ---- */
    {
        const uint8_t halt[] = { OS_EVM_HALT };
        CHECK(os_runtime_count() == 4 && os_runtime_find("eosvm") && os_runtime_find("eosvm")->guest_executable, "G17-RUNTIME-TABLE-VERSIONED");
        CHECK(os_runtime_execute("eosvm", halt, 1, 10, &r) == OS_RT_OK, "G17-RUNTIME-EOSVM-EXECUTES");
        CHECK(os_runtime_find("python") && os_runtime_find("python")->state == OS_RT_PENDING &&
              os_runtime_execute("python", halt, 1, 10, &r) == OS_RT_ERR_UNAVAILABLE, "G17-RUNTIME-PYTHON-PENDING-INERT");
        CHECK(os_runtime_execute("native-app", halt, 1, 10, &r) == OS_RT_ERR_NOT_EXECUTABLE &&
              os_runtime_execute("c-sdk", halt, 1, 10, &r) == OS_RT_ERR_NOT_EXECUTABLE &&
              os_runtime_execute("ruby", halt, 1, 10, &r) == OS_RT_ERR_UNKNOWN, "G17-RUNTIME-NON-GUEST-AND-UNKNOWN-REFUSED");
    }

    /* ---- Editor ---- */
    {
        os_editor ed;
        size_t l, c;
        os_editor_init(&ed);
        CHECK(os_editor_insert(&ed, "push 1\nhalt\n", 12) == 0 && ed.length == 12 && os_editor_line_count(&ed) == 3, "G17-EDITOR-INSERT-AND-LINES");
        os_editor_position(&ed, &l, &c);
        CHECK(l == 3 && c == 1, "G17-EDITOR-POSITION");
        CHECK(os_editor_move_line(&ed, -1) == 0 && os_editor_end(&ed) == 0 && ed.cursor == 11 && os_editor_home(&ed) == 0 && ed.cursor == 7, "G17-EDITOR-LINE-NAVIGATION");
        CHECK(os_editor_move_line(&ed, 5) != 0 && os_editor_move(&ed, -100) != 0 && os_editor_move(&ed, 100) != 0, "G17-EDITOR-NAVIGATION-BOUNDS");
        CHECK(os_editor_insert(&ed, "\x01", 1) == OS_ED_ERR_CHARACTER && os_editor_insert(&ed, "ab\0c", 4) == OS_ED_ERR_CHARACTER && ed.length == 12, "G17-EDITOR-CONTROL-BYTES-REJECTED-ATOMICALLY");
        os_editor_init(&ed);
        os_editor_insert(&ed, "abc", 3);
        CHECK(os_editor_backspace(&ed) == 0 && strcmp(ed.text, "ab") == 0 && os_editor_undo(&ed) == 0 && strcmp(ed.text, "abc") == 0, "G17-EDITOR-UNDO-DELETE");
        while (os_editor_undo(&ed) == 0) {}
        CHECK(ed.length == 0 && ed.cursor == 0 && os_editor_undo(&ed) == OS_ED_ERR_EMPTY, "G17-EDITOR-UNDO-ALL-AND-EMPTY");
        os_editor_insert(&ed, "hello world", 11);
        os_editor_move(&ed, -11);
        CHECK(os_editor_find(&ed, "world") == 0 && ed.cursor == 6 && os_editor_find(&ed, "zzz") == OS_ED_ERR_NOT_FOUND && os_editor_find(&ed, "") != 0, "G17-EDITOR-FIND");
        CHECK(os_editor_delete(&ed) == 0 && strcmp(ed.text, "hello orld") == 0 && os_editor_move(&ed, 4) == 0 && os_editor_delete(&ed) == OS_ED_ERR_BOUNDS, "G17-EDITOR-DELETE-AND-BOUNDS");
        os_editor_init(&ed);
        {
            char blob[OS_EDITOR_CAPACITY];
            memset(blob, 'x', sizeof blob);
            CHECK(os_editor_insert(&ed, blob, OS_EDITOR_CAPACITY) == OS_ED_ERR_FULL && ed.length == 0 &&
                  os_editor_insert(&ed, blob, OS_EDITOR_CAPACITY - 1u) == 0 && os_editor_insert(&ed, "y", 1) == OS_ED_ERR_FULL, "G17-EDITOR-CAPACITY-ENFORCED");
        }
        os_editor_init(&ed);
        for (int i = 0; i < 300; i++) os_editor_insert(&ed, "z", 1);
        CHECK(ed.undo_count == OS_EDITOR_UNDO_MAX && ed.length == 300, "G17-EDITOR-UNDO-JOURNAL-BOUNDED");
    }
    {
        os_storage_fs fs;
        os_editor ed, back;
        uint8_t code2[64];
        size_t n2;
        unsigned ln;
        fresh_fs(&fs);
        os_editor_init(&ed);
        os_editor_insert(&ed, "push 9\nprint\nhalt\n", 17);
        CHECK(os_editor_save(&ed, &fs, "/src/prog.eos") == 0 && !ed.dirty, "G17-EDITOR-SAVE");
        os_editor_init(&back);
        CHECK(os_editor_load(&back, &fs, "/src/prog.eos") == 0 && back.length == 17 && memcmp(back.text, ed.text, 17) == 0, "G17-EDITOR-LOAD-ROUNDTRIP");
        CHECK(os_editor_load(&back, &fs, "/src/missing.eos") == OS_ED_ERR_STORAGE, "G17-EDITOR-LOAD-MISSING-FAILS");
        CHECK(os_eosvm_assemble(back.text, back.length, code2, sizeof code2, &n2, &ln) == 0 && os_eosvm_run(code2, n2, 100, &r) == 0 && strcmp(r.output, "9\n") == 0,
              "G17-EDITOR-EDIT-ASSEMBLE-RUN-WORKFLOW");
        os_storage_write(&fs, "/src/binary", "ab\001cd", 5);
        os_editor_init(&back);
        os_editor_insert(&back, "keep", 4);
        CHECK(os_editor_load(&back, &fs, "/src/binary") == OS_ED_ERR_CHARACTER && strcmp(back.text, "keep") == 0, "G17-EDITOR-LOAD-REJECTS-BINARY-AND-KEEPS-BUFFER");
        fail_writes_after = 0;
        os_editor_insert(&ed, "x", 1);
        CHECK(os_editor_save(&ed, &fs, "/src/prog.eos") == OS_ED_ERR_STORAGE && ed.dirty, "G17-EDITOR-SAVE-FAILURE-KEEPS-DIRTY");
        fail_writes_after = -1;
    }

    /* ---- Package container ---- */
    {
        static uint8_t pkg[OS_PKG_MAX_BYTES], mut[OS_PKG_MAX_BYTES];
        const uint8_t prog[] = { OS_EVM_PUSH, 7, 0, 0, 0, OS_EVM_PRINT, OS_EVM_HALT };
        os_pkg_view v;
        size_t n = make_pkg(pkg, sizeof pkg, "hello", 1, "any", OS_PKG_KIND_EOSVM, "", prog, sizeof prog, 0, NULL);
        CHECK(n > 0 && os_pkg_inspect(pkg, n, &v) == 0 && v.signer == 0 && v.manifest.version == 1 && v.payload_len == 7 &&
              os_pkg_verify_signature(&v, KEY1, 16) == OS_PKG_ERR_SIGNATURE, "G17-PKG-UNSIGNED-BUILD-INSPECTS-BUT-NEVER-VERIFIES");
        CHECK(os_pkg_sign(pkg, n, 1, KEY1, 16) == 0 && os_pkg_inspect(pkg, n, &v) == 0 && v.signer == 1 &&
              os_pkg_verify_signature(&v, KEY1, 16) == 0 && os_pkg_verify_signature(&v, KEY2, 16) != 0, "G17-PKG-SIGN-VERIFY-AND-WRONG-KEY");
        CHECK(os_pkg_sign(pkg, n, 1, KEY1, 16) != 0, "G17-PKG-RESIGN-REFUSED");
        int rejected = 1;
        for (size_t i = 0; i < n; i++) {
            memcpy(mut, pkg, n);
            mut[i] ^= 0x01;
            os_pkg_view mv;
            int ir = os_pkg_inspect(mut, n, &mv);
            if (ir == 0 && os_pkg_verify_signature(&mv, KEY1, 16) == 0) rejected = 0;
        }
        CHECK(rejected, "G17-PKG-ANY-SINGLE-BIT-FLIP-REJECTED");
        CHECK(os_pkg_inspect(pkg, n - 1, &v) != 0 && os_pkg_inspect(pkg, 0, &v) != 0 && os_pkg_inspect(NULL, n, &v) != 0, "G17-PKG-TRUNCATED-REJECTED");
        memcpy(mut, pkg, n); mut[n] = 0;
        CHECK(os_pkg_inspect(mut, n + 1, &v) != 0, "G17-PKG-TRAILING-BYTES-REJECTED");
        {
            os_pkg_manifest m;
            uint8_t out[OS_PKG_MAX_BYTES];
            size_t on;
            memset(&m, 0, sizeof m);
            strcpy(m.id, "Bad ID"); strcpy(m.arch, "any"); m.version = 1; m.kind = OS_PKG_KIND_EOSVM; m.min_abi = 1;
            int a = os_pkg_build(&m, prog, sizeof prog, out, sizeof out, &on);
            strcpy(m.id, "ok"); strcpy(m.arch, "mips");
            int b = os_pkg_build(&m, prog, sizeof prog, out, sizeof out, &on);
            strcpy(m.arch, "any"); m.version = 0;
            int c = os_pkg_build(&m, prog, sizeof prog, out, sizeof out, &on);
            m.version = 1;
            int d = os_pkg_build(&m, prog, 0, out, sizeof out, &on);
            int e = os_pkg_build(&m, prog, sizeof prog, out, 20, &on);
            CHECK(a && b && c && d && e, "G17-PKG-BUILD-REJECTS-BAD-MANIFEST-AND-SIZES");
        }
        {
            /* A correctly digested but malformed manifest must still be refused: forge by rebuilding the container. */
            const char *bad_manifests[] = {
                "{\"id\":\"x\",\"version\":1,\"arch\":\"any\",\"kind\":\"eosvm\",\"dependency\":\"\",\"min_abi\":1,\"size_kb\":1}",
                "{\"id\":\"x\",\"id\":\"y\",\"version\":1,\"arch\":\"any\",\"kind\":\"eosvm\",\"dependency\":\"\",\"min_abi\":1,\"size_kb\":1,\"payload_sha256\":\"00\"}",
                "{\"id\":\"x\",\"version\":01,\"arch\":\"any\",\"kind\":\"eosvm\",\"dependency\":\"\",\"min_abi\":1,\"size_kb\":1,\"payload_sha256\":\"00\"}",
                "{\"id\":\"x\",\"version\":1,\"arch\":\"any\",\"kind\":\"eosvm\",\"dependency\":\"\",\"min_abi\":1,\"size_kb\":1,\"payload_sha256\":\"00\",\"extra\":1}",
                "not json",
            };
            int all = 1;
            for (size_t i = 0; i < sizeof bad_manifests / sizeof bad_manifests[0]; i++) {
                static uint8_t forged[OS_PKG_MAX_BYTES];
                size_t ml = strlen(bad_manifests[i]), total = 16 + ml + sizeof prog + 64;
                uint8_t dg[32];
                memset(forged, 0, sizeof forged);
                memcpy(forged, "EPK1", 4); forged[4] = 1; forged[6] = (uint8_t)ml; forged[8] = (uint8_t)sizeof prog;
                memcpy(forged + 16, bad_manifests[i], ml);
                memcpy(forged + 16 + ml, prog, sizeof prog);
                os_sha256_hash(forged, total - 64, dg);
                memcpy(forged + total - 64, dg, 32);
                if (os_pkg_inspect(forged, total, &v) == 0) all = 0;
            }
            CHECK(all, "G17-PKG-FORGED-MANIFESTS-REJECTED-DESPITE-VALID-DIGEST");
        }
        {
            uint8_t evil[] = { OS_EVM_JMP, 77, 0, OS_EVM_HALT };
            size_t en = make_pkg(mut, sizeof mut, "evil", 1, "any", OS_PKG_KIND_EOSVM, "", evil, sizeof evil, 0, KEY1);
            CHECK(en > 0 && os_pkg_inspect(mut, en, &v) == OS_PKG_ERR_FORMAT, "G17-PKG-INVALID-BYTECODE-PAYLOAD-REJECTED");
        }
        {
            uint8_t app[16] = { 0x45, 0x58, 0x50, 0x41, 1, 0, 1, 0, 2, 0, 0, 0 };
            size_t an = make_pkg(mut, sizeof mut, "nat", 1, "aarch64", OS_PKG_KIND_APP_IMAGE, "", app, sizeof app, 0, KEY1);
            size_t bn = make_pkg(pkg, sizeof pkg, "nat", 1, "x86_64", OS_PKG_KIND_APP_IMAGE, "", app, sizeof app, 0, KEY1);
            CHECK(an > 0 && os_pkg_inspect(mut, an, &v) == 0 && bn > 0 && os_pkg_inspect(pkg, bn, &v) == OS_PKG_ERR_COMPAT,
                  "G17-PKG-APP-IMAGE-ARCH-INSPECTED-AGAINST-MANIFEST");
        }
    }

    /* ---- Package manager ---- */
    {
        static uint8_t p1[OS_PKG_MAX_BYTES], p2[OS_PKG_MAX_BYTES], p3[OS_PKG_MAX_BYTES], dep[OS_PKG_MAX_BYTES];
        const uint8_t a1[] = { OS_EVM_PUSH, 1, 0, 0, 0, OS_EVM_PRINT, OS_EVM_HALT };
        const uint8_t a2[] = { OS_EVM_PUSH, 2, 0, 0, 0, OS_EVM_PRINT, OS_EVM_HALT };
        const uint8_t a3[] = { OS_EVM_PUSH, 3, 0, 0, 0, OS_EVM_PRINT, OS_EVM_HALT };
        os_storage_fs fs;
        os_pkg_manager mgr;
        os_pkg_view v;
        char path[64];
        size_t n1, n2, n3, nd;
        fresh_fs(&fs);
        n1 = make_pkg(p1, sizeof p1, "app", 1, "any", OS_PKG_KIND_EOSVM, "", a1, sizeof a1, 1, KEY1);
        n2 = make_pkg(p2, sizeof p2, "app", 2, "any", OS_PKG_KIND_EOSVM, "", a2, sizeof a2, 1, KEY1);
        n3 = make_pkg(p3, sizeof p3, "app", 3, "any", OS_PKG_KIND_EOSVM, "", a3, sizeof a3, 2, KEY2);
        nd = make_pkg(dep, sizeof dep, "needs", 1, "any", OS_PKG_KIND_EOSVM, "base", a1, sizeof a1, 1, KEY1);
        CHECK(os_pkg_manager_init(&mgr, &fs, "aarch64", 1) == 0 && os_pkg_manager_init(&mgr, &fs, "riscv", 1) != 0 && os_pkg_manager_init(&mgr, &fs, "aarch64", 1) == 0, "G17-MGR-INIT-VALIDATES-ARCH");
        CHECK(os_pkg_import(&mgr, p1, n1) == OS_PKG_ERR_SIGNATURE, "G17-MGR-IMPORT-NEEDS-PROVISIONED-KEY");
        CHECK(os_pkg_provision_key(&mgr, 1, KEY1) == 0 && os_pkg_provision_key(&mgr, 1, KEY2) != 0 && os_pkg_provision_key(&mgr, 3, KEY2) != 0, "G17-MGR-KEY-PROVISION-ONCE-AND-BOUNDS");
        CHECK(os_pkg_import(&mgr, p1, n1) == 0 && os_pkg_import(&mgr, p1, n1) == 0, "G17-MGR-IMPORT-IDEMPOTENT");
        os_pkg_inspect(p1, n1, &v);
        CHECK(os_pkg_install(&mgr, "app", 1, 10) == OS_PKG_ERR_APPROVAL, "G17-MGR-INSTALL-REQUIRES-RECIPIENT-APPROVAL");
        {
            uint8_t wrong[32];
            memcpy(wrong, v.digest, 32); wrong[5] ^= 1;
            CHECK(os_pkg_approve(&mgr, "app", 1, wrong, 100, 10) == OS_PKG_ERR_APPROVAL && os_pkg_approve(&mgr, "app", 1, v.digest, 10, 10) == OS_PKG_ERR_EXPIRED &&
                  os_pkg_approve(&mgr, "ghost", 1, v.digest, 100, 10) == OS_PKG_ERR_NOT_FOUND, "G17-MGR-APPROVAL-BOUND-TO-DIGEST-AND-EXPIRY");
        }
        CHECK(os_pkg_approve(&mgr, "app", 1, v.digest, 100, 10) == 0 && os_pkg_install(&mgr, "app", 1, 100) == OS_PKG_ERR_EXPIRED, "G17-MGR-EXPIRED-APPROVAL-REFUSED");
        CHECK(os_pkg_approve(&mgr, "app", 1, v.digest, 1000, 10) == 0 && os_pkg_install(&mgr, "app", 1, 20) == 0 && os_pkg_find_installed(&mgr, "app") >= 0, "G17-MGR-APPROVED-INSTALL");
        CHECK(os_pkg_run(&mgr, "app", 1000, &r) == 0 && strcmp(r.output, "1\n") == 0, "G17-MGR-INSTALLED-PROGRAM-EXECUTES");
        CHECK(os_pkg_install(&mgr, "app", 1, 20) == OS_PKG_ERR_APPROVAL, "G17-MGR-APPROVAL-IS-SINGLE-USE");
        CHECK(os_pkg_import(&mgr, p2, n2) == 0, "G17-MGR-STAGE-UPGRADE");
        os_pkg_inspect(p2, n2, &v);
        os_pkg_approve(&mgr, "app", 2, v.digest, 1000, 10);
        CHECK(os_pkg_install(&mgr, "app", 2, 20) == 0 && mgr.installed[os_pkg_find_installed(&mgr, "app")].previous == 1 &&
              os_pkg_run(&mgr, "app", 1000, &r) == 0 && strcmp(r.output, "2\n") == 0, "G17-MGR-UPGRADE-TRACKS-PREVIOUS-VERSION");
        os_pkg_inspect(p1, n1, &v);
        os_pkg_approve(&mgr, "app", 1, v.digest, 1000, 10);
        CHECK(os_pkg_install(&mgr, "app", 1, 20) == OS_PKG_ERR_VERSION, "G17-MGR-DOWNGRADE-REFUSED-WITHOUT-ROLLBACK-POLICY");
        os_pkg_set_rollback_policy(&mgr, 1);
        CHECK(os_pkg_install(&mgr, "app", 1, 20) == 0 && os_pkg_run(&mgr, "app", 1000, &r) == 0 && strcmp(r.output, "1\n") == 0, "G17-MGR-ROLLBACK-WITH-POLICY-AND-APPROVAL");
        os_pkg_set_rollback_policy(&mgr, 0);
        CHECK(os_pkg_import(&mgr, p3, n3) == OS_PKG_ERR_SIGNATURE, "G17-MGR-UNPROVISIONED-SIGNER-SLOT-REFUSED");
        os_pkg_provision_key(&mgr, 2, KEY2);
        CHECK(os_pkg_import(&mgr, p3, n3) == 0, "G17-MGR-SECOND-SIGNER-ACCEPTED");
        CHECK(os_pkg_import(&mgr, dep, nd) == 0, "G17-MGR-STAGE-DEPENDENT");
        os_pkg_inspect(dep, nd, &v);
        os_pkg_approve(&mgr, "needs", 1, v.digest, 1000, 10);
        CHECK(os_pkg_install(&mgr, "needs", 1, 20) == OS_PKG_ERR_DEPENDENCY, "G17-MGR-MISSING-DEPENDENCY-REFUSED");
        CHECK(os_pkg_export(&mgr, "app", 2) == 0 && os_storage_exists(&fs, "/out/app-2", NULL) && os_pkg_export(&mgr, "app", 9) == OS_PKG_ERR_NOT_FOUND, "G17-MGR-OFFLINE-EXPORT");
        /* Offline media round trip: read the exported file back and import it into a second node. */
        {
            static uint8_t media[OS_PKG_MAX_BYTES];
            usize sz = 0, got = 0;
            os_storage_fs fs2;
            os_pkg_manager node2;
            os_storage_exists(&fs, "/out/app-2", &sz);
            os_storage_read(&fs, "/out/app-2", 0, media, sz, &got);
            memcpy(disk[400], media, 1);
            fresh_fs(&fs2);
            os_pkg_manager_init(&node2, &fs2, "aarch64", 1);
            os_pkg_provision_key(&node2, 1, KEY1);
            CHECK(got == sz && os_pkg_import(&node2, media, sz) == 0 && os_storage_exists(&fs2, "/pkg/app-2", NULL), "G17-MGR-OFFLINE-IMPORT-ON-SECOND-NODE");
            fresh_fs(&fs);
        }
        (void)path;
    }
    {
        /* Compatibility, state persistence, integrity of stored payload, and failure safety. */
        static uint8_t px[OS_PKG_MAX_BYTES], pa[OS_PKG_MAX_BYTES], pn[OS_PKG_MAX_BYTES], pu[OS_PKG_MAX_BYTES];
        const uint8_t prog[] = { OS_EVM_PUSH, 5, 0, 0, 0, OS_EVM_PRINT, OS_EVM_HALT };
        const uint8_t app[12] = { 0x45, 0x58, 0x50, 0x41, 1, 0, 1, 0, 1, 0, 0, 0 };
        os_storage_fs fs;
        os_pkg_manager mgr, again;
        os_pkg_view v;
        size_t nx, na;
        fresh_fs(&fs);
        nx = make_pkg(px, sizeof px, "x86only", 1, "x86_64", OS_PKG_KIND_APP_IMAGE, "", app, sizeof app, 1, KEY1);
        na = make_pkg(pa, sizeof pa, "arm", 1, "aarch64", OS_PKG_KIND_EOSVM, "", prog, sizeof prog, 1, KEY1);
        os_pkg_manager_init(&mgr, &fs, "aarch64", 1);
        os_pkg_provision_key(&mgr, 1, KEY1);
        CHECK(nx > 0 && os_pkg_import(&mgr, px, nx) == OS_PKG_ERR_COMPAT, "G17-MGR-WRONG-ARCHITECTURE-REFUSED-ON-AARCH64");
        {
            os_pkg_manifest m;
            size_t n;
            memset(&m, 0, sizeof m);
            strcpy(m.id, "future"); strcpy(m.arch, "any"); m.version = 1; m.kind = OS_PKG_KIND_EOSVM; m.min_abi = 9;
            os_pkg_build(&m, prog, sizeof prog, pn, sizeof pn, &n);
            os_pkg_sign(pn, n, 1, KEY1, 16);
            CHECK(os_pkg_import(&mgr, pn, n) == OS_PKG_ERR_COMPAT, "G17-MGR-ABI-NEWER-THAN-NODE-REFUSED");
        }
        os_pkg_manager_init(&again, &fs, "x86_64", 1);
        os_pkg_provision_key(&again, 1, KEY1);
        CHECK(os_pkg_import(&again, pa, na) == OS_PKG_ERR_COMPAT && os_pkg_import(&again, px, nx) == 0, "G17-MGR-ARCH-COMPATIBILITY-PER-NODE");
        os_pkg_manager_init(&mgr, &fs, "aarch64", 1);
        os_pkg_provision_key(&mgr, 1, KEY1);
        os_pkg_import(&mgr, pa, na);
        os_pkg_inspect(pa, na, &v);
        os_pkg_approve(&mgr, "arm", 1, v.digest, 1000, 10);
        fail_writes_after = 0;
        CHECK(os_pkg_install(&mgr, "arm", 1, 20) == OS_PKG_ERR_STORAGE && os_pkg_find_installed(&mgr, "arm") < 0, "G17-MGR-INSTALL-FAILURE-LEAVES-NO-PARTIAL-RECORD");
        fail_writes_after = -1;
        CHECK(os_pkg_install(&mgr, "arm", 1, 20) == 0, "G17-MGR-INSTALL-RETRY-AFTER-FAILURE");
        os_pkg_manager_init(&again, &fs, "aarch64", 1);
        CHECK(os_pkg_load_state(&again) == 0 && os_pkg_find_installed(&again, "arm") >= 0 && again.installed[os_pkg_find_installed(&again, "arm")].version == 1, "G17-MGR-INSTALLED-VERSIONS-PERSIST");
        {
            uint8_t zeros[OS_STORAGE_BLOCK_SIZE];
            usize sz = 0;
            os_storage_exists(&fs, "/pkg/state", &sz);
            CHECK(sz > 0, "G17-MGR-STATE-FILE-EXISTS");
            (void)zeros;
        }
        /* Corrupt the stored state: loading must fail closed. */
        {
            uint8_t st[2048];
            usize sz = 0, got = 0;
            os_storage_exists(&fs, "/pkg/state", &sz);
            os_storage_read(&fs, "/pkg/state", 0, st, sz, &got);
            st[10] ^= 0x40;
            os_storage_write(&fs, "/pkg/state", st, sz);
            os_pkg_manager_init(&again, &fs, "aarch64", 1);
            CHECK(os_pkg_load_state(&again) == OS_PKG_ERR_INTEGRITY && os_pkg_find_installed(&again, "arm") < 0, "G17-MGR-CORRUPT-STATE-FAILS-CLOSED");
        }
        /* Tamper with the installed payload on disk: running must refuse. */
        {
            uint8_t evilp[] = { OS_EVM_PUSH, 66, 0, 0, 0, OS_EVM_PRINT, OS_EVM_HALT };
            os_storage_write(&fs, "/app/arm-1", evilp, sizeof evilp);
            CHECK(os_pkg_run(&mgr, "arm", 1000, &r) == OS_PKG_ERR_INTEGRITY, "G17-MGR-TAMPERED-INSTALLED-PAYLOAD-NOT-EXECUTED");
        }
        CHECK(os_pkg_run(&mgr, "nothing", 10, &r) == OS_PKG_ERR_NOT_FOUND, "G17-MGR-RUN-UNKNOWN-PACKAGE");
        /* Staged package tampered in storage must fail approval. */
        {
            size_t nu = make_pkg(pu, sizeof pu, "tmp", 1, "any", OS_PKG_KIND_EOSVM, "", prog, sizeof prog, 1, KEY1);
            os_pkg_inspect(pu, nu, &v);
            os_pkg_import(&mgr, pu, nu);
            pu[nu - 100] ^= 1;
            os_storage_write(&fs, "/pkg/tmp-1", pu, nu);
            CHECK(os_pkg_approve(&mgr, "tmp", 1, v.digest, 1000, 10) != 0, "G17-MGR-TAMPERED-STAGED-PACKAGE-REFUSED");
            pu[nu - 100] ^= 1;
        }
        {
            size_t nu = make_pkg(pu, sizeof pu, "tmp", 1, "any", OS_PKG_KIND_EOSVM, "", prog, sizeof prog, 1, KEY1);
            os_pkg_inspect(pu, nu, &v);
            os_storage_write(&fs, "/pkg/tmp-1", pu, nu);
            memcpy(pn, pu, nu);
            pn[20] ^= 0; /* identical */
            CHECK(os_pkg_import(&mgr, pu, nu) == 0, "G17-MGR-REIMPORT-IDENTICAL-OK");
            {
                const uint8_t other[] = { OS_EVM_PUSH, 8, 0, 0, 0, OS_EVM_PRINT, OS_EVM_HALT };
                size_t no = make_pkg(pn, sizeof pn, "tmp", 1, "any", OS_PKG_KIND_EOSVM, "", other, sizeof other, 1, KEY1);
                CHECK(os_pkg_import(&mgr, pn, no) == OS_PKG_ERR_VERSION, "G17-MGR-SAME-VERSION-DIFFERENT-CONTENT-REFUSED");
            }
        }
    }

    {
        os_sha256 h;
        (void)h;
    }
    printf(failures ? "G17 DEV RESULT: FAIL (%d)\n" : "G17 DEV RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
