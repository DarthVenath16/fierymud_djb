/***************************************************************************
 * $Id: rulesys.c,v 1.2 2009/06/11 00:59:32 myc Exp $
 ***************************************************************************/
/***************************************************************************
 *   File: rulesys.c                                      Part of FieryMUD *
 *  Usage: character rules infrastructure                                  *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  FieryMUD Copyright (C) 1998, 1999, 2000 by the Fiery Consortium        *
 *  FieryMUD is based on CircleMUD Copyright (C) 1993, 94 by the Trustees  *
 *  of the Johns Hopkins University                                        *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ***************************************************************************/

#include "conf.h"
#include "rules.h"
#include "structs.h"
#include "sysdep.h"
#include "utils.h"

static size_t vtable_count = 0;
static struct rule_vtable **vtables = NULL;

void REGISTER_RULE_VTABLE(struct rule_vtable *vtable) {
    size_t pos, i;
    bool swap;

    if ((pos = vtable_count++)) /* post-increment! */
        RECREATE(vtables, struct rule_vtable *, vtable_count);
    else
        CREATE(vtables, struct rule_vtable *, vtable_count);

    vtables[pos] = vtable;

    /* sort */
    ++pos;
    do {
        swap = FALSE;
        --pos;
        for (i = 0; i < pos; ++i)
            if (strcmp(vtables[i]->name, vtables[i + 1]->name) > 0) {
                vtable = vtables[i];
                vtables[i] = vtables[i + 1];
                vtables[i + 1] = vtable;
                swap = TRUE;
            }
    } while (swap);
}

struct rule_vtable *find_rule_vtable(const char *type) {
    size_t last = vtable_count - 1;
    size_t first = 0, mid;
    int comp;

    if (vtable_count == 0)
        return NULL;

    while (first <= last) {
        mid = (first + last) / 2;
        comp = strcmp(type, vtables[mid]->name);
        if (comp > 0)
            first = mid + 1; /* search top half */
        else if (comp < 0) {
            if (mid == 0)
                return NULL; /* avoid last == -1 */
            last = mid - 1;  /* search bottom half */
        } else
            return vtables[mid];
    }

    return NULL;
}

void *rulealloc(size_t elsize, struct rule_vtable *vtable) {
    rule_t *rule = (rule_t *)calloc(1, elsize);
    rule->_vtable = vtable;
    return rule;
}

rule_t *parse_rule(const char *str) {
    struct rule_vtable *vtable;
    char type[MAX_INPUT_LENGTH];
    char *ptr = type;

    str = skip_over(str, S_WHITESPACE);
    while (*str && !isspace(*str))
        *(ptr++) = *(str++);
    *ptr = '\0';
    str = skip_over(str, S_WHITESPACE);

    if (!(vtable = find_rule_vtable(type)))
        return NULL;

    return vtable->parse(str);
}

void sprint_rule(char *buf, size_t size, struct rule *rule) {
    if (rule) {
        size_t len = snprintf(buf, size, "%s ", rule->_vtable->name);
        if (len < size)
            (rule->_vtable->print)(buf + len, size - len, rule);
    } else
        snprintf(buf, size, "NULL");
}

/***************************************************************************
 * $Log: rulesys.c,v $
 * Revision 1.2  2009/06/11 00:59:32  myc
 * Fix binary search in find_rule_vtable.
 *
 * Revision 1.1  2009/05/01 05:29:40  myc
 * Initial revision
 *
 ***************************************************************************/
