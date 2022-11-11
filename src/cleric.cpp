/***************************************************************************
 *  File: cleric.c                                        Part of FieryMUD *
 *  Usage: Control Cleric type mobs, this file is closely related to ai.h  *
 *         and ai_util.c                                                   *
 *  By: Ben Horner (Proky of HubisMUD)                                     *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  FieryMUD Copyright (C) 1998, 1999, 2000 by the Fiery Consortium        *
 ***************************************************************************/
#include "cleric.hpp"

#include "ai.hpp"
#include "casting.hpp"
#include "class.hpp"
#include "comm.hpp"
#include "conf.hpp"
#include "db.hpp"
#include "handler.hpp"
#include "interpreter.hpp"
#include "lifeforce.hpp"
#include "magic.hpp"
#include "math.hpp"
#include "races.hpp"
#include "skills.hpp"
#include "structs.hpp"
#include "sysdep.hpp"
#include "utils.hpp"

/* External functions */
int mob_cast(CharData *ch, CharData *tch, ObjData *tobj, int spellnum);
bool affected_by_armor_spells(CharData *victim);

/*
 * cleric_ai_action
 *
 *
 */
bool cleric_ai_action(CharData *ch, CharData *victim) {
    int my_health, victim_health, i, counter, action = 0;

    if (!victim) {
        mudlog("No victim in cleric AI action.", NRM, LVL_GOD, false);
        return false;
    }

    /* Well no chance of casting any spells today. */
    if (EFF_FLAGGED(ch, EFF_SILENCE))
        return false;

    /* Most classes using clerical spells have alignment restrictions. */
    if ((GET_CLASS(ch) == CLASS_DIABOLIST && !IS_EVIL(ch)) || (GET_CLASS(ch) == CLASS_PRIEST && !IS_GOOD(ch)) ||
        (GET_CLASS(ch) == CLASS_PALADIN && !IS_GOOD(ch)) || (GET_CLASS(ch) == CLASS_RANGER && !IS_GOOD(ch)) ||
        (GET_CLASS(ch) == CLASS_ANTI_PALADIN && !IS_EVIL(ch)))
        return false;

    /* Calculate mob and victim health as a percentage. */
    my_health = (100 * GET_HIT(ch)) / GET_MAX_HIT(ch);
    victim_health = (100 * GET_HIT(victim)) / GET_MAX_HIT(victim);

    if (my_health > 90)
        action = 10;
    else if (my_health > 60)
        action = 4;
    else if (my_health < 30)
        action = 2;
    if (victim_health < 20)
        action += 3;
    else if (victim_health < 10)
        action += 5;

    /* If action < 6 then heal. */
    if (action < 6 && mob_heal_up(ch))
        return true;

    /* Otherwise kill or harm in some fashion */

    /* Area effects first if the victim is grouped. */
    if (group_size(victim) > 1) {
        counter = 0;
        for (i = 0; mob_cleric_area_spells[i]; i++) {
            if (!GET_SKILL(ch, mob_cleric_area_spells[i]))
                continue;
            switch (mob_cleric_area_spells[i]) {
            case SPELL_HOLY_WORD:
                /* Don't cast if it's suicidal or useless. */
                if (IS_EVIL(ch) || !evil_in_group(victim))
                    continue;
                break;
            case SPELL_UNHOLY_WORD:
                if (IS_GOOD(ch) || !good_in_group(victim))
                    continue;
                break;
            case SPELL_EARTHQUAKE:
                /* Only cast earthquake outside and in ground. */
                if (!QUAKABLE(CH_NROOM(ch)))
                    continue;
                break;
            }
            if (mob_cast(ch, victim, nullptr, mob_cleric_area_spells[i]))
                return true;
            /* Only try the mob's best two spells. */
            if (++counter >= 2)
                break;
        }
    }

    /* Try to cause an offensive affection. Only attempt one. */
    for (i = 0; mob_cleric_hindrances[i].spell; i++) {
        if (!GET_SKILL(ch, mob_cleric_hindrances[i].spell))
            continue;
        if (!has_effect(victim, &mob_cleric_hindrances[i])) {
            if (mob_cast(ch, victim, nullptr, mob_cleric_hindrances[i].spell))
                return true;
            else
                break;
        }
    }

    /* Now attempt a harming spell. */
    for (i = 0, counter = 0; mob_cleric_offensives[i]; i++) {
        switch (mob_cleric_offensives[i]) {
        case SPELL_DISPEL_EVIL:
            if (!IS_GOOD(ch) || !IS_EVIL(victim))
                continue;
            break;
        case SPELL_DISPEL_GOOD:
            if (!IS_EVIL(ch) || !IS_GOOD(victim))
                continue;
            break;
        case SPELL_FLAMESTRIKE:
            if (!IS_GOOD(ch))
                continue; /* Not worth casting if alignment is too low */
            break;
        case SPELL_DIVINE_RAY:
            if (GET_ALIGNMENT(ch) <= 650)
                continue;
            break;
        case SPELL_DESTROY_UNDEAD:
            if (GET_LIFEFORCE(victim) != LIFE_UNDEAD)
                continue;
            break;
        case SPELL_STYGIAN_ERUPTION:
            if (!IS_EVIL(ch))
                continue;
            break;
        case SPELL_HELL_BOLT:
            if (IS_EVIL(victim))
                continue;
            break;
        case SPELL_DIVINE_BOLT:
            if (IS_GOOD(victim))
                continue;
            break;
        }
        if (mob_cast(ch, victim, nullptr, mob_cleric_offensives[i]))
            return true;
        else
            counter++;
        /* Only attempt the mob's two best spells.  The rest are worthless. */
        if (counter > 2)
            break;
    }

    return false;
}

/*
 * check_cleric_status
 *
 * Makes the cleric mob check its spells.  Unlike the sorcerer function of
 * similar name, this one shouldn't be called when the mob is in combat.
 * Cleric spells are all pretty useless in battle.
 */
bool check_cleric_status(CharData *ch) {
    int i;

    /* Check bad affects */
    for (i = 0; mob_cleric_hindrances[i].spell; i++) {
        if (!GET_SKILL(ch, mob_cleric_hindrances[i].spell))
            continue;

        /* If the spell can be removed and the mob has it, try to remove it */
        if (mob_cleric_hindrances[i].remover && has_effect(ch, &mob_cleric_hindrances[i]))
            if (mob_cast(ch, ch, nullptr, mob_cleric_hindrances[i].remover))
                return true;
        /* 10% chance to cancel if in combat. */
        if (FIGHTING(ch) && !number(0, 9))
            return false;
    }

    /* Check other spells */
    for (i = 0; mob_cleric_buffs[i].spell; i++) {
        if (!GET_SKILL(ch, mob_cleric_buffs[i].spell) || !check_fluid_spell_ok(ch, ch, mob_cleric_buffs[i].spell, true))
            continue;
        switch (mob_cleric_buffs[i].spell) {
        case SPELL_GAIAS_CLOAK:
            if (CH_INDOORS(ch) || SECT(ch->in_room) == SECT_UNDERWATER || SECT(ch->in_room) == SECT_UNDERDARK)
                continue;
            /* The armor spells don't mix. */
            if (affected_by_armor_spells(ch))
                continue;
            break;
        case SPELL_DEMONSKIN:
            if (IS_GOOD(ch))
                continue;
        case SPELL_ARMOR:
        case SPELL_BARKSKIN:
            /* The armor spells don't mix. */
            if (affected_by_armor_spells(ch))
                continue;
            break;
        case SPELL_DEMONIC_ASPECT:
        case SPELL_DEMONIC_MUTATION:
            /* Demonic mutation and demonic aspect don't mix. */
            if (affected_by_spell(ch, SPELL_DEMONIC_ASPECT) || affected_by_spell(ch, SPELL_DEMONIC_MUTATION))
                continue;
            break;
        case SPELL_PROT_FROM_EVIL:
            if (IS_EVIL(ch) || EFF_FLAGGED(ch, EFF_PROTECT_EVIL))
                continue;
            break;
        case SPELL_PROT_FROM_GOOD:
            if (IS_GOOD(ch) || EFF_FLAGGED(ch, EFF_PROTECT_GOOD))
                continue;
            break;
        case SPELL_SOULSHIELD:
            if (IS_NEUTRAL(ch))
                continue;
            if (EFF_FLAGGED(ch, EFF_SOULSHIELD))
                continue;
            break;
        case SPELL_DARK_PRESENCE:
            if (IS_GOOD(ch) || affected_by_spell(ch, SPELL_BLESS) || affected_by_spell(ch, SPELL_DARK_PRESENCE))
                continue;
            break;
        default:
            if (has_effect(ch, &mob_cleric_buffs[i]))
                continue;
        }
        if (mob_cast(ch, ch, nullptr, mob_cleric_buffs[i].spell))
            return true;
    }

    return false;
}