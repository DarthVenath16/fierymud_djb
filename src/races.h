/***************************************************************************
 *  File: races.h                                         Part of FieryMUD *
 *  Usage: header file for race structures and constants                   *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  FieryMUD Copyright (C) 1998, 1999, 2000 by the Fiery Consortium        *
 *  FieryMUD is based on CircleMUD Copyright (C) 1993, 94 by the Trustees  *
 *  of the Johns Hopkins University                                        *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ***************************************************************************/

#ifndef __FIERY_RACES_H
#define __FIERY_RACES_H

#include "sysdep.h"
#include "structs.h"

/* The race of an individual is stored as a byte in struct char_player_data. */

/* The races */
#define RACE_UNDEFINED -1
#define RACE_HUMAN 0
#define RACE_ELF 1
#define RACE_GNOME 2
#define RACE_DWARF 3
#define RACE_TROLL 4
#define RACE_DROW 5
#define RACE_DUERGAR 6
#define RACE_OGRE 7
#define RACE_ORC 8
#define RACE_HALF_ELF 9
#define RACE_BARBARIAN 10
#define RACE_HALFLING 11
#define RACE_PLANT 12
#define RACE_HUMANOID 13
#define RACE_ANIMAL 14
#define RACE_DRAGON 15
#define RACE_GIANT 16
#define RACE_OTHER 17
#define RACE_GOBLIN 18
#define RACE_DEMON 19
#define RACE_BROWNIE 20

/* Make sure to update this number if you add a race. */
#define NUM_RACES 21

#define DEFAULT_RACE RACE_OTHER

/* Race-aligns */
#define RACE_ALIGN_UNKNOWN -1
#define RACE_ALIGN_GOOD 0
#define RACE_ALIGN_EVIL 1

#define NUM_RACE_SKILLS 10

/* This defines the basic attributes of a race. */

struct racedef {
    char *name;        /* The basic name, uncapitalized and uncolored. */
    char *names;       /* Additional names for searching purposes. */
    char *displayname; /* The name with colors and strategic capitalization. */
    char *fullname;    /* The long name with colors and capitalization */
    char *plainname;   /* The name with capitalization but no colors. */
    bool playable;     /* Available to mortals? */
    bool humanoid;     /* Is it humanoid? */
    int racealign;     /* Is it considered a good or evil race? */
    int def_size;      /* The default size for a member of this race. */
    int def_align;     /* Default alignment */
    int bonus_damroll;
    int bonus_hitroll;
    int def_lifeforce;   /* Default life force */
    int def_composition; /* Default composition */
    int mweight_lo;      /* Minimum weight (male) */
    int mweight_hi;      /* Maximum weight (male) */
    int mheight_lo;      /* Minimum height (male) */
    int mheight_hi;      /* Maximum height (male) */
    int fweight_lo;      /* Minimum weight (female) */
    int fweight_hi;      /* Maximum weight (female) */
    int fheight_lo;      /* Minimum height (female) */
    int fheight_hi;      /* Maximum height (female) */

    int attrib_scales[6];

    /* The following values primarily adjust stats on mob prototypes.  See db.c.
     */
    int exp_factor;
    int hit_factor;
    int hd_factor;
    int dice_factor;
    int copper_factor;
    int ac_factor;

    /* These may be NULL so that the default values of "enters" and "leaves"
     * will be used. */
    char *enter_verb;
    char *leave_verb;

    /*
     * The following data members should come last in the struct so
     * that they can be omitted when races are being defined.  Their
     * values are filled in init_races at runtime.
     */

    /* List of permanent effect flags */
    flagvector effect_flags[FLAGVECTOR_SIZE(NUM_EFF_FLAGS)];
    struct {
        int skill;             /* Skill number */
        int proficiency;       /* Default proficiency (can be ROLL_SKILL_PROF) */
    } skills[NUM_RACE_SKILLS]; /* List of racial skills */
};

extern struct racedef races[];
extern const char *race_align_abbrevs[];

#define GET_RACE(ch) ((ch)->player.race)
#define GET_RACE_ALIGN(ch) ((ch)->player.race_align)

#define VALID_RACENUM(num) (num < NUM_RACES)
#define VALID_RACE(ch) (VALID_RACENUM(GET_RACE(ch)))
#define RACE_ABBR(ch) (VALID_RACE(ch) ? races[(int)GET_RACE(ch)].displayname : "--")
#define RACE_FULL(ch) (VALID_RACE(ch) ? races[(int)GET_RACE(ch)].fullname : "--")
#define RACE_PLAINNAME(ch) (VALID_RACE(ch) ? races[(int)GET_RACE(ch)].plainname : "unknown")

#define RACE_ALIGN_ABBR(ch) (GET_RACE_ALIGN(ch) <= RACE_ALIGN_EVIL ? race_align_abbrevs[(int)GET_RACE_ALIGN(ch)] : 0)
#define IS_HUMANOID(ch) (VALID_RACE(ch) ? races[(int)GET_RACE(ch)].humanoid : FALSE)

#define ALIGN_OF_RACE(race) (VALID_RACENUM(race) ? races[race].racealign : RACE_ALIGN_GOOD)
#define SIZE_OF_RACE(race) (VALID_RACENUM(race) ? races[race].def_size : SIZE_MEDIUM)

#define RACE_EXPFACTOR(race) (VALID_RACENUM(race) ? races[race].exp_factor : 100)
#define RACE_HITFACTOR(race) (VALID_RACENUM(race) ? races[race].hit_factor : 100)
#define RACE_HDFACTOR(race) (VALID_RACENUM(race) ? races[race].hd_factor : 100)
#define RACE_DICEFACTOR(race) (VALID_RACENUM(race) ? races[race].dice_factor : 100)
#define RACE_COPPERFACTOR(race) (VALID_RACENUM(race) ? races[race].copper_factor : 100)
#define RACE_ACFACTOR(race) (VALID_RACENUM(race) ? races[race].ac_factor : 100)

void init_races(void);
int parse_race(struct char_data *ch, struct char_data *vict, char *arg);
int race_by_menu(char arg);
void send_race_menu(struct descriptor_data *d);
int interpret_race_selection(char arg);
void init_proto_race(struct char_data *ch);
void init_char_race(struct char_data *ch);
void update_char_race(struct char_data *ch);
int racial_skill_proficiency(int skill, int race, int level);
void convert_race(struct char_data *ch, int newrace);
void apply_racial_bonus_hit_and_dam(struct char_data *ch);
int natural_move(struct char_data *ch);
void scale_attribs(struct char_data *ch);

#endif

/***************************************************************************
 * $Log: races.h,v $
 * Revision 1.21  2009/03/16 09:44:38  jps
 * Added brownie race
 *
 * Revision 1.20  2008/06/21 17:28:56  jps
 * Added movement strings to race definitions. Changed some race macros
 * because the value is now an unsigned int.
 *
 * Revision 1.19  2008/04/26 23:35:43  myc
 * Info about permanent effects and race skills are stored in the
 * class/race structs now, but need to be initialized at runtime
 * by the init_races and init_classes functions.
 *
 * Revision 1.18  2008/03/26 16:44:36  jps
 * Replaced all checks for undead race with checks for undead lifeforce.
 * Replaced the undead race with the plant race.
 *
 * Revision 1.17  2008/03/22 21:43:55  jps
 * Add init_proto_race(), which is for setting default values on a
 * mob prototype according to race.  Probably during OLC.
 *
 * Revision 1.16  2008/03/22 21:23:50  jps
 * Add default life force and composition to race definitions.
 *
 * Revision 1.15  2008/03/21 15:01:17  myc
 * Removed languages.
 *
 * Revision 1.14  2008/03/11 02:13:25  jps
 * Moving size macros to chars.h.
 *
 ***************************************************************************/
