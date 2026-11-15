/* Host tests for the production expected.txt parser in gdatproto/src. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gp_manifest.h"
#include "rp_io.h"
#include "rp_mem.h"

static const char *g_manifest_text;
static size_t g_alloc_calls;
static size_t g_live_allocs;
static size_t g_fail_from_call;

/* Correction 5: stub controls and records for input-format and path tests. */
static size_t g_manifest_len;        /* bytes to serve; 0 = strlen(g_manifest_text) */
static int g_ux0_missing;            /* serve NOT_FOUND for ux0: paths (fallback test) */
static size_t g_read_calls;
static char g_read_paths[2][96];     /* first two paths passed to rp_read_all */
static size_t g_open_calls;
static char g_open_path[96];         /* last path passed to rp_open */
static int g_rejections, g_acceptances, g_path_checks, g_api_checks;

static const char VALID_MANIFEST[] =
    "GPX 1\n"
    "FILE tiny.gdat 16 00000000 1\n"
    "ENTRY 0 0 8 8 -1 -1 00000000\n"
    "FILE tiny.csh 144 00000000 -1\n"
    "CASE A-1 PATH_CASE path_open strict - ux0:data/file PATH 0 -1 -\n"
    "CASE A-2 OPEN gdat_open strict tiny.gdat - OK 0 1 00000000\n"
    "CASE A-3 ENTRY entry_load lenient tiny.gdat 0 OK 0 -1 00000000\n"
    "CASE A-4 SEQ load_seq strict tiny.gdat - OK 0 1 00000000\n"
    "CASE A-5 SHUF load_shuf lenient tiny.gdat - OK 0 1 00000000\n"
    "CASE A-6 CSH csh_check strict tiny.csh tiny.gdat OK 0 1 00000000\n"
    "CASE A-7 MISSING_PAIR csh_check strict tiny.csh missing.gdat NOT_FOUND 0 -1 -\n"
    "END\n";

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (message)); \
        exit(1); \
    } \
} while (0)

void *rp_malloc(size_t n)
{
    void *p;
    g_alloc_calls++;
    if (n == 0 || (g_fail_from_call && g_alloc_calls >= g_fail_from_call)) return NULL;
    p = malloc(n);
    if (p) g_live_allocs++;
    return p;
}

void rp_free(void *p)
{
    if (p) {
        CHECK(g_live_allocs > 0, "free without a tracked allocation");
        g_live_allocs--;
        free(p);
    }
}

rp_mem_stats rp_mem_get(void)
{
    rp_mem_stats stats = {0, 0, 0, 0, 0};
    return stats;
}

void rp_mem_reset_peak(void) {}
void rp_mem_snapshot_take(rp_mem_snapshot *snapshot) { (void)snapshot; }
size_t rp_mem_probe_max(size_t limit) { return limit; }

rp_err rp_read_all(const char *path, size_t max_bytes, uint8_t **out, size_t *out_len)
{
    size_t len;
    uint8_t *copy;
    if (g_read_calls < 2)
        snprintf(g_read_paths[g_read_calls], sizeof(g_read_paths[0]), "%s", path ? path : "(null)");
    g_read_calls++;
    *out = NULL;
    *out_len = 0;
    if (!g_manifest_text) return RP_ERR_NOT_FOUND;
    if (g_ux0_missing && path && strncmp(path, "ux0:", 4) == 0) return RP_ERR_NOT_FOUND;
    len = g_manifest_len ? g_manifest_len : strlen(g_manifest_text);
    if (len > max_bytes) return RP_ERR_TOO_LARGE;
    if (len == 0) return RP_OK;          /* like the real rp_read_all: empty file, no buffer */
    copy = (uint8_t *)rp_malloc(len);
    if (!copy) return RP_ERR_NOMEM;
    memcpy(copy, g_manifest_text, len);
    *out = copy;
    *out_len = len;
    return RP_OK;
}

rp_err rp_open(const char *path, FILE **out_fp, uint64_t *out_size)
{
    g_open_calls++;
    snprintf(g_open_path, sizeof(g_open_path), "%s", path ? path : "(null)");
    if (out_fp) *out_fp = NULL;
    if (out_size) *out_size = 0;
    return RP_ERR_ARG;
}

const char *rp_err_name(rp_err error)
{
    static const char *const names[] = {
        "OK", "ARG", "NOT_FOUND", "OPEN", "SEEK", "SHORT_READ", "WRITE", "TOO_LARGE",
        "NOMEM", "TRUNCATED", "SIZE_MISMATCH", "BAD_MAGIC", "BAD_VERSION", "BAD_HEADER_CRC",
        "BAD_INDEX", "BAD_INDEX_CRC", "COUNT", "RANGE", "OVERLAP", "LIMIT", "CRC",
        "ZLIB_DATA", "ZLIB"
    };
    if (error < 0 || error >= RP_ERR_COUNT_OF_CODES) return "UNKNOWN";
    return names[error];
}

static void reset_allocations(void)
{
    CHECK(g_live_allocs == 0, "allocation tracker not empty before test");
    g_alloc_calls = 0;
    g_fail_from_call = 0;
    g_manifest_len = 0;
    g_ux0_missing = 0;
    g_read_calls = 0;
    g_open_calls = 0;
}

static char *replace_once(const char *source, const char *old_text, const char *new_text)
{
    const char *found = strstr(source, old_text);
    size_t source_len, old_len, new_len, prefix_len;
    char *result;

    CHECK(found != NULL, "mutation target not found");
    CHECK(strstr(found + 1, old_text) == NULL, "mutation target is not unique");
    source_len = strlen(source);
    old_len = strlen(old_text);
    new_len = strlen(new_text);
    prefix_len = (size_t)(found - source);
    result = (char *)malloc(source_len - old_len + new_len + 1);
    CHECK(result != NULL, "mutation allocation failed");
    memcpy(result, source, prefix_len);
    memcpy(result + prefix_len, new_text, new_len);
    memcpy(result + prefix_len + new_len, found + old_len, source_len - prefix_len - old_len + 1);
    return result;
}

static void expect_rejected(const char *name, const char *text)
{
    gp_manifest manifest;
    char why[128];
    gp_status status;

    reset_allocations();
    g_manifest_text = text;
    status = gp_manifest_load(&manifest, why, sizeof(why));
    CHECK(status != RP_OK, name);
    CHECK(manifest.entries == NULL && manifest.n_files == 0 && manifest.n_cases == 0,
          "failed load did not clear partial manifest");
    CHECK(g_live_allocs == 0, "failed load leaked an allocation");
    g_rejections++;
    printf("reject %-30s (%s)\n", name, why);
}

static void reject_mutation(const char *name, const char *old_text, const char *new_text)
{
    char *mutated = replace_once(VALID_MANIFEST, old_text, new_text);
    expect_rejected(name, mutated);
    free(mutated);
}

/* Rejection with an exact status. len = 0 serves strlen(text) bytes. */
static void expect_status(const char *name, const char *text, size_t len, gp_status expected)
{
    gp_manifest manifest;
    char why[128], msg[160];
    gp_status status;

    reset_allocations();
    g_manifest_text = text;
    g_manifest_len = len;
    status = gp_manifest_load(&manifest, why, sizeof(why));
    snprintf(msg, sizeof(msg), "%s: expected %s, got %s (%s)", name, gp_status_name(expected),
             gp_status_name(status), why);
    CHECK(status == expected, msg);
    CHECK(manifest.entries == NULL && manifest.n_files == 0 && manifest.n_cases == 0,
          "failed load did not clear partial manifest");
    CHECK(g_live_allocs == 0, "failed load leaked an allocation");
    g_rejections++;
    printf("reject %-30s (%s)\n", name, why);
}

/* A variation the parser must accept; the manifest is freed afterwards. */
static void expect_accepted(const char *name, const char *text)
{
    gp_manifest manifest;
    char why[128], msg[160];
    gp_status status;

    reset_allocations();
    g_manifest_text = text;
    status = gp_manifest_load(&manifest, why, sizeof(why));
    snprintf(msg, sizeof(msg), "%s: expected OK, got %s (%s)", name, gp_status_name(status), why);
    CHECK(status == RP_OK, msg);
    CHECK(manifest.n_files == 2 && manifest.n_cases == 7, "accepted variation changed the record counts");
    gp_manifest_free(&manifest);
    CHECK(g_live_allocs == 0, "free after accepted variation leaked an allocation");
    g_acceptances++;
    printf("accept %-30s\n", name);
}

static void accept_mutation(const char *name, const char *old_text, const char *new_text)
{
    char *mutated = replace_once(VALID_MANIFEST, old_text, new_text);
    expect_accepted(name, mutated);
    free(mutated);
}

static void status_mutation(const char *name, const char *old_text, const char *new_text, gp_status expected)
{
    char *mutated = replace_once(VALID_MANIFEST, old_text, new_text);
    expect_status(name, mutated, 0, expected);
    free(mutated);
}

static void test_valid_manifest(void)
{
    gp_manifest manifest;
    char why[128];
    gp_status status;
    const gp_mfile *file;

    reset_allocations();
    g_manifest_text = VALID_MANIFEST;
    status = gp_manifest_load(&manifest, why, sizeof(why));
    CHECK(status == RP_OK, why);
    CHECK(manifest.n_files == 2 && manifest.n_entries == 1 && manifest.n_cases == 7,
          "valid manifest counts differ");
    file = gp_manifest_file(&manifest, "tiny.gdat");
    CHECK(file && file->n_entries == 1 && file->first_entry == 0,
          "valid file ownership metadata differs");
    CHECK(manifest.entries[0].offset == 8 && manifest.entries[0].size == 8,
          "valid entry row differs");
    CHECK(manifest.cases[6].expect == RP_ERR_NOT_FOUND,
          "documented missing CSH pair was not accepted");
    /* gp_manifest_load frees the manifest text buffer before returning; only
     * the entries array (owned by the manifest) is still live. */
    CHECK(g_live_allocs == 1, "valid load allocation count differs");
    gp_manifest_free(&manifest);
    CHECK(g_live_allocs == 0, "free after valid load leaked an allocation");
    puts("pass valid manifest with all six operations and missing CSH pair");
}

static void test_allocator_failures(void)
{
    gp_manifest manifest;
    char why[128];
    gp_status status;

    reset_allocations();
    g_manifest_text = VALID_MANIFEST;
    g_fail_from_call = 1;
    status = gp_manifest_load(&manifest, why, sizeof(why));
    CHECK(status == RP_ERR_NOMEM, "text-buffer allocation failure was not reported");
    CHECK(g_live_allocs == 0, "text-buffer allocation failure leaked memory");

    reset_allocations();
    g_manifest_text = VALID_MANIFEST;
    g_fail_from_call = 2;
    status = gp_manifest_load(&manifest, why, sizeof(why));
    CHECK(status == RP_ERR_NOMEM, "entry-array allocation failure was not reported");
    CHECK(g_live_allocs == 0, "entry-array allocation failure leaked memory");
    puts("pass allocation failure cleanup");
}

/* ------------------------------------------------------------------------
 * Correction 5: manifest structure, input format, fixture paths and API use.
 * Each rejection below also checks the exact status, that the partial
 * manifest is cleared and that nothing stays allocated.
 * ---------------------------------------------------------------------- */
static void test_manifest_structure(void)
{
    static const struct {
        const char *name, *old_text, *new_text;
        gp_status expected;
    } rejects[] = {
        {"wrong format version", "GPX 1\n", "GPX 2\n", RP_ERR_BAD_VERSION},
        {"missing header", "GPX 1\n", "", RP_ERR_BAD_VERSION},
        {"CASE before any FILE", "GPX 1\n", "GPX 1\nCASE A-0 EARLY gdat_open strict tiny.gdat - OK 0 1 00000000\n", RP_ERR_BAD_INDEX},
        {"ENTRY before any FILE", "GPX 1\n", "GPX 1\nENTRY 0 0 8 8 -1 -1 00000000\n", RP_ERR_BAD_INDEX},
        {"FILE after CASE", "END\n", "FILE late.gdat 16 00000000 -1\nEND\n", RP_ERR_BAD_INDEX},
        {"ENTRY after CASE", "END\n", "ENTRY 0 1 8 8 -1 -1 00000000\nEND\n", RP_ERR_BAD_INDEX},
        {"blank line before END", "END\n", "\nEND\n", RP_ERR_BAD_INDEX},
        {"CASE with extra token", "CASE A-4 SEQ load_seq strict tiny.gdat - OK 0 1 00000000", "CASE A-4 SEQ load_seq strict tiny.gdat - OK 0 1 00000000 extra", RP_ERR_BAD_INDEX},
        {"FILE with extra token", "FILE tiny.csh 144 00000000 -1", "FILE tiny.csh 144 00000000 -1 extra", RP_ERR_BAD_INDEX},
        {"FILE with missing token", "FILE tiny.csh 144 00000000 -1", "FILE tiny.csh 144 00000000", RP_ERR_BAD_INDEX},
        {"ENTRY for a file without rows", "FILE tiny.csh 144 00000000 -1", "FILE tiny.csh 144 00000000 -1\nENTRY 1 0 0 144 10 4 00000000", RP_ERR_BAD_INDEX},
        {"wrapper sizes on short entry", "ENTRY 0 0 8 8 -1 -1 00000000", "ENTRY 0 0 8 8 10 4 00000000", RP_ERR_BAD_INDEX},
        {"wrapper sizes missing", "FILE tiny.gdat 16 00000000 1\nENTRY 0 0 8 8 -1 -1 00000000", "FILE tiny.gdat 400 00000000 1\nENTRY 0 0 16 200 -1 -1 00000000", RP_ERR_BAD_INDEX},
        {"path_open with a file", "path_open strict - ux0:data/file", "path_open strict tiny.gdat ux0:data/file", RP_ERR_BAD_INDEX},
        {"case ID with unsafe char", "CASE A-1 PATH_CASE", "CASE A/1 PATH_CASE", RP_ERR_BAD_INDEX},
        {"case ID too long", "CASE A-1 PATH_CASE", "CASE A-1234567890 PATH_CASE", RP_ERR_BAD_INDEX},
        {"case name too long", "CASE A-1 PATH_CASE", "CASE A-1 PATH_CASE_NAME_THAT_IS_FORTY_CHARACTERS_LONG", RP_ERR_BAD_INDEX},
        {"csh pair path traversal", "tiny.csh tiny.gdat OK", "tiny.csh ../tiny.gdat OK", RP_ERR_BAD_INDEX},
        /* fixture names: only plain <name>.gdat / <name>.csh inside the fixture directory */
        {"file name path traversal", "FILE tiny.csh 144 00000000 -1", "FILE ../tiny.csh 144 00000000 -1", RP_ERR_BAD_INDEX},
        {"file name with directory", "FILE tiny.csh 144 00000000 -1", "FILE sub/tiny.csh 144 00000000 -1", RP_ERR_BAD_INDEX},
        {"file name with backslash", "FILE tiny.csh 144 00000000 -1", "FILE sub\\tiny.csh 144 00000000 -1", RP_ERR_BAD_INDEX},
        {"file name with device", "FILE tiny.csh 144 00000000 -1", "FILE ux0:tiny.csh 144 00000000 -1", RP_ERR_BAD_INDEX},
        {"file name wrong extension", "FILE tiny.csh 144 00000000 -1", "FILE tiny.txt 144 00000000 -1", RP_ERR_BAD_INDEX},
        {"file name too long", "FILE tiny.csh 144 00000000 -1", "FILE aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.csh 144 00000000 -1", RP_ERR_BAD_INDEX},
        {"duplicate name ignoring case", "FILE tiny.csh 144 00000000 -1", "FILE Tiny.gdat 144 00000000 -1", RP_ERR_BAD_INDEX},
    };
    static const struct {
        const char *name, *old_text, *new_text;
    } accepts[] = {
        {"uppercase hex CRC", "ENTRY 0 0 8 8 -1 -1 00000000", "ENTRY 0 0 8 8 -1 -1 ABCDEF01"},
        {"all eight observation bits", "CASE A-2 OPEN gdat_open strict tiny.gdat - OK 0 1 00000000", "CASE A-2 OPEN gdat_open strict tiny.gdat - OK ff 1 00000000"},
        {"tab separators", "FILE tiny.csh 144 00000000 -1", "FILE\ttiny.csh\t144\t00000000\t-1"},
        {"leading and trailing spaces", "END\n", "  END  \n"},
        {"blank lines after END", "END\n", "END\n\n\n"},
        {"full entry with wrapper sizes", "FILE tiny.gdat 16 00000000 1\nENTRY 0 0 8 8 -1 -1 00000000", "FILE tiny.gdat 400 00000000 1\nENTRY 0 0 16 200 500 150 00000000"},
        {"entry ending at file end", "ENTRY 0 0 8 8 -1 -1 00000000", "ENTRY 0 0 15 1 -1 -1 00000000"},
    };
    size_t i;

    for (i = 0; i < sizeof(rejects) / sizeof(rejects[0]); i++)
        status_mutation(rejects[i].name, rejects[i].old_text, rejects[i].new_text, rejects[i].expected);
    for (i = 0; i < sizeof(accepts) / sizeof(accepts[0]); i++)
        accept_mutation(accepts[i].name, accepts[i].old_text, accepts[i].new_text);
    puts("pass manifest structure, record order and fixture-name rules");
}

static void test_input_format(void)
{
    static const char NO_CASES[] =
        "GPX 1\n"
        "FILE tiny.gdat 16 00000000 1\n"
        "ENTRY 0 0 8 8 -1 -1 00000000\n"
        "END\n";
    const size_t valid_len = strlen(VALID_MANIFEST);
    const size_t over_limit = 2u * 1024u * 1024u + 1u;   /* GP_MANIFEST_MAX (gp_manifest.c) + 1 */
    char *text;
    size_t i, j;

    /* CRLF line endings are accepted (the parser strips the '\r'). */
    text = (char *)malloc(valid_len * 2 + 1);
    CHECK(text != NULL, "test allocation failed");
    for (i = 0, j = 0; i < valid_len; i++) {
        if (VALID_MANIFEST[i] == '\n') text[j++] = '\r';
        text[j++] = VALID_MANIFEST[i];
    }
    text[j] = '\0';
    expect_accepted("CRLF line endings", text);
    free(text);

    /* Last line without '\n'. */
    text = (char *)malloc(valid_len);
    CHECK(text != NULL, "test allocation failed");
    memcpy(text, VALID_MANIFEST, valid_len - 1);
    text[valid_len - 1] = '\0';
    expect_status("missing final newline", text, 0, RP_ERR_TRUNCATED);
    free(text);

    /* An embedded NUL byte (served with an explicit length). */
    text = (char *)malloc(valid_len + 1);
    CHECK(text != NULL, "test allocation failed");
    memcpy(text, VALID_MANIFEST, valid_len + 1);
    text[20] = '\0';
    expect_status("embedded NUL byte", text, valid_len, RP_ERR_BAD_INDEX);
    free(text);

    status_mutation("non-ASCII byte", "PATH_CASE", "PATH\x80" "CASE", RP_ERR_BAD_INDEX);
    status_mutation("control character", "PATH_CASE", "PATH\x01" "CASE", RP_ERR_BAD_INDEX);
    status_mutation("DEL character", "PATH_CASE", "PATH\x7f" "CASE", RP_ERR_BAD_INDEX);

    expect_status("empty file", "", 0, RP_ERR_TRUNCATED);
    expect_status("no CASE records", NO_CASES, 0, RP_ERR_TRUNCATED);
    expect_status("header and END only", "GPX 1\nEND\n", 0, RP_ERR_TRUNCATED);

    /* Larger than the 2 MiB manifest cap: refused by the read, at both locations. */
    text = (char *)malloc(over_limit + 1);
    CHECK(text != NULL, "test allocation failed");
    memset(text, 'x', over_limit);
    text[over_limit] = '\0';
    expect_status("manifest over 2 MiB", text, 0, RP_ERR_TOO_LARGE);
    CHECK(g_read_calls == 2, "oversized manifest was not refused at both fixture locations");
    free(text);
    puts("pass input format: line endings, bytes, empty and oversized files");
}

static void test_path_allowlist(void)
{
    static const struct {
        const char *path;
        gp_status expected;
    } paths[] = {
        {"ux0:app/VDNG00004/fixtures/v_small.gdat", RP_OK},
        {"ux0:app/VDNG00004/fixtures/expected.txt", RP_OK},
        {"app0:fixtures/v_small.gdat", RP_OK},
        {"", GP_ERR_PATH},
        {"v_small.gdat", GP_ERR_PATH},
        {"ux0:data/GdatProto/run001.csv", GP_ERR_PATH},           /* report directory: write-only */
        {"ux0:app/VDNG00003/fixtures/pc_text.z", GP_ERR_PATH},     /* Phase 8 title */
        {"ux0:app/VDNG00001/eboot.bin", GP_ERR_PATH},              /* the working app */
        {"ux0:app/VDNG00004/eboot.bin", GP_ERR_PATH},              /* own app, outside fixtures */
        {"ux0:app/VDNG00004/fixtures", GP_ERR_PATH},               /* prefix needs the trailing '/' */
        {"ux0:app/VDNG00004/fixturesX/a.gdat", GP_ERR_PATH},
        {"UX0:app/VDNG00004/fixtures/a.gdat", GP_ERR_PATH},        /* prefix is case-sensitive */
        {"app0:Fixtures/a.gdat", GP_ERR_PATH},
        {" app0:fixtures/a.gdat", GP_ERR_PATH},
        {"/app0:fixtures/a.gdat", GP_ERR_PATH},
        {"app0:fixtures/../eboot.bin", GP_ERR_PATH},
        {"ux0:app/VDNG00004/fixtures/../../VDNG00001/eboot.bin", GP_ERR_PATH},
        {"app0:fixtures/sub/../../sce_sys/param.sfo", GP_ERR_PATH},
        {"app0:fixtures/..", GP_ERR_PATH},
        {"app0:fixtures\\..\\eboot.bin", GP_ERR_PATH},
        {"app0:fixtures/a..b.gdat", GP_ERR_PATH},                  /* any "..", even inside a name */
        {"ux0:app/PCSG00000/data.dat", GP_ERR_PATH},               /* other titles' data */
        {"C:/Games/data.dat", GP_ERR_PATH},
    };
    char msg[160];
    size_t i;

    CHECK(gp_path_check(NULL) == RP_ERR_ARG, "NULL path was not rejected as an argument error");
    g_path_checks++;
    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        gp_status st = gp_path_check(paths[i].path);
        snprintf(msg, sizeof(msg), "path check for \"%s\": expected %s, got %s", paths[i].path,
                 gp_status_name(paths[i].expected), gp_status_name(st));
        CHECK(st == paths[i].expected, msg);
        g_path_checks++;
    }
    puts("pass fixture-path allowlist");
}

static void test_open_checked(void)
{
    int sentinel = 0;
    FILE *fp;
    uint64_t size;
    gp_status st;

    /* A refused path never reaches rp_open, and the FILE pointer is cleared. */
    reset_allocations();
    fp = (FILE *)(void *)&sentinel;
    st = gp_open_checked("ux0:data/GdatProto/expected.txt", &fp, &size);
    CHECK(st == GP_ERR_PATH && fp == NULL && g_open_calls == 0, "refused path reached rp_open or kept the FILE pointer");
    g_path_checks++;

    st = gp_open_checked("app0:fixtures/../eboot.bin", NULL, &size);
    CHECK(st == GP_ERR_PATH && g_open_calls == 0, "refused path with NULL FILE pointer reached rp_open");
    g_path_checks++;

    st = gp_open_checked(NULL, &fp, &size);
    CHECK(st == RP_ERR_ARG && fp == NULL && g_open_calls == 0, "NULL path reached rp_open");
    g_path_checks++;

    /* An allowed path is passed to rp_open unchanged, and its status is returned (stub: ARG). */
    st = gp_open_checked("app0:fixtures/v_small.gdat", &fp, &size);
    CHECK(st == RP_ERR_ARG && g_open_calls == 1 && strcmp(g_open_path, "app0:fixtures/v_small.gdat") == 0,
          "allowed path was not passed to rp_open unchanged");
    g_path_checks++;
    puts("pass gp_open_checked: refused paths never reach the file system");
}

static void check_file_paths(const gp_manifest *m)
{
    char path[96], msg[200];   /* room for a full path[] and dir[] in the message */
    int i;
    for (i = 0; i < m->n_files; i++) {
        gp_manifest_path(m, m->files[i].name, path, sizeof(path));
        snprintf(msg, sizeof(msg), "fixture path \"%s\" is not inside %s", path, m->dir);
        CHECK(strncmp(path, m->dir, strlen(m->dir)) == 0 && gp_path_check(path) == RP_OK, msg);
        g_path_checks++;
    }
}

static void test_manifest_location(void)
{
    gp_manifest m;
    char why[128];
    gp_status st;

    /* Normal case: read from the installed ux0 fixture directory, once. */
    reset_allocations();
    g_manifest_text = VALID_MANIFEST;
    st = gp_manifest_load(&m, why, sizeof(why));
    CHECK(st == RP_OK, why);
    CHECK(strcmp(m.dir, "ux0:app/VDNG00004/fixtures/") == 0, "manifest not taken from the ux0 fixture directory");
    CHECK(g_read_calls == 1 && strcmp(g_read_paths[0], "ux0:app/VDNG00004/fixtures/expected.txt") == 0,
          "unexpected manifest read path");
    check_file_paths(&m);
    gp_manifest_free(&m);

    /* ux0 copy unreadable: falls back to app0:, in that order. */
    reset_allocations();
    g_manifest_text = VALID_MANIFEST;
    g_ux0_missing = 1;
    st = gp_manifest_load(&m, why, sizeof(why));
    CHECK(st == RP_OK, why);
    CHECK(strcmp(m.dir, "app0:fixtures/") == 0, "manifest did not fall back to app0:fixtures/");
    CHECK(g_read_calls == 2 && strcmp(g_read_paths[0], "ux0:app/VDNG00004/fixtures/expected.txt") == 0 &&
          strcmp(g_read_paths[1], "app0:fixtures/expected.txt") == 0, "fallback order or paths differ");
    check_file_paths(&m);
    gp_manifest_free(&m);

    /* Neither location readable. */
    reset_allocations();
    g_manifest_text = NULL;
    st = gp_manifest_load(&m, why, sizeof(why));
    CHECK(st == RP_ERR_NOT_FOUND && g_read_calls == 2, "missing manifest not reported after trying both locations");
    CHECK(m.entries == NULL && m.n_files == 0 && m.n_cases == 0 && m.dir[0] == '\0', "missing manifest left state behind");
    CHECK(g_live_allocs == 0, "missing manifest leaked an allocation");
    g_path_checks += 3;
    puts("pass manifest location: ux0 first, app0 fallback, both missing");
}

static void test_api_arguments(void)
{
    gp_manifest m;
    char why[64], buf[16];
    gp_status st;

    reset_allocations();
    g_manifest_text = VALID_MANIFEST;
    CHECK(gp_manifest_load(NULL, why, sizeof(why)) == RP_ERR_ARG, "NULL manifest accepted");
    CHECK(gp_manifest_load(&m, NULL, sizeof(why)) == RP_ERR_ARG, "NULL reason buffer accepted");
    CHECK(gp_manifest_load(&m, why, 0) == RP_ERR_ARG, "zero-length reason buffer accepted");
    CHECK(g_read_calls == 0 && g_alloc_calls == 0, "invalid arguments reached the file system or allocator");
    g_api_checks += 4;

    memset(&m, 0, sizeof(m));
    gp_manifest_free(&m);
    gp_manifest_free(&m);
    gp_manifest_free(NULL);
    CHECK(gp_manifest_file(NULL, "tiny.gdat") == NULL, "lookup in a NULL manifest returned a file");
    g_api_checks += 2;

    st = gp_manifest_load(&m, why, sizeof(why));
    CHECK(st == RP_OK, why);
    CHECK(gp_manifest_file(&m, NULL) == NULL, "lookup of a NULL name returned a file");
    CHECK(gp_manifest_file(&m, "absent.gdat") == NULL, "lookup of an unknown name returned a file");
    CHECK(gp_manifest_file(&m, "TINY.GDAT") == NULL, "file lookup is not exact (case-sensitive)");
    CHECK(gp_manifest_file(&m, "tiny.gdat") != NULL, "lookup of a listed name failed");
    g_api_checks += 4;

    memset(buf, 'Z', sizeof(buf));
    gp_manifest_path(&m, "tiny.gdat", buf, 0);
    CHECK(buf[0] == 'Z', "zero-size path buffer was written");
    gp_manifest_path(NULL, "tiny.gdat", buf, sizeof(buf));
    CHECK(buf[0] == '\0', "path for a NULL manifest is not empty");
    gp_manifest_path(&m, NULL, buf, sizeof(buf));
    CHECK(buf[0] == '\0', "path for a NULL name is not empty");
    gp_manifest_path(&m, "tiny.gdat", buf, sizeof(buf));
    CHECK(strlen(buf) == sizeof(buf) - 1, "short path buffer was not truncated and terminated");
    g_api_checks += 4;

    gp_manifest_free(&m);
    CHECK(g_live_allocs == 0, "API argument tests leaked an allocation");
    puts("pass API argument handling");
}

int main(void)
{
    static const struct {
        const char *name, *old_text, *new_text;
    } mutations[] = {
        {"file size overflow", "FILE tiny.gdat 16 00000000 1", "FILE tiny.gdat 4294967296 00000000 1"},
        {"file size negative", "FILE tiny.gdat 16 00000000 1", "FILE tiny.gdat -2 00000000 1"},
        {"file size invalid chars", "FILE tiny.gdat 16 00000000 1", "FILE tiny.gdat 1x 00000000 1"},
        {"file CRC overflow", "FILE tiny.gdat 16 00000000 1", "FILE tiny.gdat 16 100000000 1"},
        {"file entry count over limit", "FILE tiny.gdat 16 00000000 1", "FILE tiny.gdat 16 00000000 65537"},
        {"file entry count negative", "FILE tiny.gdat 16 00000000 1", "FILE tiny.gdat 16 00000000 -2"},
        {"entry file index overflow", "ENTRY 0 0 8 8 -1 -1 00000000", "ENTRY 4294967296 0 8 8 -1 -1 00000000"},
        {"entry file ownership", "ENTRY 0 0 8 8 -1 -1 00000000", "ENTRY 1 0 8 8 -1 -1 00000000"},
        {"entry index negative", "ENTRY 0 0 8 8 -1 -1 00000000", "ENTRY 0 -1 8 8 -1 -1 00000000"},
        {"entry index not sequential", "ENTRY 0 0 8 8 -1 -1 00000000", "ENTRY 0 1 8 8 -1 -1 00000000"},
        {"entry offset invalid chars", "ENTRY 0 0 8 8 -1 -1 00000000", "ENTRY 0 0 8x 8 -1 -1 00000000"},
        {"entry size overflow", "ENTRY 0 0 8 8 -1 -1 00000000", "ENTRY 0 0 8 4294967296 -1 -1 00000000"},
        {"entry size negative", "ENTRY 0 0 8 8 -1 -1 00000000", "ENTRY 0 0 8 -1 -1 -1 00000000"},
        {"entry usize overflow", "ENTRY 0 0 8 8 -1 -1 00000000", "ENTRY 0 0 8 8 4294967296 -1 00000000"},
        {"entry usize invalid negative", "ENTRY 0 0 8 8 -1 -1 00000000", "ENTRY 0 0 8 8 -2 -1 00000000"},
        {"entry csize overflow", "ENTRY 0 0 8 8 -1 -1 00000000", "ENTRY 0 0 8 8 -1 4294967296 00000000"},
        {"entry raw sentinel mismatch", "ENTRY 0 0 8 8 -1 -1 00000000", "ENTRY 0 0 8 8 -1 0 00000000"},
        {"entry CRC invalid chars", "ENTRY 0 0 8 8 -1 -1 00000000", "ENTRY 0 0 8 8 -1 -1 0000000g"},
        {"entry CRC overflow", "ENTRY 0 0 8 8 -1 -1 00000000", "ENTRY 0 0 8 8 -1 -1 100000000"},
        {"offset plus size outside file", "ENTRY 0 0 8 8 -1 -1 00000000", "ENTRY 0 0 4294967295 8 -1 -1 00000000"},
        {"declared row count mismatch", "FILE tiny.gdat 16 00000000 1", "FILE tiny.gdat 16 00000000 2"},
        {"duplicate filename", "FILE tiny.csh 144 00000000 -1", "FILE tiny.gdat 16 00000000 -1\nFILE tiny.csh 144 00000000 -1"},
        {"duplicate case ID", "CASE A-2 OPEN", "CASE A-1 OPEN"},
        /* Correction 5: these two now change the observation field (token 9), which
         * they are named after; before, they changed the count field. */
        {"case observation overflow", "CASE A-2 OPEN gdat_open strict tiny.gdat - OK 0 1 00000000", "CASE A-2 OPEN gdat_open strict tiny.gdat - OK 100000000 1 00000000"},
        {"unknown observation bit", "CASE A-2 OPEN gdat_open strict tiny.gdat - OK 0 1 00000000", "CASE A-2 OPEN gdat_open strict tiny.gdat - OK 100 1 00000000"},
        /* ...and the count check they used to exercise keeps its own, correctly named test. */
        {"case count differs from file", "CASE A-2 OPEN gdat_open strict tiny.gdat - OK 0 1 00000000", "CASE A-2 OPEN gdat_open strict tiny.gdat - OK 0 2 00000000"},
        {"case count over limit", "CASE A-2 OPEN gdat_open strict tiny.gdat - OK 0 1 00000000", "CASE A-2 OPEN gdat_open strict tiny.gdat - OK 0 65537 00000000"},
        {"case count signed overflow", "CASE A-2 OPEN gdat_open strict tiny.gdat - OK 0 1 00000000", "CASE A-2 OPEN gdat_open strict tiny.gdat - OK 0 2147483648 00000000"},
        {"case count negative", "CASE A-2 OPEN gdat_open strict tiny.gdat - OK 0 1 00000000", "CASE A-2 OPEN gdat_open strict tiny.gdat - OK 0 -2 00000000"},
        {"case check overflow", "CASE A-2 OPEN gdat_open strict tiny.gdat - OK 0 1 00000000", "CASE A-2 OPEN gdat_open strict tiny.gdat - OK 0 1 100000000"},
        {"unknown operation", "gdat_open strict tiny.gdat", "gdat_archive strict tiny.gdat"},
        {"unknown mode", "gdat_open strict tiny.gdat", "gdat_open STRICT tiny.gdat"},
        {"unknown status", "CASE A-2 OPEN gdat_open strict tiny.gdat - OK 0 1 00000000", "CASE A-2 OPEN gdat_open strict tiny.gdat - SUCCESS 0 1 00000000"},
        {"missing primary file", "CASE A-2 OPEN gdat_open strict tiny.gdat - OK 0 1 00000000", "CASE A-2 OPEN gdat_open strict absent.gdat - OK 0 1 00000000"},
        {"entry argument out of range", "CASE A-3 ENTRY entry_load lenient tiny.gdat 0 OK 0 -1 00000000", "CASE A-3 ENTRY entry_load lenient tiny.gdat 1 OK 0 -1 00000000"},
        {"entry argument invalid chars", "CASE A-3 ENTRY entry_load lenient tiny.gdat 0 OK 0 -1 00000000", "CASE A-3 ENTRY entry_load lenient tiny.gdat 0x OK 0 -1 00000000"},
        {"gdat extra argument", "CASE A-2 OPEN gdat_open strict tiny.gdat - OK 0 1 00000000", "CASE A-2 OPEN gdat_open strict tiny.gdat 0 OK 0 1 00000000"},
        {"sequence extra argument", "CASE A-4 SEQ load_seq strict tiny.gdat - OK 0 1 00000000", "CASE A-4 SEQ load_seq strict tiny.gdat 0 OK 0 1 00000000"},
        {"path_open missing argument", "CASE A-1 PATH_CASE path_open strict - ux0:data/file PATH 0 -1 -", "CASE A-1 PATH_CASE path_open strict - - PATH 0 -1 -"},
        {"csh wrong pair extension", "CASE A-6 CSH csh_check strict tiny.csh tiny.gdat OK 0 1 00000000", "CASE A-6 CSH csh_check strict tiny.csh tiny.csh OK 0 1 00000000"},
        {"missing CSH pair expected success", "CASE A-7 MISSING_PAIR csh_check strict tiny.csh missing.gdat NOT_FOUND 0 -1 -", "CASE A-7 MISSING_PAIR csh_check strict tiny.csh missing.gdat OK 0 -1 -"},
        {"duplicate END", "END\n", "END\nEND\n"},
        {"malformed END row", "END\n", "END EXTRA\n"},
        {"record after END", "END\n", "END\nFILE late.gdat 0 0 -1\n"},
        {"missing END", "END\n", ""},
        {"unknown record", "END\n", "BOGUS\nEND\n"}
    };
    size_t i;

    test_valid_manifest();
    for (i = 0; i < sizeof(mutations) / sizeof(mutations[0]); i++)
        reject_mutation(mutations[i].name, mutations[i].old_text, mutations[i].new_text);
    test_manifest_structure();
    test_input_format();
    test_path_allowlist();
    test_open_checked();
    test_manifest_location();
    test_api_arguments();
    test_allocator_failures();
    CHECK(g_live_allocs == 0, "allocations remain after all tests");
    printf("summary: %d rejections (%d from the original table), %d acceptances, %d path checks, %d API checks\n",
           g_rejections, (int)(sizeof(mutations) / sizeof(mutations[0])), g_acceptances, g_path_checks, g_api_checks);
    puts("all manifest parser tests passed");
    return 0;
}
