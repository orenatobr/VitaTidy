#include "order.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const char *order_skip_article(const char *name)
{
    static const char *const articles[] = {"the ", "an ", "a "};
    for (size_t i = 0; i < sizeof articles / sizeof articles[0]; i++) {
        size_t n = strlen(articles[i]);
        if (strncasecmp(name, articles[i], n) == 0 && name[n] != '\0') return name + n;
    }
    return name;
}

int order_cmp(const char *a, const char *b)
{
    a = order_skip_article(a);
    b = order_skip_article(b);
    for (;; a++, b++) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca < cb ? -1 : 1;
        if (ca == '\0') return 0;
    }
}

size_t order_build(const OrderItem *items, size_t n, const char *const *listed, size_t m, size_t *out)
{
    if (n == 0) return 0;
    char *used = calloc(n, 1);
    size_t *rest = malloc(n * sizeof *rest);
    if (!used || !rest) { free(used); free(rest); return 0; }

    size_t count = 0;
    for (size_t k = 0; k < m; k++) {
        for (size_t i = 0; i < n; i++) {
            if (!used[i] && strcmp(items[i].id, listed[k]) == 0) {
                used[i] = 1;
                out[count++] = i;
                break;
            }
        }
    }

    size_t nrest = 0;
    for (size_t i = 0; i < n; i++)
        if (!used[i]) rest[nrest++] = i;

    /* Insertion sort by name, then id, so the result is deterministic. */
    for (size_t i = 1; i < nrest; i++) {
        size_t cur = rest[i];
        size_t j = i;
        while (j > 0) {
            int c = order_cmp(items[rest[j - 1]].name, items[cur].name);
            if (c == 0) c = strcmp(items[rest[j - 1]].id, items[cur].id);
            if (c <= 0) break;
            rest[j] = rest[j - 1];
            j--;
        }
        rest[j] = cur;
    }

    for (size_t r = 0; r < nrest; r++) {
        size_t at = count;
        for (size_t p = 0; p < count; p++) {
            if (order_cmp(items[rest[r]].name, items[out[p]].name) < 0) { at = p; break; }
        }
        memmove(&out[at + 1], &out[at], (count - at) * sizeof out[0]);
        out[at] = rest[r];
        count++;
    }

    free(used);
    free(rest);
    return count;
}

static int push(char ***list, size_t *n, const char *s, size_t len)
{
    char **grown = realloc(*list, (*n + 1) * sizeof **list);
    if (!grown) return -1;
    *list = grown;
    char *copy = malloc(len + 1);
    if (!copy) return -1;
    memcpy(copy, s, len);
    copy[len] = '\0';
    (*list)[(*n)++] = copy;
    return 0;
}

int orderfile_parse(const char *text, int inline_comments, OrderFile *out)
{
    memset(out, 0, sizeof *out);
    int in_cfw = 0;
    const char *p = text;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t len = eol ? (size_t)(eol - p) : strlen(p);
        const char *line = p;
        p = eol ? eol + 1 : p + len;

        if (inline_comments) {
            for (size_t i = 0; i < len; i++) {
                if (line[i] == '#') { len = i; break; }
            }
        } else if (len > 0 && line[0] == '#') {
            continue;
        }
        while (len > 0 && isspace((unsigned char)line[len - 1])) len--;
        while (len > 0 && isspace((unsigned char)*line)) { line++; len--; }
        if (len == 0) continue;

        if (len == 5 && strncasecmp(line, "[cfw]", 5) == 0) { in_cfw = 1; continue; }
        if (len == 7 && strncasecmp(line, "[games]", 7) == 0) { in_cfw = 0; continue; }

        int rc = in_cfw ? push(&out->cfw, &out->ncfw, line, len) : push(&out->games, &out->ngames, line, len);
        if (rc != 0) { orderfile_free(out); return -1; }
    }
    return 0;
}

void orderfile_free(OrderFile *of)
{
    for (size_t i = 0; i < of->ncfw; i++) free(of->cfw[i]);
    for (size_t i = 0; i < of->ngames; i++) free(of->games[i]);
    free(of->cfw);
    free(of->games);
    memset(of, 0, sizeof *of);
}

int orderfile_has(char *const *list, size_t n, const char *id)
{
    for (size_t i = 0; i < n; i++)
        if (strcmp(list[i], id) == 0) return 1;
    return 0;
}
