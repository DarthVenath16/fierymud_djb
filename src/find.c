/***************************************************************************
 * $Id: find.c,v 1.6 2010/05/11 05:52:56 mud Exp $
 ***************************************************************************/
/***************************************************************************
 *   File: find.c                                         Part of FieryMUD *
 *  Usage: internal funcs: finding chars/objs                              *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  FieryMUD Copyright (C) 1998, 1999, 2000 by the Fiery Consortium        *
 *  FieryMUD is based on CircleMUD Copyright (C) 1993, 94 by the Trustees  *
 *  of the Johns Hopkins University                                        *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ***************************************************************************/

#include "find.h"

#include "comm.h"
#include "conf.h"
#include "db.h"
#include "dg_scripts.h"
#include "handler.h"
#include "interpreter.h"
#include "math.h"
#include "structs.h"
#include "sysdep.h"
#include "utils.h"

/*
 * Takes a name, and if it is of the form '#.name', skips over the
 * number and period and returns the number.
 */
int grab_number(char **name) {
    int num;
    char *pos;

    if ((pos = strchr(*name, '.'))) {
        num = atoi(*name);
        *name = ++pos;
        return num > 0 ? num : 1;
    }

    return 1;
}

/******* MATCH_OBJ_FUNCs *******/
/* A MATCH_OBJ_FUNC compares an object and a find-context, returning TRUE
 * if the object matches the function predicate and FALSE if it doesn't.
 */

static MATCH_OBJ_FUNC(match_vis_obj) { return CAN_SEE_OBJ(context->ch, obj); }

static MATCH_OBJ_FUNC(match_obj_by_name) {
    if (isname(context->string, obj->name))
        if (--context->number <= 0)
            return TRUE;
    return FALSE;
}

static MATCH_OBJ_FUNC(match_vis_obj_by_name) {
    if (isname(context->string, obj->name))
        if (CAN_SEE_OBJ(context->ch, obj))
            if (--context->number <= 0)
                return TRUE;
    return FALSE;
}

static MATCH_OBJ_FUNC(match_obj_by_id) { return (GET_ID(obj) == context->number); }

static MATCH_OBJ_FUNC(match_vis_obj_by_id) { return (GET_ID(obj) == context->number) && CAN_SEE_OBJ(context->ch, obj); }

static MATCH_OBJ_FUNC(match_obj_by_rnum) { return (GET_OBJ_RNUM(obj) == context->number); }

static MATCH_OBJ_FUNC(match_obj_by_vnum) { return (GET_OBJ_VNUM(obj) == context->number); }

static MATCH_OBJ_FUNC(match_vis_obj_by_vnum) {
    return (GET_OBJ_VNUM(obj) == context->number) && CAN_SEE_OBJ(context->ch, obj);
}

static MATCH_OBJ_FUNC(match_by_type) { return (GET_OBJ_TYPE(obj) == context->number); }

static MATCH_OBJ_FUNC(match_vis_by_type) {
    return (GET_OBJ_TYPE(obj) == context->number) && CAN_SEE_OBJ(context->ch, obj);
}

/******* MATCH_CHAR_FUNCs *******/
/* A MATCH_CHAR_FUNC compares a character and a find-context, returning
 * TRUE if the character matches the function predicate and FALSE if it
 * doesn't.
 */

static MATCH_CHAR_FUNC(match_vis_char) { return CAN_SEE(context->ch, ch); }

static MATCH_CHAR_FUNC(match_char_by_name) {
    if (isname(context->string, GET_NAMELIST(ch)))
        if (--context->number <= 0)
            return TRUE;

    return FALSE;
}

static MATCH_CHAR_FUNC(match_vis_char_by_name) {
    if (isname(context->string, GET_NAMELIST(ch)))
        if (CAN_SEE(context->ch, ch))
            if (--context->number <= 0)
                return TRUE;

    return FALSE;
}

static MATCH_CHAR_FUNC(match_char_by_id) { return (GET_ID(ch) == context->number); }

static MATCH_CHAR_FUNC(match_vis_char_by_id) { return (GET_ID(ch) == context->number) && CAN_SEE(context->ch, ch); }

static MATCH_CHAR_FUNC(match_mob_by_rnum) { return (GET_MOB_RNUM(ch) == context->number); }

static MATCH_CHAR_FUNC(match_mob_by_vnum) { return (GET_MOB_VNUM(ch) == context->number); }

static MATCH_CHAR_FUNC(match_vis_mob_by_vnum) {
    return (GET_MOB_VNUM(ch) == context->number) && CAN_SEE(context->ch, ch);
}

static MATCH_CHAR_FUNC(match_player_by_name) { return !IS_NPC(ch) && match_char_by_name(context, ch); }

static MATCH_CHAR_FUNC(match_vis_player_by_name) { return !IS_NPC(ch) && match_vis_char_by_name(context, ch); }

/******* find_context factories *******/
/* A find_context factory returns a find_context based on certain values
 * provided by the user.  The find_context is then fed to a find
 * function to compare against objects or characters.  find_contexts
 * must provide match functions.
 */

struct find_context find_vis(struct char_data *ch) {
    struct find_context context = NULL_FCONTEXT;
    context.obj_func = match_vis_obj;
    context.char_func = match_vis_char;
    context.ch = ch;
    return context;
}

/* Find obj/chars using DG id */
struct find_context find_by_id(int id) {
    struct find_context context = NULL_FCONTEXT;
    context.obj_func = match_obj_by_id;   /* func for checking objs */
    context.char_func = match_char_by_id; /* func for checking mobs */
    context.number = id;
    return context;
}

/* Find obj/chars visible to a given character using DG id */
struct find_context find_vis_by_id(struct char_data *ch, int id) {
    struct find_context context = NULL_FCONTEXT;
    context.obj_func = match_vis_obj_by_id;
    context.char_func = match_vis_char_by_id;
    context.number = id;
    context.ch = ch;
    return context;
}

/* Find objs/chars using namelist */
struct find_context find_by_name(char *name) {
    struct find_context context = NULL_FCONTEXT;
    if (*name == UID_CHAR) {
        context.obj_func = match_obj_by_id;
        context.char_func = match_char_by_id;
        context.number = atoi(name + 1);
    } else {
        context.obj_func = match_obj_by_name;
        context.char_func = match_char_by_name;
        context.number = grab_number(&name);
        context.string = name;
    }
    return context;
}

/* Find objs/chars visible to a given char using namelist */
struct find_context find_vis_by_name(struct char_data *ch, char *name) {
    struct find_context context = NULL_FCONTEXT;
    if (*name == UID_CHAR) {
        context.obj_func = match_vis_obj_by_id;
        context.char_func = match_vis_char_by_id;
        context.number = atoi(name + 1);
    } else {
        context.obj_func = match_vis_obj_by_name;
        context.char_func = match_vis_char_by_name;
        context.number = grab_number(&name);
        context.string = name;
        if (!str_cmp(name, "self") || !str_cmp(name, "me"))
            context.override = TRUE;
    }
    context.ch = ch;
    return context;
}

/* Find objs/mobs by rnum */
struct find_context find_by_rnum(int rnum) {
    struct find_context context = NULL_FCONTEXT;
    context.obj_func = match_obj_by_rnum;
    context.char_func = match_mob_by_rnum;
    context.number = rnum;
    return context;
}

/* Find objs/mobs by vnum */
struct find_context find_by_vnum(int vnum) {
    struct find_context context = NULL_FCONTEXT;
    context.obj_func = match_obj_by_vnum;
    context.char_func = match_mob_by_vnum;
    context.number = vnum;
    return context;
}

/* Find objs/mobs by vnum visible to ch */
struct find_context find_vis_by_vnum(struct char_data *ch, int vnum) {
    struct find_context context = NULL_FCONTEXT;
    context.obj_func = match_vis_obj_by_vnum;
    context.char_func = match_vis_mob_by_vnum;
    context.number = vnum;
    context.ch = ch;
    return context;
}

/* Find objs by type */
struct find_context find_by_type(int type) {
    struct find_context context = NULL_FCONTEXT;
    context.obj_func = match_by_type;
    context.number = type;
    return context;
}

/* Find objs by type visible to ch */
struct find_context find_vis_by_type(struct char_data *ch, int type) {
    struct find_context context = NULL_FCONTEXT;
    context.obj_func = match_vis_by_type;
    context.number = type;
    context.ch = ch;
    return context;
}

/* Find players using name */
struct find_context find_plr_by_name(char *name) {
    struct find_context context = find_by_name(name);
    if (*name != UID_CHAR)
        context.char_func = match_player_by_name;
    return context;
}

/* Find players visible to a given char using name */
struct find_context find_vis_plr_by_name(struct char_data *ch, char *name) {
    struct find_context context = find_vis_by_name(ch, name);
    if (*name != UID_CHAR)
        context.char_func = match_vis_player_by_name;
    return context;
}

/******** Iterator functions *******/
/* Iterator functions each perform an action on an iterator, such
 * as progressing to the next item in an iteration.
 */

/* used by find_objs_in_list */
static struct obj_data *next_obj_in_list(struct obj_iterator *iter) {
    struct obj_data *list = iter->next;
    while (list) {
        if (OBJ_MATCH(iter->context, list))
            break;
        list = list->next_content;
    }
    iter->next = list ? list->next_content : NULL;
    return iter->obj = list;
}

static struct obj_data *next_obj_in_eq(struct obj_iterator *iter) {
    struct char_data *ch = iter->data;

    while (iter->pos < NUM_WEARS) {
        if (GET_EQ(ch, iter->pos) && OBJ_MATCH(iter->context, GET_EQ(ch, iter->pos)))
            return iter->obj = GET_EQ(ch, iter->pos++);
        iter->pos++;
    }
    return NULL;
}

static struct char_data *next_char_in_room(struct char_iterator *iter) {
    struct char_data *list = iter->next;
    while (list) {
        if (CHAR_MATCH(iter->context, list))
            break;
        list = list->next_in_room;
    }
    iter->next = list ? list->next_in_room : NULL;
    return iter->ch = list;
}

static struct char_data *next_char_in_world(struct char_iterator *iter) {
    struct char_data *list = iter->next;
    while (list) {
        if (CHAR_MATCH(iter->context, list))
            break;
        list = list->next;
    }
    iter->next = list ? list->next : NULL;
    return iter->ch = list;
}

/******** Traversal functions *******/
/* Traversal functions each implement a different traversal method and
 * require a find_context.  Some traversal functions require additional
 * information about what they should traverse over.
 */

/* Traverse the contents of an object list, looking for a matching obj
 * by using the provided find context.
 */
struct obj_data *find_obj_in_list(struct obj_data *list, struct find_context context) {
    while (list) {
        if (OBJ_MATCH(context, list))
            return list;
        list = list->next_content;
    }

    return NULL;
}

struct obj_iterator find_objs_in_list(struct obj_data *list, struct find_context context) {
    struct obj_iterator iter = NULL_ITER(NULL);
    iter.next = list;
    iter.context = context;
    iter.next_func = next_obj_in_list;
    return iter;
}

/* Find a matching object in a list by using the provided find context.
 * Recurs into containers.  Preorder matching.
 */
struct obj_data *find_obj_in_list_recur(struct obj_data *list, struct find_context context) {
    struct obj_data *i;

    while (list) {
        if (OBJ_MATCH(context, list))
            return list;
        if ((i = find_obj_in_list(list, context)))
            return i;
        list = list->next_content;
    }

    return NULL;
}

/* Iterate through the equipment on a character, looking for a matching
 * object by using the provided find context.
 */
struct obj_data *find_obj_in_eq(struct char_data *ch, int *where, struct find_context context) {
    int i;

    for (i = 0; i < NUM_WEARS; ++i)
        if (GET_EQ(ch, i) && OBJ_MATCH(context, GET_EQ(ch, i))) {
            if (where)
                *where = i;
            return GET_EQ(ch, i);
        }

    if (where)
        *where = -1;

    return NULL;
}

/* Iterate through the global object list, looking for a matching object
 * by using the provided find context.
 */
struct obj_data *find_obj_in_world(struct find_context context) {
    struct obj_data *obj = object_list;

    while (obj) {
        if (OBJ_MATCH(context, obj))
            return obj;
        obj = obj->next;
    }

    return NULL;
}

/* Look for a matching object using a provided find context, moving
 * progressively outwards from a given character.  Checks the
 * character's equipment first, followed by his/her inventory,
 * the contents of the room, and finally the world at large.
 */
struct obj_data *find_obj_around_char(struct char_data *ch, struct find_context context) {
    struct obj_data *obj;

    /* scan equipment */
    if ((obj = find_obj_in_eq(ch, NULL, context)))
        return obj;

    /* scan inventory */
    if ((obj = find_obj_in_list(ch->carrying, context)))
        return obj;

    /* scan room */
    if ((obj = find_obj_in_list(world[ch->in_room].contents, context)))
        return obj;

    /* scan world */
    if ((obj = find_obj_in_world(context)))
        return obj;

    return NULL;
}

/* Look for a matching object using a provided find context, moving
 * progressively outwards from a given object.  Checks the object itself
 * first, followed by the contents of the obj, the containing obj,
 * other equipped objects if the obj is being worn by a character,
 * other carried objects if the obj is in a character's inventory,
 * other objects in the same room, and finally a search of the whole
 * world.
 */
struct obj_data *find_obj_around_obj(struct obj_data *obj, struct find_context context) {
    struct obj_data *tobj;
    int rm;

    extern int obj_room(struct obj_data * obj); /* from dg_scripts.c */

    /* self */
    if (OBJ_MATCH(context, obj))
        return obj;

    /* a contained obj */
    if (obj->contains && (tobj = find_obj_in_list(obj->contains, context)))
        return tobj;

    /* the containing obj */
    if (obj->in_obj && OBJ_MATCH(context, obj->in_obj))
        return obj->in_obj;

    if (obj->worn_by && (tobj = find_obj_in_eq(obj->worn_by, NULL, context)))
        return tobj; /* something else worn by char */

    if (obj->carried_by && (tobj = find_obj_in_list(obj->carried_by->carrying, context)))
        return tobj; /* something else carried by char */

    if ((rm = obj_room(obj)) != NOWHERE && (tobj = find_obj_in_list(world[rm].contents, context)))
        return tobj;

    return find_obj_in_world(context);
}

/* Look for a matching object using a find_context, looking in a given
 * room.  If searching by id, and a match is not found in the room,
 * automatically searches the world.  It does this because searching
 * by id is typically done by scripts.  When this is done, it is
 * important that we find the object regardless of whether it is in the
 * room or not.
 */
struct obj_data *find_obj_around_room(struct room_data *room, struct find_context context) {
    struct obj_data *obj;

    if ((obj = find_obj_in_list(room->contents, context)))
        return obj;

    if (context.obj_func == match_obj_by_id || context.obj_func == match_vis_obj_by_id)
        return find_obj_in_world(context);

    return NULL;
}

/* Look for a matching object by name, moving progressively outwards
 * from a given character.  The difference between this function and
 *   find_obj_around_char(ch, find_vis_by_name(ch, name))
 * is that on the final scan through the entire world, it uses
 * find_by_name_(name) instead, ignoring visibility.  This is because
 * if it gets this far,
 *
 */
struct obj_data *find_obj_for_mtrig(struct char_data *ch, char *name) {
    struct obj_data *obj;
    struct find_context context = find_vis_by_name(ch, name);

    /* scan equipment */
    if ((obj = find_obj_in_eq(ch, NULL, context)))
        return obj;

    /* scan inventory */
    if ((obj = find_obj_in_list(ch->carrying, context)))
        return obj;

    /* scan room */
    if ((obj = find_obj_in_list(world[ch->in_room].contents, context)))
        return obj;

    /* scan world */
    if ((obj = find_obj_in_world(find_by_name(name))))
        return obj;

    return NULL;
}

/* Traverse the people in a room, looking for a matching char by using
 * the provided find context.
 */
struct char_data *find_char_in_room(struct room_data *room, struct find_context context) {
    struct char_data *ch = room ? room->people : NULL;

    if (context.override)
        return context.ch;

    while (ch) {
        if (CHAR_MATCH(context, ch))
            return ch;
        ch = ch->next_in_room;
    }

    return NULL;
}

/* Iterate through the global character list, looking for a matching
 * character by using the provided find context.
 */
struct char_data *find_char_in_world(struct find_context context) {
    struct char_data *ch = character_list;

    if (context.override)
        return context.ch;

    while (ch) {
        if (CHAR_MATCH(context, ch))
            return ch;
        ch = ch->next;
    }

    return NULL;
}

/* Iterate through the descriptor list, looking for a matching character
 * by using the provided find context.
 */
struct char_data *find_char_by_desc(struct find_context context) {
    struct descriptor_data *d = descriptor_list;

    if (context.override)
        return context.ch;

    while (d) {
        if (STATE(d) == CON_PLAYING) {
            if (d->character && CHAR_MATCH(context, d->character))
                return d->character;
            if (d->original && CHAR_MATCH(context, d->original))
                return d->original;
        }
        d = d->next;
    }

    return NULL;
}

/* Look for a matching character using a provided find context, moving
 * progressively outwards from a given character.  Checks the room
 * followed by the world at large.
 */
struct char_data *find_char_around_char(struct char_data *ch, struct find_context context) {
    struct char_data *targ;

    /* scan room */
    if ((targ = find_char_in_room(&world[ch->in_room], context)))
        return targ;

    /* scan world */
    if ((targ = find_char_in_world(context)))
        return targ;

    return NULL;
}

/* Look for a matching character using a provided find context, moving
 * progressively outwards from a given object.  Checks the holder of
 * the object, followed by other characters in the room with the object,
 * followed by a search of the whole world.
 */
struct char_data *find_char_around_obj(struct obj_data *obj, struct find_context context) {
    struct char_data *ch;

    while (obj->in_obj)
        obj = obj->in_obj;

    if (obj->carried_by && CHAR_MATCH(context, obj->carried_by))
        return obj->carried_by;

    if (obj->worn_by && CHAR_MATCH(context, obj->worn_by))
        return obj->worn_by;

    if (obj->in_room != NOWHERE && (ch = find_char_in_room(&world[obj->in_room], context)))
        return ch;

    return find_char_in_world(context);
}

/* Look for a matching character using a find_context, looking in a
 * given room.  If searching by id, and a match is not found in the
 * room, automatically searches the world.  It does this because
 * searching by id is typically done by scripts.  When this is done, it
 * is important that we find the character regardless of whether it is
 * in the room or not.
 */
struct char_data *find_char_around_room(struct room_data *room, struct find_context context) {
    struct char_data *ch;

    extern MATCH_CHAR_FUNC(match_dg_vis_char_by_id);

    if ((ch = find_char_in_room(room, context)))
        return ch;

    if (context.char_func == match_char_by_id || context.char_func == match_vis_char_by_id ||
        context.char_func == match_dg_vis_char_by_id)
        return find_char_in_world(context);

    return NULL;
}

/* Look for a matching character by name, moving progressively outwards
 * from a given character.  The difference between this function and
 *   find_char_around_char(ch, find_vis_by_name(ch, name))
 * is that on the final scan through the entire world, it uses
 * find_by_name(name) instead, ignoring visibility.  This is because
 * if it gets this far, it probably means
 *
 */
struct char_data *find_char_for_mtrig(struct char_data *ch, char *name) {
    struct char_data *targ;

    /* scan room */
    if ((targ = find_char_in_room(&world[ch->in_room], find_vis_by_name(ch, name))))
        return targ;

    /* scan world */
    if ((targ = find_char_in_world(find_by_name(name))))
        return targ;

    return NULL;
}

/* Generic Find, designed to find any object/character                    */
/* Calling :                                                              */
/*  *arg     is the sting containing the string to be searched for.       */
/*           This string doesn't have to be a single word, the routine    */
/*           extracts the next word itself.                               */
/*  bitv..   All those bits that you want to "search through".            */
/*           Bit found will be result of the function                     */
/*  *ch      This is the person that is trying to "find"                  */
/*  **tch    Will be NULL if no character was found, otherwise points     */
/*  **tobj   Will be NULL if no object was found, otherwise points        */
/*                                                                        */
/* The routine returns a pointer to the next word in *arg (just like the  */
/* one_argument routine).                                                 */

int generic_find(char *arg, int bitvector, struct char_data *ch, struct char_data **tch, struct obj_data **tobj) {
    char name[MAX_INPUT_LENGTH];
    one_argument(arg, name);
    if (!*name)
        return 0;
    return universal_find(find_vis_by_name(ch, name), bitvector, tch, tobj);
}

/*
 * Universal find, designed to find any object/character
 * universal_find acts just like generic_find, but the user chooses
 * what criteria to search by.  Additionally, the find_context *must*
 * supply a char_data pointer if any of the FIND_ flags besides
 * FIND_CHAR_WORLD and FIND_OBJ_WORLD are asserted.
 */
int universal_find(struct find_context context, int bitvector, struct char_data **tch, struct obj_data **tobj) {
    struct char_data *ch = context.ch;

    if (tch)
        *tch = NULL;
    if (tobj)
        *tobj = NULL;

    if (IS_SET(bitvector, FIND_CHAR_ROOM) && tch && ch && ch->in_room != NOWHERE) {
        if ((*tch = find_char_in_room(&world[ch->in_room], context)))
            return (FIND_CHAR_ROOM);
    }
    if (IS_SET(bitvector, FIND_CHAR_WORLD) && tch) {
        if ((*tch = find_char_in_world(context)))
            return (FIND_CHAR_WORLD);
    }
    if (IS_SET(bitvector, FIND_OBJ_EQUIP) && tobj && ch) {
        if ((*tobj = find_obj_in_eq(ch, NULL, context)))
            return (FIND_OBJ_EQUIP);
    }
    if (IS_SET(bitvector, FIND_OBJ_INV) && tobj && ch) {
        if ((*tobj = find_obj_in_list(ch->carrying, context)))
            return (FIND_OBJ_INV);
    }
    if (IS_SET(bitvector, FIND_OBJ_ROOM) && tobj && ch && ch->in_room != NOWHERE) {
        if ((*tobj = find_obj_in_list(world[ch->in_room].contents, context)))
            return (FIND_OBJ_ROOM);
    }
    if (IS_SET(bitvector, FIND_OBJ_WORLD) && tobj) {
        if ((*tobj = find_obj_in_world(context)))
            return (FIND_OBJ_WORLD);
    }
    return (0);
}

static struct obj_data *next_obj(struct obj_iterator *iter) {
    struct char_data *ch = iter->data;
    struct obj_data *next;

    if (IS_SET(iter->mode, FIND_OBJ_EQUIP) && ch) {
        if ((next = next_obj_in_eq(iter)))
            return next;
        REMOVE_BIT(iter->mode, FIND_OBJ_EQUIP);
        return next_obj(iter);
    }

    else if (IS_SET(iter->mode, FIND_OBJ_INV) && ch) {
        if (!iter->next)
            iter->next = ch->carrying;
        if ((next = next_obj_in_list(iter))) {
            if (!iter->next)
                REMOVE_BIT(iter->mode, FIND_OBJ_INV);
            return next;
        }
        iter->next = NULL;
        REMOVE_BIT(iter->mode, FIND_OBJ_INV);
        return next_obj(iter);
    }

    else if (IS_SET(iter->mode, FIND_OBJ_ROOM) && ch && ch->in_room != NOWHERE) {
        if (!iter->next)
            iter->next = world[ch->in_room].contents;
        if ((next = next_obj_in_list(iter))) {
            if (!iter->next)
                REMOVE_BIT(iter->mode, FIND_OBJ_ROOM);
            return next;
        }
        iter->next = NULL;
        REMOVE_BIT(iter->mode, FIND_OBJ_ROOM);
        return next_obj(iter);
    }

    else if (IS_SET(iter->mode, FIND_OBJ_WORLD)) {
        if (!iter->next)
            iter->next = object_list;
        if ((next = next_obj_in_list(iter))) {
            if (!iter->next)
                REMOVE_BIT(iter->mode, FIND_OBJ_WORLD);
            return next;
        }
        iter->next = NULL;
        REMOVE_BIT(iter->mode, FIND_OBJ_WORLD);
        return next_obj(iter);
    }

    else
        return NULL;
}

static struct char_data *next_char(struct char_iterator *iter) {
    struct char_data *ch = iter->data;
    struct char_data *next;

    if (IS_SET(iter->mode, FIND_CHAR_ROOM) && ch && ch->in_room != NOWHERE) {
        if (!iter->next)
            iter->next = world[ch->in_room].people;
        if ((next = next_char_in_room(iter)))
            return next;
        iter->next = NULL;
        REMOVE_BIT(iter->mode, FIND_CHAR_ROOM);
        return next_char(iter);
    }

    else if (IS_SET(iter->mode, FIND_CHAR_WORLD) && ch) {
        if (!iter->next)
            iter->next = character_list;
        if ((next = next_char_in_world(iter)))
            return next;
        iter->next = NULL;
        REMOVE_BIT(iter->mode, FIND_CHAR_WORLD);
        return next_char(iter);
    }

    else
        return NULL;
}

struct obj_iterator find_objs(struct find_context context, int bitvector) {
    struct obj_iterator iter = NULL_ITER(NULL);
    iter.mode = bitvector;
    iter.data = context.ch;
    iter.context = context;
    iter.next_func = next_obj;
    if (IS_SET(bitvector, FIND_OBJ_INV))
        iter.next = context.ch ? context.ch->carrying : NULL;
    /*
     * It's not absolutely necessary to initialize the next member here;
     * next_objs() will automatically do this.  However, we do it anyway
     * to avoid a subtle bug when find_objs is used by do_get: if a
     * player gets items from an equipped container, and these items
     * are containers, they will be put into the inventory before
     * the iteration over the inventory begins, causing them to be
     * considered by the get command.
     *
     * For instance, a player is wearing a bag (bag A).  Bag A has another
     * bag inside (bag B).  Bag B has a third bag (bag C) inside.  If the
     * player tries to 'get all.bag all.bag', it will correctly remove bag
     * B from bag A then incorrectly remove bag C from bag B.  This
     * behavior also occurs for examples like 'get all.pack all'.
     */
    return iter;
}

struct char_iterator find_chars(struct find_context context, int bitvector) {
    struct char_iterator iter = NULL_ITER(NULL);
    iter.mode = bitvector;
    iter.data = context.ch;
    iter.context = context;
    iter.next_func = next_char;
    return iter;
}

struct obj_data *find_obj_for_keyword(struct obj_data *obj, const char *name) {
    if (!str_cmp(name, "self") || !str_cmp(name, "me"))
        return obj;
    return NULL;
}

struct char_data *find_char_for_keyword(struct char_data *ch, const char *name) {
    if (!str_cmp(name, "self") || !str_cmp(name, "me"))
        return ch;
    return NULL;
}

/* a function to scan for "all" or "all.x" */
int find_all_dots(char **arg) {
    if (!strcmp(*arg, "all"))
        return FIND_ALL;
    else if (!strncmp(*arg, "all.", 4)) {
        *arg += 4;
        return FIND_ALLDOT;
    } else
        return FIND_INDIV;
}

struct char_data *get_random_char_around(struct char_data *ch, int mode) {
    int count = 0;
    struct char_data *vict = NULL;
    struct char_data *i;

    if (!ch)
        return vict;

    for (i = world[ch->in_room].people; i; i = i->next_in_room) {
        if (IS_SET(mode, RAND_PLAYERS) && IS_NPC(i))
            continue;
        if (IS_SET(mode, RAND_HASSLE) && PRF_FLAGGED(i, PRF_NOHASSLE))
            continue;
        if (IS_SET(mode, RAND_VISIBLE) && !CAN_SEE(ch, i))
            continue;
        if (IS_SET(mode, RAND_NOT_SELF) && ch == i)
            continue;
        if (IS_SET(mode, RAND_WIZ_VIS) && GET_INVIS_LEV(i))
            continue;
        /*
         * 100% chance to pick the first person,
         * 50% chance to pick the second person (50% to keep first),
         * 33% chance to pick the third person (66% to keep one of first two),
         * etc...
         */
        if (number(0, count++) == 0)
            vict = i;
    }

    return vict;
}

/***************************************************************************
 * $Log: find.c,v $
 * Revision 1.6  2010/05/11 05:52:56  mud
 * Fix a crash bug caused by attempting to check the name of a
 * character that is in the process of logging in.
 *
 * Revision 1.5  2009/03/19 23:16:23  myc
 * Added find_vis() premade context and several new iterators.
 *
 * Revision 1.4  2009/03/09 02:22:32  myc
 * Added find_vis_by_vnum and find_vis_by_type find context factories.
 * Also added universal_find, which is just like generic_find, but
 * accepts a custom find context.
 *
 * Revision 1.3  2009/03/06 00:46:31  myc
 * Make MATCH_CHAR_FUNC(match_dg_vis_char_by_id) visible outside the
 * file since find_char_around_room needs to know its address.
 *
 * Revision 1.2  2009/03/06 00:24:39  myc
 * Fix bug in find_char_around_obj.
 *
 * Revision 1.1  2009/03/03 19:43:44  myc
 * Initial revision
 *
 ***************************************************************************/
