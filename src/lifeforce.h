/***************************************************************************
 *  File: lifeforce.h                                     Part of FieryMUD *
 *  Usage: header file for character life forces                           *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  FieryMUD Copyright (C) 1998, 1999, 2000 by the Fiery Consortium        *
 *  FieryMUD is based on CircleMUD Copyright (C) 1993, 94 by the Trustees  *
 *  of the Johns Hopkins University                                        *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ***************************************************************************/

#ifndef __FIERY_LIFEFORCE_H
#define __FIERY_LIFEFORCE_H

#include "sysdep.h"
#include "structs.h"

#define LIFE_UNDEFINED -1
#define LIFE_LIFE 0 /* normal folks */
#define LIFE_UNDEAD 1
#define LIFE_MAGIC 2     /* golems */
#define LIFE_CELESTIAL 3 /* angels */
#define LIFE_DEMONIC 4
#define LIFE_ELEMENTAL 5
#define NUM_LIFEFORCES 6 /* keep updated */

struct lifedef {
    /* The first element of this struct, 'name', is used by parse_obj_name()
     * and must not be changed. */
    char *name;
    char *color;
    int sus_heal;
    int sus_discorporate;
    int sus_dispel;
    int sus_mental;
};
extern struct lifedef lifeforces[];

extern int parse_lifeforce(struct char_data *ch, char *arg);
extern void convert_lifeforce(struct char_data *ch, int newlifeforce);
#define VALID_LIFEFORCENUM(num) ((num) >= 0 && (num) < NUM_LIFEFORCES)
#define VALID_LIFEFORCE(ch) (VALID_LIFEFORCENUM(GET_LIFEFORCE(ch)))
#define LIFEFORCE_NAME(ch) (VALID_LIFEFORCE(ch) ? lifeforces[GET_LIFEFORCE(ch)].name : "<INVALID LIFEFORCE>")
#define LIFEFORCE_COLOR(ch) (VALID_LIFEFORCE(ch) ? lifeforces[GET_LIFEFORCE(ch)].color : "")

#endif

/***************************************************************************
 * $Log: lifeforce.h,v $
 * Revision 1.1  2009/03/08 21:42:09  jps
 * Initial revision
 *
 ***************************************************************************/
