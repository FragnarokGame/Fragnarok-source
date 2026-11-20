/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
//
#include "g_local.h"

// g_client.c -- client functions that don't happen every frame

static vec3_t	playerMins = {-15, -15, -24};
static vec3_t	playerMaxs = {15, 15, 32};

static qboolean G_QuietBotJoins( void ) {
#ifdef STANDALONE
	int target = trap_Cvar_VariableIntegerValue( "g_targetPopulation" );
	if ( target < 0 ) {
		return !g_dedicated.integer;
	}
	return level.campaign || target > 64;
#else
	return qfalse;
#endif
}

/*QUAKED info_player_deathmatch (1 0 1) (-16 -16 -24) (16 16 32) initial
potential spawning position for deathmatch games.
The first time a player enters the game, they will be at an 'initial' spot.
Targets will be fired when someone spawns in on them.
"nobots" will prevent bots from using this spot.
"nohumans" will prevent non-bots from using this spot.
*/
void SP_info_player_deathmatch( gentity_t *ent ) {
	int		i;

	G_SpawnInt( "nobots", "0", &i);
	if ( i ) {
		ent->flags |= FL_NO_BOTS;
	}
	G_SpawnInt( "nohumans", "0", &i );
	if ( i ) {
		ent->flags |= FL_NO_HUMANS;
	}
}

/*QUAKED info_player_start (1 0 0) (-16 -16 -24) (16 16 32)
equivalent to info_player_deathmatch
*/
void SP_info_player_start(gentity_t *ent) {
	ent->classname = "info_player_deathmatch";
	SP_info_player_deathmatch( ent );
}

/*QUAKED info_player_intermission (1 0 1) (-16 -16 -24) (16 16 32)
The intermission will be viewed from this point.  Target an info_notnull for the view direction.
*/
void SP_info_player_intermission( gentity_t *ent ) {

}



/*
=======================================================================

  SelectSpawnPoint

=======================================================================
*/

/*
================
SpotWouldTelefrag

================
*/
qboolean SpotWouldTelefrag( gentity_t *spot ) {
	int			i, num;
	int			touch[MAX_GENTITIES];
	gentity_t	*hit;
	vec3_t		mins, maxs;

	VectorAdd( spot->s.origin, playerMins, mins );
	VectorAdd( spot->s.origin, playerMaxs, maxs );
	num = trap_EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );

	for (i=0 ; i<num ; i++) {
		hit = &g_entities[touch[i]];
		//if ( hit->client && hit->client->ps.stats[STAT_HEALTH] > 0 ) {
		if ( hit->client) {
			return qtrue;
		}

	}

	return qfalse;
}

/*
================
SelectNearestDeathmatchSpawnPoint

Find the spot that we DON'T want to use
================
*/
#define	MAX_SPAWN_POINTS	128
gentity_t *SelectNearestDeathmatchSpawnPoint( vec3_t from ) {
	gentity_t	*spot;
	vec3_t		delta;
	float		dist, nearestDist;
	gentity_t	*nearestSpot;

	nearestDist = 999999;
	nearestSpot = NULL;
	spot = NULL;

	while ((spot = G_Find (spot, FOFS(classname), "info_player_deathmatch")) != NULL) {

		VectorSubtract( spot->s.origin, from, delta );
		dist = VectorLength( delta );
		if ( dist < nearestDist ) {
			nearestDist = dist;
			nearestSpot = spot;
		}
	}

	return nearestSpot;
}


/*
================
SelectRandomDeathmatchSpawnPoint

go to a random point that doesn't telefrag
================
*/
#define	MAX_SPAWN_POINTS	128
gentity_t *SelectRandomDeathmatchSpawnPoint(qboolean isbot) {
	gentity_t	*spot;
	int			count;
	int			selection;
	gentity_t	*spots[MAX_SPAWN_POINTS];

	count = 0;
	spot = NULL;

	while((spot = G_Find (spot, FOFS(classname), "info_player_deathmatch")) != NULL && count < MAX_SPAWN_POINTS)
	{
		if(SpotWouldTelefrag(spot))
			continue;

		if(((spot->flags & FL_NO_BOTS) && isbot) ||
		   ((spot->flags & FL_NO_HUMANS) && !isbot))
		{
			// spot is not for this human/bot player
			continue;
		}

		spots[count] = spot;
		count++;
	}

	if ( !count ) {	// no spots that won't telefrag
		return G_Find( NULL, FOFS(classname), "info_player_deathmatch");
	}

	selection = rand() % count;
	return spots[ selection ];
}

/*
===========
SelectRandomFurthestSpawnPoint

Chooses a player start, deathmatch start, etc
============
*/
gentity_t *SelectRandomFurthestSpawnPoint ( vec3_t avoidPoint, vec3_t origin, vec3_t angles, qboolean isbot ) {
	gentity_t	*spot;
	vec3_t		delta;
	float		dist;
	float		list_dist[MAX_SPAWN_POINTS];
	gentity_t	*list_spot[MAX_SPAWN_POINTS];
	int			numSpots, rnd, i, j;

	numSpots = 0;
	spot = NULL;

	while((spot = G_Find (spot, FOFS(classname), "info_player_deathmatch")) != NULL)
	{
		if(SpotWouldTelefrag(spot))
			continue;

		if(((spot->flags & FL_NO_BOTS) && isbot) ||
		   ((spot->flags & FL_NO_HUMANS) && !isbot))
		{
			// spot is not for this human/bot player
			continue;
		}

		VectorSubtract( spot->s.origin, avoidPoint, delta );
		dist = VectorLength( delta );

		for (i = 0; i < numSpots; i++)
		{
			if(dist > list_dist[i])
			{
				if (numSpots >= MAX_SPAWN_POINTS)
					numSpots = MAX_SPAWN_POINTS - 1;
					
				for(j = numSpots; j > i; j--)
				{
					list_dist[j] = list_dist[j-1];
					list_spot[j] = list_spot[j-1];
				}
				
				list_dist[i] = dist;
				list_spot[i] = spot;
				
				numSpots++;
				break;
			}
		}
		
		if(i >= numSpots && numSpots < MAX_SPAWN_POINTS)
		{
			list_dist[numSpots] = dist;
			list_spot[numSpots] = spot;
			numSpots++;
		}
	}
	
	if(!numSpots)
	{
		spot = G_Find(NULL, FOFS(classname), "info_player_deathmatch");

		if (!spot)
			G_Error( "Couldn't find a spawn point" );

		VectorCopy (spot->s.origin, origin);
		origin[2] += 9;
		VectorCopy (spot->s.angles, angles);
		return spot;
	}

	// select a random spot from the spawn points furthest away
	rnd = random() * (numSpots / 2);

	VectorCopy (list_spot[rnd]->s.origin, origin);
	origin[2] += 9;
	VectorCopy (list_spot[rnd]->s.angles, angles);

	return list_spot[rnd];
}

/*
===========
SelectSpawnPoint

Chooses a player start, deathmatch start, etc
============
*/
#ifdef STANDALONE
/*
===========
Spawn lists

Big maps carry hundreds of spawn points. The stock selectors only look at the
first or furthest 128, which piles a 1024-player match into a few corners, so
every candidate is collected once per map load and sampled at random.

Team deathmatch uses team_CTF_redspawn and team_CTF_bluespawn. A spot with a
"stages" key (Bastion's assault markers) only counts when stage 1 is listed.
Maps without enough team spawns split their deathmatch spawns into two ends
along the map's long axis, so the teams still start on opposite sides.
============
*/
#define	MAX_SPAWN_LIST			2048
#define	MIN_TEAM_SPAWNS			4
#define	SPAWN_AVOID_DIST		1024.0f
#define	SPAWN_PICK_ATTEMPTS		24

typedef struct {
	gentity_t	*spots[MAX_SPAWN_LIST];
	int			count;
} spawnList_t;

static spawnList_t	dmSpawnList;
static spawnList_t	teamSpawnList[TEAM_NUM_TEAMS];

static void G_AddSpawnToList( spawnList_t *list, gentity_t *spot ) {
	if ( list->count < MAX_SPAWN_LIST ) {
		list->spots[list->count++] = spot;
	}
}

static void G_CollectSpawns( spawnList_t *list, const char *classname, qboolean stageOne ) {
	gentity_t *spot = NULL;

	while ( ( spot = G_Find( spot, FOFS(classname), classname ) ) != NULL ) {
		// count holds the assault stage bits; 0 means every stage
		if ( stageOne && spot->count && !( spot->count & 2 ) ) {
			continue;
		}
		G_AddSpawnToList( list, spot );
	}
}

typedef struct {
	float		angle;
	gentity_t	*spot;
} spawnAngle_t;

static spawnAngle_t spawnAngles[MAX_SPAWN_LIST];

static int QDECL G_CompareSpawnAngles( const void *a, const void *b ) {
	float d = ((const spawnAngle_t *)a)->angle - ((const spawnAngle_t *)b)->angle;

	return d < 0 ? -1 : ( d > 0 ? 1 : 0 );
}

/*
Three or four teams: sort the spawns by angle around their average position
and cut the circle into equal-count wedges, starting at the widest empty gap.
Equal-angle wedges left some teams with no spawns on maps whose spawns
cluster on one side.
*/
static void G_DistributeSpawnWedges( int teamCount ) {
	vec3_t centre;
	float gap, bestGap = -1.0f;
	int i, n = dmSpawnList.count, start = 0;

	VectorClear( centre );
	for ( i = 0; i < n; i++ ) {
		VectorAdd( centre, dmSpawnList.spots[i]->s.origin, centre );
	}
	VectorScale( centre, 1.0f / n, centre );
	for ( i = 0; i < n; i++ ) {
		spawnAngles[i].spot = dmSpawnList.spots[i];
		spawnAngles[i].angle = atan2( spawnAngles[i].spot->s.origin[1] - centre[1],
			spawnAngles[i].spot->s.origin[0] - centre[0] );
	}
	qsort( spawnAngles, n, sizeof( spawnAngles[0] ), G_CompareSpawnAngles );
	for ( i = 0; i < n; i++ ) {
		gap = i ? spawnAngles[i].angle - spawnAngles[i - 1].angle
			: spawnAngles[0].angle + 2.0f * M_PI - spawnAngles[n - 1].angle;
		if ( gap > bestGap ) {
			bestGap = gap;
			start = i;
		}
	}
	for ( i = 0; i < n; i++ ) {
		G_AddSpawnToList( &teamSpawnList[BG_TeamForIndex( i * teamCount / n )],
			spawnAngles[( start + i ) % n].spot );
	}
}

static void G_DistributeDeathmatchSpawns( int teamCount ) {
	vec3_t mins, maxs;
	float along, range;
	int axis, i, j, teamIndex;

	if ( !dmSpawnList.count ) {
		return;
	}
	for ( i = 0; i < teamCount; i++ ) {
		teamSpawnList[BG_TeamForIndex( i )].count = 0;
	}
	if ( teamCount > 2 ) {
		G_DistributeSpawnWedges( teamCount );
		return;
	}
	VectorCopy( dmSpawnList.spots[0]->s.origin, mins );
	VectorCopy( dmSpawnList.spots[0]->s.origin, maxs );
	for ( i = 1; i < dmSpawnList.count; i++ ) {
		for ( j = 0; j < 2; j++ ) {
			along = dmSpawnList.spots[i]->s.origin[j];
			if ( along < mins[j] ) mins[j] = along;
			if ( along > maxs[j] ) maxs[j] = along;
		}
	}
	axis = ( maxs[1] - mins[1] > maxs[0] - mins[0] ) ? 1 : 0;
	range = maxs[axis] - mins[axis];
	for ( i = 0; i < dmSpawnList.count; i++ ) {
		along = dmSpawnList.spots[i]->s.origin[axis];
		teamIndex = range > 1.0f ? (int)( ( along - mins[axis] ) * teamCount / range ) : i % teamCount;
		if ( teamIndex >= teamCount ) {
			teamIndex = teamCount - 1;
		}
		G_AddSpawnToList( &teamSpawnList[BG_TeamForIndex( teamIndex )], dmSpawnList.spots[i] );
	}
}

/*
Four teams on a map with red and blue base spawns: each base is split in two
across the line between the bases, so red and green share the red base and
blue and yellow the blue one, a neighbour close by and the other pair far
away. Spawns within BASE_SPLIT_GAP of the base's own centre line (on Skyreach
the tower front, back and spiral) are left out so the two halves don't spawn
on top of each other; small bases that can't spare them split down the middle.
*/
#define	BASE_SPLIT_GAP			600

static spawnList_t	baseHalves[4];

static int G_SplitBaseHalf( spawnList_t *base, const vec3_t perp, float gap,
		spawnList_t *pos, spawnList_t *neg ) {
	vec3_t centre;
	float offset;
	int i;

	pos->count = neg->count = 0;
	VectorClear( centre );
	for ( i = 0; i < base->count; i++ ) {
		VectorAdd( centre, base->spots[i]->s.origin, centre );
	}
	VectorScale( centre, 1.0f / base->count, centre );
	for ( i = 0; i < base->count; i++ ) {
		offset = ( base->spots[i]->s.origin[0] - centre[0] ) * perp[0] +
			( base->spots[i]->s.origin[1] - centre[1] ) * perp[1];
		if ( offset > gap ) {
			G_AddSpawnToList( pos, base->spots[i] );
		} else if ( offset < -gap || ( gap == 0 && offset < 0 ) ) {
			G_AddSpawnToList( neg, base->spots[i] );
		} else if ( gap == 0 ) {
			G_AddSpawnToList( pos, base->spots[i] );
		}
	}
	return pos->count < neg->count ? pos->count : neg->count;
}

static qboolean G_SplitBaseSpawns( void ) {
	spawnList_t *red = &teamSpawnList[TEAM_RED], *blue = &teamSpawnList[TEAM_BLUE];
	vec3_t redCentre, blueCentre, perp;
	float gap;
	int i, pass;

	if ( red->count < MIN_TEAM_SPAWNS * 2 || blue->count < MIN_TEAM_SPAWNS * 2 ) {
		return qfalse;
	}
	VectorClear( redCentre );
	VectorClear( blueCentre );
	for ( i = 0; i < red->count; i++ ) {
		VectorAdd( redCentre, red->spots[i]->s.origin, redCentre );
	}
	for ( i = 0; i < blue->count; i++ ) {
		VectorAdd( blueCentre, blue->spots[i]->s.origin, blueCentre );
	}
	VectorScale( redCentre, 1.0f / red->count, redCentre );
	VectorScale( blueCentre, 1.0f / blue->count, blueCentre );
	VectorSet( perp, redCentre[1] - blueCentre[1], blueCentre[0] - redCentre[0], 0 );
	if ( VectorNormalize( perp ) < 1.0f ) {
		return qfalse;
	}
	for ( pass = 0; pass < 2; pass++ ) {
		gap = pass ? 0 : BASE_SPLIT_GAP;
		if ( G_SplitBaseHalf( red, perp, gap, &baseHalves[0], &baseHalves[1] ) >= MIN_TEAM_SPAWNS &&
			G_SplitBaseHalf( blue, perp, gap, &baseHalves[2], &baseHalves[3] ) >= MIN_TEAM_SPAWNS ) {
			// red and blue take opposite sides, so each faces the far pair across the map
			teamSpawnList[TEAM_RED] = baseHalves[0];
			teamSpawnList[TEAM_GREEN] = baseHalves[1];
			teamSpawnList[TEAM_BLUE] = baseHalves[3];
			teamSpawnList[TEAM_YELLOW] = baseHalves[2];
			return qtrue;
		}
	}
	return qfalse;
}

void G_InitSpawnLists( void ) {
	int i, teamCount;

	memset( &dmSpawnList, 0, sizeof( dmSpawnList ) );
	memset( teamSpawnList, 0, sizeof( teamSpawnList ) );

	G_CollectSpawns( &dmSpawnList, "info_player_deathmatch", qfalse );
	if ( g_gametype.integer != GT_TEAM ) {
		return;
	}
	G_CollectSpawns( &teamSpawnList[TEAM_RED], "team_CTF_redspawn", qtrue );
	G_CollectSpawns( &teamSpawnList[TEAM_BLUE], "team_CTF_bluespawn", qtrue );
	teamCount = G_ActiveTeamCount();
	if ( teamCount == 4 && G_SplitBaseSpawns() ) {
		G_Printf( "Four teams share the two bases.\n" );
	} else if ( teamCount > 2 || teamSpawnList[TEAM_RED].count < MIN_TEAM_SPAWNS ||
		teamSpawnList[TEAM_BLUE].count < MIN_TEAM_SPAWNS ) {
		G_DistributeDeathmatchSpawns( teamCount );
	}
	G_Printf( "Spawn points: %d deathmatch across %d teams", dmSpawnList.count, teamCount );
	for ( i = 0; i < teamCount; i++ ) {
		team_t team = BG_TeamForIndex( i );
		G_Printf( ", %s=%d", BG_TeamCommandName( team ), teamSpawnList[team].count );
	}
	G_Printf( "\n" );
}

static spawnList_t *G_SpawnListForTeam( team_t team ) {
	if ( g_gametype.integer == GT_TEAM && G_IsActiveTeam( team ) &&
		teamSpawnList[team].count ) {
		return &teamSpawnList[team];
	}
	return &dmSpawnList;
}

/*
Sample random spots and take the first one that is free and well away from
where the player died; otherwise the furthest free one seen. When every
sample is occupied the crowd placement below spreads players around it.
*/
static gentity_t *G_PickSpawnFromList( spawnList_t *list, const vec3_t avoidPoint, qboolean isbot ) {
	gentity_t *spot, *best = NULL;
	float dist, bestDist = -1.0f;
	int attempt;

	if ( !list->count ) {
		return NULL;
	}
	for ( attempt = 0; attempt < SPAWN_PICK_ATTEMPTS; attempt++ ) {
		spot = list->spots[rand() % list->count];
		if ( ( ( spot->flags & FL_NO_BOTS ) && isbot ) ||
			( ( spot->flags & FL_NO_HUMANS ) && !isbot ) ) {
			continue;
		}
		if ( SpotWouldTelefrag( spot ) ) {
			continue;
		}
		dist = avoidPoint ? Distance( spot->s.origin, avoidPoint ) : SPAWN_AVOID_DIST;
		if ( dist >= SPAWN_AVOID_DIST ) {
			return spot;
		}
		if ( dist > bestDist ) {
			bestDist = dist;
			best = spot;
		}
	}
	return best ? best : list->spots[rand() % list->count];
}

gentity_t *G_SelectTeamSpawnPoint( team_t team, vec3_t origin, vec3_t angles, qboolean isbot ) {
	gentity_t *spot = G_PickSpawnFromList( G_SpawnListForTeam( team ), NULL, isbot );

	if ( !spot ) {
		return SelectSpawnPoint( vec3_origin, origin, angles, isbot );
	}
	VectorCopy( spot->s.origin, origin );
	origin[2] += 9;
	VectorCopy( spot->s.angles, angles );
	return spot;
}
#endif

gentity_t *SelectSpawnPoint ( vec3_t avoidPoint, vec3_t origin, vec3_t angles, qboolean isbot ) {
#ifdef STANDALONE
	gentity_t *spot = G_PickSpawnFromList( &dmSpawnList, avoidPoint, isbot );

	if ( spot ) {
		VectorCopy( spot->s.origin, origin );
		origin[2] += 9;
		VectorCopy( spot->s.angles, angles );
		return spot;
	}
#endif
	return SelectRandomFurthestSpawnPoint( avoidPoint, origin, angles, isbot );

	/*
	gentity_t	*spot;
	gentity_t	*nearestSpot;

	nearestSpot = SelectNearestDeathmatchSpawnPoint( avoidPoint );

	spot = SelectRandomDeathmatchSpawnPoint ( );
	if ( spot == nearestSpot ) {
		// roll again if it would be real close to point of death
		spot = SelectRandomDeathmatchSpawnPoint ( );
		if ( spot == nearestSpot ) {
			// last try
			spot = SelectRandomDeathmatchSpawnPoint ( );
		}		
	}

	// find a single player start spot
	if (!spot) {
		G_Error( "Couldn't find a spawn point" );
	}

	VectorCopy (spot->s.origin, origin);
	origin[2] += 9;
	VectorCopy (spot->s.angles, angles);

	return spot;
	*/
}

#ifdef STANDALONE
/*
===========
SelectCrowdSpawnPosition

Expand the authored deathmatch starts into nearby floor positions once the
chosen spawn is occupied, which is the normal state when a few dozen spawn
points serve hundreds of players. The normal spawn remains the fallback for
tight maps.
===========
*/
static qboolean SpawnOriginOccupied( gentity_t *ent, const vec3_t origin ) {
	int touch[MAX_GENTITIES];
	vec3_t mins, maxs;
	int numTouch, i;

	VectorAdd( origin, playerMins, mins );
	VectorAdd( origin, playerMaxs, maxs );
	numTouch = trap_EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );
	for ( i = 0; i < numTouch; i++ ) {
		if ( touch[i] != ent - g_entities && g_entities[touch[i]].client ) {
			return qtrue;
		}
	}
	return qfalse;
}

static qboolean SelectCrowdSpawnPosition( gentity_t *ent, gentity_t *chosen, vec3_t origin, vec3_t angles ) {
	spawnList_t *list;
	gentity_t *spot;
	trace_t trace;
	vec3_t start, end, mins, maxs;
	int touch[MAX_GENTITIES];
	int clientNum, attempt, numTouch, i;
	unsigned int seed;
	float radius, angle;

	if ( !SpawnOriginOccupied( ent, origin ) ) {
		return qfalse;
	}

	list = G_SpawnListForTeam( ent->client->sess.sessionTeam );
	if ( !list->count ) {
		return qfalse;
	}

	clientNum = ent - g_entities;
	for ( attempt = 0; attempt < 320; attempt++ ) {
		seed = (unsigned int)clientNum * 1103515245u +
			(unsigned int)attempt * 2654435761u + 12345u;
		// Fill in around the chosen spot first so a team spawn cluster grows
		// outward, then spread over the rest of the list.
		if ( chosen && attempt < 64 ) {
			spot = chosen;
			radius = 48.0f + (float)( ( seed >> 8 ) & 31 ) * 16.0f;
		} else {
			spot = list->spots[( clientNum * 17 + attempt * 31 ) % list->count];
			if ( spot->flags & FL_NO_BOTS ) {
				continue;
			}
			radius = 48.0f + (float)( ( seed >> 8 ) & 31 ) * 40.0f;
		}
		angle = (float)( seed & 4095 ) * ( 360.0f / 4096.0f );

		VectorCopy( spot->s.origin, start );
		start[0] += cos( DEG2RAD( angle ) ) * radius;
		start[1] += sin( DEG2RAD( angle ) ) * radius;
		start[2] += 192.0f;
		VectorCopy( start, end );
		end[2] -= 768.0f;

		trap_Trace( &trace, start, playerMins, playerMaxs, end,
			clientNum, MASK_PLAYERSOLID );
		if ( trace.startsolid || trace.allsolid || trace.fraction >= 1.0f ||
			trace.plane.normal[2] < 0.65f ||
			( trace.contents & ( CONTENTS_LAVA | CONTENTS_SLIME ) ) ) {
			continue;
		}

		VectorCopy( trace.endpos, start );
		start[2] += 1.0f;
		if ( trap_AAS_Initialized() && !trap_AAS_PointAreaNum( start ) ) {
			continue;
		}

		VectorAdd( start, playerMins, mins );
		VectorAdd( start, playerMaxs, maxs );
		numTouch = trap_EntitiesInBox( mins, maxs, touch, MAX_GENTITIES );
		for ( i = 0; i < numTouch; i++ ) {
			if ( touch[i] != clientNum && g_entities[touch[i]].client ) {
				break;
			}
		}
		if ( i != numTouch ) {
			continue;
		}

		VectorCopy( start, origin );
		VectorCopy( spot->s.angles, angles );
		return qtrue;
	}

	return qfalse;
}
#endif

/*
===========
SelectInitialSpawnPoint

Try to find a spawn point marked 'initial', otherwise
use normal spawn selection.
============
*/
gentity_t *SelectInitialSpawnPoint( vec3_t origin, vec3_t angles, qboolean isbot ) {
	gentity_t	*spot;

	spot = NULL;
	
	while ((spot = G_Find (spot, FOFS(classname), "info_player_deathmatch")) != NULL)
	{
		if(((spot->flags & FL_NO_BOTS) && isbot) ||
		   ((spot->flags & FL_NO_HUMANS) && !isbot))
		{
			continue;
		}
		
		if((spot->spawnflags & 0x01))
			break;
	}

	if (!spot || SpotWouldTelefrag(spot))
		return SelectSpawnPoint(vec3_origin, origin, angles, isbot);

	VectorCopy (spot->s.origin, origin);
	origin[2] += 9;
	VectorCopy (spot->s.angles, angles);

	return spot;
}

/*
===========
SelectSpectatorSpawnPoint

============
*/
gentity_t *SelectSpectatorSpawnPoint( vec3_t origin, vec3_t angles ) {
	FindIntermissionPoint();

	VectorCopy( level.intermission_origin, origin );
	VectorCopy( level.intermission_angle, angles );

	return NULL;
}

/*
=======================================================================

BODYQUE

=======================================================================
*/

/*
===============
InitBodyQue
===============
*/
void InitBodyQue (void) {
	int		i;
	gentity_t	*ent;

	level.bodyQueIndex = 0;
	for (i=0; i<BODY_QUEUE_SIZE ; i++) {
		ent = G_Spawn();
		ent->classname = "bodyque";
		ent->neverFree = qtrue;
		level.bodyQue[i] = ent;
	}
}

/*
=============
BodySink

After sitting around for five seconds, fall into the ground and disappear
=============
*/
void BodySink( gentity_t *ent ) {
	if ( level.time - ent->timestamp > 6500 ) {
		// the body ques are never actually freed, they are just unlinked
		trap_UnlinkEntity( ent );
		ent->physicsObject = qfalse;
		return;	
	}
	ent->nextthink = level.time + 100;
	ent->s.pos.trBase[2] -= 1;
}

/*
=============
CopyToBodyQue

A player is respawning, so make an entity that looks
just like the existing corpse to leave behind.
=============
*/
void CopyToBodyQue( gentity_t *ent ) {
#ifdef MISSIONPACK
	gentity_t	*e;
	int i;
#endif
	gentity_t		*body;
	int			contents;

	trap_UnlinkEntity (ent);

	// if client is in a nodrop area, don't leave the body
	contents = trap_PointContents( ent->s.origin, -1 );
	if ( contents & CONTENTS_NODROP ) {
		return;
	}

	// grab a body que and cycle to the next one
	body = level.bodyQue[ level.bodyQueIndex ];
	level.bodyQueIndex = (level.bodyQueIndex + 1) % BODY_QUEUE_SIZE;

	body->s = ent->s;
	body->s.eFlags = EF_DEAD;		// clear EF_TALK, etc
#ifdef MISSIONPACK
	if ( ent->s.eFlags & EF_KAMIKAZE ) {
		body->s.eFlags |= EF_KAMIKAZE;

		// check if there is a kamikaze timer around for this owner
		for (i = 0; i < level.num_entities; i++) {
			e = &g_entities[i];
			if (!e->inuse)
				continue;
			if (e->activator != ent)
				continue;
			if (strcmp(e->classname, "kamikaze timer"))
				continue;
			e->activator = body;
			break;
		}
	}
#endif
	body->s.powerups = 0;	// clear powerups
	body->s.loopSound = 0;	// clear lava burning
	body->s.number = body - g_entities;
	body->timestamp = level.time;
	body->physicsObject = qtrue;
	body->physicsBounce = 0;		// don't bounce
	if ( body->s.groundEntityNum == ENTITYNUM_NONE ) {
		body->s.pos.trType = TR_GRAVITY;
		body->s.pos.trTime = level.time;
		VectorCopy( ent->client->ps.velocity, body->s.pos.trDelta );
	} else {
		body->s.pos.trType = TR_STATIONARY;
	}
	body->s.event = 0;

	// change the animation to the last-frame only, so the sequence
	// doesn't repeat anew for the body
	switch ( body->s.legsAnim & ~ANIM_TOGGLEBIT ) {
	case BOTH_DEATH1:
	case BOTH_DEAD1:
		body->s.torsoAnim = body->s.legsAnim = BOTH_DEAD1;
		break;
	case BOTH_DEATH2:
	case BOTH_DEAD2:
		body->s.torsoAnim = body->s.legsAnim = BOTH_DEAD2;
		break;
	case BOTH_DEATH3:
	case BOTH_DEAD3:
	default:
		body->s.torsoAnim = body->s.legsAnim = BOTH_DEAD3;
		break;
	}

	body->r.svFlags = ent->r.svFlags;
	VectorCopy (ent->r.mins, body->r.mins);
	VectorCopy (ent->r.maxs, body->r.maxs);
	VectorCopy (ent->r.absmin, body->r.absmin);
	VectorCopy (ent->r.absmax, body->r.absmax);

	body->clipmask = CONTENTS_SOLID | CONTENTS_PLAYERCLIP;
	body->r.contents = CONTENTS_CORPSE;
	body->r.ownerNum = ent->s.number;

	// Keep corpses in the fixed body queue until their slot is reused.
	body->nextthink = 0;
	body->think = NULL;

	body->die = body_die;

	// don't take more damage if already gibbed
	if ( ent->health <= GIB_HEALTH ) {
		body->takedamage = qfalse;
	} else {
		body->takedamage = qtrue;
	}


	VectorCopy ( body->s.pos.trBase, body->r.currentOrigin );
	trap_LinkEntity (body);
}

#ifdef STANDALONE
static gentity_t *CompanionSpawnPartner( gentity_t *ent ) {
	int clientNum = ent - g_entities;
	int i;

	if ( ent->client->pers.isCompanion ) {
		i = ent->client->pers.companionOwner;
		if ( i >= 0 && i < level.maxclients ) {
			gentity_t *owner = &g_entities[i];
			if ( owner->inuse && owner->client &&
				owner->client->pers.connected == CON_CONNECTED && owner->health > 0 &&
				( !G_ConversionModeActive() || OnSameTeam( ent, owner ) ) ) {
				return owner;
			}
		}
		return NULL;
	}

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *companion = &g_entities[i];
		if ( companion->inuse && companion->client &&
			companion->client->pers.connected == CON_CONNECTED &&
			companion->client->pers.isCompanion &&
			companion->client->pers.companionOwner == clientNum &&
			companion->health > 0 && ( !G_ConversionModeActive() || OnSameTeam( ent, companion ) ) ) {
			return companion;
		}
	}
	return NULL;
}

static qboolean SelectCompanionSpawnPosition( gentity_t *ent, vec3_t origin, vec3_t angles ) {
	static const float offsets[8][2] = {
		{ -56, 0 }, { 56, 0 }, { 0, -56 }, { 0, 56 },
		{ -48, -48 }, { 48, -48 }, { -48, 48 }, { 48, 48 }
	};
	gentity_t *partner = CompanionSpawnPartner( ent );
	trace_t trace;
	vec3_t start, end;
	int i;

	if ( !partner ) {
		return qfalse;
	}

	for ( i = 0; i < 8; i++ ) {
		VectorCopy( partner->r.currentOrigin, start );
		start[0] += offsets[i][0];
		start[1] += offsets[i][1];
		start[2] += 32;
		VectorCopy( start, end );
		end[2] -= 96;
		trap_Trace( &trace, start, playerMins, playerMaxs, end,
			ent - g_entities, MASK_PLAYERSOLID );
		if ( trace.startsolid || trace.allsolid || trace.fraction >= 1.0f ) {
			continue;
		}
		VectorCopy( trace.endpos, origin );
		VectorCopy( partner->client->ps.viewangles, angles );
		if ( trap_Cvar_VariableIntegerValue( "g_debugCompanion" ) ) {
			vec3_t separation;
			VectorSubtract( origin, partner->r.currentOrigin, separation );
			G_Printf( "partner respawn: client=%d role=%s partner=%d distance=%.0f origin=(%.0f %.0f %.0f)\n",
				(int)( ent - g_entities ), ent->client->pers.isCompanion ? "companion" : "owner",
				(int)( partner - g_entities ), VectorLength( separation ),
				origin[0], origin[1], origin[2] );
		}
		return qtrue;
	}

	return qfalse;
}
#endif

//======================================================================


/*
==================
SetClientViewAngle

==================
*/
void SetClientViewAngle( gentity_t *ent, vec3_t angle ) {
	int			i;

	// set the delta angle
	for (i=0 ; i<3 ; i++) {
		int		cmdAngle;

		cmdAngle = ANGLE2SHORT(angle[i]);
		ent->client->ps.delta_angles[i] = cmdAngle - ent->client->pers.cmd.angles[i];
	}
	VectorCopy( angle, ent->s.angles );
	VectorCopy (ent->s.angles, ent->client->ps.viewangles);
}

/*
================
ClientRespawn
================
*/
void ClientRespawn( gentity_t *ent ) {
	if ( G_LivesModeActive() && level.lmsStarted && ent->client->pers.lmsLives <= 0 ) {
		SetTeam( ent, "spectator" );
		return;
	}

#ifdef STANDALONE
	if ( G_PlayerClassesActive() && !( ent->r.svFlags & SVF_BOT ) &&
		ent->health <= 0 && !ent->client->pers.classSpawnConfirmed ) return;
#endif

	CopyToBodyQue (ent);
	ClientSpawn(ent);
}

/*
================
TeamCount

Returns number of players on a team
================
*/
int TeamCount( int ignoreClientNum, team_t team ) {
	int		i;
	int		count = 0;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( i == ignoreClientNum ) {
			continue;
		}
		if ( level.clients[i].pers.connected == CON_DISCONNECTED ) {
			continue;
		}
		if ( level.clients[i].sess.sessionTeam == team ) {
			count++;
		}
	}

	return count;
}

/*
================
TeamLeader

Returns the client number of the team leader
================
*/
int TeamLeader( int team ) {
	int		i;

	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( level.clients[i].pers.connected == CON_DISCONNECTED ) {
			continue;
		}
		if ( level.clients[i].sess.sessionTeam == team ) {
			if ( level.clients[i].sess.teamLeader )
				return i;
		}
	}

	return -1;
}


/*
================
PickTeam

================
*/
team_t PickTeam( int ignoreClientNum ) {
	int i, count, bestCount = MAX_CLIENTS + 1, bestScore = 0x7fffffff;
	team_t team, bestTeam = TEAM_RED;

	if ( G_InfectedActive() ) {
		return G_InfectedPickTeam();
	}

	for ( i = 0; i < G_ActiveTeamCount(); i++ ) {
		team = BG_TeamForIndex( i );
		count = TeamCount( ignoreClientNum, team );
		if ( count < bestCount || ( count == bestCount && level.teamScores[team] < bestScore ) ) {
			bestCount = count;
			bestScore = level.teamScores[team];
			bestTeam = team;
		}
	}
	return bestTeam;
}

/*
===========
ForceClientSkin

Forces a client's skin (for teamplay)
===========
*/
/*
static void ForceClientSkin( gclient_t *client, char *model, const char *skin ) {
	char *p;

	if ((p = strrchr(model, '/')) != 0) {
		*p = 0;
	}

	Q_strcat(model, MAX_QPATH, "/");
	Q_strcat(model, MAX_QPATH, skin);
}
*/

/*
===========
ClientCleanName
============
*/
static void ClientCleanName(const char *in, char *out, int outSize)
{
	int outpos = 0, colorlessLen = 0, spaces = 0;

	// discard leading spaces
	for(; *in == ' '; in++);
	
	for(; *in && outpos < outSize - 1; in++)
	{
		out[outpos] = *in;

		if(*in == ' ')
		{
			// don't allow too many consecutive spaces
			if(spaces > 2)
				continue;
			
			spaces++;
		}
		else if(outpos > 0 && out[outpos - 1] == Q_COLOR_ESCAPE)
		{
			if(Q_IsColorString(&out[outpos - 1]))
			{
				colorlessLen--;
				
				if(ColorIndex(*in) == 0)
				{
					// Disallow color black in names to prevent players
					// from getting advantage playing in front of black backgrounds
					outpos--;
					continue;
				}
			}
			else
			{
				spaces = 0;
				colorlessLen++;
			}
		}
		else
		{
			spaces = 0;
			colorlessLen++;
		}
		
		outpos++;
	}

	out[outpos] = '\0';

	// don't allow empty names
	if( *out == '\0' || colorlessLen == 0)
		Q_strncpyz(out, "UnnamedPlayer", outSize );
}


/*
===========
ClientUserInfoChanged

Called from ClientConnect when the player first connects and
directly by the server system when the player updates a userinfo variable.

The game can override any of the settings and call trap_SetUserinfo
if desired.
============
*/
void ClientUserinfoChanged( int clientNum ) {
	gentity_t *ent;
	int		teamTask, teamLeader, health;
	char	*s;
	char	model[MAX_QPATH];
	char	headModel[MAX_QPATH];
	char	oldname[MAX_STRING_CHARS];
	gclient_t	*client;
	char	c1[MAX_INFO_STRING];
	char	c2[MAX_INFO_STRING];
	char	redTeam[MAX_INFO_STRING];
	char	blueTeam[MAX_INFO_STRING];
	char	userinfo[MAX_INFO_STRING];

	ent = g_entities + clientNum;
	client = ent->client;

	trap_GetUserinfo( clientNum, userinfo, sizeof( userinfo ) );

	// check for malformed or illegal info strings
	if ( !Info_Validate(userinfo) ) {
		strcpy (userinfo, "\\name\\badinfo");
		// don't keep those clients and userinfo
		trap_DropClient(clientNum, "Invalid userinfo");
	}

#ifdef STANDALONE
	// Observer status is fixed at connection time, not mutable userinfo.
	Info_SetValueForKey( userinfo, "menuwatch", client->pers.menuObserver ? "1" : "0" );
	trap_SetUserinfo( clientNum, userinfo );
	if ( client->pers.menuObserver ) {
		Q_strncpyz( client->pers.netname, "Menu camera", sizeof( client->pers.netname ) );
		trap_SetConfigstring( CS_PLAYERS + clientNum, "" );
		return;
	}
	Q_strncpyz( client->pers.botProfile, Info_ValueForKey( userinfo, "botprofile" ),
		sizeof( client->pers.botProfile ) );
	client->pers.isCompanion = atoi( Info_ValueForKey( userinfo, "companion" ) ) ? qtrue : qfalse;
	client->pers.companionOwner = client->pers.isCompanion ?
		atoi( Info_ValueForKey( userinfo, "companionOwner" ) ) : -1;
	if ( client->pers.companionOwner < 0 || client->pers.companionOwner >= level.maxclients ) {
		client->pers.isCompanion = qfalse;
		client->pers.companionOwner = -1;
	}
#endif

	// check the item prediction
	s = Info_ValueForKey( userinfo, "cg_predictItems" );
	if ( !atoi( s ) ) {
		client->pers.predictItemPickup = qfalse;
	} else {
		client->pers.predictItemPickup = qtrue;
	}

	// set name
	Q_strncpyz ( oldname, client->pers.netname, sizeof( oldname ) );
	s = Info_ValueForKey (userinfo, "name");
	ClientCleanName( s, client->pers.netname, sizeof(client->pers.netname) );

	if ( client->sess.sessionTeam == TEAM_SPECTATOR ) {
		if ( client->sess.spectatorState == SPECTATOR_SCOREBOARD ) {
			Q_strncpyz( client->pers.netname, "scoreboard", sizeof(client->pers.netname) );
		}
	}

#ifdef STANDALONE
	if ( ent->r.svFlags & SVF_BOT ) {
		G_UniqueBotName( clientNum, client->pers.netname, sizeof( client->pers.netname ) );
		if ( strcmp( client->pers.netname, Info_ValueForKey( userinfo, "name" ) ) ) {
			Info_SetValueForKey( userinfo, "name", client->pers.netname );
			trap_SetUserinfo( clientNum, userinfo );
		}
	}
#endif

	if ( client->pers.connected == CON_CONNECTED ) {
		if ( strcmp( oldname, client->pers.netname ) ) {
			trap_SendServerCommand( -1, va("print \"%s" S_COLOR_WHITE " renamed to %s\n\"", oldname, 
				client->pers.netname) );
		}
	}

	// set max health
#ifdef MISSIONPACK
	if (client->ps.powerups[PW_GUARD]) {
		client->pers.maxHealth = 200;
	} else {
		health = atoi( Info_ValueForKey( userinfo, "handicap" ) );
		client->pers.maxHealth = health;
		if ( client->pers.maxHealth < 1 || client->pers.maxHealth > 100 ) {
			client->pers.maxHealth = 100;
		}
	}
#else
	health = atoi( Info_ValueForKey( userinfo, "handicap" ) );
	client->pers.maxHealth = health;
	if ( client->pers.maxHealth < 1 || client->pers.maxHealth > 100 ) {
		client->pers.maxHealth = 100;
	}
#endif
#ifdef STANDALONE
	if ( G_PlayerClassesActive() ) {
		client->pers.maxHealth = MAX( 1, client->pers.maxHealth *
			BG_PlayerClass( client->pers.activeClass )->health / 100 );
	}
	if ( !G_ConversionModeActive() && !( client->ps.eFlags & EF_LEPRECHAUN ) &&
		( !g_davidGoliath.integer || client->ps.pm_type == PM_DEAD ||
			client->ps.stats[STAT_GOLIATH_FRAGS] == 0 ) )
#endif
		client->ps.stats[STAT_MAX_HEALTH] = client->pers.maxHealth;

	// set model
	if( g_gametype.integer >= GT_TEAM ) {
		Q_strncpyz( model, Info_ValueForKey (userinfo, "team_model"), sizeof( model ) );
		Q_strncpyz( headModel, Info_ValueForKey (userinfo, "team_headmodel"), sizeof( headModel ) );
	} else {
		Q_strncpyz( model, Info_ValueForKey (userinfo, "model"), sizeof( model ) );
		Q_strncpyz( headModel, Info_ValueForKey (userinfo, "headmodel"), sizeof( headModel ) );
	}

/*	NOTE: all client side now

	// team
	switch( team ) {
	case TEAM_RED:
		ForceClientSkin(client, model, "red");
//		ForceClientSkin(client, headModel, "red");
		break;
	case TEAM_BLUE:
		ForceClientSkin(client, model, "blue");
//		ForceClientSkin(client, headModel, "blue");
		break;
	}
	// don't ever use a default skin in teamplay, it would just waste memory
	// however bots will always join a team but they spawn in as spectator
	if ( g_gametype.integer >= GT_TEAM && team == TEAM_SPECTATOR) {
		ForceClientSkin(client, model, "red");
//		ForceClientSkin(client, headModel, "red");
	}
*/

#ifdef MISSIONPACK
	if (g_gametype.integer >= GT_TEAM && !(ent->r.svFlags & SVF_BOT)) {
		client->pers.teamInfo = qtrue;
	} else {
		s = Info_ValueForKey( userinfo, "teamoverlay" );
		if ( ! *s || atoi( s ) != 0 ) {
			client->pers.teamInfo = qtrue;
		} else {
			client->pers.teamInfo = qfalse;
		}
	}
#else
	// teamInfo
	s = Info_ValueForKey( userinfo, "teamoverlay" );
	if ( ! *s || atoi( s ) != 0 ) {
		client->pers.teamInfo = qtrue;
	} else {
		client->pers.teamInfo = qfalse;
	}
#endif
	/*
	s = Info_ValueForKey( userinfo, "cg_pmove_fixed" );
	if ( !*s || atoi( s ) == 0 ) {
		client->pers.pmoveFixed = qfalse;
	}
	else {
		client->pers.pmoveFixed = qtrue;
	}
	*/

	// team task (0 = none, 1 = offence, 2 = defence)
	teamTask = atoi(Info_ValueForKey(userinfo, "teamtask"));
	// team Leader (1 = leader, 0 is normal player)
	teamLeader = client->sess.teamLeader;

	// colors
	Q_strncpyz(c1, Info_ValueForKey( userinfo, "color1" ), sizeof( c1 ));
	Q_strncpyz(c2, Info_ValueForKey( userinfo, "color2" ), sizeof( c2 ));

	Q_strncpyz(redTeam, Info_ValueForKey( userinfo, "g_redteam" ), sizeof( redTeam ));
	Q_strncpyz(blueTeam, Info_ValueForKey( userinfo, "g_blueteam" ), sizeof( blueTeam ));
	
	// send over a subset of the userinfo keys so other clients can
	// print scoreboards, display models, and play custom sounds
	if (ent->r.svFlags & SVF_BOT)
	{
		s = va("n\\%s\\t\\%i\\model\\%s\\hmodel\\%s\\c1\\%s\\c2\\%s\\hc\\%i\\w\\%i\\l\\%i\\skill\\%s\\tt\\%d\\tl\\%d",
			client->pers.netname, client->sess.sessionTeam, model, headModel, c1, c2,
			client->pers.maxHealth, client->sess.wins, client->sess.losses,
			Info_ValueForKey( userinfo, "skill" ), teamTask, teamLeader );
	}
	else
	{
		s = va("n\\%s\\t\\%i\\model\\%s\\hmodel\\%s\\g_redteam\\%s\\g_blueteam\\%s\\c1\\%s\\c2\\%s\\hc\\%i\\w\\%i\\l\\%i\\tt\\%d\\tl\\%d",
			client->pers.netname, client->sess.sessionTeam, model, headModel, redTeam, blueTeam, c1, c2, 
			client->pers.maxHealth, client->sess.wins, client->sess.losses, teamTask, teamLeader);
	}

	trap_SetConfigstring( CS_PLAYERS+clientNum, s );

	// this is not the userinfo, more like the configstring actually
	G_LogPrintf( "ClientUserinfoChanged: %i %s\n", clientNum, s );
#ifdef STANDALONE
	if ( !( ent->r.svFlags & SVF_BOT ) ) {
		G_ResolveBotNameCollisions( clientNum );
	}
#endif
}


/*
===========
ClientConnect

Called when a player begins connecting to the server.
Called again for every map change or tournement restart.

The session information will be valid after exit.

Return NULL if the client should be allowed, otherwise return
a string with the reason for denial.

Otherwise, the client will be sent the current gamestate
and will eventually get to ClientBegin.

firstTime will be qtrue the very first time a client connects
to the server machine, but qfalse on map changes and tournement
restarts.
============
*/
char *ClientConnect( int clientNum, qboolean firstTime, qboolean isBot ) {
	char		*value;
//	char		*areabits;
	gclient_t	*client;
	char		userinfo[MAX_INFO_STRING];
	gentity_t	*ent;

	ent = &g_entities[ clientNum ];

	trap_GetUserinfo( clientNum, userinfo, sizeof( userinfo ) );

 	// IP filtering
#ifdef STANDALONE
	if ( !isBot && atoi( Info_ValueForKey( userinfo, "menuwatch" ) ) &&
		!trap_Cvar_VariableIntegerValue( "g_menuCamera" ) ) return "Menu camera unavailable";
#endif
 	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=500
 	// recommanding PB based IP / GUID banning, the builtin system is pretty limited
 	// check to see if they are on the banned IP list
	value = Info_ValueForKey (userinfo, "ip");
	if ( G_FilterPacket( value ) ) {
		return "You are banned from this server.";
	}

  // we don't check password for bots and local client
  // NOTE: local client <-> "ip" "localhost"
  //   this means this client is not running in our current process
	if ( !isBot && (strcmp(value, "localhost") != 0)) {
		// check for a password
		value = Info_ValueForKey (userinfo, "password");
		if ( g_password.string[0] && Q_stricmp( g_password.string, "none" ) &&
			strcmp( g_password.string, value) != 0) {
			return "Invalid password";
		}
	}
	// if a player reconnects quickly after a disconnect, the client disconnect may never be called, thus flag can get lost in the ether
	if (ent->inuse) {
		G_LogPrintf("Forcing disconnect on active client: %i\n", clientNum);
		// so lets just fix up anything that should happen on a disconnect
		ClientDisconnect(clientNum);
	}
	// they can connect
	ent->client = level.clients + clientNum;
	client = ent->client;

//	areabits = client->areabits;

	memset( client, 0, sizeof(*client) );

	client->pers.connected = CON_CONNECTING;

#ifdef STANDALONE
	client->pers.menuObserver = !isBot &&
		trap_Cvar_VariableIntegerValue( "g_menuCamera" ) &&
		atoi( Info_ValueForKey( userinfo, "menuwatch" ) );
	client->pers.playerClass = isBot ? clientNum % CLASS_COUNT :
		BG_PlayerClassForName( Info_ValueForKey( userinfo, "playerclass" ) );
	if ( client->pers.playerClass < 0 ) client->pers.playerClass = CLASS_VANGUARD;
	client->pers.activeClass = client->pers.playerClass;
#endif

	// check for local client
	value = Info_ValueForKey( userinfo, "ip" );
	if ( !strcmp( value, "localhost" ) ) {
		client->pers.localClient = qtrue;
	}

	if( isBot ) {
		ent->r.svFlags |= SVF_BOT;
		ent->inuse = qtrue;
		if( !G_BotConnect( clientNum, !firstTime ) ) {
			return "BotConnectfailed";
		}
	}

	// read or initialize the session data
	if ( firstTime || level.newSession ) {
		G_InitSessionData( client, userinfo );
	}
	G_ReadSessionData( client );
	if ( client->pers.menuObserver || trap_Cvar_VariableIntegerValue( "g_matchLobby" ) ) {
		client->sess.sessionTeam = TEAM_SPECTATOR;
		client->sess.spectatorState = SPECTATOR_FREE;
		client->sess.spectatorClient = -1;
		client->sess.lmsWaiting = qfalse;
	}
	if ( G_LivesModeActive() ) {
		if ( level.lmsStarted && ( isBot || !G_LivesModeCanJoin( client ) ) ) {
			if ( client->sess.sessionTeam != TEAM_SPECTATOR ) {
				client->sess.sessionTeam = TEAM_SPECTATOR;
				client->sess.spectatorState = SPECTATOR_FREE;
				client->sess.lmsWaiting = qtrue;
			}
		} else if ( client->sess.lmsWaiting ) {
			client->sess.sessionTeam = G_BugHuntActive() ? PickTeam( clientNum ) : TEAM_FREE;
			client->sess.spectatorState = SPECTATOR_NOT;
			client->sess.lmsWaiting = qfalse;
		}
	}
	if ( G_ConversionModeActive() && client->sess.sessionTeam != TEAM_SPECTATOR ) {
		client->sess.sessionTeam = PickTeam( clientNum );
		G_WriteClientSessionData( client );
	}

#ifdef STANDALONE
	// Keep first-time selection outside the round roster and the combat world.
	if ( G_PlayerClassesActive() && !isBot &&
		( client->sess.sessionTeam != TEAM_SPECTATOR || client->sess.lmsWaiting ) ) {
		client->pers.classInitialPending = qtrue;
		client->pers.classJoinTeam = client->sess.sessionTeam == TEAM_SPECTATOR ?
			( G_BugHuntActive() ? PickTeam( clientNum ) : TEAM_FREE ) : client->sess.sessionTeam;
		client->sess.sessionTeam = TEAM_SPECTATOR;
		client->sess.spectatorState = SPECTATOR_FREE;
	}
#endif

	// get and distribute relevant parameters
	G_LogPrintf( client->pers.menuObserver ? "MenuObserver: %i connected\n" : "ClientConnect: %i\n", clientNum );
	ClientUserinfoChanged( clientNum );

	// don't do the "xxx connected" messages if they were caried over from previous level
	if ( firstTime && !client->pers.menuObserver && ( !isBot || !G_QuietBotJoins() ) ) {
		trap_SendServerCommand( -1, va("print \"%s" S_COLOR_WHITE " connected\n\"", client->pers.netname) );
	}

	if ( g_gametype.integer >= GT_TEAM &&
		client->sess.sessionTeam != TEAM_SPECTATOR ) {
		BroadcastTeamChange( client, -1 );
	}

	// count current clients and rank for scoreboard
	CalculateRanks();

	// for statistics
//	client->areabits = areabits;
//	if ( !client->areabits )
//		client->areabits = G_Alloc( (trap_AAS_PointReachabilityAreaIndex( NULL ) + 7) / 8 );

	return NULL;
}

/*
===========
ClientBegin

called when a client has finished connecting, and is ready
to be placed into the level.  This will happen every level load,
and on transition between teams, but doesn't happen on respawns
============
*/
void ClientBegin( int clientNum ) {
	gentity_t	*ent;
	gclient_t	*client;
	int			flags;

	ent = g_entities + clientNum;

	client = level.clients + clientNum;

	if ( ent->r.linked ) {
		trap_UnlinkEntity( ent );
	}
	G_InitGentity( ent );
	ent->touch = 0;
	ent->pain = 0;
	ent->client = client;

	client->pers.connected = CON_CONNECTED;
	client->pers.enterTime = level.time;
	client->pers.teamState.state = TEAM_BEGIN;
	if ( G_LivesModeActive() && G_LivesModeCanJoin( client ) &&
		( client->sess.sessionTeam == TEAM_FREE || G_IsActiveTeam( client->sess.sessionTeam ) ) ) {
		client->pers.lmsLives = G_LivesModeStartingLives();
		client->pers.lmsParticipant = qtrue;
	}

	// save eflags around this, because changing teams will
	// cause this to happen with a valid entity, and we
	// want to make sure the teleport bit is set right
	// so the viewpoint doesn't interpolate through the
	// world to the new position
	flags = client->ps.eFlags;
	memset( &client->ps, 0, sizeof( client->ps ) );
	client->ps.eFlags = flags;

	// locate ent at a spawn point
	ClientSpawn( ent );
#ifdef STANDALONE
	G_ArmsRaceClientBegin( ent );
#endif

	if ( client->sess.sessionTeam != TEAM_SPECTATOR &&
		( !( ent->r.svFlags & SVF_BOT ) || !G_QuietBotJoins() ) ) {
		if ( g_gametype.integer != GT_TOURNAMENT  ) {
			trap_SendServerCommand( -1, va("print \"%s" S_COLOR_WHITE " entered the game\n\"", client->pers.netname) );
		}
	}
	G_LogPrintf( client->pers.menuObserver ? "MenuObserver: %i ready\n" : "ClientBegin: %i\n", clientNum );

	// count current clients and rank for scoreboard
	CalculateRanks();
}

/*
===========
ClientSpawn

Called every time a client is placed fresh in the world:
after the first ClientBegin, and after each respawn
Initializes all non-persistant parts of playerState
============
*/
#ifdef STANDALONE
/*
g_spawnWeapons lists what a player respawns with, by name: "pistol", "rifle",
"rockets", "mg", or "all". Shared loadouts include the pistol as an
backup, except for the "odin-only" match preset.
*/
typedef struct {
	const char *name;
	int weapon;
	int ammo;	// starting rounds when spawned with it
} spawnWeapon_t;

static const spawnWeapon_t spawnWeapons[] = {
	{ "rifle", WP_MACHINEGUN, 100 },
	{ "rockets", WP_ROCKET_LAUNCHER, 10 },
	{ "mg", WP_PLASMAGUN, 200 },
	{ "flak", WP_FLAK, 8 },
	{ "arc", WP_LIGHTNING, 100 },
	{ "rail", WP_RAILGUN, 10 },
	{ "siege", WP_GRENADE_LAUNCHER, 8 },
	{ "oden", WP_BFG, 2 },
};

static qboolean G_SpawnWeaponListed( const char *list, const char *name ) {
	char token[MAX_TOKEN_CHARS];
	const char *p = list;

	for ( ;; ) {
		Q_strncpyz( token, COM_Parse( (char **)&p ), sizeof( token ) );
		if ( !token[0] ) {
			return qfalse;
		}
		if ( !Q_stricmp( token, name ) || !Q_stricmp( token, "all" ) ) {
			return qtrue;
		}
	}
}

qboolean G_PlayerClassesActive( void ) {
#if !FAB_PLAYER_CLASSES
	return qfalse;
#endif
	return g_playerClasses.integer && !level.odinOnly && !G_ArmsRaceActive() &&
		!trap_Cvar_VariableIntegerValue( "g_menuBackground" ) &&
		!trap_Cvar_VariableIntegerValue( "g_matchLobby" );
}

qboolean G_TestChamberEquipment( const gclient_t *client ) {
	// the Sentinel sword and shield need melee in this build
	return FAB_WEAPON_MELEE && level.testChamber && !G_PlayerClassesActive() && !G_ArmsRaceActive() && client->pers.localClient &&
		G_SpawnWeaponListed( g_spawnWeapons.string, "all" );
}

qboolean G_ClassSelectionPending( void ) {
	int i;
	if ( !G_PlayerClassesActive() ) return qfalse;
	for ( i = 0; i < level.maxclients; i++ ) {
		gclient_t *client = &level.clients[i];
		if ( client->pers.connected == CON_CONNECTED && client->pers.classInitialPending &&
			!client->sess.lmsWaiting ) return qtrue;
	}
	return qfalse;
}

void G_ClassSelectionStatus( gentity_t *ent ) {
	gclient_t *client = ent->client;
	client->ps.stats[STAT_PLAYER_CLASS] = G_PlayerClassesActive() || G_TestChamberEquipment( client ) ?
		client->pers.activeClass + 1 : 0;
	client->ps.stats[STAT_CLASS_SELECTION] = 0;
	client->ps.stats[STAT_CLASS_RESPAWN_WAIT] = 0;
	if ( !G_PlayerClassesActive() || ent->r.svFlags & SVF_BOT || level.intermissiontime || level.intermissionQueued ) return;
	if ( client->pers.classInitialPending ) {
		client->ps.stats[STAT_CLASS_SELECTION] = 1;
	} else if ( client->sess.sessionTeam != TEAM_SPECTATOR && ent->health <= 0 &&
		!( G_LivesModeActive() && level.lmsStarted && client->pers.lmsLives <= 0 ) ) {
		client->ps.stats[STAT_CLASS_SELECTION] = 2;
		client->ps.stats[STAT_CLASS_RESPAWN_WAIT] = MIN( 32767, MAX( 0, client->respawnTime - level.time + 1 ) );
	}
}

void G_SelectPlayerClass( gentity_t *ent, const char *name ) {
	gclient_t *client = ent->client;
	int selected = BG_PlayerClassForName( name );
	if ( selected < 0 ) {
		trap_SendServerCommand( ent - g_entities, "print \"Unknown class. Choose vanguard, demolitionist, gunner, wraith, or sentinel.\n\"" );
		return;
	}
	if ( !G_PlayerClassesActive() || level.intermissiontime || level.intermissionQueued ) return;
	if ( !client->pers.classInitialPending && client->sess.sessionTeam != TEAM_SPECTATOR && ent->health > 0 ) {
		trap_SendServerCommand( ent - g_entities, "print \"Choose a class after dying.\n\"" );
		return;
	}
	if ( !client->pers.classInitialPending && client->sess.sessionTeam != TEAM_SPECTATOR &&
		level.time <= client->respawnTime ) return;
	client->pers.playerClass = selected;
	if ( client->pers.classInitialPending ) {
		team_t team = client->pers.classJoinTeam;
		client->pers.classInitialPending = qfalse;
		SetTeam( ent, g_gametype.integer >= GT_TEAM ? BG_TeamCommandName( team ) : "free" );
	} else if ( client->sess.sessionTeam != TEAM_SPECTATOR ) {
		client->pers.classSpawnConfirmed = qtrue;
		ClientRespawn( ent );
	}
	G_ClassSelectionStatus( ent );
}

// Infinite ammo tops every carried weapon up each frame rather than using
// the engine's -1 marker, which bots read as an empty gun.
void G_RefillInfiniteAmmo( gclient_t *client ) {
	int i;

	if ( G_ArmsRaceActive() ) {
		G_ArmsRaceRefill( client );
		return;
	}
	if ( !g_infiniteAmmo.integer || level.odinOnly ) {
		return;
	}
	for ( i = 0; i < (int)ARRAY_LEN( spawnWeapons ); i++ ) {
		if ( client->ps.stats[STAT_WEAPONS] & ( 1 << spawnWeapons[i].weapon ) ) {
			client->ps.ammo[spawnWeapons[i].weapon] = 999;
		}
	}
}

// Weapons switched off with FAB_WEAPON_* come out of every spawn loadout, so
// FULL ARSENAL and the class kits stop at the guns in this build.
static void G_RemoveHeldBackWeapons( gclient_t *client ) {
	int weapon, best = WP_NONE;

	for ( weapon = WP_GAUNTLET; weapon < WP_NUM_WEAPONS; weapon++ ) {
		if ( !BG_WeaponEnabled( weapon ) ) {
			client->ps.stats[STAT_WEAPONS] &= ~( 1 << weapon );
			client->ps.ammo[weapon] = 0;
		}
	}
	// a kit that was only held-back weapons (Sentinel without melee) keeps the pistol
	if ( !( client->ps.stats[STAT_WEAPONS] & ~1 ) ) {
		client->ps.stats[STAT_WEAPONS] = 1 << WP_SHOTGUN;
		client->ps.ammo[WP_SHOTGUN] = -1;
	}
	if ( client->ps.stats[STAT_WEAPONS] & ( 1 << client->ps.weapon ) ) {
		return;
	}
	// the raised weapon was taken away; raise the best one left
	for ( weapon = WP_GAUNTLET; weapon < WP_NUM_WEAPONS; weapon++ ) {
		if ( ( client->ps.stats[STAT_WEAPONS] & ( 1 << weapon ) ) && client->ps.ammo[weapon] &&
			( best == WP_NONE || BG_WeaponAutoswitchRank( weapon ) > BG_WeaponAutoswitchRank( best ) ) ) {
			best = weapon;
		}
	}
	client->ps.weapon = best != WP_NONE ? best : WP_SHOTGUN;
}

static void G_GiveSpawnLoadout( gclient_t *client ) {
	int i;

	if ( G_ArmsRaceActive() ) {
		G_ArmsRaceEquip( client );
		return;
	}
	if ( level.odinOnly ) {
		client->ps.stats[STAT_WEAPONS] = 1 << WP_BFG;
		client->ps.ammo[WP_BFG] = 10;
		client->ps.weapon = WP_BFG;
		return;
	}

	client->ps.stats[STAT_WEAPONS] = ( 1 << WP_SHOTGUN ) | ( 1 << WP_GAUNTLET );
	client->ps.ammo[WP_SHOTGUN] = -1;
	client->ps.ammo[WP_GAUNTLET] = -1;
	client->ps.ammo[WP_GRAPPLING_HOOK] = -1;
	client->ps.weapon = WP_SHOTGUN;
	for ( i = 0; i < (int)ARRAY_LEN( spawnWeapons ); i++ ) {
		client->ps.ammo[spawnWeapons[i].weapon] = 0;
		if ( G_SpawnWeaponListed( g_spawnWeapons.string, spawnWeapons[i].name ) ) {
			client->ps.stats[STAT_WEAPONS] |= 1 << spawnWeapons[i].weapon;
			client->ps.ammo[spawnWeapons[i].weapon] = spawnWeapons[i].ammo;
			// raise the first listed gun, so the rifle when it is in the set
			if ( client->ps.weapon == WP_SHOTGUN ) {
				client->ps.weapon = spawnWeapons[i].weapon;
			}
		}
	}
	if ( G_PlayerClassesActive() && !G_SpawnWeaponListed( g_spawnWeapons.string, "all" ) ) {
		const playerClassInfo_t *info = BG_PlayerClass( client->pers.activeClass );
		client->ps.stats[STAT_WEAPONS] = ( 1 << WP_SHOTGUN ) | ( 1 << WP_GAUNTLET ) | ( 1 << info->weapon );
		for ( i = 0; i < (int)ARRAY_LEN( spawnWeapons ); i++ ) client->ps.ammo[spawnWeapons[i].weapon] = 0;
		client->ps.ammo[info->weapon] = info->ammo;
		client->ps.weapon = info->weapon;
		if ( client->pers.activeClass == CLASS_SENTINEL ) {
			client->ps.stats[STAT_WEAPONS] = 1 << WP_GAUNTLET;
			client->ps.ammo[WP_SHOTGUN] = 0;
		}
	}
	G_RemoveHeldBackWeapons( client );
	G_RefillInfiniteAmmo( client );
}
#endif

void ClientSpawn(gentity_t *ent) {
	int		index;
	vec3_t	spawn_origin, spawn_angles;
	gclient_t	*client;
	int		i;
	clientPersistant_t	saved;
	clientSession_t		savedSess;
	int		persistant[MAX_PERSISTANT];
	gentity_t	*spawnPoint;
	gentity_t *tent;
	int		flags;
	int		savedPing;
//	char	*savedAreaBits;
	int		accuracy_hits, accuracy_shots;
	int		eventSequence;
	int		spawnWeapon;
	char	userinfo[MAX_INFO_STRING];
#ifdef STANDALONE
	qboolean respawning;
	qboolean partnerSpawn;
#endif

	index = ent - g_entities;
	client = ent->client;
#ifdef STANDALONE
	respawning = client->ps.persistant[PERS_SPAWN_COUNT] > 0;
	partnerSpawn = qfalse;
#endif

	VectorClear(spawn_origin);

	// find a spawn point
	// do it before setting health back up, so farthest
	// ranging doesn't count this client
	if ( client->sess.sessionTeam == TEAM_SPECTATOR ) {
		spawnPoint = SelectSpectatorSpawnPoint ( 
						spawn_origin, spawn_angles);
	} else if (g_gametype.integer >= GT_CTF ) {
		// all base oriented team games use the CTF spawn points
		spawnPoint = SelectCTFSpawnPoint ( 
						client->sess.sessionTeam, 
						client->pers.teamState.state, 
						spawn_origin, spawn_angles,
						!!(ent->r.svFlags & SVF_BOT));
	}
#ifdef STANDALONE
	else if ( g_gametype.integer == GT_TEAM ) {
		// team deathmatch starts each side at its own end of the map
		spawnPoint = G_SelectTeamSpawnPoint( client->sess.sessionTeam,
			spawn_origin, spawn_angles, !!(ent->r.svFlags & SVF_BOT) );
	}
#endif
	else
	{
		// the first spawn should be at a good looking spot
		if ( !client->pers.initialSpawn && client->pers.localClient )
		{
			client->pers.initialSpawn = qtrue;
			spawnPoint = SelectInitialSpawnPoint(spawn_origin, spawn_angles,
							     !!(ent->r.svFlags & SVF_BOT));
		}
		else
		{
			// don't spawn near existing origin if possible
			spawnPoint = SelectSpawnPoint ( 
				client->ps.origin, 
				spawn_origin, spawn_angles, !!(ent->r.svFlags & SVF_BOT));
		}
	}
#ifdef STANDALONE
	if ( respawning && !client->pers.freshRespawn &&
		client->sess.sessionTeam != TEAM_SPECTATOR ) {
		partnerSpawn = SelectCompanionSpawnPosition( ent, spawn_origin, spawn_angles );
	}
	client->pers.freshRespawn = qfalse;
	if ( !partnerSpawn && client->sess.sessionTeam != TEAM_SPECTATOR ) {
		SelectCrowdSpawnPosition( ent, spawnPoint, spawn_origin, spawn_angles );
	}
	// A practice bot starts at the checked spot, before KillBox can affect anyone.
	if ( client->pers.practiceBotSpawn ) {
		VectorCopy( client->pers.practiceBotOrigin, spawn_origin );
		VectorCopy( client->pers.practiceBotAngles, spawn_angles );
		client->pers.practiceBotSpawn = qfalse;
		spawnPoint = NULL;
	}
#endif
	client->pers.teamState.state = TEAM_ACTIVE;

	// always clear the kamikaze flag
	ent->s.eFlags &= ~EF_KAMIKAZE;

	// toggle the teleport bit so the client knows to not lerp
	// and never clear the voted flag
	flags = ent->client->ps.eFlags & (EF_TELEPORT_BIT | EF_VOTED | EF_TEAMVOTED);
	flags ^= EF_TELEPORT_BIT;

	// clear everything but the persistant data

	saved = client->pers;
	savedSess = client->sess;
	savedPing = client->ps.ping;
//	savedAreaBits = client->areabits;
	accuracy_hits = client->accuracy_hits;
	accuracy_shots = client->accuracy_shots;
	for ( i = 0 ; i < MAX_PERSISTANT ; i++ ) {
		persistant[i] = client->ps.persistant[i];
	}
	eventSequence = client->ps.eventSequence;

	Com_Memset (client, 0, sizeof(*client));

	client->pers = saved;
	client->sess = savedSess;
	client->ps.ping = savedPing;
//	client->areabits = savedAreaBits;
	client->accuracy_hits = accuracy_hits;
	client->accuracy_shots = accuracy_shots;
	client->lastkilled_client = -1;

	for ( i = 0 ; i < MAX_PERSISTANT ; i++ ) {
		client->ps.persistant[i] = persistant[i];
	}
	client->ps.eventSequence = eventSequence;
	// increment the spawncount so the client will detect the respawn
	client->ps.persistant[PERS_SPAWN_COUNT]++;
	client->ps.persistant[PERS_TEAM] = client->sess.sessionTeam;
	client->ps.persistant[PERS_LMS_LIVES] = client->pers.lmsParticipant ? client->pers.lmsLives : -1;

	client->airOutTime = level.time + 12000;

	trap_GetUserinfo( index, userinfo, sizeof(userinfo) );
	// set max health
	client->pers.maxHealth = atoi( Info_ValueForKey( userinfo, "handicap" ) );
	if ( client->pers.maxHealth < 1 || client->pers.maxHealth > 100 ) {
		client->pers.maxHealth = 100;
	}
	// clear entity values
	client->ps.stats[STAT_MAX_HEALTH] = client->pers.maxHealth;
	client->ps.eFlags = flags;
#ifdef STANDALONE
	client->pers.activeClass = client->pers.playerClass;
	if ( G_TestChamberEquipment( client ) ) client->pers.activeClass = CLASS_SENTINEL;
	client->pers.classSpawnConfirmed = qfalse;
	if ( G_PlayerClassesActive() ) {
		const playerClassInfo_t *info = BG_PlayerClass( client->pers.activeClass );
		client->pers.maxHealth = MAX( 1, client->pers.maxHealth * info->health / 100 );
		client->ps.stats[STAT_MAX_HEALTH] = client->pers.maxHealth;
		client->ps.stats[STAT_ARMOR] = info->armor;
		client->ps.stats[STAT_SHIELD_HEALTH] = client->pers.activeClass == CLASS_SENTINEL ? SHIELD_MAX_HEALTH : 0;
	}
	if ( G_TestChamberEquipment( client ) ) client->ps.stats[STAT_SHIELD_HEALTH] = SHIELD_MAX_HEALTH;
#endif
	if ( G_KillshiftActive() ) {
		client->ps.stats[STAT_MAX_HEALTH] = (int)ceil( client->pers.maxHealth *
			G_KillshiftStrength( client->sess.sessionTeam ) );
	} else if ( G_InfectedActive() ) {
		client->ps.stats[STAT_MAX_HEALTH] = (int)ceil( client->pers.maxHealth *
			G_InfectedStrength( client->sess.sessionTeam ) );
	}

	ent->s.groundEntityNum = ENTITYNUM_NONE;
	ent->client = &level.clients[index];
	ent->takedamage = qtrue;
	ent->inuse = qtrue;
	ent->classname = "player";
	ent->r.contents = CONTENTS_BODY;
	ent->clipmask = MASK_PLAYERSOLID;
	ent->die = player_die;
	ent->waterlevel = 0;
	ent->watertype = 0;
	ent->flags = 0;
	
	VectorCopy (playerMins, ent->r.mins);
	VectorCopy (playerMaxs, ent->r.maxs);

	client->ps.clientNum = index;

#ifdef STANDALONE
	G_GiveSpawnLoadout( client );
	spawnWeapon = client->ps.weapon;
#else
	client->ps.stats[STAT_WEAPONS] = ( 1 << WP_GAUNTLET ) | ( 1 << WP_SHOTGUN ) |
		( 1 << WP_MACHINEGUN ) | ( 1 << WP_ROCKET_LAUNCHER ) |
		( 1 << WP_PLASMAGUN );
	client->ps.ammo[WP_SHOTGUN] = -1;
	client->ps.ammo[WP_GAUNTLET] = -1;
	client->ps.ammo[WP_GRAPPLING_HOOK] = -1;
	client->ps.ammo[WP_MACHINEGUN] = 120;
	client->ps.ammo[WP_ROCKET_LAUNCHER] = 5;
	client->ps.ammo[WP_PLASMAGUN] = 240;
	spawnWeapon = WP_MACHINEGUN;
#endif
	client->ps.stats[STAT_RIFLE_RESERVE] = 0;
	client->ps.stats[STAT_RIFLE_RELOAD_END] = 0;
	client->ps.stats[STAT_RIFLE_TRIGGER_BUFFER] = 0;
	client->ps.stats[STAT_RIFLE_TRIGGER_HELD] = 0;

	// health will count down towards max_health
	ent->health = client->ps.stats[STAT_HEALTH] = client->ps.stats[STAT_MAX_HEALTH] + 25;
#ifdef STANDALONE
	if ( G_PlayerClassesActive() ) ent->health = client->ps.stats[STAT_HEALTH] = client->ps.stats[STAT_MAX_HEALTH];
	G_ClassSelectionStatus( ent );
#endif

	G_SetOrigin( ent, spawn_origin );
	VectorCopy( spawn_origin, client->ps.origin );

	// the respawned flag will be cleared after the attack and jump keys come up
	client->ps.pm_flags |= PMF_RESPAWNED;

	trap_GetUsercmd( client - level.clients, &ent->client->pers.cmd );
	SetClientViewAngle( ent, spawn_angles );
	// don't allow full run speed for a bit
	client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
	client->ps.pm_time = 100;

	client->respawnTime = level.time;
	client->inactivityTime = level.time + g_inactivity.integer * 1000;
	client->latched_buttons = 0;

	// set default animations
	client->ps.torsoAnim = TORSO_STAND;
	client->ps.legsAnim = LEGS_IDLE;

	if (!level.intermissiontime) {
		if (ent->client->sess.sessionTeam != TEAM_SPECTATOR) {
			G_KillBox(ent);
			// force the base weapon up
			client->ps.weapon = spawnWeapon;
			client->ps.weaponstate = WEAPON_READY;
			// fire the targets of the spawn point
			G_UseTargets(spawnPoint, ent);
			// The loadout's first gun (normally the rifle) is the neutral default.
			client->ps.weapon = spawnWeapon;
			// positively link the client, even if the command times are weird
			VectorCopy(ent->client->ps.origin, ent->r.currentOrigin);

			tent = G_TempEntity(ent->client->ps.origin, EV_PLAYER_TELEPORT_IN);
			tent->s.clientNum = ent->s.clientNum;

			trap_LinkEntity (ent);
		}
	} else {
		// move players to intermission
		MoveClientToIntermission(ent);
	}
	// run a client frame to drop exactly to the floor,
	// initialize animations and other things
	client->ps.commandTime = level.time - 100;
	ent->client->pers.cmd.serverTime = level.time;
	ClientThink( ent-g_entities );
	// run the presend to set anything else, follow spectators wait
	// until all clients have been reconnected after map_restart
	if ( ent->client->sess.spectatorState != SPECTATOR_FOLLOW ) {
		ClientEndFrame( ent );
	}

	// clear entity state values
	BG_PlayerStateToEntityState( &client->ps, &ent->s, qtrue );
}


/*
===========
ClientDisconnect

Called when a player drops from the server.
Will not be called between levels.

This should NOT be called directly by any game logic,
call trap_DropClient(), which will call this and do
server system housekeeping.
============
*/
void ClientDisconnect( int clientNum ) {
	gentity_t	*ent;
	gentity_t	*tent;
	int			i;

	// cleanup if we are kicking a bot that
	// hasn't spawned yet
	G_RemoveQueuedBotBegin( clientNum );

	G_ClearSiegeTraps( &g_entities[clientNum] );
	ent = g_entities + clientNum;
	if (!ent->client || ent->client->pers.connected == CON_DISCONNECTED) {
		return;
	}

	G_LeprechaunLeave( ent );

	// stop any following clients
	for ( i = 0 ; i < level.maxclients ; i++ ) {
		if ( level.clients[i].sess.sessionTeam == TEAM_SPECTATOR
			&& level.clients[i].sess.spectatorState == SPECTATOR_FOLLOW
			&& level.clients[i].sess.spectatorClient == clientNum ) {
			StopFollowing( &g_entities[i] );
		}
	}

	// send effect if they were completely connected
	if ( ent->client->pers.connected == CON_CONNECTED 
		&& ent->client->sess.sessionTeam != TEAM_SPECTATOR ) {
		tent = G_TempEntity( ent->client->ps.origin, EV_PLAYER_TELEPORT_OUT );
		tent->s.clientNum = ent->s.clientNum;

		// They don't get to take powerups with them!
		// Especially important for stuff like CTF flags
		TossClientItems( ent );
#ifdef MISSIONPACK
		TossClientPersistantPowerups( ent );
		if( g_gametype.integer == GT_HARVESTER ) {
			TossClientCubes( ent );
		}
#endif

	}

	G_LogPrintf( ent->client->pers.menuObserver ? "MenuObserver: %i disconnected\n" : "ClientDisconnect: %i\n", clientNum );

#ifdef STANDALONE
	if ( ent->r.svFlags & SVF_BOT ) {
		G_RememberPopulationBot( clientNum );
	}
#endif

	// if we are playing in tourney mode and losing, give a win to the other player
	if ( (g_gametype.integer == GT_TOURNAMENT )
		&& !level.intermissiontime
		&& !level.warmupTime && level.sortedClients[1] == clientNum ) {
		level.clients[ level.sortedClients[0] ].sess.wins++;
		ClientUserinfoChanged( level.sortedClients[0] );
	}

	if( g_gametype.integer == GT_TOURNAMENT &&
		ent->client->sess.sessionTeam == TEAM_FREE &&
		level.intermissiontime ) {

		trap_SendConsoleCommand( EXEC_APPEND, "map_restart 0\n" );
		level.restarted = qtrue;
		level.changemap = NULL;
		level.intermissiontime = 0;
	}

	trap_UnlinkEntity (ent);
	ent->s.modelindex = 0;
	ent->inuse = qfalse;
	ent->classname = "disconnected";
	ent->client->pers.connected = CON_DISCONNECTED;
	ent->client->ps.persistant[PERS_TEAM] = TEAM_FREE;
	ent->client->sess.sessionTeam = TEAM_FREE;

	trap_SetConfigstring( CS_PLAYERS + clientNum, "");

	CalculateRanks();

#ifdef STANDALONE
	G_RequestPopulationCheck();
#endif

	if ( ent->r.svFlags & SVF_BOT ) {
		BotAIShutdownClient( clientNum, qfalse );
	}
}


