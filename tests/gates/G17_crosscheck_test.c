#include <stdio.h>
#include <string.h>
#include "devpkg.h"
#include "eosvm.h"

static int failures;
#define CHECK(c, name) do { if (c) printf("PASS %s\n", name); else { printf("FAIL %s\n", name); failures++; } } while (0)

#define BLOCKS 256u
static uint8_t disk[BLOCKS][OS_STORAGE_BLOCK_SIZE];
static int dev_read(void *c, u32 b, u8 d[OS_STORAGE_BLOCK_SIZE]) { (void)c; if (b >= BLOCKS) return 0; memcpy(d, disk[b], OS_STORAGE_BLOCK_SIZE); return 1; }
static int dev_write(void *c, u32 b, const u8 d[OS_STORAGE_BLOCK_SIZE]) { (void)c; if (b >= BLOCKS) return 0; memcpy(disk[b], d, OS_STORAGE_BLOCK_SIZE); return 1; }
static os_storage_device device = { 0, BLOCKS, dev_read, dev_write };

static size_t slurp(const char *dir, const char *name, uint8_t *buf, size_t cap)
{
    char path[1024];
    FILE *f;
    size_t n;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "rb");
    if (!f) return 0;
    n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

int main(int argc, char **argv)
{
    static uint8_t srcbuf[8192], pycode[OS_EOSVM_CODE_MAX], ccode[OS_EOSVM_CODE_MAX];
    static uint8_t pypkg[OS_PKG_MAX_BYTES], cpkg[OS_PKG_MAX_BYTES];
    static os_storage_fs fs;
    static os_pkg_manager mgr;
    const uint8_t key[16] = { 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5, 6 };
    const char *root, *dir;
    char path[1024];
    size_t pycode_len, ccode_len = 0, pypkg_len, cpkg_len = 0, src_len;
    unsigned line;
    os_pkg_manifest m;
    os_eosvm_result r;
    FILE *f;

    if (argc < 3) { printf("usage: crosscheck <root> <pydir>\n"); return 2; }
    root = argv[1]; dir = argv[2];
    snprintf(path, sizeof path, "%s/sdk/examples/fib.eos", root);
    f = fopen(path, "rb");
    if (!f) { printf("FAIL G17-XCHECK-SOURCE\n"); return 1; }
    src_len = fread(srcbuf, 1, sizeof srcbuf - 1, f);
    fclose(f);
    pycode_len = slurp(dir, "fib.bin", pycode, sizeof pycode);
    pypkg_len = slurp(dir, "fib.epk", pypkg, sizeof pypkg);
    CHECK(pycode_len > 0 && pypkg_len > 0, "G17-XCHECK-PYTHON-ARTIFACTS-PRESENT");
    CHECK(os_eosvm_assemble((const char *)srcbuf, src_len, ccode, sizeof ccode, &ccode_len, &line) == 0 &&
          ccode_len == pycode_len && memcmp(ccode, pycode, ccode_len) == 0, "G17-XCHECK-ASSEMBLERS-BYTE-IDENTICAL");
    memset(&m, 0, sizeof m);
    strcpy(m.id, "fib"); m.version = 1; strcpy(m.arch, "aarch64"); m.kind = OS_PKG_KIND_EOSVM; m.min_abi = 1;
    CHECK(os_pkg_build(&m, ccode, ccode_len, cpkg, sizeof cpkg, &cpkg_len) == 0 &&
          os_pkg_sign(cpkg, cpkg_len, 1, key, 16) == 0 &&
          cpkg_len == pypkg_len && memcmp(cpkg, pypkg, cpkg_len) == 0, "G17-XCHECK-PACKAGES-BYTE-IDENTICAL");
    memset(disk, 0, sizeof disk);
    os_storage_format(&device);
    os_storage_mount(&fs, &device);
    os_pkg_manager_init(&mgr, &fs, "aarch64", 1);
    os_pkg_provision_key(&mgr, 1, key);
    CHECK(os_pkg_import(&mgr, pypkg, pypkg_len) == 0, "G17-XCHECK-PYTHON-PACKAGE-IMPORTS");
    {
        uint8_t digest[32];
        os_pkg_view v;
        os_pkg_inspect(pypkg, pypkg_len, &v);
        memcpy(digest, v.digest, 32);
        CHECK(os_pkg_approve(&mgr, "fib", 1, digest, 1000, 10) == 0 && os_pkg_install(&mgr, "fib", 1, 10) == 0, "G17-XCHECK-PYTHON-PACKAGE-INSTALLS-WITH-APPROVAL");
    }
    CHECK(os_pkg_run(&mgr, "fib", OS_EOSVM_FUEL_DEFAULT, &r) == 0 && strcmp(r.output, "0\n1\n1\n2\n3\n5\n8\n13\n21\n34\n") == 0, "G17-XCHECK-PYTHON-PACKAGE-EXECUTES");
    printf(failures ? "G17 XCHECK RESULT: FAIL (%d)\n" : "G17 XCHECK RESULT: PASS\n", failures);
    return failures ? 1 : 0;
}
