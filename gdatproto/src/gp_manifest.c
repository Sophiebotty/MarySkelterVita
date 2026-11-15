/*
 * gp_manifest.c - checked parser and preflight validator for expected.txt.
 */
#include <stdio.h>
#include <string.h>

#include "gp_manifest.h"
#include "rp_io.h"
#include "rp_mem.h"

#define GP_MANIFEST_MAX (2u * 1024u * 1024u)
#define GP_MAX_MANIFEST_ENTRIES (GP_MAX_FILES * GP_MAX_ENTRIES)
#define GP_MAX_TOKENS 11

static int is_space(char c)
{
    return c == ' ' || c == '\t';
}

static int ascii_lower(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (int)(c - 'A' + 'a') : (int)c;
}

static int ascii_equal_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (ascii_lower((unsigned char)*a) != ascii_lower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static int safe_label(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    if (!*p) return 0;
    for (; *p; p++) {
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '-'))
            return 0;
    }
    return 1;
}

static int known_operation(const char *op)
{
    return strcmp(op, "path_open") == 0 || strcmp(op, "gdat_open") == 0 ||
           strcmp(op, "entry_load") == 0 || strcmp(op, "load_seq") == 0 ||
           strcmp(op, "load_shuf") == 0 || strcmp(op, "csh_check") == 0;
}

static int split_tokens(char *line, char **tokens, size_t cap, size_t *count)
{
    char *p = line;
    size_t n = 0;

    while (*p) {
        while (is_space(*p)) p++;
        if (!*p) break;
        if (n == cap) return 0;
        tokens[n++] = p;
        while (*p && !is_space(*p)) p++;
        if (*p) *p++ = '\0';
    }
    *count = n;
    return 1;
}

static int parse_u32(const char *s, unsigned base, uint32_t *out)
{
    uint64_t value = 0;
    size_t i, n;

    if (!s || !*s || (base != 10 && base != 16)) return 0;
    n = strlen(s);
    if (base == 16 && n > 8) return 0;
    for (i = 0; i < n; i++) {
        unsigned digit;
        char c = s[i];
        if (c >= '0' && c <= '9') digit = (unsigned)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f') digit = (unsigned)(c - 'a') + 10u;
        else if (base == 16 && c >= 'A' && c <= 'F') digit = (unsigned)(c - 'A') + 10u;
        else return 0;
        if (digit >= base || value > (UINT32_MAX - digit) / base) return 0;
        value = value * base + digit;
    }
    *out = (uint32_t)value;
    return 1;
}

static int parse_file_entry_count(const char *s, int32_t *out)
{
    uint32_t value;
    if (strcmp(s, "-1") == 0) {
        *out = -1;
        return 1;
    }
    if (!parse_u32(s, 10, &value) || value > GP_MAX_ENTRIES) return 0;
    *out = (int32_t)value;
    return 1;
}

static int parse_case_count(const char *s, int32_t *out)
{
    uint32_t value;
    if (strcmp(s, "-1") == 0) {
        *out = -1;
        return 1;
    }
    if (!parse_u32(s, 10, &value) || value > GP_MAX_ENTRIES) return 0;
    *out = (int32_t)value;
    return 1;
}

static int parse_optional_u32(const char *s, int64_t *out)
{
    uint32_t value;
    if (strcmp(s, "-1") == 0) {
        *out = -1;
        return 1;
    }
    if (!parse_u32(s, 10, &value)) return 0;
    *out = (int64_t)value;
    return 1;
}

static int copy_token(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) return 0;
    memcpy(dst, src, n + 1);
    return 1;
}

static int has_suffix(const char *s, const char *suffix)
{
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && memcmp(s + n - m, suffix, m) == 0;
}

static int safe_fixture_name(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    if (!s[0] || strstr(s, "..") || (!has_suffix(s, ".gdat") && !has_suffix(s, ".csh"))) return 0;
    for (; *p; p++) {
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '.'))
            return 0;
    }
    return 1;
}

static void explain(char *why, size_t why_len, int line_no, gp_status status)
{
    if (why && why_len)
        snprintf(why, why_len, "line %d: %s", line_no, gp_status_name(status));
}

static gp_status fail_line(char *why, size_t why_len, int line_no, gp_status status)
{
    explain(why, why_len, line_no, status);
    return status;
}

static gp_status finish_file(const gp_manifest *m, int active_file, uint32_t entry_fill)
{
    const gp_mfile *mf;
    uint32_t actual;
    if (active_file < 0) return RP_OK;
    if (active_file >= m->n_files) return RP_ERR_BAD_INDEX;
    mf = &m->files[active_file];
    if (entry_fill < mf->first_entry) return RP_ERR_BAD_INDEX;
    actual = entry_fill - mf->first_entry;
    if ((mf->n_entries < 0 && actual != 0) ||
        (mf->n_entries >= 0 && actual != (uint32_t)mf->n_entries))
        return RP_ERR_BAD_INDEX;
    return RP_OK;
}

static gp_status parse_file(gp_manifest *m, char **t, size_t n, uint32_t entry_fill,
                            int *active_file)
{
    gp_mfile *mf;
    uint32_t size, crc;
    int32_t count;
    int i;

    if (n != 5 || m->n_files >= GP_MAX_FILES || !safe_fixture_name(t[1]) ||
        strlen(t[1]) >= sizeof(m->files[0].name) ||
        !parse_u32(t[2], 10, &size) || !parse_u32(t[3], 16, &crc) ||
        !parse_file_entry_count(t[4], &count))
        return RP_ERR_BAD_INDEX;
    for (i = 0; i < m->n_files; i++)
        if (ascii_equal_ci(m->files[i].name, t[1])) return RP_ERR_BAD_INDEX;

    mf = &m->files[m->n_files];
    if (!copy_token(mf->name, sizeof(mf->name), t[1])) return RP_ERR_BAD_INDEX;
    mf->size = size;
    mf->crc = crc;
    mf->n_entries = count;
    mf->first_entry = entry_fill;
    *active_file = m->n_files++;
    return RP_OK;
}

static gp_status parse_entry(gp_manifest *m, char **t, size_t n, uint32_t *entry_fill,
                             uint32_t total, int active_file)
{
    gp_mfile *mf;
    gp_mentry *me;
    uint32_t fi, idx, offset, size, crc;
    int64_t usize, csize;
    uint64_t end;
    uint32_t rows_so_far;

    if (n != 8 || active_file < 0 ||
        !parse_u32(t[1], 10, &fi) || fi >= (uint32_t)m->n_files || fi != (uint32_t)active_file ||
        !parse_u32(t[2], 10, &idx) ||
        !parse_u32(t[3], 10, &offset) || !parse_u32(t[4], 10, &size) ||
        !parse_optional_u32(t[5], &usize) || !parse_optional_u32(t[6], &csize) ||
        !parse_u32(t[7], 16, &crc) || *entry_fill >= total)
        return RP_ERR_BAD_INDEX;

    mf = &m->files[active_file];
    rows_so_far = *entry_fill - mf->first_entry;
    if (mf->n_entries < 0 || rows_so_far >= (uint32_t)mf->n_entries || idx != rows_so_far)
        return RP_ERR_BAD_INDEX;
    end = (uint64_t)offset + (uint64_t)size;
    if (end > (uint64_t)mf->size) return RP_ERR_RANGE;
    if ((usize == -1) != (csize == -1)) return RP_ERR_BAD_INDEX;
    if ((size < GP_WRAP_TOTAL && (usize != -1 || csize != -1)) ||
        (size >= GP_WRAP_TOTAL && (usize < 0 || csize < 0)))
        return RP_ERR_BAD_INDEX;

    me = &m->entries[*entry_fill];
    me->offset = offset;
    me->size = size;
    me->usize = usize;
    me->csize = csize;
    me->crc = crc;
    (*entry_fill)++;
    return RP_OK;
}

static gp_status parse_case(gp_manifest *m, char **t, size_t n)
{
    gp_mcase parsed, *mc;
    uint32_t obs, check;
    int32_t count;
    int i, ok;

    if (n != 11 || m->n_cases >= GP_MAX_CASES ||
        strlen(t[1]) >= sizeof(m->cases[0].id) ||
        strlen(t[2]) >= sizeof(m->cases[0].name) ||
        strlen(t[3]) >= sizeof(m->cases[0].op) ||
        strlen(t[5]) >= sizeof(m->cases[0].file) ||
        strlen(t[6]) >= sizeof(m->cases[0].arg) ||
        !safe_label(t[1]) || !safe_label(t[2]) || !known_operation(t[3]))
        return RP_ERR_BAD_INDEX;
    if (strcmp(t[4], "strict") != 0 && strcmp(t[4], "lenient") != 0)
        return RP_ERR_BAD_INDEX;
    if (!parse_u32(t[8], 16, &obs) || (obs & ~0xFFu) != 0)
        return RP_ERR_BAD_INDEX;
    if (!parse_case_count(t[9], &count))
        return RP_ERR_BAD_INDEX;
    if (strcmp(t[10], "-") != 0 && !parse_u32(t[10], 16, &check))
        return RP_ERR_BAD_INDEX;

    for (i = 0; i < m->n_cases; i++)
        if (ascii_equal_ci(m->cases[i].id, t[1])) return RP_ERR_BAD_INDEX;

    memset(&parsed, 0, sizeof(parsed));
    if (!copy_token(parsed.id, sizeof(parsed.id), t[1]) ||
        !copy_token(parsed.name, sizeof(parsed.name), t[2]) ||
        !copy_token(parsed.op, sizeof(parsed.op), t[3]))
        return RP_ERR_BAD_INDEX;
    parsed.mode = strcmp(t[4], "strict") == 0 ? GP_STRICT : GP_LENIENT;
    if (strcmp(t[5], "-") != 0 && !copy_token(parsed.file, sizeof(parsed.file), t[5]))
        return RP_ERR_BAD_INDEX;
    if (strcmp(t[6], "-") != 0 && !copy_token(parsed.arg, sizeof(parsed.arg), t[6]))
        return RP_ERR_BAD_INDEX;
    parsed.expect = gp_status_from_name(t[7], &ok);
    if (!ok) return RP_ERR_BAD_INDEX;
    parsed.obs = obs;
    parsed.count = count;
    parsed.has_check = strcmp(t[10], "-") != 0;
    parsed.check = parsed.has_check ? check : 0u;
    mc = &m->cases[m->n_cases];
    *mc = parsed;
    m->n_cases++;
    return RP_OK;
}

static gp_status validate_case(const gp_manifest *m, const gp_mcase *mc)
{
    const gp_mfile *mf, *pair;
    uint32_t index;

    if (strcmp(mc->op, "path_open") == 0)
        return mc->file[0] == '\0' && mc->arg[0] != '\0' ? RP_OK : RP_ERR_BAD_INDEX;

    mf = gp_manifest_file(m, mc->file);
    if (!mf) return RP_ERR_BAD_INDEX;

    if (strcmp(mc->op, "gdat_open") == 0) {
        if (!has_suffix(mc->file, ".gdat") || mc->arg[0] != '\0') return RP_ERR_BAD_INDEX;
        if (mc->expect == RP_OK && mf->n_entries >= 0 && mc->count != mf->n_entries)
            return RP_ERR_BAD_INDEX;
        return RP_OK;
    }
    if (strcmp(mc->op, "entry_load") == 0) {
        if (!has_suffix(mc->file, ".gdat") || mf->n_entries < 0 ||
            !parse_u32(mc->arg, 10, &index) || index >= (uint32_t)mf->n_entries || mc->count != -1)
            return RP_ERR_BAD_INDEX;
        return RP_OK;
    }
    if (strcmp(mc->op, "load_seq") == 0 || strcmp(mc->op, "load_shuf") == 0) {
        if (!has_suffix(mc->file, ".gdat") || mf->n_entries < 0 || mc->arg[0] != '\0')
            return RP_ERR_BAD_INDEX;
        if (mc->expect == RP_OK && mc->count != mf->n_entries) return RP_ERR_BAD_INDEX;
        return RP_OK;
    }
    if (strcmp(mc->op, "csh_check") == 0) {
        if (!has_suffix(mc->file, ".csh") || !safe_fixture_name(mc->arg) ||
            !has_suffix(mc->arg, ".gdat"))
            return RP_ERR_BAD_INDEX;
        pair = gp_manifest_file(m, mc->arg);
        if (!pair)
            return mc->expect == RP_ERR_NOT_FOUND ? RP_OK : RP_ERR_BAD_INDEX;
        if (!has_suffix(pair->name, ".gdat")) return RP_ERR_BAD_INDEX;
        if (mc->expect == RP_OK && pair->n_entries >= 0 && mc->count != pair->n_entries)
            return RP_ERR_BAD_INDEX;
        return RP_OK;
    }
    return RP_ERR_BAD_INDEX;
}

static uint32_t count_entry_rows(const char *text, size_t len, int *ok)
{
    const char *p = text, *end = text + len;
    uint32_t count = 0;
    *ok = 1;
    while (p < end) {
        const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
        size_t line_len;
        if (!nl) { *ok = 0; return 0; }
        line_len = (size_t)(nl - p);
        while (line_len && is_space(*p)) {
            p++;
            line_len--;
        }
        if (line_len >= 6 && memcmp(p, "ENTRY", 5) == 0 && is_space(p[5])) {
            if (count >= GP_MAX_MANIFEST_ENTRIES) { *ok = 0; return 0; }
            count++;
        }
        p = nl + 1;
    }
    return count;
}

static gp_status parse(gp_manifest *m, char *text, size_t len, char *why, size_t why_len)
{
    char *p = text, *end = text + len;
    uint32_t total, entry_fill = 0;
    size_t bytes;
    int line_no = 0, header_seen = 0, end_seen = 0, cases_started = 0;
    int active_file = -1, rows_ok;
    gp_status st = RP_ERR_BAD_INDEX;

    if (!text || len < 7 || len > GP_MANIFEST_MAX || text[len - 1] != '\n')
        return fail_line(why, why_len, 1, RP_ERR_TRUNCATED);
    {
        size_t k;
        for (k = 0; k < len; k++) {
            unsigned char c = (unsigned char)text[k];
            if (c == '\0' || (c < 0x20 && c != '\n' && c != '\r' && c != '\t') || c > 0x7E)
                return fail_line(why, why_len, 1, RP_ERR_BAD_INDEX);
        }
    }
    total = count_entry_rows(text, len, &rows_ok);
    if (!rows_ok) return fail_line(why, why_len, 1, RP_ERR_COUNT);
    /* Bound the row count before multiplying. With total <= RP_MAX_ALLOC /
     * sizeof(gp_mentry), the product is at most RP_MAX_ALLOC (64 MiB), so it
     * cannot overflow size_t on the Vita (32-bit) or on a 64-bit host. */
    if (total > RP_MAX_ALLOC / sizeof(gp_mentry))
        return fail_line(why, why_len, 1, RP_ERR_LIMIT);
    bytes = (size_t)total * sizeof(gp_mentry);
    if (bytes > RP_MAX_ALLOC) return fail_line(why, why_len, 1, RP_ERR_LIMIT);
    m->n_entries = total;
    if (total) {
        m->entries = (gp_mentry *)rp_malloc(bytes);
        if (!m->entries) return fail_line(why, why_len, 1, RP_ERR_NOMEM);
    }

    while (p < end) {
        char *nl = (char *)memchr(p, '\n', (size_t)(end - p));
        char *tokens[GP_MAX_TOKENS];
        size_t token_count = 0, line_len;
        if (!nl) { st = fail_line(why, why_len, line_no + 1, RP_ERR_TRUNCATED); goto done; }
        line_no++;
        line_len = (size_t)(nl - p);
        *nl = '\0';
        if (line_len && p[line_len - 1] == '\r') p[line_len - 1] = '\0';
        if (!split_tokens(p, tokens, GP_MAX_TOKENS, &token_count)) {
            st = fail_line(why, why_len, line_no, RP_ERR_BAD_INDEX); goto done;
        }
        if (token_count == 0) {
            if (end_seen) { p = nl + 1; continue; }
            st = fail_line(why, why_len, line_no, RP_ERR_BAD_INDEX); goto done;
        }
        if (!header_seen) {
            if (token_count != 2 || strcmp(tokens[0], "GPX") != 0 || strcmp(tokens[1], "1") != 0) {
                st = fail_line(why, why_len, line_no, RP_ERR_BAD_VERSION); goto done;
            }
            header_seen = 1;
            p = nl + 1;
            continue;
        }
        if (end_seen) {
            st = fail_line(why, why_len, line_no, RP_ERR_BAD_INDEX); goto done;
        }
        if (strcmp(tokens[0], "END") == 0) {
            if (token_count != 1) { st = fail_line(why, why_len, line_no, RP_ERR_BAD_INDEX); goto done; }
            st = finish_file(m, active_file, entry_fill);
            if (st != RP_OK) { st = fail_line(why, why_len, line_no, st); goto done; }
            active_file = -1;
            end_seen = 1;
            p = nl + 1;
            continue;
        }
        if (strcmp(tokens[0], "FILE") == 0) {
            if (cases_started) { st = fail_line(why, why_len, line_no, RP_ERR_BAD_INDEX); goto done; }
            st = finish_file(m, active_file, entry_fill);
            if (st != RP_OK) { st = fail_line(why, why_len, line_no, st); goto done; }
            st = parse_file(m, tokens, token_count, entry_fill, &active_file);
        } else if (strcmp(tokens[0], "ENTRY") == 0) {
            if (cases_started) { st = RP_ERR_BAD_INDEX; }
            else st = parse_entry(m, tokens, token_count, &entry_fill, total, active_file);
        } else if (strcmp(tokens[0], "CASE") == 0) {
            if (!m->n_files) { st = RP_ERR_BAD_INDEX; }
            else {
                st = finish_file(m, active_file, entry_fill);
                active_file = -1;
                cases_started = 1;
                if (st == RP_OK) st = parse_case(m, tokens, token_count);
            }
        } else {
            st = RP_ERR_BAD_INDEX;
        }
        if (st != RP_OK) { st = fail_line(why, why_len, line_no, st); goto done; }
        p = nl + 1;
    }

    if (!header_seen || !end_seen || !m->n_files || !m->n_cases || entry_fill != total) {
        st = fail_line(why, why_len, line_no, RP_ERR_TRUNCATED);
        goto done;
    }
    {
        int i;
        for (i = 0; i < m->n_cases; i++) {
            st = validate_case(m, &m->cases[i]);
            if (st != RP_OK) {
                st = fail_line(why, why_len, line_no, st);
                goto done;
            }
        }
    }
    if (why && why_len) why[0] = '\0';
    return RP_OK;

done:
    return st;
}

gp_status gp_manifest_load(gp_manifest *m, char *why, size_t why_len)
{
    static const char *const dirs[2] = { GP_FIX_DIR_UX0, GP_FIX_DIR_APP0 };
    char path[96];
    uint8_t *buf = NULL;
    size_t len = 0;
    gp_status st = RP_ERR_NOT_FOUND;
    int d;

    if (!m || !why || why_len == 0) return RP_ERR_ARG;
    memset(m, 0, sizeof(*m));
    snprintf(why, why_len, "expected.txt not found");
    for (d = 0; d < 2; d++) {
        snprintf(path, sizeof(path), "%sexpected.txt", dirs[d]);
        st = gp_path_check(path);
        if (st != RP_OK) { gp_manifest_free(m); return st; }
        st = rp_read_all(path, GP_MANIFEST_MAX, &buf, &len);
        if (st == RP_OK) {
            if (!buf || len == 0 || len > GP_MANIFEST_MAX) {
                rp_free(buf);
                gp_manifest_free(m);
                return len > GP_MANIFEST_MAX ? RP_ERR_TOO_LARGE : RP_ERR_TRUNCATED;
            }
            snprintf(m->dir, sizeof(m->dir), "%s", dirs[d]);
            break;
        }
        rp_free(buf);
        buf = NULL;
        len = 0;
    }
    if (st != RP_OK) { gp_manifest_free(m); return st; }

    st = parse(m, (char *)buf, len, why, why_len);
    rp_free(buf);
    if (st != RP_OK) gp_manifest_free(m);
    return st;
}

void gp_manifest_free(gp_manifest *m)
{
    if (!m) return;
    rp_free(m->entries);
    memset(m, 0, sizeof(*m));
}

const gp_mfile *gp_manifest_file(const gp_manifest *m, const char *name)
{
    int i;
    if (!m || !name) return NULL;
    for (i = 0; i < m->n_files; i++)
        if (strcmp(m->files[i].name, name) == 0) return &m->files[i];
    return NULL;
}

void gp_manifest_path(const gp_manifest *m, const char *name, char *buf, size_t n)
{
    if (!buf || n == 0) return;
    if (!m || !name) { buf[0] = '\0'; return; }
    snprintf(buf, n, "%s%s", m->dir, name);
}
