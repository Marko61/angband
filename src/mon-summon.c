/**
 * \file mon-summon.c
 * \brief Monster summoning
 *
 * Copyright (c) 1997-2007 Ben Harrison, James E. Wilson, Robert A. Koeneke
 *
 * This work is free software; you can redistribute it and/or modify it
 * under the terms of either:
 *
 * a) the GNU General Public License as published by the Free Software
 *    Foundation, version 2, or
 *
 * b) the "Angband licence":
 *    This software may be copied and distributed for educational, research,
 *    and not for profit purposes provided that this copyright and statement
 *    are included in all such copies.  Other copyrights may also apply.
 */

#include "angband.h"
#include "cave.h"
#include "mon-group.h"
#include "mon-make.h"
#include "mon-summon.h"
#include "mon-util.h"

/**
 * The "type" of the current "summon specific"
 */
static int summon_specific_type = 0;

/**
 * Maximum number of summon types
 */
static int summon_max = 0;

/**
 * The kin base for S_KIN
 */
struct monster_base *kin_base;

/**
 * The summon array
 */
struct summon *summons;


/**
 * Lookup function to translate names of summons to indices
 */
int summon_name_to_idx(const char *name)
{
    int i;
    for (i = 0; i < summon_max; i++) {
        if (name && streq(name, summons[i].name)) {
            return i;
		}
    }

    return -1;
}

/**
 * The message type for a particular summon
 */
int summon_message_type(int summon_type)
{
	return summons[summon_type].message_type;
}

/**
 * The fallback type for a particular summon
 */
int summon_fallback_type(int summon_type)
{
	return summons[summon_type].fallback;
}

/**
 * The description for a particular summon
 */
const char *summon_desc(int type)
{
	if (type < 0 || type >= summon_max)
		return 0;

	return summons[type].desc;
}

/**
 * Allocate the summons list array and initialize it from the
 * result of running the summon_parser.
 */
void create_summons(struct summon *parsed_summons)
{
	struct summon *summon, *next;
	int count = 0;

	/* Count the entries */
	summon_max = 0;
	summon = parsed_summons;
	while (summon) {
		summon_max++;
		summon = summon->next;
	}

	/* Allocate the direct access list array and copy the data to it */
	summons = mem_zalloc((summon_max + 1) * sizeof(*summon));
	for (summon = parsed_summons; summon; summon = next, count++) {
		memcpy(&summons[count], summon, sizeof(*summon));
		next = summon->next;
		summons[count].next = NULL;

		mem_free(summon);
	}
	summon_max += 1;

	/* Add indices of fallback summons */
	for (count = 0; count < summon_max; count++) {
		char *name = summons[count].fallback_name;
		summons[count].fallback = summon_name_to_idx(name);
	}
}

/**
 * Free the summons list array. See the summon_parser cleanup.
 */
void free_summons(void)
{
	int idx;
	for (idx = 0; idx < summon_max; idx++) {
		struct monster_base_list *s = summons[idx].bases;
		while (s) {
			struct monster_base_list *next = s->next;
			mem_free(s);
			s = next;
		}
		string_free(summons[idx].desc);
		string_free(summons[idx].fallback_name);
		string_free(summons[idx].name);
	}
	mem_free(summons);
}

/**
 * Decide if a monster race is "okay" to summon.
 *
 * Compares the given monster to the monster type specified by
 * summon_specific_type. Returns true if the monster is eligible to
 * be summoned, false otherwise.
 */
static bool summon_specific_okay(struct monster_race *race)
{
	struct summon *summon = &summons[summon_specific_type];
	struct monster_base_list *bases = summon->bases;
	bool unique = rf_has(race->flags, RF_UNIQUE);

	/* Forbid uniques? */
	if (!summon->unique_allowed && unique) {
		return false;
	}

	/* A valid base and no match means disallowed */
	while (bases) {
		if (race->base == bases->base) break;
		if (bases->next == NULL) return false;
		bases = bases->next;
	}

	/* A valid race flag and no match means disallowed */
	if (summon->race_flag && !rf_has(race->flags, summon->race_flag)) {
		return false;
	}

	/* Special case - summon kin */
	if (summon_specific_type == summon_name_to_idx("KIN")) {
		return (!unique && race->base == kin_base);
	}

	/* If we made it here, we're fine */
	return true;
}

/**
 * Check to see if you can call the monster
 */
static bool can_call_monster(struct loc grid, struct monster *mon)
{
	/* Skip dead monsters */
	if (!mon->race) return (false);

	/* Only consider callable monsters */
	if (!summon_specific_okay(mon->race)) return (false);

	/* Make sure the summoned monster is not in LOS of the summoner */
	if (los(cave, grid, mon->grid)) return (false);

	return (true);
}


/**
 * Calls a monster from the level and moves it to the desired spot
 */
static int call_monster(struct loc grid)
{
	int i, mon_count, choice;
	int *mon_indices;
	struct monster *mon;

	mon_count = 0;

	for (i = 1; i < cave_monster_max(cave); i++) {
		mon = cave_monster(cave, i);

		/* Figure out how many good monsters there are */
		if (can_call_monster(grid, mon)) mon_count++;
	}

	/* There were no good monsters on the level */
	if (mon_count == 0) return (0);

	/* Make the array */
	mon_indices = mem_zalloc(mon_count * sizeof(int));

	/* Reset mon_count */
	mon_count = 0;

	/* Now go through a second time and store the indices */
	for (i = 1; i < cave_monster_max(cave); i++) {
		mon = cave_monster(cave, i);

		/* Save the values of the good monster */
		if (can_call_monster(grid, mon)){
			mon_indices[mon_count] = i;
			mon_count++;
		}
	}

	/* Pick one */
	choice = randint0(mon_count - 1);

	/* Get the lucky monster */
	mon = cave_monster(cave, mon_indices[choice]);
	mem_free(mon_indices);

	/* Swap the monster */
	monster_swap(mon->grid, grid);

	/* Wake it up, make it aware */
	monster_wake(mon, false, 100);

	/* Set it's energy to 0 */
	mon->energy = 0;

	return (mon->race->level);
}


/**
 * Places a monster (of the specified "type") near the given
 * location.  Return the siummoned monster's level iff a monster was
 * actually summoned.
 *
 * We will attempt to place the monster up to 10 times before giving up.
 *
 * This function takes the "monster level"
 * of the summoning monster as a parameter, and use that, along with
 * the current dungeon level, to help determine the level of the
 * desired monster.  Note that this is an upper bound, and also
 * tends to "prefer" monsters of that level.  Currently, we use
 * the average of the dungeon and monster levels, and then add
 * five to allow slight increases in monster power.
 *
 * Note that we use the new "monster allocation table" creation code
 * to restrict the "get_mon_num()" function to the set of "legal"
 * monsters, making this function much faster and more reliable.
 *
 * Note that this function may not succeed, though this is very rare.
 */
int summon_specific(struct loc grid, int lev, int type, bool delay, bool call)
{
	int i;
	struct loc near;
	struct monster *mon;
	struct monster_race *race;
	struct monster_group_info info = { 0, 0 };

	/* Look for a location, allow up to 4 squares away */
	for (i = 0; i < 60; ++i) {
		/* Pick a distance */
		int d = (i / 15) + 1;

		/* Pick a location */
		scatter(cave, &near, grid, d, true);

		/* Require "empty" floor grid */
		if (!square_isempty(cave, near)) continue;

		/* No summon on glyphs */
		if (square_iswarded(cave, near) || square_isdecoyed(cave, near)) {
			continue;
		}

		/* Okay */
		break;
	}

	/* Failure */
	if (i == 60) return (0);

	/* Save the "summon" type */
	summon_specific_type = type;

	/* Use the new calling scheme if requested */
	if (call && (type != summon_name_to_idx("UNIQUE")) &&
		(type != summon_name_to_idx("WRAITH"))) {
		return (call_monster(near));
	}

	/* Prepare allocation table */
	get_mon_num_prep(summon_specific_okay);

	/* Pick a monster, using the level calculation */
	race = get_mon_num((player->depth + lev) / 2 + 5);

	/* Prepare allocation table */
	get_mon_num_prep(NULL);

	/* Handle failure */
	if (!race) return (0);

	/* Put summons in the group of any summoner */
	if (cave->mon_current > 0) {
		struct monster_group *group = summon_group(cave, cave->mon_current);
		info.index = group->index;
		info.role = MON_GROUP_SUMMON;
	}

	/* Attempt to place the monster (awake, don't allow groups) */
	if (!place_new_monster(cave, near, race, false, false, info,
						   ORIGIN_DROP_SUMMON)) {
		return (0);
	}

	/* Success, return the level of the monster */
	mon = square_monster(cave, near);

	/* If delay, try to let the player act before the summoned monsters,
	 * including holding faster monsters for the required number of turns */
	if (delay) {
		int turns = (mon->race->speed + 9 - player->state.speed) / 10;
		mon->energy = 0;
		if (turns) {
			/* Set timer directly to avoid resistance */
			mon->m_timed[MON_TMD_HOLD] = turns;
		}
	}

	return (mon->race->level);
}

/**
 * Select a race for a monster shapechange from its possible summons
 */
struct monster_race *select_shape(struct monster *mon, int type)
{
	struct monster_race *race = NULL;

	/* Save the "summon" type */
	summon_specific_type = type;

	/* Prepare allocation table */
	get_mon_num_prep(summon_specific_okay);

	/* Pick a monster */
	race = get_mon_num(player->depth + 5);

	/* Prepare allocation table */
	get_mon_num_prep(NULL);

	return race;
}
