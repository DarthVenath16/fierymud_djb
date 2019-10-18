/***************************************************************************
 * $Id: act.movement.c,v 1.173 2011/03/16 13:39:58 myc Exp $
 ***************************************************************************/
/***************************************************************************
 *   File: act.movement.c                                 Part of FieryMUD *
 *  Usage: movement commands, door handling, & sleep/rest/etc state        *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  FieryMUD Copyright (C) 1998, 1999, 2000 by the Fiery Consortium        *
 *  FieryMUD is based on CircleMUD Copyright (C) 1993, 94 by the Trustees  *
 *  of the Johns Hopkins University                                        *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ***************************************************************************/

#include "conf.h"
#include "sysdep.h"
#include <math.h>

#include "structs.h"
#include "utils.h"
#include "constants.h"
#include "comm.h"
#include "interpreter.h"
#include "handler.h"
#include "db.h"
#include "casting.h"
#include "house.h"
#include "dg_scripts.h"
#include "weather.h"
#include "races.h"
#include "skills.h"
#include "events.h"
#include "math.h"
#include "screen.h"
#include "regen.h"
#include "exits.h"
#include "rooms.h"
#include "fight.h"
#include "magic.h"
#include "modify.h"
#include "act.h"
#include "movement.h"
#include "composition.h"
#include "charsize.h"
#include "board.h"
#include "directions.h"

/* external vars  */
extern int pk_allowed;

/* external functs */
int special(struct char_data *ch, int cmd, char *arg);
void appear(struct char_data *ch);
bool senses_living(struct char_data *ch, struct char_data *vict, int basepct); /* act.informative.c */

#define BLOOD_DROP_OBJ     34   /* the vnum of the blood object */
#define BLOOD_POOL_OBJ     35   /* the vnum of the blood object */

void spill_blood(struct char_data *ch) {
  struct obj_data *obj, *next;

  if (GET_HIT(ch) > (GET_MAX_HIT(ch) * 3) / 10)
    return;

  if (ch->in_room == NOWHERE)
    return;

  for (obj = world[ch->in_room].contents; obj; obj = next) {
    next = obj->next_content;
    if (GET_OBJ_VNUM(obj) == 34 || GET_OBJ_VNUM(obj) == 35)
      extract_obj(obj);
  }

  if (GET_HIT(ch) > GET_MAX_HIT(ch) / 10) {
    obj = read_object(BLOOD_POOL_OBJ, VIRTUAL);
    GET_OBJ_DECOMP(obj) = 3;
  }
  else {
    obj = read_object(BLOOD_DROP_OBJ, VIRTUAL);
    GET_OBJ_DECOMP(obj) = 2;
  }

  GET_OBJ_VAL(obj, 0) = ch->in_room;
  obj_to_room(obj, ch->in_room);
}

int do_misdirected_move(struct char_data *actor, int dir)
{
   struct char_data *people;
   char mmsg[MAX_STRING_LENGTH];
   char rmsg[MAX_STRING_LENGTH];

   sprintf(mmsg, "$n %s %s.",
         movewords(actor, dir, actor->in_room, TRUE), dirs[dir]);
   sprintf(rmsg, "$n's &5illusion &0%s %s.",
         movewords(actor, dir, actor->in_room, TRUE), dirs[dir]);

   LOOP_THRU_PEOPLE(people, actor) {
      if (actor == people) {
         sprintf(buf, "Your &5illusion&0 %s %s.\r\n",
            movewords(actor, dir, actor->in_room, TRUE), dirs[dir]);
         send_to_char(buf, actor);
      } else if (!AWAKE(people))
         ;
      else if (SEES_THROUGH_MISDIRECTION(people, actor))
         act(rmsg, FALSE, actor, 0, people, TO_VICT);
      else
         act(mmsg, FALSE, actor, 0, people, TO_VICT);
   }

   return 1;
}

bool try_to_sense_arrival(struct char_data *observer, struct char_data *mover)
{
   if (senses_living(observer, mover, 50)) {
      send_to_char("You sense that a living creature has arrived.\r\n", observer);
      return TRUE;
   } else {
      return FALSE;
   }
}


bool try_to_sense_departure(struct char_data *observer, struct char_data *mover)
{
   if (senses_living(observer, mover, 50)) {
      send_to_char("You feel that a living creature has departed.\r\n", observer);
      return TRUE;
   } else {
      return FALSE;
   }
}

void observe_char_leaving(struct char_data *observer, struct char_data *mover,
      struct char_data *mount, char *msg, int direction)
{
   if (observer == mover || observer == mount || !AWAKE(observer))
      return;

   if (observer == RIDING(mover)) {
      sprintf(buf, "%s you.", msg);
      act(buf, FALSE, mover, 0, observer, TO_VICT);
      return;
   }

   if (mount) {
      /* MOUNTED MOVEMENT */
      if (CAN_SEE(observer, mount) || CAN_SEE(observer, mover)) {
         sprintf(buf, "%s %s.", msg, CAN_SEE(observer, mount) ?
            GET_NAME(mount) : "something");
         act(buf, FALSE, mover, 0, observer, TO_VICT);
      } else if (CAN_SEE_BY_INFRA(observer, mover) || CAN_SEE_BY_INFRA(observer, mount)) {
         sprintf(buf, "&1&bA %s-sized creature rides %s on a %s mount.&0\r\n",
               SIZE_DESC(mover),
               dirs[direction],
               SIZE_DESC(mount));
         send_to_char(buf, observer);
      } else if (!try_to_sense_departure(observer, mount)) {
         /* Try sensing because it's too dark or mover is invis
          * (sneaking and misdirection do not apply when mounted) */
         try_to_sense_departure(observer, mover);
      }
   /* NO MOUNT */
   } else if (EFF_FLAGGED(mover, EFF_MISDIRECTING)) {
      if (SEES_THROUGH_MISDIRECTION(observer, mover))
         act(msg, FALSE, mover, 0, observer, TO_VICT);
   } else if (!CAN_SEE(observer, mover)) {
      if (CAN_SEE_BY_INFRA(observer, mover)) {
         sprintf(buf1, "&1&bA %s-sized creature leaves %s.&0\r\n",
                  SIZE_DESC(mover),
                  dirs[direction]);
         send_to_char(buf1, observer);
      } else {
         /* Try sensing because it's too dark, or mover is invis */
         try_to_sense_departure(observer, mover);
      }
   } else if (!IS_HIDDEN(mover) && !OUTDOOR_SNEAK(mover) && !EFF_FLAGGED(mover, EFF_SNEAK))
      act(msg, FALSE, mover, 0, observer, TO_VICT);
   else if (GET_LEVEL(observer) >= LVL_IMMORT ?
              GET_LEVEL(observer) >= GET_LEVEL(mover) :
              GET_PERCEPTION(observer) >= GET_HIDDENNESS(mover))
      act(msg, FALSE, mover, 0, observer, TO_VICT);
   else
      /* Try sensing because mover is sneaking */
      try_to_sense_departure(observer, mover);
}

void observe_char_arriving(struct char_data *observer, struct char_data *mover,
      struct char_data *mount, char *mountmsg, char *stdmsg, int direction)
{
   if (observer == mover || observer == mount || !AWAKE(observer))
      return;

   if (mount) {
      /* MOUNTED MOVEMENT */
      sprintf(buf2, "%s%s.", mountmsg,
         CAN_SEE(observer, mount) ? GET_NAME(mount) : "something");
      if (CAN_SEE(observer, mount) || CAN_SEE(observer, mover)) {
         act(buf2, FALSE, mover, 0, observer, TO_VICT);
      } else if (CAN_SEE_BY_INFRA(observer, mover) || CAN_SEE_BY_INFRA(observer, mount)) {
         sprintf(buf1, "&1&bA %s-sized creature arrives from %s%s, riding a %s mount.&0\r\n",
               SIZE_DESC(mover),
               (direction < UP  ? "the " : ""),
               (direction == UP ? "below": direction == DOWN ? "above" : dirs[rev_dir[direction]]),
               SIZE_DESC(mount));
         send_to_char(buf1, observer);
      } else if (!try_to_sense_arrival(observer, mount)) {
         try_to_sense_arrival(observer, mover);
      }
   /* NO MOUNT */
   } else if (!CAN_SEE(observer, mover)) {
      /* Invisibility or darkness - try to see by infra, or sense life. */
      if (CAN_SEE_BY_INFRA(observer, mover)) {
         sprintf(buf1, "&1&bA %s-sized creature arrives from %s%s.&0\r\n",
                  SIZE_DESC(mover),
                  (direction < UP  ? "the " : ""),
                  (direction == UP ? "below": direction == DOWN ? "above" : dirs[rev_dir[direction]]));
         send_to_char(buf1, observer);
      } else {
         try_to_sense_arrival(observer, mover);
      }
   } else if (!IS_HIDDEN(mover) && !OUTDOOR_SNEAK(mover) && !EFF_FLAGGED(mover, EFF_SNEAK))
      /* No invisibility, darkness, or sneaking - mover is visible */
      act(stdmsg, FALSE, mover, 0, observer, TO_VICT);
   else if (PRF_FLAGGED(observer, PRF_HOLYLIGHT) ?
            GET_LEVEL(observer) >= GET_LEVEL(mover) :
            GET_PERCEPTION(observer) >= GET_HIDDENNESS(mover))
      act(stdmsg, FALSE, mover, 0, observer, TO_VICT);
      /* Not visible by any standard means - try to sense life */
   else
      try_to_sense_arrival(observer, mover);
}

/* do_simple_move assumes
 *    1. That there is no master and no followers.
 *    2. That the direction exists.
 */

bool do_simple_move(struct char_data *ch, int dir, int need_specials_check)
{
   int flying = 0, levitating = 0;
   bool boat;
   int need_movement, vnum;
   char mmsg[MAX_STRING_LENGTH];
   char tmp[MAX_STRING_LENGTH];
   struct char_data *observer;

   /* Possible situations:
    *                             Actor           Motivator      Mount
    * 1. ch alone                 ch              ch             NULL
    * 2. ch mounted upon someone  ch              RIDING(ch)     RIDING(ch)
    * 3. ch ridden by someone     RIDDEN_BY(ch)   ch             ch
    *
    * actor      - the one who is *said* to be moving
    * motivator  - the one who is doing the work
    */
   struct char_data *actor = ch, *motivator = ch, *mount = NULL;
   int was_in = IN_ROOM(actor);

  /*
   * Check for special routines (North is 1 in command list, but 0 here) Note
   * -- only check if following; this avoids 'double spec-proc' bug
   */
   if (need_specials_check && special(ch, dir + 1, ""))
      return FALSE;

   /* Check in room afterwards in case there was a teleport in the trigger. */
   if (!leave_mtrigger(ch, dir) || IN_ROOM(ch) != was_in)
     return FALSE;
   if (!leave_wtrigger(&world[IN_ROOM(ch)], ch, dir) || IN_ROOM(ch) != was_in)
     return FALSE;
   if (!leave_otrigger(&world[IN_ROOM(ch)], ch, dir) || IN_ROOM(ch) != was_in)
     return FALSE;

   if (EVENT_FLAGGED(ch, EVENT_GRAVITY) && !EFF_FLAGGED(ch, EFF_LEVITATE)) {
      send_to_char("You're in free fall!\r\n", ch);
      return FALSE;
   }

   if (EFF_FLAGGED(ch, EFF_IMMOBILIZED)) {
      send_to_char("You're unable to move!\r\n", ch);
      return FALSE;
   }

   /* Work out who's the actor and who's the motivator */

   if (RIDING(ch)) {
      /* Sanity check - are you in the same room as your mount? */
      if (IN_ROOM(ch) == IN_ROOM(RIDING(ch)))
         motivator = RIDING(ch);
      else {
         sprintf(buf, "do_simple_move: %s is at %d, while mount %s is at %d",
               GET_NAME(ch), IN_ROOM(ch), GET_NAME(RIDING(ch)), IN_ROOM(RIDING(ch)));
         mudlog(buf, NRM, LVL_IMMORT, FALSE);
      }
      mount = motivator;
   } else if (RIDDEN_BY(ch)) {
      /* Sanity check - are you in the same room as your rider? */
      if (IN_ROOM(ch) == IN_ROOM(RIDDEN_BY(ch)))
         actor = RIDDEN_BY(ch);
      else {
         sprintf(buf, "do_simple_move: %s is at %d, while rider %s is at %d",
               GET_NAME(ch), IN_ROOM(ch), GET_NAME(RIDDEN_BY(ch)), IN_ROOM(RIDDEN_BY(ch)));
         mudlog(buf, NRM, LVL_IMMORT, FALSE);
      }
      mount = motivator;
   }

   /* Is mount standing and conscious? */
   if (mount) {
      if (GET_STANCE(mount) < STANCE_RESTING) {
         sprintf(buf, "You aren't riding $N anywhere while $E's %s.",
               stance_types[GET_STANCE(mount)]);
         act(buf, FALSE, actor, 0, mount, TO_CHAR);
         sprintf(buf, "$n tries to ride the %s $D.", stance_types[GET_STANCE(mount)]);
         act(buf, TRUE, actor, 0, mount, TO_ROOM);
         return FALSE;
      }
      if (GET_POS(mount) < POS_STANDING) {
         sprintf(buf, "You can't ride away on $N while $E's %s.",
               position_types[GET_POS(mount)]);
         act(buf, FALSE, actor, 0, mount, TO_CHAR);
         return FALSE;
      }
      if (GET_STANCE(mount) == STANCE_RESTING) {
         alter_pos(mount, GET_POS(mount), STANCE_ALERT);
         act("$n stops resting and looks ready to head out.", TRUE, mount, 0, 0, TO_ROOM);
      }
   }

   /* BOAT */
   boat = can_travel_on_water(actor) || can_travel_on_water(motivator);

   /* FLYING */
   flying =
      GET_POS(actor) == POS_FLYING ||
      GET_POS(motivator) == POS_FLYING ||
      GET_LEVEL(actor) >= LVL_IMMORT ||
      GET_LEVEL(motivator) >= LVL_IMMORT;

   /* LEVITATING */
   if (!flying &&
         (EFF_FLAGGED(actor, EFF_LEVITATE) ||
          EFF_FLAGGED(motivator, EFF_LEVITATE)))
      levitating = 1;

   /* Determine movement points needed */

   if (GET_LEVEL(motivator) >= LVL_IMMORT)
      need_movement = 0;
   else if (flying)
      need_movement = 1;
   else if (boat && SECT(motivator->in_room) == SECT_SHALLOWS)
      need_movement = 2;
   else
      need_movement = (sectors[SECT(CH_NDEST(motivator, dir))].mv
            + sectors[SECT(motivator->in_room)].mv) / 2;

   if (levitating && need_movement > 2)
      need_movement = 2;

   /* Do you have enough energy? */

   if (GET_MOVE(motivator) < need_movement) {
      if (need_specials_check && motivator->master)
         send_to_char("You are too exhausted to follow.\r\n", motivator);
      else
         send_to_char("You are too exhausted.\r\n", motivator);
      if (mount) {
         if (need_specials_check && motivator->master)
            send_to_char("Your mount is too exhausted to follow.\r\n", actor);
         else
            send_to_char("Your mount is too exhausted.\r\n", actor);
      }
      return FALSE;
   }

   /* Mobs with STAY_ZONE shouldn't wander outside their zone for any reason */
   if (MOB_FLAGGED(actor, MOB_STAY_ZONE) && CH_DEST_ZONE(actor, dir) != CH_ROOM(actor)->zone) {
     send_to_char("You feel a strong urge to stay within your zone!\r\n", actor);
     return FALSE;
   }

    /* Likewise, mobs shouldn't enter NOMOB or DEATH rooms */
    if (IS_MOB(actor) && (ROOM_FLAGGED(CH_NDEST(actor, dir), ROOM_NOMOB)
                          || ROOM_FLAGGED(CH_NDEST(actor, dir), ROOM_DEATH))) {
        send_to_char("You really don't want to go that way!\r\n", actor);
        return FALSE;
    }

   /* What to do if a mount attempts to wander */

   if (mount && ch != actor) {

      /* A tamed mount shouldn't wander off while you're mounted upon it */
      if (EFF_FLAGGED(ch, EFF_TAMED))
         send_to_char("You've been tamed.  Now act it!\r\n", ch);

      /* A non-tamed mount will attempt to buck instead of wandering, during a fight */
      else if (FIGHTING(actor) && movement_bucked(actor, mount)) {
         act("$N rears backwards, throwing you to the ground.",
               FALSE, actor, 0, mount, TO_CHAR);
         act("You rear backwards, throwing $n to the ground.",
               FALSE, actor, 0, mount, TO_VICT);
         act("$N rears backwards, throwing $n to the ground.",
               FALSE, actor, 0, mount, TO_NOTVICT);
         improve_skill(actor, SKILL_RIDING);
         damage(actor, actor, dice(1,6), -1);
         dismount_char(actor);
      }

      return FALSE;
   }

   /* If load 90% and over, bite the dust. */
   if (!mount && !flying && CURRENT_LOAD(actor) >= 9 && GET_LEVEL(actor) < LVL_IMMORT) {
      act("You stagger about, then fall under your heavy load.",
            FALSE, actor, 0, 0, TO_CHAR);
      act("$n staggers about, then collapses under $s heavy load.",
            TRUE, actor, 0, 0, TO_ROOM);
      alter_pos(actor, POS_SITTING, STANCE_ALERT);
      return FALSE;
   }

   /* Charmed critters can't wander off (unless animated) */
   if (EFF_FLAGGED(actor, EFF_CHARM) && !MOB_FLAGGED(actor, MOB_ANIMATED) &&
            actor->master && actor->in_room == actor->master->in_room) {
      send_to_char("The thought of leaving your master makes you weep.\r\n", actor);
      return FALSE;
   }

   /* check for a wall in room */
   if (wall_block_check(actor, motivator, dir))
      return FALSE;

   /* Fishes and the like can't enter land rooms. */
   if (MOB_FLAGGED(actor, MOB_AQUATIC) && !flying) {
     if (!IS_WATER(actor->in_room)) {
       send_to_char("There isn't enough water here to swim that way!\r\n", actor);
       act("$n flounders on the ground, gasping for air!", FALSE, actor, 0, 0, TO_ROOM);
       return FALSE;
     }
     if (!IS_WATER(CH_NDEST(actor, dir)) &&
         SECT(CH_NDEST(actor, dir)) != SECT_AIR) {
       send_to_char("You can't swim that way!\r\n", actor);
       return FALSE;
     }
   }
   else if (mount && MOB_FLAGGED(mount, MOB_AQUATIC) && !flying) {
     if (!IS_WATER(actor->in_room)) {
       send_to_char("There isn't enough water here to swim that way!\r\n", actor);
       act("$n flounders on the ground, gasping for air!", FALSE, mount, 0, 0, TO_ROOM);
       return FALSE;
     }
     if (!IS_WATER(CH_NDEST(actor, dir)) &&
         SECT(CH_NDEST(actor, dir)) != SECT_AIR) {
       act("$N can't swim that way!", FALSE, actor, 0, mount, TO_CHAR);
       return FALSE;
     }
   }

   if ((SECT(actor->in_room) == SECT_AIR) ||
         (SECT(CH_NDEST(actor, dir)) == SECT_AIR)) {
      if (!flying && dir == UP) {
         send_to_char("&7Try flapping your wings.&0\r\n", actor);
         return FALSE;
      }
   }

   /* We need either a boat or fly spell in water rooms */
   if ((SECT(actor->in_room) == SECT_WATER) ||
         (SECT(CH_NDEST(actor, dir)) == SECT_WATER)) {
      if (!boat && !flying) {
         send_to_char("You need a boat or wings to go there.\r\n", actor);
         return FALSE;
      }
   }

   /* Riding check for each move to see if the rider gets tossed */

   if (mount) {
      improve_skill(actor, SKILL_RIDING);
      if (movement_bucked(actor, mount)) {
         act("$N rears backwards, throwing you to the ground.",
               FALSE, actor, 0, mount, TO_CHAR);
         act("You rear backwards, throwing $n to the ground.",
               FALSE, actor, 0, mount, TO_VICT);
         act("$N rears backwards, throwing $n to the ground.",
               FALSE, actor, 0, mount, TO_NOTVICT);
         dismount_char(actor);
         damage(actor, actor, dice(1, 6), -1);
         return FALSE;
      }
   }

   vnum = CH_VDEST(actor, dir);

   if (ROOM_FLAGGED(CH_NROOM(actor), ROOM_ATRIUM)) {
      if (!House_can_enter(actor, vnum)) {
         send_to_char("That's private property -- no trespassing!\r\n", actor);
         return FALSE;
      }
   }

   if (GET_LEVEL(ch) < LVL_IMMORT) {
     if (ROOM_FLAGGED(CH_NDEST(actor, dir), ROOM_TUNNEL)) {
       if (mount) {
         send_to_char("There isn't enough room there, while mounted.\r\n", actor);
         return FALSE;
       }
       if (num_pc_in_room(CH_DEST(actor, dir)) > 1) {
         send_to_char("There isn't enough room there for more than one person!\r\n", actor);
         return FALSE;
       }
     }
   }

   /* see if an entry trigger disallows the move */
   if (!entry_mtrigger(actor, vnum))
      return FALSE;
   if (!preentry_wtrigger(CH_DEST(actor, dir), actor, dir))
      return FALSE;

   /* Don't allow people in god rooms (unless following a deity) */
   if (GET_LEVEL(ch) < LVL_IMMORT) {
      if (ch->master) {
         if (GET_LEVEL(ch->master) < LVL_IMMORT) {
            if (ROOM_FLAGGED(CH_NDEST(ch, dir), ROOM_GODROOM)) {
               send_to_char("&0&8A mysterious powerful force pushes you back.&0\r\n", ch);
               return FALSE;
            }
         } else {
            if (ch->master->in_room != CH_NDEST(ch, dir)) {
               if (ROOM_FLAGGED(CH_NDEST(ch, dir), ROOM_GODROOM)) {
                  send_to_char("&0&8A mysterious powerful force pushes you back.&0\r\n", ch);
                  return FALSE;
               }
            }
         }
      } else {
         if (ROOM_FLAGGED(CH_NDEST(ch, dir), ROOM_GODROOM)) {
            send_to_char("&0&8A mysterious powerful force pushes you back.&0\r\n", ch);
            return FALSE;
         }
      }
   }

   /* alter the need_movement depending on wind direction and speed */
   if (CH_OUTSIDE(motivator) &&
         (zone_table[world[motivator->in_room].zone]).wind_speed >= WIND_GALE) {
      if ((zone_table[world[motivator->in_room].zone]).wind_dir == (dir + 2) % 4) {
         need_movement *= 2;
         send_to_char("The wind beats hard upon your body as you forge ahead.\r\n", actor);
      }
   }

   /* If camouflaged and entering a building, snap into visibility */
   if (EFF_FLAGGED(ch, EFF_CAMOUFLAGED) && INDOORS(CH_NDEST(actor, dir))) {
     send_to_char("You reveal yourself as you step indoors.\r\n", ch);
     effect_from_char(ch, SPELL_NATURES_EMBRACE);
     GET_HIDDENNESS(ch) = 0;
   }

   alter_move(motivator, need_movement);

   if (mount) {
      sprintf(buf2, "You ride %s on %s.\r\n", dirs[dir], PERS(mount, actor));
      act(buf2, TRUE, actor, 0, 0, TO_CHAR);
      sprintf(mmsg, "$n rides %s on", dirs[dir]);
   } else {
      sprintf(mmsg, "$n %s %s.", movewords(actor, dir, actor->in_room, TRUE), dirs[dir]);
   }

   if (IS_HIDDEN(motivator) || EFF_FLAGGED(motivator, EFF_SNEAK)) {
     if (EFF_FLAGGED(motivator, EFF_SNEAK)) {
       if (!IS_NPC(motivator))
         GET_HIDDENNESS(motivator) = MAX(0, GET_HIDDENNESS(motivator) - number(2, 5));
     }
     /* Camouflage makes you lose minimal hide points per move */
     else if (EFF_FLAGGED(motivator, EFF_CAMOUFLAGED))
       GET_HIDDENNESS(motivator) = MAX(0, GET_HIDDENNESS(motivator) - number(3, 7));
     /* Chance for hiddenness to decrease with a bonus for dex apply
      * and for already sneaking. */
     else if (number(1, 101) > GET_SKILL(motivator, SKILL_SNEAK) +
         dex_app_skill[GET_DEX(motivator)].sneak + 15)
       GET_HIDDENNESS(motivator) = MAX(0, GET_HIDDENNESS(motivator) - GET_LEVEL(motivator) / 2);
     if (!IS_HIDDEN(motivator))
       effect_from_char(motivator, SPELL_NATURES_EMBRACE);
     if (GET_SKILL(motivator, SKILL_SNEAK))
       improve_skill(motivator, SKILL_SNEAK);
     if (GET_SKILL(motivator, SKILL_STEALTH))
       improve_skill(motivator, SKILL_STEALTH);
   }

   LOOP_THRU_PEOPLE(observer, actor) {
      observe_char_leaving(observer, actor, mount, mmsg, dir);
   }

   /* BLOOD TRAILS (pk only) */
   if (pk_allowed)
     spill_blood(actor);

   /* If the room is affected by a circle of fire, damage the person */
   if (ROOM_EFF_FLAGGED(actor->in_room, ROOM_EFF_CIRCLE_FIRE) &&
         !EFF_FLAGGED(actor, EFF_NEGATE_HEAT)) {
      if (GET_LEVEL(actor) < LVL_IMMORT) {
         mag_damage(GET_LEVEL(actor) + number(1, 10), actor, actor,
               SPELL_CIRCLE_OF_FIRE, SAVING_SPELL);
      }
      if (mount && GET_LEVEL(mount) < LVL_IMMORT) {
         mag_damage(GET_LEVEL(mount) + number(1, 10), mount, mount,
               SPELL_CIRCLE_OF_FIRE, SAVING_SPELL);
      }
      if (DECEASED(actor) || (mount && DECEASED(mount)))
         return FALSE;
   }

   /* At last, it is time to actually move */
   char_from_room(actor);
   char_to_room(actor, world[was_in].exits[dir]->to_room);
   if (mount) {
      char_from_room(mount);
      char_to_room(mount, actor->in_room);
      look_at_room(mount, TRUE);

      sprintf(tmp, "$n arrives from %s%s, riding ",
            (dir < UP  ? "the " : ""),
            (dir == UP ? "below": dir == DOWN ? "above" : dirs[rev_dir[dir]]));
   } else {
      sprintf(buf, "$n %s from %s%s.", movewords(actor, dir, actor->in_room, FALSE),
            (dir < UP  ? "the " : ""),
            (dir == UP ? "below": dir == DOWN ? "above" : dirs[rev_dir[dir]]));
   }

   LOOP_THRU_PEOPLE(observer, actor) {
      observe_char_arriving(observer, actor, mount, tmp, buf, dir);
   }

   /* If the room is affected by a circle of fire, damage the person */
   /* if it dies, don't do anything else */
   if (ROOM_EFF_FLAGGED(actor->in_room, ROOM_EFF_CIRCLE_FIRE) &&
         !EFF_FLAGGED(actor, EFF_NEGATE_HEAT)) {
      mag_damage(GET_LEVEL(actor) + number(1, 5), actor, actor,
            SPELL_CIRCLE_OF_FIRE, SAVING_SPELL);
      if (DECEASED(actor))
         return TRUE;
   }

   if (actor->desc != NULL)
      look_at_room(actor, FALSE);

   if (!greet_mtrigger(actor, dir)) {
      if (DECEASED(actor))
         return FALSE;
      char_from_room(actor);
      char_to_room(actor, was_in);
      look_at_room(actor, TRUE);
   }

   /* Check for post-entry triggers */

   postentry_wtrigger(actor, dir);
   /* Please leave this death check, in case anything substantive ever
    * gets added beyond this point. The trigger may kill the actor. */
   if (DECEASED(actor))
      return TRUE;

   return TRUE;
}


bool perform_move(struct char_data *ch, int dir, int need_specials_check, bool misdirection)
{
  room_num was_in, to_room;
  struct follow_type *k, *next;

  if (ch == NULL || dir < 0 || dir >= NUM_OF_DIRS)
    return 0;
  else if (!misdirection && !check_can_go(ch, dir, FALSE))
    return 0;
  else {
    if (!ch->followers) {
      if (misdirection)
        return do_misdirected_move(ch, dir);
      else
        return do_simple_move(ch, dir, need_specials_check);
    }

    /* Could the followers see the leader before he leaves? */
    for (k = ch->followers; k; k = k->next)
      if (CAN_SEE_MOVING(k->follower, ch))
        k->can_see_master = TRUE;
      else
        k->can_see_master = FALSE;

    was_in = ch->in_room;
    /* During misdirection, there might not even be an exit... */
    if (!CH_EXIT(ch, dir))
       to_room= NOWHERE;
    else
       to_room = CH_NDEST(ch, dir);

    if (misdirection)
      do_misdirected_move(ch, dir);
    else if (!do_simple_move(ch, dir, need_specials_check))
      /* Failed the move for some reason.  No one follows. */
      return 0;

      for (k = ch->followers; k; k = next) {
         next = k->next;
         if (k->follower->in_room != to_room &&
               k->follower->in_room == was_in &&
               GET_POS(k->follower) >= POS_STANDING &&
               !FIGHTING(k->follower) &&
               !CASTING(k->follower) &&
               k->can_see_master) {
            if (CONFUSED(k->follower) && number(1, 5) == 1) {
               act("&5You tried to follow $N&0&5, but got all turned around.&0",
                     FALSE, k->follower, 0, ch, TO_CHAR);
               perform_move(k->follower, number(0, 5), 1, FALSE);
               if (IN_ROOM(k->follower) != IN_ROOM(ch))
                  act("&3Oops!  $n&0&3 seems to have wandered off again.&0",
                        FALSE, k->follower, 0, ch, TO_VICT);
            } else {
               act("You follow $N.\r\n", FALSE, k->follower, 0, ch, TO_CHAR);
               perform_move(k->follower, dir, 1, FALSE);
            }
         }
      }
      return 1;
   }
   return 0;
}


ACMD(do_move)
{
  /*
   * This is basically a mapping of cmd numbers to perform_move indices.
   * It cannot be done in perform_move because perform_move is called
   * by other functions which do not require the remapping.
   */

  int misdir;

  one_argument(argument, arg);

  if (*arg && EFF_FLAGGED(ch, EFF_MISDIRECTION) &&
        subcmd >= SCMD_STAY && subcmd <= SCMD_DOWN) {

     if (RIDING(ch) || RIDDEN_BY(ch)) {
        send_to_char("You can't misdirect people while riding.\r\n", ch);
        return;
     }

     if (!strncmp("st", arg, 2)) {
        /* Misdirection is "stay" */
        misdir = -1;
     } else {
        misdir = parse_direction(arg);
        if (misdir < 0) {
           send_to_char("That isn't a direction.\r\n", ch);
           return;
        }
     }

     if (misdir > -1)
        perform_move(ch, misdir, 0, TRUE);
     SET_FLAG(EFF_FLAGS(ch), EFF_MISDIRECTING);
     /* Send confused people off in a random direction. */
     if (CONFUSED(ch) && number(0, 1) == 0) {
       send_to_char("&5You are confused!&0\r\n", ch);
       subcmd = SCMD_STAY + number(1, 6);
     }
     perform_move(ch, subcmd - 1, 0, FALSE);
     REMOVE_FLAG(EFF_FLAGS(ch), EFF_MISDIRECTING);
     return;
  }

  /* Send confused people off in a random direction. */
  if (CONFUSED(ch) && number(0, 1) == 0) {
    send_to_char("&5You are confused!&0\r\n", ch);
    subcmd = SCMD_STAY + number(1, 6);
  }

  switch (subcmd) {
    case SCMD_STAY:
       send_to_char("Ok, you stay put.\r\n", ch);
       break;
    case SCMD_NORTH:
    case SCMD_EAST:
    case SCMD_SOUTH:
    case SCMD_WEST:
    case SCMD_UP:
    case SCMD_DOWN:
      perform_move(ch, subcmd - 1, 0, FALSE);
      break;
    default:
      if (argument && *arg &&
          (subcmd = searchblock(arg, dirs, 0)) >= 0)
        perform_move(ch, subcmd, 0, FALSE);
      else
        send_to_char("Which way do you want to go?\r\n", ch);
  }
}


int find_door(struct char_data *ch, char *name, char *dirname, char *cmdname, int quiet)
{
   int dir;
   struct exit *exit;

   if (*dirname) {   /* a direction was specified */

      if ((dir = parse_direction(dirname)) == -1) {  /* Partial Match */
         if (!quiet)
            send_to_char("That's not a direction.\r\n", ch);
         return -1;
      }

      if ((exit = CH_EXIT(ch, dir)) && !EXIT_IS_HIDDEN(exit)) {
         if (exit_has_keyword(exit, name))
            return dir;
         else {
            if (!quiet) {
               sprintf(buf2, "I see no %s there.\r\n", name);
               send_to_char(buf2, ch);
            }
            return -1;
         }
      } else {
         if (!quiet)
            send_to_char("I really don't see how you can close anything there.\r\n", ch);
         return -1;
      }
   } else {            /* try to locate the keyword */
      if (!name || !*name) {
         if (!quiet) {
            sprintf(buf2, "What is it you want to %s?\r\n", cmdname);
            send_to_char(buf2, ch);
         }
         return -1;
      }
      for (dir = 0; dir < NUM_OF_DIRS; dir++) {
         if ((exit = CH_EXIT(ch, dir)) && !EXIT_IS_HIDDEN(exit) &&
               exit_has_keyword(exit, name))
            return dir;
      }
      if (!quiet) {
         sprintf(buf2, "There doesn't seem to be %s %s here.\r\n", AN(name), name);
         send_to_char(buf2, ch);
      }
      return -1;
   }
}


struct obj_data *carried_key(struct char_data *ch, int keyvnum)
{
  struct obj_data *o;

  if (keyvnum < 0 || !ch) return NULL;

  for (o = ch->carrying; o; o = o->next_content)
    if (GET_OBJ_VNUM(o) == keyvnum)
      return o;

  if (GET_EQ(ch, WEAR_HOLD))
    if (GET_OBJ_VNUM(GET_EQ(ch, WEAR_HOLD)) == keyvnum)
      return GET_EQ(ch, WEAR_HOLD);

  if (GET_EQ(ch, WEAR_HOLD2))
    if (GET_OBJ_VNUM(GET_EQ(ch, WEAR_HOLD2)) == keyvnum)
      return GET_EQ(ch, WEAR_HOLD2);

  return NULL;
}

char *cmd_door[] =
{
  "open",
  "close",
  "unlock",
  "lock",
  "pick"
};


/* For the following four functions - for opening, closing, locking, and
 * unlocking objects - it is assumed that the object is a closeable
 * container. The caller should have verified this. */

void open_object(struct char_data *ch, struct obj_data *obj, bool quiet)
{
   /* The object must:
    *
    * 1. Be closed
    * 2. Be unlocked
    */

   if (OBJ_IS_OPEN(obj)) {
      if (!quiet && ch)
         send_to_char("It's already open.\r\n", ch);
      return;
   }

   if (!OBJ_IS_UNLOCKED(obj)) {
      if (!quiet && ch) {
         send_to_char("It seems to be locked.\r\n", ch);
         act("$n tries to open $p, but it's locked.",
               FALSE, ch, obj, 0, TO_ROOM);
      }
      return;
   }

   /* Success. */

   REMOVE_BIT(GET_OBJ_VAL(obj, VAL_CONTAINER_BITS), CONT_CLOSED);

   /* Feedback. */

   if (ch && !quiet) {
      act("$n opens $p.", FALSE, ch, obj, 0, TO_ROOM);
      send_to_char(OK, ch);
   }
}

void close_object(struct char_data *ch, struct obj_data *obj, bool quiet)
{
   /* The object must:
    *
    * 1. Be open
    */

   if (!OBJ_IS_OPEN(obj)) {
      if (!quiet && ch)
         send_to_char("It's already closed.\r\n", ch);
      return;
   }

   /* Success. */

   SET_BIT(GET_OBJ_VAL(obj, VAL_CONTAINER_BITS), CONT_CLOSED);

   /* Feedback. */

   if (ch && !quiet) {
      act("$n closes $p.", FALSE, ch, obj, 0, TO_ROOM);
      send_to_char(OK, ch);
   }
}

void unlock_object(struct char_data *ch, struct obj_data *obj, bool quiet)
{
   int keyvnum;
   struct obj_data *key = NULL;

   /* The object must:
    *
    * 1. Be closed
    * 2. Be locked
    */

   if (OBJ_IS_OPEN(obj)) {
      if (!quiet && ch)
         send_to_char("It's already open!\r\n", ch);
      return;
   }

   /* If you aren't a god, you may need a key. */

   if (ch && GET_LEVEL(ch) < LVL_IMMORT) {
      keyvnum = GET_OBJ_VAL(obj, VAL_CONTAINER_KEY);
      if (keyvnum < 0) {
         if (!quiet)
            send_to_char("Odd - you can't seem to find a keyhole.\r\n", ch);
         return;
      }
      if (!(key = carried_key(ch, keyvnum))) {
         if (!quiet)
            send_to_char("You don't seem to have the proper key.\r\n", ch);
         return;
      }
   }

   /* Now that the key is known to be in hand, see whether the object
    * is already unlocked. */

   if (OBJ_IS_UNLOCKED(obj)) {
      if (!quiet && ch) {
         if (key) {
            act("You insert $P, only to find that $p isn't locked.",
                  FALSE, ch, obj, key, TO_CHAR);
            act("$n inserts $P into $p, only to find that it isn't locked.",
                  FALSE, ch, obj, key, TO_ROOM);
         } else {
            send_to_char("Oh... it wasn't locked.\r\n", ch);
         }
      }
      return;
   }

   /* Success. */

   REMOVE_BIT(GET_OBJ_VAL(obj, VAL_CONTAINER_BITS), CONT_LOCKED);

   /* Feedback. */

   if (ch && !quiet) {
      if (key) {
         act("$n unlocks $p with $P.", FALSE, ch, obj, key, TO_ROOM);
         act("You unlock $p with $P.", FALSE, ch, obj, key, TO_CHAR);
      } else {
         act("$n unlocks $p.", FALSE, ch, obj, 0, TO_ROOM);
         act("You unlock $p.", FALSE, ch, obj, 0, TO_CHAR);
      }
   }
}

void lock_object(struct char_data *ch, struct obj_data *obj, bool quiet)
{
   int keyvnum;
   struct obj_data *key = NULL;

   /* The object must:
    *
    * 1. Be closed
    * 2. Be unlocked
    */

   if (OBJ_IS_OPEN(obj)) {
      if (!quiet && ch)
         send_to_char("You'll need to close it first.\r\n", ch);
      return;
   }

   /* If you aren't a god, you may need a key. */

   if (ch && GET_LEVEL(ch) < LVL_IMMORT) {
      keyvnum = GET_OBJ_VAL(obj, VAL_CONTAINER_KEY);
      if (keyvnum < 0) {
         if (!quiet)
            send_to_char("Odd - you can't seem to find a keyhole.\r\n", ch);
         return;
      }
      if (!(key = carried_key(ch, keyvnum))) {
         if (!quiet)
            send_to_char("You don't seem to have the proper key.\r\n", ch);
         return;
      }
   }

   /* Now that the key is known to be in hand, see whether the object
    * is already locked. */

   if (!OBJ_IS_UNLOCKED(obj)) {
      if (!quiet && ch) {
         if (key) {
            act("You insert $P, only to find that $p is already locked.",
                  FALSE, ch, obj, key, TO_CHAR);
            act("$n inserts $P into $p, only to find that it's already locked.",
                  FALSE, ch, obj, key, TO_ROOM);
         } else {
            send_to_char("It seems to be locked already.\r\n", ch);
         }
      }
      return;
   }

   /* Success. */

   SET_BIT(GET_OBJ_VAL(obj, VAL_CONTAINER_BITS), CONT_LOCKED);

   /* Feedback. */

   if (ch && !quiet) {
      if (key) {
         act("$n inserts $P into $p and locks it.", FALSE, ch, obj, key, TO_ROOM);
         act("You insert $P into $p and lock it.", FALSE, ch, obj, key, TO_CHAR);
      } else {
         act("$n locks $p.", FALSE, ch, obj, 0, TO_ROOM);
         act("You lock $p.", FALSE, ch, obj, 0, TO_CHAR);
      }
   }
}

void pick_object(struct char_data *ch, struct obj_data *obj)
{
   struct obj_data *key = NULL;

   if (!ch) {
      mudlog("SYSERR: pick_object() called with no actor", BRF, LVL_GOD, FALSE);
      return;
   }

   /* The object must:
    *
    * 1. Be closed
    * 2. Be locked
    * 3. Be pickable
    */

   if (OBJ_IS_OPEN(obj)) {
      send_to_char("It's already open!\r\n", ch);
      return;
   }

   if (OBJ_IS_UNLOCKED(obj)) {
      send_to_char("Oh... it wasn't locked, after all.\r\n", ch);
      act("$n sets to picking $p, but soon realizes that it isn't locked.",
            FALSE, ch, obj, 0, TO_ROOM);
      return;
   }

   if (OBJ_IS_PICKPROOF(obj)) {
      send_to_char("It resists your attempts to pick it.\r\n", ch);
      act("$n sets to picking $p, but soon gives up.",
            FALSE, ch, obj, 0, TO_ROOM);
      return;
   }

   /* Try your skill. */

   if (number(1, 101) > GET_SKILL(ch, SKILL_PICK_LOCK)) {
      send_to_char("You failed to pick the lock.\r\n", ch);
      improve_skill(ch, SKILL_PICK_LOCK);
      return;
   }

   /* Success. */

   REMOVE_BIT(GET_OBJ_VAL(obj, VAL_CONTAINER_BITS), CONT_LOCKED);

   /* Feedback. */

   act("$n skillfully picks the lock on $p.", FALSE, ch, obj, key, TO_ROOM);
   send_to_char("You skillfully pick the lock.\r\n", ch);
}

void perform_door_action(struct char_data *ch, int subcmd, int door)
{
   if (!SOLIDCHAR(ch) && GET_LEVEL(ch) < LVL_IMMORT) {
      cprintf(ch, "You can't operate doors in your %s%s&0 state.\r\n",
               COMPOSITION_COLOR(ch), COMPOSITION_ADJECTIVE(ch));
      return;
   }

   if (!door_mtrigger(ch, subcmd, door) || !door_wtrigger(ch, subcmd, door))
      return;

   switch (subcmd) {
      case SCMD_OPEN:
         open_door(ch, CH_NROOM(ch), door, FALSE);
         break;
      case SCMD_CLOSE:
         close_door(ch, CH_NROOM(ch), door, FALSE);
         break;
      case SCMD_UNLOCK:
         unlock_door(ch, CH_NROOM(ch), door, FALSE);
         break;
      case SCMD_LOCK:
         lock_door(ch, CH_NROOM(ch), door, FALSE);
         break;
      case SCMD_PICK:
         pick_door(ch, CH_NROOM(ch), door);
         break;
      default:
         sprintf(buf, "SYSERR: perform_door_action() called with invalid subcommand %d",
               subcmd);
         mudlog(buf, BRF, LVL_GOD, FALSE);
         cprintf(ch, "Sorry, there's been an internal error.\r\n");
         break;
   }
}

ACMD(do_gen_door)
{
   int door = -1, bits;
   char type[MAX_INPUT_LENGTH], dir[MAX_INPUT_LENGTH];
   struct obj_data *obj = NULL;
   struct char_data *victim = NULL;

   if (FIGHTING(ch)) {
      send_to_char("You are too busy fighting to concentrate on that right now.\r\n",
               ch);
      return;
   }

   skip_spaces(&argument);
   if (!*argument) {
      sprintf(buf, "%s what?\r\n", cmd_door[subcmd]);
      send_to_char(CAP(buf), ch);
      return;
   }

   two_arguments(argument, type, dir);

   /* Identify the thing to be manipulated.
    * If a direction is specified, or no such object could be found, we'll
    * try to find a door. */

   if (*dir ||
            !(bits =
                generic_find(type, FIND_OBJ_EQUIP |
                                   FIND_OBJ_INV |
                                   FIND_OBJ_ROOM,
                             ch, &victim, &obj))) {
      door = find_door(ch, type, dir, cmd_door[subcmd], FALSE);
      if (door > -1) perform_door_action(ch, subcmd, door);
      return;
   }

   if (!obj) return;

   /* If you've named a board and you're trying to lock/unlock
    * it, see if you're allowed to do so. */
   if (GET_OBJ_TYPE(obj) == ITEM_BOARD && 
       (subcmd == SCMD_LOCK || subcmd == SCMD_UNLOCK)) {
     struct board_data *brd = board(GET_OBJ_VAL(obj, VAL_BOARD_NUMBER));
     if (!brd || !has_board_privilege(ch, brd, BPRIV_LOCK))
       send_to_char("You can't figure out how.\r\n", ch);
     else if (subcmd == SCMD_LOCK) {
       if (brd->locked)
         cprintf(ch, "%s is already locked.\r\n", obj->short_description);
       else
         cprintf(ch, "You lock %s, preventing others from posting.\r\n", obj->short_description);
       brd->locked = TRUE;
     }
     else {
       if (brd->locked)
         cprintf(ch, "You unlock %s, allowing others to post.\r\n", obj->short_description);
       else
         cprintf(ch, "%s is already unlocked.\r\n", obj->short_description);
       brd->locked = FALSE;
     }
     return;
   }

   /* If you've named an object that doesn't open/close, you might have
    * intended to manipulate a door. */

   if (!OBJ_IS_OPENABLE(obj)) {
      door = find_door(ch, type, dir, cmd_door[subcmd], TRUE);
      if (door >= 0) {
         perform_door_action(ch, subcmd, door);
      } else {
         cprintf(ch, "You can't %s that.\r\n", cmd_door[subcmd]);
      }
      return;
   }

   /* If you're made of fluid, the only way you can open/close/etc. something
    * is if it's an object in your inventory. */
   if (!SOLIDCHAR(ch) && obj->carried_by != ch && GET_LEVEL(ch) < LVL_IMMORT) {
      cprintf(ch, "You can't manipulate solid objects in your %s%s&0 state.\r\n",
               COMPOSITION_COLOR(ch), COMPOSITION_ADJECTIVE(ch));
      return;
   }

   switch (subcmd) {
      case SCMD_OPEN:
         open_object(ch, obj, FALSE);
         break;
      case SCMD_CLOSE:
         close_object(ch, obj, FALSE);
         break;
      case SCMD_UNLOCK:
         unlock_object(ch, obj, FALSE);
         break;
      case SCMD_LOCK:
         lock_object(ch, obj, FALSE);
         break;
      case SCMD_PICK:
         pick_object(ch, obj);
         break;
      default:
         sprintf(buf, "SYSERR: do_gen_door() called with invalid subcommand %d",
               subcmd);
         mudlog(buf, BRF, LVL_GOD, FALSE);
         cprintf(ch, "Sorry, there's been an internal error.\r\n");
         break;
   }
}

const char *portal_entry_messages[] = {
  "&8&b$p &0&b&8flares white as $n enters it and disappears.&0\r\n",
  "&8&b$p &0&b&8flares as $n enters it and disappears.&0\r\n",
  "&8&b$p &0&b&8vibrates violently as $n enters it and then stops.&0\r\n",
  "\n",
};

const char *portal_character_messages[] = {
  "",
  "&8&bYou feel your body being ripped apart!&0\r\n",
  "&8&b$p &0&b&8vibrates violently as you enter.&0\r\n",
  "&8&bYour molecules are ripped apart as you enter $p.&0\r\n",
  "&8&bYou appear in a completely different location!&0\r\n",
  "&9&bYou feel your energy being drained!&0\r\n",
  "&8&bYour molecules are ripped apart as you enter $p.&0\r\n\r\n"
    "&8&bYou catch a glimpse of a giant white leopard!&0\r\n\r\n"
    "&9&bYou feel your energy being drained!&0\r\n",
  "\n",
};

const char *portal_exit_messages[] = {
  "$p flares white as $n emerges from it.\r\n",
  "$p flares as $n emerges from it.\r\n",
  "$n appears from nowhere!\r\n",
  "There is a loud POP sound as $n emerges from $p.\r\n",
  "\n",
};

ACMD(do_enter)
{
  struct obj_data *obj = NULL;
  int i, rnum;

  if (FIGHTING(ch)) {
    send_to_char("You are too busy fighting to leave right now.\r\n",ch);
    return;
  }

  one_argument(argument, arg);

  /* Enter without args: try to enter an adjacent building. */
  if (!*arg) {
    if (ROOM_FLAGGED(CH_NROOM(ch), ROOM_INDOORS))
      send_to_char("You are already indoors.\r\n", ch);
    else
      for (i = 0; i < NUM_OF_DIRS; ++i)
        if (CH_EXIT(ch, i) &&
              !EXIT_IS_CLOSED(CH_EXIT(ch, i)) &&
            ROOM_FLAGGED(CH_NDEST(ch, i), ROOM_INDOORS)) {
          if (CONFUSED(ch)) {
             send_to_char("&5You are confused!&0\r\n", ch);
             i = number(0, 5);
          }
          perform_move(ch, i, 1, FALSE);
          return;
        }
    send_to_char("You can't seem to find anything to enter.\r\n", ch);
    return;
  }

  if (!(obj = find_obj_in_list(world[ch->in_room].contents, find_vis_by_name(ch, arg)))) {
    sprintf(buf, "There is no %s here.\r\n", arg);
    send_to_char(buf, ch);
    return;
  }

  if (GET_OBJ_TYPE(obj) != ITEM_PORTAL) {
    send_to_char("You can't enter that!\r\n", ch);
    return;
  }

  if (GET_OBJ_LEVEL(obj) > GET_LEVEL(ch)) {
    act("You are not experienced enough to use $p.", FALSE, ch, obj, 0, TO_CHAR);
    return;
  }

  if ((rnum = real_room(GET_OBJ_VAL(obj, VAL_PORTAL_DESTINATION))) == NOWHERE) {
    act("$p flares for a second then dies down.&0", FALSE, ch, obj, 0, TO_ROOM);
    send_to_char("It flares and then dies down - it must be broken!\r\n", ch);
    return;
  }

  /* Some special cases */
  if (GET_OBJ_VNUM(obj) == OBJ_VNUM_HEAVENSGATE) {
    if (GET_ALIGNMENT(ch) < 500 && GET_ALIGNMENT(ch) > -500) {
      act("&0Upon attempting to enter $p&0, you are pushed back by a powerful force.", FALSE, ch, obj, 0, TO_CHAR);
      act("&0Upon attempting to enter $p&0, $n&0 is pushed back by a powerful force.", TRUE, ch, obj, 0, TO_ROOM);
      return;
    }
    else if (GET_ALIGNMENT(ch) <= -500) {
      act("&9&bUpon entering $p&9&b, you begin to &1burn&9... Your screams can be heard throughtout the &7heavens&9...&0", FALSE, ch, obj, 0, TO_CHAR);
      act("&9&b$n&9&b enters $p&9&b... Following a &0&1blood&9&b curdling scream that spans the &7heavens&9, $n&9&b flails back out of the tunnel, &1&bon fire!&0", FALSE, ch, obj, 0, TO_ROOM);
      damage(ch, ch, abs(GET_ALIGNMENT(ch)) / 10, TYPE_SUFFERING);
      SET_FLAG(EFF_FLAGS(ch), EFF_ON_FIRE);
      return;
    }
  }
  else if (GET_OBJ_VNUM(obj) == OBJ_VNUM_HELLGATE) {
    if (GET_ALIGNMENT(ch) < 500 && GET_ALIGNMENT(ch) > -500) {
      act("&0Upon attempting to enter $p&0, you are pushed back by a powerful force.", FALSE, ch, obj, 0, TO_CHAR);
      act("&0Upon attempting to enter $p&0, $n&0 is pushed back by a powerful force.", TRUE, ch, obj, 0, TO_ROOM);
      return;
    }
    else if (GET_ALIGNMENT(ch) >= 500) {
      act("&9&bUpon catching a glimpse of &1hell&9 itself, your &0&5mind&9&b twists and distorts... &0", FALSE, ch, obj, 0, TO_CHAR);
      act("&9&b$n&9&b enters $p&9&b... Strange moaning sounds can be heard as $n&9&b wanders back out, twitching and drooling on $mself.&0", FALSE, ch, obj, 0, TO_ROOM);
      damage(ch, ch, abs(GET_ALIGNMENT(ch)) /10, TYPE_SUFFERING);
      mag_affect(70, ch, ch, SPELL_INSANITY, SAVING_SPELL, CAST_SPELL);
      mag_affect(70, ch, ch, SPELL_DISEASE, SAVING_SPELL, CAST_SPELL);
      return;
    }
  }

  /* Display entry message. */
  if (GET_OBJ_VAL(obj, VAL_PORTAL_ENTRY_MSG) >= 0) {
    for (i = 0; i < GET_OBJ_VAL(obj, VAL_PORTAL_ENTRY_MSG) &&
         *portal_entry_messages[i] != '\n' ; ++i);
    if (*portal_entry_messages[i] != '\n')
      act(portal_entry_messages[i], TRUE, ch, obj, 0, TO_ROOM);
  }

  /* Display character message. */
  if (GET_OBJ_VAL(obj, 2) >= VAL_PORTAL_CHAR_MSG) {
    for (i = 0; i < GET_OBJ_VAL(obj, VAL_PORTAL_CHAR_MSG) &&
         *portal_character_messages[i] != '\n'; ++i);
    if (*portal_character_messages[i] != '\n')
      act(portal_character_messages[i], TRUE, ch, obj, 0, TO_CHAR);
  }

  char_from_room(ch);
  char_to_room(ch, rnum);

  /* Display exit message. */
  if (GET_OBJ_VAL(obj, VAL_PORTAL_EXIT_MSG) >= 0) {
    for (i = 0; i < GET_OBJ_VAL(obj, VAL_PORTAL_EXIT_MSG) &&
         *portal_exit_messages[i] != '\n'; ++i);
    if (*portal_exit_messages[i] != '\n')
      act(portal_exit_messages[i], TRUE, ch, obj, 0, TO_ROOM);
  }

  look_at_room(ch, TRUE);

  if (RIDING(ch)) {
    char_from_room(RIDING(ch));
    char_to_room(RIDING(ch), rnum);
    look_at_room(RIDING(ch), TRUE);
  }
}


ACMD(do_leave)
{
  int door;

  if (FIGHTING(ch)) {
    send_to_char("You are too busy fighting to leave!\r\n",ch);
    return;
  }
  if (!ROOM_FLAGGED(CH_NROOM(ch), ROOM_INDOORS))
    send_to_char("You are outside.. where do you want to go?\r\n", ch);
  else {
    for (door = 0; door < NUM_OF_DIRS; door++)
      if (CH_NDEST(ch, door) != NOWHERE)
        if (!EXIT_IS_CLOSED(CH_EXIT(ch, door)) &&
            !ROOM_FLAGGED(CH_NDEST(ch, door), ROOM_INDOORS)) {
          if (CONFUSED(ch)) {
            send_to_char("&5You are confused!&0\r\n", ch);
            door = number(0, 5);
          }
        perform_move(ch, door, 1, FALSE);
        return;
      }
    send_to_char("I see no obvious exits to the outside.\r\n", ch);
  }
}

ACMD(do_doorbash)
#define EXITK(room, dir) (world[room].exits[dir])
#define OPEN_DOORK(room, door) (TOGGLE_BIT(EXITK(room, door)->exit_info,EX_CLOSED))
{
   char arg[MAX_INPUT_LENGTH];
   int dir = 0, chance, probability, dam;
   struct exit *exit;
   struct room_data *dest;
   room_num ndest;
   int was_in = IN_ROOM(ch);

   chance = number(0, 101);

   if (GET_LEVEL(ch) < LVL_IMMORT) {
      if (FIGHTING(ch)) {
         send_to_char("You can't take time out to do that just yet!\r\n",ch);
         return ;
      }
      if (GET_SIZE(ch) < SIZE_LARGE) {
         send_to_char("&0You do not feel massive enough!&0\r\n", ch);
         return;
      }
      if (!GET_SKILL(ch, SKILL_DOORBASH)) {
         send_to_char("You don't have that skill.", ch);
         return;
      }
      probability = chance + GET_LEVEL(ch);
   } else {
      probability = 101;
   }

   one_argument(argument, arg);
   if (!*arg) {
      cprintf(ch, "What direction?\r\n");
      return;
   }

   if ((dir = parse_direction(arg)) < 0) {
      cprintf(ch, "That isn't a valid direction.\r\n");
      return;
   }

   if (CONFUSED(ch) && number(0, 1) == 0) {
       send_to_char("&5You are confused!&0\r\n", ch);
       dir = number(0, 5);
   }

   /* There can be walls even where there aren't exits.  So now, prior to
    * checking for an exit, we check to see if you're bashing a wall. */

   /* Returns true if you ran into a wall, in which case our work here is done. */
   if (wall_charge_check(ch, dir)) return;

   exit = CH_EXIT(ch, dir);
   if (!exit || EXIT_NDEST(exit) == NOWHERE || EXIT_IS_HIDDEN(exit)) {
      cprintf(ch, "Even YOU couldn't bash through that!\r\n");
      return;
   }

   dest = EXIT_DEST(exit);
   ndest = EXIT_NDEST(exit);

   /* Is the way open already? */
   if (EXIT_IS_OPEN(exit)) {
      cprintf(ch, "There is no obstruction!\r\n");
      return;
   }

   /* Is the exit broken? */
   if (exit->keyword == NULL) {
      mprintf(L_WARN, LVL_GOD,
            "SYSERR:act.item.c:do_doorbash():A one sided door in room %d",
            world[ch->in_room].vnum);
      cprintf(ch, "This exit seems broken.   Please tell a god.\r\n");
      return;
   } else if (!EXIT_IS_DOOR(exit)) {
      mprintf(L_WARN, LVL_GOD,
            "SYSERR:act.item.c:do_doorbash():A closed nondoor exit in room %d",
            world[ch->in_room].vnum);
      cprintf(ch, "This exit seems broken.   Please tell a god.\r\n");
      return;
   }

   /* Established:
    *  - There is an exit that way
    *  - It goes to a room
    *  - It is not hidden
    *  - It is not broken
    *  - It is closed or locked (distinct in this mud...)
    */

   if (EXIT_IS_LOCKED(exit) && (EXIT_IS_PICKPROOF(exit) || chance < 80)) {
      /* You failed, or it was unbashable. */
      cprintf(ch, "You CHARGE at the %s &0but merely bounce off!&0\r\n",
            exit_name(exit));
      sprintf(buf, "$n &0CHARGES at the %s&0 and literally bounces off!",
            exit_name(exit));
      act(buf, FALSE, ch, 0, 0, TO_ROOM);

      if (GET_LEVEL(ch) < LVL_IMMORT) {
         /* You're going to get hurt... */
         dam = ((chance / 10) * (GET_LEVEL(ch) / 10)) + GET_LEVEL(ch);
         /* But you won't die... */
         if (GET_HIT(ch) - dam < -5)
             dam = GET_HIT(ch) + 5;
         hurt_char(ch, NULL, dam, TRUE);
         /* You fell to a sitting position (unless you were knocked out) */
         if (GET_POS(ch) >= POS_STANDING)
            alter_pos(ch, POS_SITTING, STANCE_ALERT);
         WAIT_STATE(ch, PULSE_VIOLENCE * 3);
      }

      room_printf(ndest, "There is a *CRASH* and %s shudders a bit.\r\n",
            exit_name(exit));
      return;
   }

   /* Success */
   OPEN_DOORK(ch->in_room, dir);

   sprintf(buf, "&0$n &0*CRASHES* through the %s&0!", exit_name(exit));
   act(buf, FALSE, ch, 0, 0, TO_ROOM);
   sprintf(buf, "&0You *CHARGE* at the %s &0and crash through it!&0\r\n",
         exit_name(exit));
   send_to_char(buf, ch);

   if (dest->exits[rev_dir[dir]]) {
      OPEN_DOORK(ndest, rev_dir[dir]);
   }

   /* This next bit is tricky. I wans to call perform_move so it will handle
    * movement points, circle of fire, etc., but I need to send a message
    * WITH this guy as the subject first. */

   /* The things we do to send a message to the destination room... */
   char_from_room(ch);
   char_to_room(ch, ndest);
   sprintf(buf, "&b&8Splinters and dust fly as $n &0&b&8*CRASHES* into the room!&0");
   act(buf, FALSE, ch, 0, 0, TO_ROOM);
   char_from_room(ch);
   char_to_room(ch, was_in);

   /* Wish this wouldn't send a message, but there would be significant coding to
    * replicate the movement here */
   perform_move(ch, dir, 1, FALSE);
   WAIT_STATE(ch, PULSE_VIOLENCE);
}

void char_drag_entry(struct char_data *ch, struct char_data *vict)
{
   act("&3$n&3 drags $N&3 behind $m.&0", TRUE, ch, 0, vict, TO_NOTVICT);

   /* Get awakened by being dragged? */
   if (GET_STANCE(vict) == STANCE_SLEEPING && !EFF_FLAGGED(vict, EFF_SLEEP)) {
      GET_STANCE(vict) = STANCE_ALERT;

      act("&3You are rudely awakened, and discover $N&3 dragging you along!",
         FALSE, vict, 0, ch, TO_CHAR);
      act("&3$n&3 awakes from $s slumber and looks around.&0", TRUE, vict, 0, 0, TO_ROOM);
   }

   if (AWAKE(vict))
      look_at_room(vict, FALSE);
   else
      send_to_char("Your dreams grow bumpy, as if someone were dragging you...\r\n",
      vict);
}

ACMD(do_drag)
{
  int from_room, to_room, move_cost, found, dir;
  struct char_data *tch = NULL;
  struct obj_data *tobj = NULL;
  struct obj_data *portal = NULL;

  argument = one_argument(argument, arg);
  skip_spaces(&argument);

  if (!*arg || !*argument) {
    send_to_char("Drag what? Where?\r\n", ch);
    return;
  }

  if (GET_POS(ch) != POS_STANDING) {
    send_to_char("You don't have the proper leverage to do that.  Try standing.\r\n", ch);
    return;
  }

  /*
   * Start by figuring out what (or whom) we're dragging.
   */
  if (!(found = generic_find(arg, FIND_OBJ_ROOM | FIND_CHAR_ROOM, ch, &tch, &tobj))) {
    send_to_char("Can't find that!\r\n", ch);
    return;
  }

  /* If you're made of fluid, you can only drag a similar person */
  /* (we don't do illusory here because that would give it away
   * also, you might think they're substantial-feeling because that's
   * part of the illusion) */
  if (tobj && !RIGID(ch) && GET_LEVEL(ch) < LVL_IMMORT) {
     sprintf(buf, "You can't handle solid objects in your %s%s&0 condition.\r\n",
           COMPOSITION_COLOR(ch), COMPOSITION_ADJECTIVE(ch));
     send_to_char(buf, ch);
     return;
  } else if (tch && GET_LEVEL(ch) < LVL_IMMORT) {
     if (RIGID(ch) && !RIGID(tch)) {
        sprintf(buf, "$N's %s%s&0 seems to pass right through your fingers.",
              COMPOSITION_COLOR(tch), COMPOSITION_MASS(tch));
        act(buf, FALSE, ch, 0, tch, TO_CHAR);
        sprintf(buf, "$n tries to grab $N, but $e can't get a grip on $N's %s%s&0 flesh.",
              COMPOSITION_COLOR(tch), COMPOSITION_ADJECTIVE(tch));
        act(buf, TRUE, ch, 0, tch, TO_NOTVICT);
        sprintf(buf, "$n tries to grab you, but $e can't get a grip on your %s%s&0 flesh.",
              COMPOSITION_COLOR(tch), COMPOSITION_ADJECTIVE(tch));
        act(buf, TRUE, ch, 0, tch, TO_VICT);
        return;
     } else if (!RIGID(ch) && RIGID(tch)) {
        sprintf(buf, "You can't get hold of $N with your %s%s&0 grip.",
              COMPOSITION_COLOR(ch), COMPOSITION_ADJECTIVE(ch));
        act(buf, FALSE, ch, 0, tch, TO_CHAR);
        sprintf(buf, "$n tries to grab $N, but $s %s%s&0 fingers can't get a grip.",
              COMPOSITION_COLOR(ch), COMPOSITION_ADJECTIVE(ch));
        act(buf, TRUE, ch, 0, tch, TO_NOTVICT);
        sprintf(buf, "$n tries to grab you, but $s %s%s&0 fingers can't get a grip.",
              COMPOSITION_COLOR(ch), COMPOSITION_ADJECTIVE(ch));
        act(buf, TRUE, ch, 0, tch, TO_VICT);
        return;
     }
  }

  /* Trying to drag a character? */
  if (found == FIND_CHAR_ROOM) {

    if (ch == tch) {
      send_to_char("One foot in front of the other, now...\r\n", ch);
      return;
    }

    if (GET_LEVEL(ch) < LVL_IMMORT && GET_LEVEL(tch) < LVL_IMMORT) {
      if (IS_NPC(tch)) {
        send_to_char("You can't drag NPC's!\r\n", ch);
        return;
      }

      if (CONSENT(tch) != ch) {
        send_to_char("Not without consent you don't!\r\n", ch);
        return;
      }

      /*
       * The code below can handle the maximum dragging position set to
       * as high as POS_SITTING.  I wouldn't recommend setting it any
       * higher.
       */
      if (GET_POS(tch) > POS_SITTING) {
        act("$N isn't quite relaxed enough to be dragged.", FALSE, ch, 0, tch, TO_CHAR);
        return;
      }

      if (GET_WEIGHT(tch) > 3 * CAN_CARRY_W(ch)) {
        act("$N is too heavy for you to drag.\r\n", FALSE, ch, 0, tch, TO_CHAR);
        return;
      }
    } else if (GET_LEVEL(tch) >= GET_LEVEL(ch) && CONSENT(tch) != ch) {
      /* Immorts can drag anyone, except other immorts of same or higher level */
      send_to_char("You can't drag someone a higher level than you.\r\n", ch);
      return;
    }

    move_cost = MIN(4, GET_WEIGHT(tch) / 50 + sectors[SECT(ch->in_room)].mv);
  }

  /* Trying to drag an object in the room? */
  else if (found == FIND_OBJ_ROOM) {

    if (GET_LEVEL(ch) < LVL_GOD) {
      /*
       * Items can be made draggable by giving them the TAKE wear flag.
       * Here we check for that flag.  But an exception is made so that
       * you can also drag corpses.
       */
      if (!CAN_WEAR(tobj, ITEM_WEAR_TAKE) && !IS_CORPSE(tobj)) {
        send_to_char("You cant drag that!\r\n", ch);
        return;
      }

      if (GET_OBJ_WEIGHT(tobj) > 3 * CAN_CARRY_W(ch)) {
        send_to_char("It is too heavy for you to drag.\r\n", ch);
        return;
      }
    }

    if (IS_PLR_CORPSE(tobj) && !has_corpse_consent(ch, tobj)) {
      mprintf(L_STAT, LVL_IMMORT,
              "CORPSE: %s tried to drag %s without CONSENT!",
              GET_NAME(ch), tobj->short_description);
      return;
    }

    move_cost = MIN(4, GET_OBJ_WEIGHT(tobj) / 50 + sectors[SECT(ch->in_room)].mv);
  }

  else {
    send_to_char("You can't drag that!\r\n", ch);
    return;
  }

  from_room = IN_ROOM(ch);

  /* Now determine the direction. */
  argument = any_one_arg(argument, arg);

  if ((dir = parse_direction(arg)) < 0) {
    /* Not a valid direction.  Try to drag into a portal. */
    if (!(portal = find_obj_in_list(world[from_room].contents, find_vis_by_name(ch, arg)))) {
      sprintf(buf, "Can't find a '%s' to drag into!\r\n", arg);
      send_to_char(buf, ch);
      return;
    }

    if (GET_OBJ_TYPE(portal) != ITEM_PORTAL) {
      send_to_char("You can only drag things into portals!\r\n", ch);
      return;
    }

    if (portal == tobj) {
      act("You tug and pull, but are unable to drag $p into itself.",
          FALSE, ch, tobj, portal, TO_CHAR);
      return;
    }

    if (tobj) {
      act("&3$n&3 drags $p&3 into $P&3.&0", TRUE, ch, tobj, portal, TO_ROOM);
      act("&3You drag $p&3 into $P&3.&0", FALSE, ch, tobj, portal, TO_CHAR);
    }
    else if (tch) {
      act("&3$n&3 drags $N&3 into $p&3.&0", TRUE, ch, portal, tch, TO_NOTVICT);
      act("&3$n&3 drags you into $p&3.&0", TRUE, ch, portal, tch, TO_VICT);
      act("&3You drag $N&3 into $p&3.&0", FALSE, ch, portal, tch, TO_CHAR);
    }
    do_enter(ch, arg, 0, 0);
    to_room = IN_ROOM(ch);
    if (tobj) {
      act("&3$n&3 drags $p&3 from $P.&0", TRUE, ch, tobj, portal, TO_ROOM);
      obj_from_room(tobj);
      obj_to_room(tobj, to_room);
    }
    else if (tch) {
      act("&3$n&3 drags $N&3 from $p.&0", TRUE, ch, portal, tch, TO_ROOM);
      char_from_room(tch);
      char_to_room(tch, to_room);
      char_drag_entry(ch, tch);
    }

    if (tobj && IS_PLR_CORPSE(tobj))
      log("CORPSE: %s dragged %s through %s from room %d to room %d",
          GET_NAME(ch), tobj->short_description,
          strip_ansi(portal->short_description),
          world[from_room].vnum, world[to_room].vnum);
  }

  else {
    /* Try to drag in a direction. */

    if (GET_LEVEL(ch) < LVL_GOD) {
      if (GET_MOVE(ch) < move_cost + 6) {
        send_to_char("You are too exhausted!\r\n", ch);
        return;
      }
    }

    if (CONFUSED(ch)) {
       send_to_char("&5You are confused!&0\r\n", ch);
       dir = number(0, 5);
    }

    /* Take tch from the room so they don't see ch's leave message */
    if (tch)
      char_from_room(tch);
    if (!perform_move(ch, dir, FALSE, FALSE)) {
      if (tch)
        char_to_room(tch, from_room);
      sprintf(buf, "&3Looking confused, $n&0&3 tried to drag $N&0&3 %s.&0",
            dirs[dir]);
      act(buf, FALSE, ch, 0, tch, TO_NOTVICT);
      sprintf(buf, "&3Looking confused, $n&0&3 tried to drag you %s.&0",
            dirs[dir]);
      act(buf, FALSE, ch, 0, tch, TO_VICT);
      return;
    }

    alter_move(ch, move_cost);

    to_room = IN_ROOM(ch);

    /*
     * perform_move was successful, so move ch back to from_room so
     * we can display act() messages
     */
    char_from_room(ch);
    char_to_room(ch, from_room);
    if (tobj) {
      act("&3You drag $p&3 behind you.&0", FALSE, ch, tobj, 0, TO_CHAR);
      act("&3$n&3 drags $p&3 behind $m.&0", TRUE, ch, tobj, 0, TO_ROOM);
    }
    else if (tch) {
      act("&3You drag $N&3 behind you.&0", FALSE, ch, 0, tch, TO_CHAR);
      act("&3$n&3 drags you $t behind $m.&0", TRUE, ch, (void *) dirs[dir], tch, TO_VICT);
      act("&3$n&3 drags $N&3 behind $m.&0", TRUE, ch, 0, tch, TO_NOTVICT);
    }

    /* now display act() messages to the target room */
    char_from_room(ch);
    char_to_room(ch, to_room);
    if (tobj) {
      obj_from_room(tobj);
      obj_to_room(tobj, to_room);
      act("&3$n&3 drags $p&3 behind $m.&0", TRUE, ch, tobj, 0, TO_ROOM);
    }
    else if (tch) {
      /* char_from_room already called above. */
      char_to_room(tch, to_room);
      char_drag_entry(ch, tch);
    }

    if (tobj && IS_PLR_CORPSE(tobj))
      log("CORPSE: %s drags %s from room %d to room %d.",
          GET_NAME(ch), tobj->short_description,
          world[from_room].vnum, world[to_room].vnum);
  }
}

ACMD(do_fly)
{
   switch (GET_STANCE(ch)) {
      case STANCE_DEAD:
         act("Rest in peace.", FALSE, ch, 0, 0, TO_CHAR);
         return;
      case STANCE_MORT:
      case STANCE_INCAP:
      case STANCE_STUNNED:
         act("Alas, you cannot summon the will.", FALSE, ch, 0, 0, TO_CHAR);
         return;
      case STANCE_SLEEPING:
         act("You have to wake up first!", FALSE, ch, 0, 0, TO_CHAR);
         return;
   }

   if (EFF_FLAGGED(ch, EFF_MINOR_PARALYSIS) || EFF_FLAGGED(ch, EFF_MAJOR_PARALYSIS)) {
      send_to_char("You can't even twitch, much less fly!\r\n", ch);
      return;
   }

   if (!EFF_FLAGGED(ch, EFF_FLY) && GET_LEVEL(ch) < LVL_IMMORT) {
      send_to_char("You do not have the means to fly.\r\n",ch);
      return;
   }

   if (GET_POS(ch) != POS_FLYING && too_heavy_to_fly(ch)) {
      cprintf(ch, "You try to rise up, but you can't get off the ground!\r\n");
      act("$n rises up on $s toes, as if trying to fly.", TRUE, ch, 0, 0, TO_ROOM);
      return;
   }

   switch (GET_POS(ch)) {
      case POS_STANDING:
         act("&6&bYou begin to float.&0", FALSE, ch, 0, 0, TO_CHAR);
         act("&6&b$n&6&b begins to float.&0", TRUE, ch, 0, 0, TO_ROOM);
         break;
      case POS_FLYING:
         send_to_char("You are already flying.\r\n",ch);
         return;
      case POS_PRONE:
      case POS_SITTING:
      case POS_KNEELING:
      default:
         act("&6&bYou get to your feet and begin floating.&0", FALSE, ch, 0, 0, TO_CHAR);
         act("&6&b$n&6&b gets to $s feet and begins to float.&0", TRUE, ch, 0, 0, TO_ROOM);
         break;
   }

   GET_POS(ch) = POS_FLYING;
   GET_STANCE(ch) = FIGHTING(ch) ? STANCE_FIGHTING : STANCE_ALERT;
   mount_pos_check(ch);
}

ACMD(do_stand)
{
   if (IS_NPC(ch)) {
      WAIT_STATE(ch, PULSE_VIOLENCE * 2);
   };

   switch (GET_STANCE(ch)) {
      case STANCE_DEAD:
         act("Rest in peace.", FALSE, ch, 0, 0, TO_CHAR);
         return;
      case STANCE_MORT:
      case STANCE_INCAP:
      case STANCE_STUNNED:
         act("Alas, you cannot summon the will.", FALSE, ch, 0, 0, TO_CHAR);
         return;
      case STANCE_SLEEPING:
         act("You have to wake up first!", FALSE, ch, 0, 0, TO_CHAR);
         return;
   }

   if (EFF_FLAGGED(ch, EFF_MINOR_PARALYSIS) || EFF_FLAGGED(ch, EFF_MAJOR_PARALYSIS)) {
      send_to_char("You can't even twitch, much less stand up!\r\n", ch);
      return;
   }

   switch (GET_POS(ch)) {
      case POS_PRONE:
         act("You sit up and rise to your feet.", FALSE, ch, 0, 0, TO_CHAR);
         act("$n sits up, and rises to $s feet.", FALSE, ch, 0, 0, TO_ROOM);
         break;
      case POS_STANDING:
         act("You are already standing.", FALSE, ch, 0, 0, TO_CHAR);
         return;
         break;
      case POS_FLYING:
         act("You slowly descend to the surface beneath you.",
               FALSE, ch, 0, 0, TO_CHAR);
         act("$n gently descends to the surface beneath $s feet.",
               TRUE, ch, 0, 0, TO_ROOM);
         break;
      case POS_SITTING:
      case POS_KNEELING:
      default:
         act("You stand up.", FALSE, ch, 0, 0, TO_CHAR);
         act("$n clambers to $s feet.", TRUE, ch, 0, 0, TO_ROOM);
         break;
   }

   GET_POS(ch) = POS_STANDING;
   GET_STANCE(ch) = FIGHTING(ch) ? STANCE_FIGHTING : STANCE_ALERT;
   falling_check(ch);
   mount_pos_check(ch);
}

ACMD(do_sit)
{
   switch (GET_STANCE(ch)) {
      case STANCE_DEAD:
         act("Rest in peace.", FALSE, ch, 0, 0, TO_CHAR);
         return;
      case STANCE_MORT:
      case STANCE_INCAP:
      case STANCE_STUNNED:
         act("Alas, you cannot summon the will.", FALSE, ch, 0, 0, TO_CHAR);
         return;
      case STANCE_SLEEPING:
         act("You have to wake up first!", FALSE, ch, 0, 0, TO_CHAR);
         return;
      case STANCE_FIGHTING:
         act("Sit down while fighting?  Are you MAD?", FALSE, ch, 0, 0, TO_CHAR);
         return;
   }

   if (EFF_FLAGGED(ch, EFF_MINOR_PARALYSIS) || EFF_FLAGGED(ch, EFF_MAJOR_PARALYSIS)) {
      send_to_char("You can't even twitch, much less sit down!\r\n", ch);
      return;
   }

   if (EFF_FLAGGED(ch, EFF_BERSERK)) {
      send_to_char("You're too incensed to sit down!\r\n", ch);
      return;
   }

   switch (GET_POS(ch)) {
      case POS_PRONE:
         act("You sit up.", FALSE, ch, 0, 0, TO_CHAR);
         act("$n sits up.", FALSE, ch, 0, 0, TO_ROOM);
         break;
      case POS_SITTING:
         send_to_char("You're sitting already.\r\n", ch);
         return;
      case POS_KNEELING:
         act("You stop kneeling and sit down.", FALSE, ch, 0, 0, TO_CHAR);
         act("$n stops kneeling, and sits down.", FALSE, ch, 0, 0, TO_ROOM);
         break;
      case POS_FLYING:
         act("You stop flying and sit down.", FALSE, ch, 0, 0, TO_CHAR);
         act("$n stops flying and sits down.", TRUE, ch, 0, 0, TO_ROOM);
         break;
      case POS_STANDING:
      default:
         act("You sit down.", FALSE, ch, 0, 0, TO_CHAR);
         act("$n sits down.", FALSE, ch, 0, 0, TO_ROOM);
         break;
   }

   GET_POS(ch) = POS_SITTING;
   GET_STANCE(ch) = STANCE_ALERT;
   falling_check(ch);
   mount_pos_check(ch);
}


ACMD(do_kneel)
{
   switch (GET_STANCE(ch)) {
      case STANCE_DEAD:
         act("Rest in peace.", FALSE, ch, 0, 0, TO_CHAR);
         return;
      case STANCE_MORT:
      case STANCE_INCAP:
      case STANCE_STUNNED:
         act("Alas, you cannot summon the will.", FALSE, ch, 0, 0, TO_CHAR);
         return;
      case STANCE_SLEEPING:
         send_to_char("You seem to be having a dream of infancy.\r\n", ch);
         return;
      case STANCE_FIGHTING:
         act("Kneel while fighting?  Are you MAD?", FALSE, ch, 0, 0, TO_CHAR);
         return;
   }

   if (EFF_FLAGGED(ch, EFF_BERSERK)) {
      send_to_char("You're too incensed to kneel down!\r\n", ch);
      return;
   }

   if (EFF_FLAGGED(ch, EFF_MINOR_PARALYSIS) || EFF_FLAGGED(ch, EFF_MAJOR_PARALYSIS)) {
      send_to_char("You can't even twitch, much less get to your knees!\r\n", ch);
      return;
   }

   switch (GET_POS(ch)) {
      case POS_PRONE:
         act("You swing up onto your knees.", FALSE, ch, 0, 0, TO_CHAR);
         act("$n levers up onto $s knees.", TRUE, ch, 0, 0, TO_ROOM);
         break;
      case POS_SITTING:
         act("You move from your butt to your knees.", FALSE, ch, 0, 0, TO_CHAR);
         act("$n stops sitting and gets to $s knees.", TRUE, ch, 0, 0, TO_ROOM);
         break;
      case POS_KNEELING:
         send_to_char("You are kneeling already.\r\n", ch);
         return;
      case POS_FLYING:
         act("You stop flying and kneel.", FALSE, ch, 0, 0, TO_CHAR);
         act("$n stops flying and settles to $s knees.", TRUE, ch, 0, 0, TO_ROOM);
         break;
      case POS_STANDING:
      default:
         act("You kneel.", FALSE, ch, 0, 0, TO_CHAR);
         act("$n settles to $s knees.", TRUE, ch, 0, 0, TO_ROOM);
   }
   GET_POS(ch) = POS_KNEELING;
   falling_check(ch);
   mount_pos_check(ch);
}

ACMD(do_recline)
{
   switch (GET_STANCE(ch)) {
      case STANCE_DEAD:
         act("Rest in peace.", FALSE, ch, 0, 0, TO_CHAR);
         return;
      case STANCE_MORT:
      case STANCE_INCAP:
      case STANCE_STUNNED:
         act("Alas, you cannot summon the will.", FALSE, ch, 0, 0, TO_CHAR);
         return;
      case STANCE_SLEEPING:
         send_to_char("You dream of lying down.\r\n", ch);
         return;
      case STANCE_FIGHTING:
         act("Lie down while fighting?  Are you MAD?", FALSE, ch, 0, 0, TO_CHAR);
         return;
   }

   if (EFF_FLAGGED(ch, EFF_MINOR_PARALYSIS) || EFF_FLAGGED(ch, EFF_MAJOR_PARALYSIS)) {
      send_to_char("You can't even twitch, much less lie down!\r\n", ch);
      return;
   }

   if (EFF_FLAGGED(ch, EFF_BERSERK)) {
      send_to_char("You're too incensed to lie down!\r\n", ch);
      return;
   }

   switch (GET_POS(ch)) {
      case POS_PRONE:
         send_to_char("You are already lying down.\r\n", ch);
         return;
      case POS_SITTING:
         act("You stop sitting and lie down.", FALSE, ch, 0, 0, TO_CHAR);
         act("$n stops sitting and lies down.", TRUE, ch, 0, 0, TO_ROOM);
         break;
      case POS_KNEELING:
         act("You recline.", FALSE, ch, 0, 0, TO_CHAR);
         act("$n eases off $s knees and lies down.", TRUE, ch, 0, 0, TO_ROOM);
         break;
      case POS_FLYING:
         act("You gently land on your belly.", FALSE, ch, 0, 0, TO_CHAR);
         act("$n stops flying and drops flat to the ground.", TRUE, ch, 0, 0, TO_ROOM);
         break;
      case POS_STANDING:
      default:
         act("You drop to your belly.", FALSE, ch, 0, 0, TO_CHAR);
         act("$n drops flat to the ground.", TRUE, ch, 0, 0, TO_ROOM);
         break;
   }

   GET_POS(ch) = POS_PRONE;
   falling_check(ch);
   mount_pos_check(ch);
}

ACMD(do_rest)
{
   switch (GET_STANCE(ch)) {
      case STANCE_DEAD:
         act("You are already resting, after a fashion.", FALSE, ch, 0, 0, TO_CHAR);
         return;
      case STANCE_MORT:
      case STANCE_INCAP:
      case STANCE_STUNNED:
         act("That is the least of your concerns.", FALSE, ch, 0, 0, TO_CHAR);
         return;
      case STANCE_SLEEPING:
         send_to_char("You seem to be quite restful already.\r\n", ch);
         return;
      case STANCE_RESTING:
         send_to_char("You are already resting.\r\n", ch);
         return;
      case STANCE_FIGHTING:
         act("Rest while fighting?  Are you MAD?", FALSE, ch, 0, 0, TO_CHAR);
         return;
   }

   if (EFF_FLAGGED(ch, EFF_MINOR_PARALYSIS) || EFF_FLAGGED(ch, EFF_MAJOR_PARALYSIS)) {
      send_to_char("In your paralyzed state, you find this impossible.\r\n", ch);
      return;
   }

   if (EFF_FLAGGED(ch, EFF_BERSERK)) {
      send_to_char("You're too incensed to rest!\r\n", ch);
     return;
   }

   switch (GET_POS(ch)) {
      case POS_PRONE:
         send_to_char("You relax.\r\n", ch);
         act("You see some of the tension leave $n's body.", TRUE, ch, 0, 0,
            TO_ROOM);
         break;
      case POS_SITTING:
         act("You find a comfortable spot where you are sitting.", FALSE, ch, 0, 0, TO_CHAR);
         act("$n finds a comfortable spot where $e is sitting.", TRUE, ch, 0, 0, TO_ROOM);
         break;
      case POS_KNEELING:
         send_to_char("You slump and relax your posture.\r\n", ch);
         act("$n relaxes a bit.", TRUE, ch, 0, 0, TO_ROOM);
         GET_POS(ch) = POS_SITTING;
         break;
      case POS_FLYING:
         send_to_char("You stop floating and rest your tired bones.\r\n", ch);
         act("$n stops floating and takes a rest.", TRUE, ch, 0, 0, TO_ROOM);
         GET_POS(ch) = POS_SITTING;
         break;
      case POS_STANDING:
      default:
         send_to_char("You sit down and relax.\r\n", ch);
         act("$n sits down in a comfortable spot.", TRUE, ch, 0, 0, TO_ROOM);
         GET_POS(ch) = POS_SITTING;
         break;
   }

   GET_STANCE(ch) = STANCE_RESTING;
   falling_check(ch);
   mount_pos_check(ch);
}

/* new, does opposite of 'rest' */

ACMD(do_alert)
{
   switch (GET_STANCE(ch)) {
      case STANCE_DEAD:
         act("Totally impossible.", FALSE, ch, 0, 0, TO_CHAR);
         return;
      case STANCE_MORT:
      case STANCE_INCAP:
      case STANCE_STUNNED:
         act("That is utterly beyond your current abilities.", FALSE, ch, 0, 0, TO_CHAR);
         return;
      case STANCE_SLEEPING:
         send_to_char("Let's try this in stages.  How about waking up first?\r\n", ch);
         return;
      case STANCE_ALERT:
         send_to_char("You are already about as tense as you can get.\r\n", ch);
         return;
      case STANCE_FIGHTING:
         send_to_char("Being as you're in a battle and all, you're pretty alert already!\r\n", ch);
         return;
   }

   if (EFF_FLAGGED(ch, EFF_MINOR_PARALYSIS) || EFF_FLAGGED(ch, EFF_MAJOR_PARALYSIS)) {
      send_to_char("In your paralyzed state, you find this impossible.\r\n", ch);
      return;
   }

   switch (GET_POS(ch)) {
      case POS_PRONE:
         send_to_char("You stop relaxing and try to become more aware of your surroundings.\r\n", ch);
         break;
      case POS_SITTING:
         send_to_char("You sit up straight and start to pay attention.\r\n", ch);
         act("$n sits at attention.", TRUE, ch, 0, 0, TO_ROOM);
         break;
      case POS_KNEELING:
         send_to_char("You straighten up a bit.\r\n", ch);
         act("$n straightens up a bit.", TRUE, ch, 0, 0, TO_ROOM);
         break;
      default:
         send_to_char("You tense up and become more alert.\r\n", ch);
         break;
   }

   GET_STANCE(ch) = STANCE_ALERT;
   mount_pos_check(ch);
}

ACMD(do_sleep)
{
   switch (GET_STANCE(ch)) {
      case STANCE_DEAD:
         act("Rest in peace.", FALSE, ch, 0, 0, TO_CHAR);
         return;
      case STANCE_MORT:
      case STANCE_INCAP:
      case STANCE_STUNNED:
         act("You are beyond sleep.", FALSE, ch, 0, 0, TO_CHAR);
         return;
      case STANCE_SLEEPING:
         send_to_char("You are already sound asleep.\r\n", ch);
         return;
      case STANCE_FIGHTING:
         send_to_char("Sleep while fighting?  Are you MAD?\r\n", ch);
         return;
   }

   if (EFF_FLAGGED(ch, EFF_MINOR_PARALYSIS) || EFF_FLAGGED(ch, EFF_MAJOR_PARALYSIS)) {
      send_to_char("In your paralyzed state, you find this impossible.\r\n", ch);
      return;
   }

   if (EFF_FLAGGED(ch, EFF_BERSERK)) {
      send_to_char("You're too incensed to take a nap!\r\n", ch);
      return;
   }

   switch (GET_POS(ch)) {
      case POS_PRONE:
         send_to_char("You go to sleep.\r\n", ch);
         act("$n goes to sleep.", TRUE, ch, 0, 0, TO_ROOM);
         break;
      case POS_FLYING:
         act("You stop floating, and lie down to sleep.",
               FALSE, ch, 0, 0, TO_CHAR);
         act("$n stops floating, and lies down to sleep.",
               TRUE, ch, 0, 0, TO_ROOM);
         break;
      default:
         send_to_char("You lie down and go to sleep.\r\n", ch);
         act("$n lies down and goes to sleep.", TRUE, ch, 0, 0, TO_ROOM);
   }

   GET_POS(ch) = POS_PRONE;
   GET_STANCE(ch) = STANCE_SLEEPING;
   falling_check(ch);
   mount_pos_check(ch);
}


ACMD(do_wake)
{
  struct char_data *vict;
  int self = 0;

  one_argument(argument, arg);
  if (*arg) {
    if (GET_STANCE(ch) < STANCE_RESTING)
      send_to_char("Maybe you should wake yourself up first.\r\n", ch);
    else if ((vict = find_char_in_room(&world[ch->in_room], find_vis_by_name(ch, arg))) == NULL)
      send_to_char(NOPERSON, ch);
    else if (vict == ch)
      self = 1;
    else if (GET_STANCE(vict) > STANCE_SLEEPING)
      act("$E is already awake.", FALSE, ch, 0, vict, TO_CHAR);
    else if (EFF_FLAGGED(vict, EFF_SLEEP))
      act("You can't wake $M up!", FALSE, ch, 0, vict, TO_CHAR);
    else if (GET_STANCE(vict) < STANCE_SLEEPING)
      act("$E's in pretty bad shape!", FALSE, ch, 0, vict, TO_CHAR);
    else {
      act("You wake $M up.", FALSE, ch, 0, vict, TO_CHAR);
      act("You are awakened by $n.", FALSE, ch, 0, vict, TO_VICT |
          TO_SLEEP);
      GET_POS(vict) = POS_SITTING;
      GET_STANCE(vict) = STANCE_RESTING;
    }
    if (!self)
      return;
  }
  if (EFF_FLAGGED(ch, EFF_SLEEP))
    send_to_char("You can't wake up!\r\n", ch);
  else if (GET_STANCE(ch) > STANCE_SLEEPING)
    send_to_char("You are already awake...\r\n", ch);
  else {
    send_to_char("You awaken.\r\n", ch);
    act("$n awakens.", TRUE, ch, 0, 0, TO_ROOM);
    GET_POS(ch) = POS_SITTING;
    GET_STANCE(ch) = STANCE_RESTING;
  }
   mount_pos_check(ch);
}

ACMD(do_follow)
{
  struct char_data *leader;

  if (subcmd == SCMD_SHADOW && !GET_SKILL(ch, SKILL_SHADOW)) {
    send_to_char("You aren't skilled enough to shadow someone.\r\n", ch);
    return;
  }

  one_argument(argument, arg);

  if (!*arg) {
    if (ch->master)
      act("You are following $N.", FALSE, ch, 0, ch->master, TO_CHAR);
    else
      send_to_char("You are not following anyone.\r\n", ch);
    return;
  }

  if (!str_cmp(arg, "off"))
    leader = ch;
  else if (!(leader = find_char_in_room(&world[ch->in_room], find_vis_by_name(ch, arg)))) {
    send_to_char(NOPERSON, ch);
    return;
  }

  /* Charmies aren't allowed to stop following. */
  if (EFF_FLAGGED(ch, EFF_CHARM) && ch->master)
    act("But you only feel like following $N!", FALSE, ch, 0, ch->master, TO_CHAR);
  /* If 'fol self' then stop following. */
  else if (ch == leader) {
    if (ch->master)
      stop_follower(ch, 0);
    else
      send_to_char("You aren't following anyone.\r\n", ch);
  }
  else if (ch->master == leader)
    act("You are already following $N.", FALSE, ch, 0, leader, TO_CHAR);
  else if (GET_LEVEL(ch) < LVL_GOD && PRF_FLAGGED(leader, PRF_NOFOLLOW))
    send_to_char("That person would rather not have followers right now.\r\n", ch);
  else {
    if (ch->master)
      stop_follower(ch, 0);

    if (subcmd == SCMD_SHADOW)
      SET_FLAG(EFF_FLAGS(ch), EFF_SHADOWING);

    add_follower(ch, leader);

    if (subcmd == SCMD_SHADOW) {
      int chance = GET_SKILL(ch, SKILL_SHADOW);
      chance += dex_app_skill[GET_DEX(ch)].sneak;

      if (chance < number(1, 101)) {
        REMOVE_FLAG(EFF_FLAGS(ch), EFF_SHADOWING);
        act("You are noticed as you attempt to secretly follow $N.", FALSE, ch, 0, leader, TO_CHAR);
        act("$n attempts to secretly follow you, but you spot $m.", TRUE, ch, 0, leader, TO_VICT);
        act("$n attempts to follow $N secretly, but you notice $m.", TRUE, ch, 0, leader, TO_NOTVICT);
      }
      else
        act("You start shadowing $N.", FALSE, ch, 0, leader, TO_CHAR);

      WAIT_STATE(ch, PULSE_VIOLENCE / 2);
      improve_skill(ch, SKILL_SHADOW);
    }
  }
}

ACMD(do_abandon)
{
  bool found = FALSE;
  struct char_data *follower;
  struct follow_type *k;
  char buf[MAX_STRING_LENGTH];

  one_argument(argument, arg);

  buf[0] = 0;


  if (pk_allowed) {

    if (!*arg) {
      if (ch->followers) {
        sprintf(buf, "You are being followed by:\r\n");
        for (k = ch->followers; k; k = k->next) {
          if(GET_INVIS_LEV(k->follower) < GET_LEVEL(ch)) {
            if (CAN_SEE_MOVING(ch, k->follower)) {
              sprintf(buf,"%s  %s\r\n", buf, GET_NAME(k->follower));
              found = TRUE;
            }
          }
        }
      }
      if (found) {
        page_string(ch, buf);
      }
      else {
        send_to_char("Nobody is following you.\r\n", ch);
      }
      return;
    }

    if (!str_cmp(arg, "all")) {
      if (ch->followers) {
        for (k = ch->followers; k; k = k->next) {
          if(GET_LEVEL(k->follower) < LVL_GOD) {
            if (CAN_SEE_MOVING(ch, k->follower)) {
              stop_follower(k->follower, 0);
              found = TRUE;
            }
          }
          else {
            if(GET_INVIS_LEV(k->follower) < GET_LEVEL(ch)) {
              send_to_char("You can not abandon immortals.\r\n", ch);
              found = TRUE;
            }
          }
        }
      }
      if (!found) {
        send_to_char("Nobody is following you.\r\n", ch);
      }
      return;
    }

    if (!str_cmp(arg, "pets")) {
      if (ch->followers) {
        for (k = ch->followers; k; k = k->next) {
          if (EFF_FLAGGED(k->follower, EFF_CHARM)) {
            stop_follower(k->follower, 0);
            found = TRUE;
          }
        }
      }
      if(!found) {
        send_to_char("You do not have any pets following you.\r\n", ch);
      }
      return;
    }

    if (!str_cmp(arg, "players")) {
      if (ch->followers) {
        for (k = ch->followers; k; k = k->next) {
          if (!(EFF_FLAGGED(k->follower, EFF_CHARM))) {
            if(GET_LEVEL(k->follower) < LVL_GOD) {
              if (CAN_SEE_MOVING(ch, k->follower)) {
                stop_follower(k->follower, 0);
                found = TRUE;
              }
            }
            else {
              if(GET_INVIS_LEV(k->follower) < GET_LEVEL(ch)) {
                send_to_char("You can not abandon immortals.\r\n", ch);
                found = TRUE;
              }
            }
          }
        }
      }
      if(!found){
        send_to_char("You do not have any players following you.\r\n", ch);
      }
      return;
    }

    if (!(follower = find_char_for_mtrig(ch, arg))) {
      send_to_char(NOPERSON, ch);
      return;
    }
    else if (ch == follower) {
      send_to_char("You can't abandon yourself.\r\n", ch);
      return;
    }
    else if (ch->followers) {
      for (k = ch->followers; k; k = k->next) {
        if((k->follower == follower) && (CAN_SEE_MOVING(ch, k->follower))) {
          if(GET_LEVEL(k->follower) < LVL_GOD) {
            stop_follower(k->follower, 0);
            found = TRUE;
          }
          else {
            send_to_char("You can not abandon immortals.\r\n", ch);
            found = TRUE;
          }
        }
      }
    }
    else if(!found) {
      send_to_char(NOPERSON, ch);
    }
    return;
  }
  else {
    if (!*arg) {
      if (ch->followers) {
        sprintf(buf, "You are being followed by:\r\n");
        for (k = ch->followers; k; k = k->next) {
          if(GET_INVIS_LEV(k->follower) < GET_LEVEL(ch)) {
            if (EFF_FLAGGED(k->follower, EFF_INVISIBLE)) {
              sprintf(buf,"%s  %s (invis)\r\n", buf, GET_NAME(k->follower));
              found = TRUE;
            }
            else {
              sprintf(buf,"%s  %s\r\n", buf, GET_NAME(k->follower));
              found = TRUE;
            }
          }
        }
      }
      if (found) {
        page_string(ch, buf);
      }
      else {
        send_to_char("Nobody is following you.\r\n", ch);
      }
      return;
    }

    if (!str_cmp(arg, "all")) {
      if (ch->followers) {
        for (k = ch->followers; k; k = k->next) {
          if(GET_LEVEL(k->follower) < LVL_GOD) {
            if ((EFF_FLAGGED(k->follower, EFF_INVISIBLE)) && (!CAN_SEE(ch, k->follower))) {
              sprintf(buf,"%s stops following you.\r\n", GET_NAME(k->follower));
              send_to_char(buf, ch);
            }
            stop_follower(k->follower, 0);
            found = TRUE;
          }
          else {
            if(GET_INVIS_LEV(k->follower) < GET_LEVEL(ch)) {
              send_to_char("You can not abandon immortals.\r\n", ch);
              found = TRUE;
            }
          }
        }
      }
      if (!found) {
        send_to_char("Nobody is following you.\r\n", ch);
      }
      return;
    }

    if (!str_cmp(arg, "pets")) {
      if (ch->followers) {
        for (k = ch->followers; k; k = k->next) {
          if (EFF_FLAGGED(k->follower, EFF_CHARM)) {
            stop_follower(k->follower, 0);
            found = TRUE;
          }
        }
      }
      if(!found) {
        send_to_char("You do not have any pets following you.\r\n", ch);
      }
      return;
    }

    if (!str_cmp(arg, "players")) {
      if (ch->followers) {
        for (k = ch->followers; k; k = k->next) {
          if (!(EFF_FLAGGED(k->follower, EFF_CHARM))) {
            if(GET_LEVEL(k->follower) < LVL_GOD) {
              if ((EFF_FLAGGED(k->follower, EFF_INVISIBLE)) && (!CAN_SEE(ch, k->follower))) {
                sprintf(buf,"%s stops following you.\r\n", GET_NAME(k->follower));
                send_to_char(buf, ch);
              }
              stop_follower(k->follower, 0);
              found = TRUE;
            }
            else {
              if(GET_INVIS_LEV(k->follower) < GET_LEVEL(ch)) {
                send_to_char("You can not abandon immortals.\r\n", ch);
                found = TRUE;
              }
            }
          }
        }
      }
      if(!found) {
        send_to_char("You do not have any players following you.\r\n", ch);
      }
      return;
    }

    if (!(follower = find_char_for_mtrig(ch, arg))) {
      send_to_char(NOPERSON, ch);
      return;
    }
    else if (ch == follower) {
      send_to_char("You can't abandon yourself.\r\n", ch);
      return;
    }
    else if (ch->followers) {
      for (k = ch->followers; k; k = k->next) {
        if(k->follower == follower) {
          if(GET_LEVEL(k->follower) < LVL_GOD) {
            if ((EFF_FLAGGED(k->follower, EFF_INVISIBLE)) && (!CAN_SEE(ch, k->follower))) {
              sprintf(buf,"%s stops following you.\r\n", GET_NAME(k->follower));
              send_to_char(buf, ch);
            }
            stop_follower(k->follower, 0);
            found = TRUE;
          }
          else {
            send_to_char("You can not abandon immortals.\r\n", ch);
            found = TRUE;
          }
        }
      }
    }
    if(!found) {
      send_to_char(NOPERSON, ch);
    }
    return;
  }
}

void perform_buck(struct char_data *mount, int whilemounting)
{
   struct char_data *rider = RIDDEN_BY(mount);

   if (!rider) return;

   if (IS_SPLASHY(IN_ROOM(mount))) {
      if (whilemounting) {
         act("$N suddenly bucks upwards, throwing you violently into the water!",
               FALSE, rider, 0, mount, TO_CHAR);
         act("$n is thrown down with a splash as $N violently bucks!", TRUE, rider, 0, mount, TO_NOTVICT);
         act("You buck violently and throw $n into the water.", FALSE, rider, 0, mount, TO_VICT);
      } else {
         act("You quickly buck, throwing $N into the water.", FALSE, mount, 0, rider, TO_CHAR);
         act("$n quickly bucks, throwing you into the water.", FALSE, mount, 0, rider, TO_VICT);
         act("$n quickly bucks, throwing $N into the water.", FALSE, mount, 0, rider, TO_NOTVICT);
      }
   } else {
      if (whilemounting) {
         act("$N suddenly bucks upwards, throwing you violently to the ground!",
               FALSE, rider, 0, mount, TO_CHAR);
         act("$n is thrown to the ground as $N violently bucks!", TRUE, rider, 0, mount, TO_NOTVICT);
         act("You buck violently and throw $n to the ground.", FALSE, rider, 0, mount, TO_VICT);
      } else {
         act("You quickly buck, throwing $N to the ground.", FALSE, mount, 0, rider, TO_CHAR);
         act("$n quickly bucks, throwing you to the ground.", FALSE, mount, 0, rider, TO_VICT);
         act("$n quickly bucks, throwing $N to the ground.", FALSE, mount, 0, rider, TO_NOTVICT);
      }
   }

   dismount_char(rider);
   if (!whilemounting)
      alter_pos(rider, POS_SITTING, STANCE_ALERT);
   hurt_char(rider, NULL, dice(1, 3), TRUE);
}

ACMD(do_mount) {
   char arg[MAX_INPUT_LENGTH];
   struct char_data *vict;

   if (FIGHTING(ch)) {
      send_to_char("You are too busy fighting to try that right now.\r\n",ch);
      return;
   }

   one_argument(argument, arg);

   if (!*arg) {
      send_to_char("Mount who?\r\n", ch);
      return;
   } else if (!(vict = find_char_in_room(&world[ch->in_room], find_vis_by_name(ch, arg)))) {
      send_to_char("There is no-one by that name here.\r\n", ch);
      return;
   } else if (!IS_NPC(vict)) {
      send_to_char("Ehh... no.\r\n", ch);
      return;
   } else if (RIDING(ch) || RIDDEN_BY(ch)) {
      send_to_char("You are already mounted.\r\n", ch);
      return;
   } else if (RIDING(vict) || RIDDEN_BY(vict)) {
      send_to_char("It is already mounted.\r\n", ch);
      return;
   } else if (GET_LEVEL(ch) < LVL_IMMORT && !MOB_FLAGGED(vict, MOB_MOUNTABLE)) {
      send_to_char("You can't mount that!\r\n", ch);
      return;
   } else if (!GET_SKILL(ch, SKILL_MOUNT)) {
      send_to_char("First you need to learn *how* to mount.\r\n", ch);
      return;
   } else if (ch == vict) {
      send_to_char("Not likely.\r\n", ch);
      return;
   } else if (RIGID(ch) && !RIGID(vict) && GET_LEVEL(ch) < LVL_IMMORT) {
      if (GET_COMPOSITION(vict) == COMP_ETHER) {
         act("You go to mount $N, but you pass right through $M.", FALSE,
               ch, 0, vict, TO_CHAR);
         act("$n tries to mount you",
               FALSE, ch, 0, vict, TO_VICT);
         act("$n tries to mount $N, but only flails about in a vain attempt to touch $M.",
               TRUE, ch, 0, vict, TO_NOTVICT);
      } else {
         act("You lay a hand on $N, but it slips right through.", FALSE,
               ch, 0, vict, TO_CHAR);
         act("$n tries to mount you, but you are too fluid to support $m",
               FALSE, ch, 0, vict, TO_VICT);
         act("$n tries to mount $N, but $E isn't solid enough to support a rider.",
               TRUE, ch, 0, vict, TO_NOTVICT);
      }
      return;
   } else if (!RIGID(ch) && RIGID(vict) && GET_LEVEL(ch) < LVL_IMMORT) {
      if (GET_COMPOSITION(ch) == COMP_ETHER) {
         act("You try to climb $N, but you simply pass through $M.",
               FALSE, ch, 0, vict, TO_CHAR);
         act("$n tries to mount you, but $e can't seem to touch you.",
               FALSE, ch, 0, vict, TO_VICT);
         act("$n tries to mount $N, but makes $M shudder as $e passes through.",
               FALSE, ch, 0, vict, TO_NOTVICT);
      } else {
         act("You try to climb $M, but you just flow right off.",
               FALSE, ch, 0, vict, TO_CHAR);
         act("$n tries to mount you, but just flows right off.",
               FALSE, ch, 0, vict, TO_VICT);
         act("$n tries to mount $N, but just flows right off.",
               FALSE, ch, 0, vict, TO_NOTVICT);
      }
      return;
   }

   WAIT_STATE(ch, PULSE_VIOLENCE / 2);

   if (mount_fall(ch, vict)) {
      act("You try to mount $N, but slip and fall off.", FALSE, ch, 0, vict, TO_CHAR);
      act("$n tries to mount you, but slips and falls off.", FALSE, ch, 0, vict, TO_VICT);
      act("$n tries to mount $N, but slips and falls off.", TRUE, ch, 0, vict, TO_NOTVICT);
      mount_warning(ch, vict);
      alter_pos(ch, POS_SITTING, STANCE_ALERT);
      improve_skill(ch, SKILL_MOUNT);
      hurt_char(ch, NULL, dice(1, 2), TRUE);
      return;
   }

   act("You mount $N.", FALSE, ch, 0, vict, TO_CHAR);
   act("$n mounts you.", FALSE, ch, 0, vict, TO_VICT);
   act("$n mounts $N.", TRUE, ch, 0, vict, TO_NOTVICT);
   mount_char(ch, vict);
   improve_skill(ch, SKILL_MOUNT);

   if (IS_NPC(vict) && !EFF_FLAGGED(vict, EFF_TAMED) && mount_bucked(ch, vict)) {
      perform_buck(vict, TRUE);
      mount_warning(ch, vict);
   }
}



ACMD(do_dismount) {
  if (!RIDING(ch))
    send_to_char("You aren't even riding anything.\r\n", ch);
  else if (FIGHTING(ch))
    send_to_char("You would get hacked to pieces if you dismount now!\r\n",ch);
  else if (SECT(ch->in_room) == SECT_WATER &&
        !can_travel_on_water(ch) && GET_POS(ch) != POS_FLYING)
    send_to_char("Yah, right, and then drown...\r\n", ch);
  else if (mount_fall(ch, RIDING(ch)) && number(0, 1) == 0) {
    act("As you begin dismounting, you slip and fall down.", FALSE, ch, 0, RIDING(ch), TO_CHAR);
    act("$n starts to dismount, but slips and falls down.", FALSE, ch, 0, RIDING(ch), TO_ROOM);
    alter_pos(ch, POS_SITTING, STANCE_ALERT);
    improve_skill(ch, SKILL_MOUNT);
    hurt_char(ch, NULL, dice(1, 2), TRUE);
  } else {
    act("You dismount $N.", FALSE, ch, 0, RIDING(ch), TO_CHAR);
    act("$n dismounts from you.", FALSE, ch, 0, RIDING(ch), TO_VICT);
    act("$n dismounts $N.", TRUE, ch, 0, RIDING(ch), TO_NOTVICT);
    dismount_char(ch);
  }
}


ACMD(do_buck) {
  if (!RIDDEN_BY(ch))
    send_to_char("You're not even being ridden!\r\n", ch);
  else if (FIGHTING(ch))
    send_to_char("Worry about this &bafter&0 the battle!\r\n",ch);
  else if (EFF_FLAGGED(ch, EFF_TAMED))
    send_to_char("But you're tamed!\r\n", ch);
  else
    perform_buck(ch, FALSE);
}


ACMD(do_tame) {
   char arg[MAX_INPUT_LENGTH];
   struct effect eff;
   struct char_data *vict;
   int tame_duration = 0;
   int chance_tame, chance_attack, lvldiff;

   if (FIGHTING(ch)) {
      send_to_char("You can't tame while fighting!\r\n",ch);
      return;
   }
   one_argument(argument, arg);

   if (!*arg) {
      send_to_char("Tame who?\r\n", ch);
      return;
   } else if (!(vict = find_char_in_room(&world[ch->in_room], find_vis_by_name(ch, arg)))) {
      send_to_char("There is no such creature here.\r\n", ch);
      return;
   } else if (!IS_NPC(vict) || (GET_LEVEL(ch) < LVL_IMMORT && !MOB_FLAGGED(vict, MOB_MOUNTABLE))) {
      act("You can't tame $N.", FALSE, ch, 0, vict, TO_CHAR);
      return;
   } else if (EFF_FLAGGED(vict, EFF_TAMED)) {
      act("$E seems quite tame already.", FALSE, ch, 0, vict, TO_CHAR);
      return;
   } else if (!GET_SKILL(ch, SKILL_TAME)) {
      send_to_char("You don't even know how to tame something.\r\n", ch);
      return;
   }

   WAIT_STATE(ch, PULSE_VIOLENCE);

   lvldiff = mountlevel(vict) - ideal_tamelevel(ch);
   if (lvldiff < 1) {
      chance_tame = 1100;
      chance_attack = 0;
   } else {
      chance_tame = 5 + pow(2 * (MOUNT_LEVEL_FUDGE - lvldiff + 1) / MOUNT_LEVEL_FUDGE, 3) * 900 / 8;
      chance_attack = 5 + pow(2 * lvldiff / MOUNT_LEVEL_FUDGE, 3) * 600 / 8;
   }

   if (number(0, 999) >= chance_tame) {
      if (number(0, 999) < chance_attack) {
         act("Your shocking lack of tact has greatly annoyed $N!", FALSE, ch, 0, vict, TO_CHAR);
         act("$n has really pissed $N off!", FALSE, ch, 0, vict, TO_ROOM);
         if (IS_NPC(vict) && AWAKE(vict))
            attack(vict, ch);
      } else {
         act("You failed to tame $M.", FALSE, ch, 0, vict, TO_CHAR);
         act("$n tries lamely to tame $N.", FALSE, ch, 0, vict, TO_ROOM);
         improve_skill(ch, SKILL_TAME);
      }
      return;
   }

   improve_skill(ch, SKILL_TAME);

   tame_duration = (int)(GET_CHA(ch) / 3) + 10;
   if (GET_CLASS(ch) == CLASS_DRUID) {
      tame_duration += 30;
   }
   if (GET_CLASS(ch) == CLASS_RANGER) {
      tame_duration += 15;
   }

  memset(&eff, 0, sizeof(struct effect));
  eff.type = SKILL_TAME;
  eff.duration = tame_duration;
  eff.modifier = 0;
  eff.location = APPLY_NONE;
  SET_FLAG(eff.flags, EFF_TAMED);
  effect_to_char(vict, &eff);

  act("You tame $N.", FALSE, ch, 0, vict, TO_CHAR);
  act("$n tames you.", FALSE, ch, 0, vict, TO_VICT);
  act("$n tames $N.", FALSE, ch, 0, vict, TO_NOTVICT);
}


/***************************************************************************
 * $Log: act.movement.c,v $
 * Revision 1.173  2011/03/16 13:39:58  myc
 * Fix all warnings for "the address of X will always evaluate to 'true'",
 * where X is a variable.
 *
 * Revision 1.172  2010/06/05 04:43:57  mud
 * Replacing ocean sector type with cave.
 *
 * Revision 1.171  2009/07/18 01:17:23  myc
 * Immobilized characters can't move.
 *
 * Revision 1.170  2009/03/19 23:16:23  myc
 * Renamed corpse_consent -> has_corpse_consent.
 *
 * Revision 1.169  2009/03/09 04:33:20  jps
 * Moved direction information from structs.h, constants.h, and constants.c
 * into directions.h and directions.c.
 *
 * Revision 1.168  2009/03/09 02:22:32  myc
 * Added functionality for locking new boards to the lock/unlock
 * command.
 *
 * Revision 1.167  2009/03/08 23:34:14  jps
 * Renamed spells.[ch] to casting.
 *
 * Revision 1.166  2009/03/08 21:43:27  jps
 * Split lifeforce, composition, charsize, and damage types from chars.c
 *
 * Revision 1.165  2009/03/07 09:34:47  jps
 * Changed name of room Entry trigger to Preentry. Added a Postentry room trigger type.
 *
 * Revision 1.164  2009/03/03 19:41:50  myc
 * New target finding mechanism in find.c.
 *
 * Revision 1.163  2009/02/21 03:30:16  myc
 * Removed L_FILE flag--mprintf now logs to file by default; assert
 * L_NOFILE to prevent that.
 *
 * Revision 1.162  2008/12/03 03:14:16  myc
 * Make the stand and fly commands set stance to FIGHTING if the character
 * is fighting and ALERT otherwise.
 *
 * Revision 1.161  2008/09/21 21:04:20  jps
 * Passing cast type to mag_affect so that potions of bless/dark presence can be quaffed by neutral people.
 *
 * Revision 1.160  2008/09/20 17:38:00  jps
 * Fixed messages about riding incapacitated mounts.
 *
 * Revision 1.159  2008/09/20 08:00:09  jps
 * Don't allow anyone to mount players.
 *
 * Revision 1.158  2008/09/20 07:58:01  jps
 * Don't allow anyone to tame players.
 *
 * Revision 1.157  2008/09/13 17:20:55  jps
 * Don't allow riding when your mount can't move. Check for falling off a mount
 * when it changes position. Move the god-room-enter check after such things as
 * walls.
 *
 * Revision 1.156  2008/09/11 04:22:04  jps
 * Fix bug in do_gen_door regarding rooms with no doors.
 *
 * Revision 1.155  2008/09/09 08:23:37  jps
 * Placed sector info into a struct and moved its macros into rooms.h.
 *
 * Revision 1.154  2008/09/07 01:27:15  jps
 * Moved functions to movement.c. If you stop flying while in an air
 * room (e.g., by sitting or standing) you will fall.
 *
 * Revision 1.153  2008/09/04 06:47:36  jps
 * Changed sector constants to match their strings
 *
 * Revision 1.152  2008/09/02 07:16:00  mud
 * Changing object TIMER uses into DECOMP where appropriate
 *
 * Revision 1.151  2008/09/01 23:47:49  jps
 * Using movement.h/c for movement functions.
 *
 * Revision 1.150  2008/09/01 08:35:04  jps
 * Pass vnum not rnum to entry trigger
 *
 * Revision 1.149  2008/08/30 22:02:42  myc
 * Actions affecting corpses just go to the syslog now, not the RIP log.
 *
 * Revision 1.148  2008/08/29 04:16:26  myc
 * Changed calls to look_at_room and moved its prototype to a header file.
 *
 * Revision 1.147  2008/08/28 23:42:53  rbr
 * Added do_abandon (abandon (lists follower), abandon players (abandon all players), abandon pets (abandon all charmies), abandon <player> (abandons a specific player) Note there are slight differences in whether PK is on or off.
 *
 * Revision 1.146  2008/08/19 02:11:14  jps
 * Don't apply fluid/rigidity restrictions to immortals.
 *
 * Revision 1.145  2008/08/18 01:35:38  jps
 * Replaced all \\n\\r with \\r\\n, not that it was really necessary...
 *
 * Revision 1.144  2008/08/17 20:24:31  jps
 * Rewrite doorbash.
 *
 * Revision 1.143  2008/08/09 20:35:57  jps
 * Changed sense life so that it has a chance of detecting the presence and movement
 * of creatures with a "healable" life force. Increased spell duration to 17-50 hrs.
 *
 * Revision 1.142  2008/07/10 21:23:54  jps
 * Add some fail-to-mount messages for ether critters, which are different from
 * the messages involving other "fluid" critters.
 *
 * Revision 1.141  2008/06/09 23:00:13  myc
 * Got rid of the disembark command and the ocean current list.
 *
 * Revision 1.140  2008/06/07 19:06:46  myc
 * Moved all object-related constants and structures to objects.h
 *
 * Revision 1.139  2008/05/19 06:53:45  jps
 * Got rid of fup and fdown directions.
 *
 * Revision 1.138  2008/05/18 20:16:11  jps
 * Created fight.h and set dependents.
 *
 * Revision 1.137  2008/05/18 05:18:06  jps
 * Renaming room_data struct's member "number" to "vnum", cos it's
 * a virtual number.
 *
 * Revision 1.136  2008/05/17 22:03:01  jps
 * Moving room-related code into rooms.h and rooms.c.
 *
 * Revision 1.135  2008/05/17 04:32:25  jps
 * Moved exits into exits.h/exits.c and changed the name to "exit".
 *
 * Revision 1.134  2008/05/14 05:11:28  jps
 * Using hurt_char for play-time harm, while alter_hit is for changing hp only.
 *
 * Revision 1.133  2008/05/11 05:43:26  jps
 * Using alter_hit() and alter_pos() instead of calling damage and update_pos.
 *
 * Revision 1.132  2008/04/19 22:43:09  jps
 * There's a chance of falling when you dismount.
 *
 * Revision 1.131  2008/04/14 05:11:40  jps
 * Renamed EFF_FLYING to EFF_FLY, since it only indicates an ability
 * to fly - not that the characer is actually flying.
 *
 * Revision 1.130  2008/04/14 02:18:45  jps
 * Removing unused function prototype.
 *
 * Revision 1.129  2008/04/14 01:26:18  jps
 * Change confused movement to happen only 50% of the time.
 *
 * Revision 1.128  2008/04/13 21:56:22  jps
 * Moved doorbash here from act.item.c.
 *
 * Revision 1.127  2008/04/13 20:07:25  jps
 * Implemented confusion, making it hard to walk around or
 * drag people.  Following will succeed 80% of the time, though.
 *
 * Revision 1.126  2008/04/13 01:09:51  jps
 * Don't allow rigid/fluid mount/rider pairs.
 *
 * Revision 1.125  2008/04/07 17:22:12  jps
 * Fix bug waking other characters.
 *
 * Revision 1.124  2008/04/07 15:35:47  jps
 * Updated the fly command.
 *
 * Revision 1.123  2008/04/07 04:54:55  jps
 * Fix drag bug.
 *
 * Revision 1.122  2008/04/07 03:02:37  jps
 * Made it impossible to drag solid people when you're fluid.
 *
 * Revision 1.121  2008/04/06 19:48:19  jps
 * Don't allow open/close/lock/unlock of solid objects when you're in a fluid state.
 *
 * Revision 1.120  2008/04/04 06:12:52  myc
 * Removed justice code.
 *
 * Revision 1.119  2008/04/03 02:05:34  myc
 * Depending on screen.h now.
 *
 * Revision 1.118  2008/03/28 17:54:53  myc
 * Now using flagvectors for effect, mob, player, preference, room, and
 * room effect flags.  AFF, AFF2, and AFF3 flags are now just EFF flags.
 *
 * Revision 1.117  2008/03/10 20:46:55  myc
 * Renamed POS1 to 'stance'.
 *
 * Revision 1.116  2008/03/09 18:16:08  jps
 * Implement misdirection. When you have AFF3_MISDIRECTION, you can enter
 * "<dir> <dir>" to go the first direction while pretending to go the
 * second. Each dir can be "stay".
 *
 * Revision 1.115  2008/03/08 23:31:22  jps
 * Typo!
 *
 * Revision 1.114  2008/03/08 23:28:44  jps
 * Separate AFF_CHARM from MOB2_ILLUSORY.
 *
 * Revision 1.113  2008/02/09 04:27:47  myc
 * Now relying on math header file.
 *
 * Revision 1.112  2008/02/07 01:46:14  myc
 * Removing the size_abbrevs array and renaming SIZE_ABBR to SIZE_DESC,
 * which points to the sizes array.
 *
 * Revision 1.111  2008/02/06 21:53:53  myc
 * do_enter was reading through the portal messages incorrectly.
 *
 * Revision 1.110  2008/02/02 04:27:55  myc
 * Adding leave and door manipulation triggers.
 *
 * Revision 1.109  2008/01/30 19:20:57  myc
 * Gravity is now a delayed event; don't allow people to 'move' while falling.
 * Putting the flight room check before the water room one so you get
 * the right message when trying to go up into a flight room from a
 * water one.
 *
 * Revision 1.108  2008/01/29 21:02:31  myc
 * Removing a lot of extern declarations from code files and moving
 * them to header files, mostly db.h and constants.h.
 *
 * Revision 1.107  2008/01/27 21:09:12  myc
 * Prevent characters from sitting, kneeling, reclining, resting, or
 * sleeping while berserking.
 *
 * Revision 1.106  2008/01/26 12:30:01  jps
 * Use skills.h to import improve_skill().
 *
 * Revision 1.105  2008/01/25 21:05:45  myc
 * Added attack() as a macro alias for hit() with fewer arguments.
 *
 * Revision 1.104  2008/01/24 15:43:47  myc
 * Sneak and camouflaged flags were too powereful for players.  Made them
 * lose more hide points per move.
 *
 * Revision 1.103  2008/01/15 03:18:19  myc
 * Rewrote give command so it always gives feedback.  Also made REMOVE
 * triggers not prevent immortals from removing items.
 *
 * Revision 1.102  2008/01/13 23:06:04  myc
 * Removed the statsave function.
 *
 * Revision 1.101  2008/01/11 03:35:36  myc
 * Disallow dragging live mobiles.
 *
 * Revision 1.100  2008/01/11 02:06:07  myc
 * Moved do_drag here from act.item.c.  It now allows you to drag
 * characters.  Also both characters and items may be dragged through
 * portals.
 *
 * Revision 1.99  2008/01/09 10:08:48  jps
 * Allow folks with infrared to see people moving to and from dark rooms.
 *
 * Revision 1.98  2008/01/07 11:56:31  jps
 * Allow illusory mobs to wander even if charmed.
 *
 * Revision 1.97  2008/01/04 01:53:26  jps
 * Added races.h file and created global array "races" for much
 * race-related information.
 *
 * Revision 1.96  2008/01/01 03:20:19  jps
 * Allow open/close/lock/unlock on doors even if you have a same-named
 * item in your inventory, provided it doesn't open.
 *
 * Revision 1.95  2007/12/08 21:02:45  jps
 * Abort processing of do_simple_move if a mob greet trigger has
 * killed the actor (fixes crash bug).
 *
 * Revision 1.94  2007/11/24 20:57:29  jps
 * Fix crash bug in enter - act() was being used to send a message
 * to a player while not in a room, which can cause segfaults.
 * Also fixed the markup in portal messages.
 *
 * Revision 1.93  2007/11/24 01:21:25  jps
 * Allow "open <str> <dir>" to ignore all objects and be used to
 * force the target to be a room's door.  Add a relative location string
 * to the failure message when trying to open/close/etc. an object.
 *
 * Revision 1.92  2007/10/20 19:01:48  myc
 * Make pick lock skill improve on failures.
 *
 * Revision 1.91  2007/10/17 17:18:04  myc
 * Renamed the search_block and search_block2 functions.
 * searchblock is now case sensitive, and search_block is not.
 *
 * Revision 1.90  2007/10/11 20:14:48  myc
 * Make immortals able to mount, ride, and tame anyone.
 *
 * Revision 1.89  2007/10/04 16:20:24  myc
 * Fixed a variable bug in do_enter.
 *
 * Revision 1.88  2007/10/02 02:52:27  myc
 * Changed AFF_SNEAK flag to be perma sneak so we can still have permasneak
 * mobs and permasneak items.
 *
 * Revision 1.87  2007/09/28 20:49:35  myc
 * Removed a redundant IS_AFFECTED check.
 *
 * Revision 1.86  2007/09/20 21:20:43  myc
 * Hide points and perception are in.  AFF_HIDE and ITEM_HIDDEN are now
 * unused.  Sneak chance is now calculated based on hiddenness.  Cleaned
 * up blood pool code too.  Cleaned up do_enter a lot.  Portal messages
 * are now in arrays.
 *
 * Revision 1.85  2007/09/15 15:36:48  myc
 * Natures embrace now sets camouflage bit, which lets you be hidden as long
 * as you are outside.
 *
 * Revision 1.84  2007/09/12 22:23:04  myc
 * You can now use the 'walk' and 'go' commands to travel in different
 * directions.
 *
 * Revision 1.83  2007/09/11 19:41:07  myc
 * Being an AQUATIC mobile now counts as being able to travel through water
 * squares.  Also, can't ride an aquatic mobile onto land now.
 *
 * Revision 1.82  2007/09/11 16:34:24  myc
 * Adding MOB_AQUATIC flag that prevents water mobs from entering non-water
 * rooms.
 *
 * Revision 1.81  2007/09/08 22:03:19  jps
 * Fix typo, change message in position -> standing messages.
 *
 * Revision 1.80  2007/09/03 21:20:46  jps
 * Moved the code that checks whether a magic wall is blocking movement
 * to magic.c.
 *
 * Revision 1.79  2007/08/31 05:36:19  jps
 * Add variable "destination" to mob Entry trigger.
 *
 * Revision 1.78  2007/08/20 00:41:16  jps
 * Pets won't follow anyone if someone is mounted on them. do_simple_move()
 * will handle any combination of rider/ridee correctly.
 *
 * Revision 1.77  2007/08/14 22:43:07  myc
 * Adding 'shadow' skill, which lets you follow someone sneakily.
 *
 * Revision 1.76  2007/08/05 20:21:51  myc
 * Moved do_simple_move's comments closer to the function.
 *
 * Revision 1.75  2007/08/05 01:50:16  myc
 * Follow, guard, and consent all follow similar usage patterns now.
 * <action> off/self/me stops whatever it is, <action> target starts
 * on the target, and <action> shows who is currently targeted.
 *
 * Revision 1.74  2007/08/04 22:20:38  jps
 * Different buck messages whether you're in a splashy room or not.
 *
 * Revision 1.73  2007/08/04 02:13:27  myc
 * Circle of fire damage should occur before you enter the room.
 *
 * Revision 1.72  2007/08/04 02:10:57  myc
 * You now take damage from circle of fire when entering and exiting a room.
 *
 * Revision 1.71  2007/08/03 22:00:11  myc
 * Fixed several \r\n typos in send_to_chars.
 *
 * Revision 1.70  2007/08/01 02:10:39  jps
 * Make mounts more difficult to handle.
 *
 * Revision 1.69  2007/07/14 04:17:35  jps
 * Updated call to stop_follower(), which cares whether this is being
 * done due to a violent action or not.
 *
 * Revision 1.68  2007/07/14 02:16:22  jps
 * Major revamp of mounts. The character's skill in mount/ride/tame
 * is pitted against the victim's level. Thus, a player with poor
 * skill could use a low-level mount, which would, of course, not
 * be as good as a higher-level mount.
 *
 * Revision 1.67  2007/07/14 01:42:58  jps
 * Doors can now properly lock only from one side, if builders have
 * made them that way.
 *
 * Revision 1.66  2007/05/28 23:51:35  jps
 * Make open/close interact first with equipped items.
 *
 * Revision 1.65  2007/04/25 06:41:27  jps
 * Fixed bug where you buy a mount, then mount it, and suddenly the other
 * folks following you wouldn't be any more.
 *
 * Revision 1.64  2007/04/19 00:53:54  jps
 * Create macros for stopping spellcasting.
 *
 * Revision 1.63  2007/02/08 01:30:00  myc
 * If circle of fire kills someone, hopefully the game won't crash now.
 *
 * Revision 1.62  2006/11/21 20:15:01  jps
 * Use correct pronouns in "tame" feedback.
 *
 * Revision 1.61  2006/11/20 19:52:04  jps
 * Movement cost with levitate is max 2
 *
 * Revision 1.60  2006/11/18 07:17:16  jps
 * *** empty log message ***
 *
 * Revision 1.59  2006/11/14 23:24:39  jps
 * Stop the gale winds from blowing hard on you while indoors.
 *
 * Revision 1.58  2006/11/14 22:38:04  jps
 * Fix bug of lower-level imms seeing "someone" move about when a higher
 * level invis imm moves.
 *
 * Revision 1.57  2006/11/13 15:54:22  jps
 * Fix widespread misuse of the hide_invisible parameter to act().
 *
 * Revision 1.56  2006/11/13 14:57:54  jps
 * Fix typos when mortal tried to enter a godroom.
 * Don't allow mortals to see that an imm is following, even with sense life.
 *
 * Revision 1.55  2006/11/13 13:32:25  jps
 * Prevent characters from mounting themselves.
 *
 * Revision 1.54  2006/11/12 02:31:01  jps
 * Mount goes with you through portals.
 *
 * Revision 1.53  2006/11/08 08:51:58  jps
 * Fix missing period in message about trying to fly while fighting.
 *
 * Revision 1.52  2006/11/08 08:36:54  jps
 * Fixed a lot of lay/lie typos.
 *
 * Revision 1.51  2006/11/08 08:16:14  jps
 * Fix missing period at the end of arrival message, when riding.
 *
 * Revision 1.50  2006/11/07 16:53:29  jps
 * A boat will reduce movement cost in WATER_NOSWIM from 4 to 2.
 *
 * Revision 1.49  2006/07/20 07:42:36  cjd
 * Typo fixes.
 *
 * Revision 1.48  2005/02/12 00:12:52  rls
 * Commented out that damned annoying wind knockdown effect.
 *
 * Revision 1.47  2004/11/29 01:56:31  rls
 * Fixed a bug where you could have two mobs follow each other though
 * a loop room and lag/crash the mud.  See... Druids ARE so broken!
 *
 * Revision 1.45  2004/11/01 05:18:24  jjl
 * CI'd too quick
 *
 * Revision 1.44  2004/11/01 05:17:05  jjl
 * Added pk_checks for act.movement.c
 *
 * Revision 1.43  2003/07/04 22:10:55  jjl
 * Let gods walk through doors.
 *
 * Revision 1.42  2003/06/23 01:47:09  jjl
 * Added a NOFOLLOW flag, and the "note" command, and show notes <player>
 *
 * Revision 1.41  2003/06/20 13:17:47  rls
 * Changed scale and added bonus to already sneaking people so it
 * isn't as hard to remain sneaking while walking.  Works 99.9
 * of the time for high level skill... about 1/2 the time for
 * mid range and low skill.
 *
 * Revision 1.40  2003/02/02 01:33:17  jjl
 * Fixing stand, removing the 8 second delay. Doh
 *
 * Revision 1.39  2002/12/28 21:56:42  jjl
 * Added delay for punk-mobs standing up and fleeing.
 *
 * Revision 1.38  2002/11/30 19:44:06  jjl
 * Goes to asleep?  The hell you say!
 *
 * Revision 1.37  2002/09/22 19:33:05  jjl
 * Fixed a bug pergy introduced, whereby the AFF_SNEAK bit was stripped,
 * but the actual spell wasn'. The net affect is you could get infinite
 * sneak "spells" upon yourself.
 * /
 *
 * Revision 1.36  2002/09/13 02:32:10  jjl
 * Updated header comments
 *
 * Revision 1.35  2002/07/17 17:04:37  rls
 * fixed hide & sneak check to 100 pnt based chance (get_skill uses /10)
 *
 * Revision 1.34  2002/07/16 19:38:44  rls
 * Adjusted sneak w/ hide to a 5% chance failure for those without sneak
 *
 * Revision 1.33  2002/07/16 19:29:35  rls
 * Changed movement while sneaking to require a successful sneak check every
 * round... also allowed the ability to move while hidden if successfully sneaking.
 *
 * Revision 1.32  2002/02/13 23:30:07  dce
 * Went through all of the change position commands (sit, stand
 * , kneel, sleep, etc..) to make sure that it accounted for
 * fighting and floating positions.
 *
 * Revision 1.31  2002/02/11 02:47:57  dce
 * When you stagger under a heavy load in do_simple_move, I
 * added a update_pos call.
 *
 * Revision 1.30  2001/10/16 00:13:58  rjd
 * Fly semantics improved.
 *
 * Revision 1.29  2001/10/11 07:19:25  rjd
 * Immorts+ can now fly upon demand, walk on water, and travel about
 * without having to worry about staggering under any weight.
 *
 * Revision 1.28  2001/04/07 14:14:30  dce
 * You must be the proper level to enter a portal.
 *
 * Revision 1.27  2001/03/04 15:11:33  dce
 * Fixed a crash bug in do_simple_move, changed riding to
 * RIDING(ch).
 *
 * Revision 1.26  2001/01/23 23:48:26  dce
 * Fixed's someones double declaration error
 *
 * Revision 1.25  2000/11/28 00:42:15  mtp
 * removed mobprog references
 *
 * Revision 1.24  2000/11/20 03:43:03  rsd
 * Added back rlog messages from prior to the addition of
 * the $log$ string
 *
 * Revision 1.23  2000/11/03 17:28:33  jimmy
 * Added better checks for real_room to stop players/objs from
 * being placed in room NOWHERE.  This should help pinpoint any
 * weirdness.
 *
 * Revision 1.22  2000/09/19 21:37:17  rsd
 * fixed a typo in riding.
 *
 * Revision 1.21  2000/09/18 01:32:48  rsd
 * altered the mount and riding code checks to add skill bonus'
 * for the mount being charmed.
 *
 * Revision 1.20  2000/09/07 01:54:32  rsd
 * added a space at the end of the riding message to fix a typo.
 *
 * Revision 1.19  2000/06/05 18:43:56  rsd
 * Revision 1.18  2000/06/05 00:21:06  rsd
 * Retabbed and braced sections of do_dimple_move also fixed
 * the riding check in do simple move to be a little more
 * sane. I think.
 * For 1.19
 * I added bonuses for charisma and class to tame.
 * I added bonuses for class to mount and riding.
 * I also added a tame check to riding.
 *
 * Revision 1.17  2000/05/31 21:54:14  rsd
 * made the likelyhood of getting bucked decrease dramatically
 *
 * Revision 1.16  2000/04/22 22:25:46  rsd
 * removed hubis reference
 *
 * Revision 1.15  2000/04/21 00:56:44  rsd
 * tabbed and braced perform_move as well as fixed the following
 * into dark bug associated with can see.
 *
 * Revision 1.14  2000/04/17 00:49:58  rsd
 * altered comment header while browsing the file
 *
 * Revision 1.13  1999/12/06 20:19:59  cso
 * Fixed a typo in line "You are too busy fighting.."
 *
 * Revision 1.12  1999/11/28 22:35:18  cso
 * mobs affected by charm can now do_move if they're also animated.
 * this allows 'order fol north'.
 *
 * Revision 1.11  1999/09/08 07:06:03  jimmy
 * More insure++ runtime fixes.  Some small, but hardcore fixes mostly to do
 * with blood and killing
 * --gurlaek
 *
 * Revision 1.10  1999/09/05 07:00:39  jimmy
 * Added RCS Log and Id strings to each source file
 *
 * Revision 1.9  1999/09/03 23:05:48  mtp
 * added soe IS_FIGHTING checks
 *
 * Revision 1.8  1999/07/25 04:33:11  jimmy
 * No more following people if you can't see them. Invis/dark etc.
 * gurlaek
 *
 * Revision 1.7  1999/06/10 16:56:28  mud
 * This is a mass check in after a code freeze due to an upgrade to RedHat 6.0.
 * This fixes all of the warnings associated with the new compiler and
 * libraries.  Many many curly braces had to be added to "if" statements to
 * clarify their behavior to the compiler.  The name approval code was also
 * debugged, and tested to be stable.  The xnames list was converted from an
 * array to a linked list to allow for on the fly adding of names to the
 * xnames list.  This code compiles fine under both gcc RH5.2 and egcs RH6.0
 *
 * Revision 1.6  1999/04/07 01:20:18  dce
 * Allows extra descriptions on no exits.
 *
 * Revision 1.5  1999/03/14 14:28:11  jimmy
 * Movement now has bite!  removed extra "flying" from
 * movement_loss in constants.c to fix the mv bug.  reduced the
 * movement gain by 5 for all ages in limits.c.  Removed the +5
 * and +6 static movement gain so that it now actually updates
 * based on the function in regen.c.  Gosh i'm a bastard.
 * Fingon
 *
 * Revision 1.4  1999/03/14 03:18:02  jimmy
 * Fixed the mount fleeing while your fighting after his TAME wear's off
 * crashbug i hope.  Also, fixed do_tame to work properly.  fixed
 * do_dismount to check for flying in water rooms. fixed do_simple_move
 * to properly handle boats and fly in water rooms while mounted or not
 * mounted.  Lots of little things in the mount code.
 * fingon
 *
 * Revision 1.3  1999/03/12 18:05:43  dce
 * No flying over oceans
 *
 * Revision 1.2  1999/02/20 18:41:36  dce
 * Adds improve_skill calls so that players can imprve their skills.
 *
 * Revision 1.1  1999/01/29 01:23:30  mud
 * Initial revision
 *
 ***************************************************************************/
