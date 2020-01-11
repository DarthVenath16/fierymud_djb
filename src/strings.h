/***************************************************************************
 * $Id: strings.h,v 1.7 2009/06/09 05:48:39 myc Exp $
 ***************************************************************************/
/***************************************************************************
 *   File: strings.h                                      Part of FieryMUD *
 *  Usage: header file: string management functions                        *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  FieryMUD Copyright (C) 1998, 1999, 2000 by the Fiery Consortium        *
 *  FieryMUD is based on CircleMUD Copyright (C) 1993, 94 by the Trustees  *
 *  of the Johns Hopkins University                                        *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ***************************************************************************/

#ifndef __FIERY_STRINGS_H
#define __FIERY_STRINGS_H

const void *ellipsis(const char *str, int maxlen, int mode);

#define EOL "\r\n"

#define ELLIPSIS_FMT "%-*.*s%s"
#define __ELLIPSIS_W1 (1 << 0)
#define __ELLIPSIS_W2 (1 << 1)
#define __ELLIPSIS_E (1 << 2)
#define ELLIPSIS_STR(str, maxlen)                                                                                      \
    (int)ellipsis((str), (maxlen), __ELLIPSIS_W1), (int)ellipsis((str), (maxlen), __ELLIPSIS_W2), (str),               \
        (char *)ellipsis((str), (maxlen), __ELLIPSIS_E)

int str_cmp(const char *arg1, const char *arg2);
int strn_cmp(const char *arg1, const char *arg2, int n);
const char *str_str(const char *cs, const char *ct);
char *strdupf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* TODO: move result to the first arg for sprintbit/type */
void sprintbit(long vektor, const char *names[], char *result);
void sprinttype(int type, const char *names[], char *result);
void sprintflag(char *result, flagvector flags[], int num_flags, const char *names[]);
int sprintascii(char *out, flagvector bits);

typedef struct str_token *str_token;
str_token str_start(char *buf, size_t max_size);
void str_cat(char *buf, const char *str);
void strn_cat(char *buf, const char *str, size_t size);
void str_catf(char *buf, const char *format, ...) __attribute__((format(printf, 2, 3)));
void strn_catf(char *buf, size_t size, const char *format, ...) __attribute__((format(printf, 3, 4)));
char *str_end(char *buf);
size_t str_space(char *buf);

#endif

/***************************************************************************
 * $Log: strings.h,v $
 * Revision 1.7  2009/06/09 05:48:39  myc
 * Moving a large number of functions from here to text.c.
 *
 * Revision 1.6  2009/05/01 05:29:40  myc
 * Added skip_over and skip_chars functions.  Also adding
 * fetch_word which does the same thing as any_one_arg, but
 * with const buffers.
 *
 * Revision 1.5  2009/03/17 09:01:41  jps
 * Add EOL macro and function capitalize()
 *
 * Revision 1.4  2009/03/17 07:59:42  jps
 * Moved str_str to strings.c
 *
 * Revision 1.3  2009/03/17 07:55:37  jps
 * Moved ellipsis string formatting to strings.c.
 *
 * Revision 1.2  2009/03/09 04:41:56  jps
 * Put FORMAT_INDENT definition in strings.h
 *
 * Revision 1.1  2009/03/09 03:33:03  myc
 * Initial revision
 *
 ***************************************************************************/
