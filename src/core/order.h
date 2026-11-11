/* Title ordering: alphabetical without leading articles, with an optional priority list. */
#ifndef VT_ORDER_H
#define VT_ORDER_H

#include <stddef.h>

typedef struct {
    const char *id;   /* stable identifier: title id or file name */
    const char *name; /* text used for the alphabetical position */
} OrderItem;

/* Returns name without a leading "The ", "A " or "An ". */
const char *order_skip_article(const char *name);
/* Case-insensitive comparison that ignores a leading article. */
int order_cmp(const char *a, const char *b);

/*
 * Orders items. Items whose id is in listed[] come first, in the listed order.
 * Every other item is placed alphabetically: before the first already placed
 * item that sorts after it, or at the end. out[] receives item indexes and must
 * hold n entries. Returns the number written (always n).
 */
size_t order_build(const OrderItem *items, size_t n, const char *const *listed, size_t m, size_t *out);

typedef struct {
    char **cfw;
    size_t ncfw;
    char **games;
    size_t ngames;
} OrderFile;

/*
 * Parses an order file: one id per line, "#" starts a comment, "[cfw]" and
 * "[games]" switch section (lines before any section belong to games).
 * inline_comments allows "id  # comment"; without it only whole-line comments
 * are recognised, so file names may contain " #". Returns 0 on success.
 */
int orderfile_parse(const char *text, int inline_comments, OrderFile *out);
void orderfile_free(OrderFile *of);
int orderfile_has(char *const *list, size_t n, const char *id);

#endif
