/*
===========================================================================
Bug Hunt spiders: a neutral swarm that attacks every player regardless of
team. Like npc_enemy they are plain game entities rather than bot clients,
so a swarm costs no client slots. They hunt the nearest player they can see,
follow the map's AAS routes toward players they can't, bite at close range
and pounce from mid range. A pounce is also how they drop off ledges and
jump gaps the walking move can't handle.

s.frame carries the animation state for cgame (SPIDER_ANIM_*), s.time the
level time that state started, for the one-shot animations.
===========================================================================
*/

#include "g_local.h"
#include "../botlib/botlib.h"
#include "../botlib/be_aas.h"

#define SPIDER_THINK_MSEC		50
#define SPIDER_SIGHT_RANGE		2400.0f
#define SPIDER_BITE_RANGE		72.0f
#define SPIDER_BITE_DELAY		200		// strike lands at model frame 25
#define SPIDER_BITE_COOLDOWN	750
#define SPIDER_LEAP_MIN			140.0f
#define SPIDER_LEAP_MAX			380.0f
#define SPIDER_LEAP_COOLDOWN	1600
#define SPIDER_LEAP_SPEED		560.0f
#define SPIDER_LEAP_LIFT		300.0f
#define SPIDER_ROUTE_MSEC		400
#define SPIDER_CORPSE_MSEC		3000
#define SPIDER_BASE_HEALTH		45
#define SPIDER_BITE_DAMAGE		9
#define SPIDER_POUNCE_DAMAGE	14
#define SPIDER_SPEED			290

typedef struct {
	vec3_t	velocity;		// while airborne
	vec3_t	waypoint;		// next AAS route point toward the target
	int		routeTime;		// when to recompute the waypoint
	int		biteTime;		// pending bite lands at this time, 0 for none
	int		nextAttack;
	int		nextLeap;
	int		nextChitter;
	qboolean	airborne;
	qboolean	pounceHit;	// a pounce damages at most once
} spiderState_t;

static spiderState_t spiderStates[MAX_GENTITIES];

qboolean G_IsSpider( const gentity_t *ent ) {
	return ent && ent->inuse && ent->classname && !Q_stricmp( ent->classname, "npc_spider" );
}

static void Spider_SetAnim( gentity_t *self, int anim ) {
	if ( self->s.frame == anim ) {
		return;
	}
	self->s.frame = anim;
	self->s.time = level.time;
}

static qboolean Spider_TargetValid( gentity_t *target ) {
	return target && target->inuse && target->client &&
		target->client->pers.connected == CON_CONNECTED && target->health > 0 &&
		G_IsActiveTeam( target->client->sess.sessionTeam ) &&
		!( target->flags & FL_NOTARGET );
}

static qboolean Spider_CanSee( gentity_t *self, gentity_t *target ) {
	trace_t tr;
	vec3_t start, end;

	VectorCopy( self->r.currentOrigin, start );
	start[2] += 24;
	VectorCopy( target->r.currentOrigin, end );
	trap_Trace( &tr, start, NULL, NULL, end, self->s.number, MASK_SOLID );
	return tr.fraction == 1.0f;
}

/*
Nearest visible player, or failing that the nearest player anywhere: the
swarm always closes in, it just has to path when it can't see anyone.
*/
static gentity_t *Spider_FindTarget( gentity_t *self, qboolean *visible ) {
	int i;
	float bestSeen = SPIDER_SIGHT_RANGE * SPIDER_SIGHT_RANGE, bestAny = 1e18f;
	gentity_t *seen = NULL, *any = NULL;

	for ( i = 0; i < level.numConnectedClients; i++ ) {
		gentity_t *candidate = &g_entities[level.sortedClients[i]];
		float distance;

		if ( !Spider_TargetValid( candidate ) ) {
			continue;
		}
		distance = DistanceSquared( candidate->r.currentOrigin, self->r.currentOrigin );
		if ( distance < bestAny ) {
			bestAny = distance;
			any = candidate;
		}
		if ( distance < bestSeen && Spider_CanSee( self, candidate ) ) {
			bestSeen = distance;
			seen = candidate;
		}
	}
	*visible = seen != NULL;
	return seen ? seen : any;
}

// Next point along the AAS route to the target, false when there's no route.
static qboolean Spider_RouteWaypoint( gentity_t *self, gentity_t *target, vec3_t waypoint ) {
	aas_predictroute_t route;
	vec3_t start, goal;
	int area, goalArea;

	VectorCopy( self->r.currentOrigin, start );
	start[2] += 24;
	VectorCopy( target->r.currentOrigin, goal );
	area = trap_AAS_PointAreaNum( start );
	goalArea = trap_AAS_PointAreaNum( goal );
	if ( !area || !goalArea ) {
		return qfalse;
	}
	if ( area == goalArea ) {
		VectorCopy( goal, waypoint );
		return qtrue;
	}
	memset( &route, 0, sizeof( route ) );
	trap_AAS_PredictRoute( &route, area, start, goalArea, TFL_DEFAULT, 1, 400,
		RSE_NONE, 0, 0, 0 );
	if ( route.stopevent & RSE_NOROUTE ) {
		return qfalse;
	}
	VectorCopy( route.endpos, waypoint );
	return qtrue;
}

static void Spider_Sound( gentity_t *self, const char *name ) {
	G_Sound( self, CHAN_VOICE, G_SoundIndex( va( "sound/enemies/spider/%s.wav", name ) ) );
}

static void Spider_Leap( gentity_t *self, const vec3_t toward, float speed, float lift ) {
	spiderState_t *st = &spiderStates[self->s.number];
	vec3_t dir;

	VectorCopy( toward, dir );
	dir[2] = 0;
	if ( VectorNormalize( dir ) < 0.01f ) {
		return;
	}
	VectorScale( dir, speed, st->velocity );
	st->velocity[2] = lift;
	st->airborne = qtrue;
	st->pounceHit = qfalse;
	st->nextLeap = level.time + SPIDER_LEAP_COOLDOWN + ( rand() % 900 );
	Enemy_FaceDirection( self, dir );
	Spider_SetAnim( self, SPIDER_ANIM_LEAP );
	Spider_Sound( self, "leap" );
}

static void Spider_Hit( gentity_t *self, gentity_t *target, int damage ) {
	vec3_t dir;

	VectorSubtract( target->r.currentOrigin, self->r.currentOrigin, dir );
	VectorNormalize( dir );
	G_Damage( target, self, self, dir, target->r.currentOrigin, damage, 0, MOD_SPIDER );
}

// Ballistic flight for pounces and drops; lands on anything walkable.
static void Spider_Fly( gentity_t *self ) {
	spiderState_t *st = &spiderStates[self->s.number];
	float dt = SPIDER_THINK_MSEC / 1000.0f;
	trace_t tr;
	vec3_t end;

	st->velocity[2] -= g_gravity.value * dt;
	VectorMA( self->r.currentOrigin, dt, st->velocity, end );
	trap_Trace( &tr, self->r.currentOrigin, self->r.mins, self->r.maxs, end,
		self->s.number, MASK_PLAYERSOLID );
	if ( tr.startsolid ) {
		st->airborne = qfalse;
		return;
	}
	G_SetOrigin( self, tr.endpos );
	self->s.pos.trType = TR_INTERPOLATE;
	trap_LinkEntity( self );
	if ( tr.fraction == 1.0f ) {
		return;
	}
	if ( tr.entityNum < MAX_CLIENTS && Spider_TargetValid( &g_entities[tr.entityNum] ) ) {
		if ( !st->pounceHit ) {
			st->pounceHit = qtrue;
			Spider_Hit( self, &g_entities[tr.entityNum], SPIDER_POUNCE_DAMAGE );
			Spider_Sound( self, "attack" );
		}
		// slide off whoever was hit
		st->velocity[0] *= -0.2f;
		st->velocity[1] *= -0.2f;
		return;
	}
	if ( tr.plane.normal[2] >= 0.7f ) {
		st->airborne = qfalse;
		VectorClear( st->velocity );
		Spider_SetAnim( self, SPIDER_ANIM_IDLE );
		return;
	}
	// hit a wall: lose the horizontal speed and drop
	VectorMA( st->velocity, -DotProduct( st->velocity, tr.plane.normal ), tr.plane.normal, st->velocity );
}

static qboolean Spider_OnGround( gentity_t *self ) {
	trace_t tr;
	vec3_t end;

	VectorCopy( self->r.currentOrigin, end );
	end[2] -= 4;
	trap_Trace( &tr, self->r.currentOrigin, self->r.mins, self->r.maxs, end,
		self->s.number, MASK_PLAYERSOLID );
	return tr.fraction < 1.0f && tr.plane.normal[2] >= 0.7f;
}

static void Spider_Think( gentity_t *self ) {
	spiderState_t *st = &spiderStates[self->s.number];
	gentity_t *target;
	vec3_t delta, toward;
	float distance;
	qboolean visible;

	self->nextthink = level.time + SPIDER_THINK_MSEC;
	if ( self->s.frame == SPIDER_ANIM_DEAD ) {
		if ( level.time >= self->timestamp ) {
			G_FreeEntity( self );
		}
		return;
	}
	if ( level.intermissiontime || level.intermissionQueued ) {
		return;
	}
	if ( st->airborne ) {
		Spider_Fly( self );
		return;
	}
	if ( !Spider_OnGround( self ) ) {
		// walked or was pushed off something
		VectorClear( st->velocity );
		st->airborne = qtrue;
		st->pounceHit = qtrue;
		return;
	}
	if ( level.time < self->pain_debounce_time ) {
		return;
	}
	if ( st->biteTime && level.time >= st->biteTime ) {
		st->biteTime = 0;
		if ( Spider_TargetValid( self->enemy ) &&
			Distance( self->enemy->r.currentOrigin, self->r.currentOrigin ) <= SPIDER_BITE_RANGE + 24 ) {
			Spider_Hit( self, self->enemy, self->damage );
		}
	}
	if ( self->s.frame == SPIDER_ANIM_ATTACK && level.time < self->s.time + 520 ) {
		return;		// let the strike play out
	}

	target = Spider_FindTarget( self, &visible );
	self->enemy = target;
	if ( !target ) {
		Spider_SetAnim( self, SPIDER_ANIM_IDLE );
		return;
	}
	if ( level.time >= st->nextChitter && visible ) {
		Spider_Sound( self, va( "chitter%i", 1 + rand() % 3 ) );
		st->nextChitter = level.time + 2500 + rand() % 4000;
	}

	VectorSubtract( target->r.currentOrigin, self->r.currentOrigin, delta );
	distance = VectorLength( delta );

	if ( visible && distance <= SPIDER_BITE_RANGE + 16 && fabs( delta[2] ) < 64 ) {
		Enemy_FaceDirection( self, delta );
		if ( level.time >= st->nextAttack ) {
			Spider_SetAnim( self, SPIDER_ANIM_ATTACK );
			self->s.time = level.time;
			st->biteTime = level.time + SPIDER_BITE_DELAY;
			st->nextAttack = level.time + SPIDER_BITE_COOLDOWN;
			Spider_Sound( self, "attack" );
		} else {
			Spider_SetAnim( self, SPIDER_ANIM_IDLE );
		}
		return;
	}
	if ( visible && distance >= SPIDER_LEAP_MIN && distance <= SPIDER_LEAP_MAX &&
		level.time >= st->nextLeap && delta[2] < 96 && ( rand() & 1 ) == 0 ) {
		Spider_Leap( self, delta, SPIDER_LEAP_SPEED * ( 0.6f + 0.4f * distance / SPIDER_LEAP_MAX ),
			SPIDER_LEAP_LIFT + delta[2] );
		return;
	}

	if ( visible ) {
		VectorCopy( delta, toward );
	} else {
		if ( level.time >= st->routeTime ) {
			st->routeTime = level.time + SPIDER_ROUTE_MSEC + ( rand() % 200 );
			if ( !Spider_RouteWaypoint( self, target, st->waypoint ) ) {
				VectorCopy( target->r.currentOrigin, st->waypoint );
			}
		}
		VectorSubtract( st->waypoint, self->r.currentOrigin, toward );
	}
	if ( Enemy_MoveWithAvoidance( self, toward ) ) {
		Spider_SetAnim( self, SPIDER_ANIM_WALK );
		Enemy_FaceDirection( self, self->movedir );
		return;
	}
	// Blocked: a ledge to drop off or a gap to jump. Hop toward the goal.
	if ( level.time >= st->nextLeap ) {
		Spider_Leap( self, toward, 300, 260 );
		st->nextLeap = level.time + 900;
		st->routeTime = 0;
		return;
	}
	Spider_SetAnim( self, SPIDER_ANIM_IDLE );
}

static void Spider_Pain( gentity_t *self, gentity_t *attacker, int damage ) {
	spiderState_t *st = &spiderStates[self->s.number];

	if ( st->airborne || self->s.frame == SPIDER_ANIM_ATTACK ) {
		return;
	}
	if ( level.time >= self->fly_sound_debounce_time ) {
		Spider_Sound( self, "pain" );
		self->fly_sound_debounce_time = level.time + 500;
		Spider_SetAnim( self, SPIDER_ANIM_PAIN );
		self->pain_debounce_time = level.time + 150;
	}
}

static void Spider_Die( gentity_t *self, gentity_t *inflictor, gentity_t *attacker,
	int damage, int mod ) {
	spiderStates[self->s.number].airborne = qfalse;
	spiderStates[self->s.number].biteTime = 0;
	self->takedamage = qfalse;
	self->r.contents = 0;
	self->s.eFlags |= EF_DEAD;
	self->timestamp = level.time + SPIDER_CORPSE_MSEC;
	Spider_SetAnim( self, SPIDER_ANIM_DEAD );
	Spider_Sound( self, "death" );
	if ( attacker && attacker->client ) {
		AddScore( attacker, self->r.currentOrigin, 1 );
	}
	G_BugHuntSpiderKilled( self, attacker );
	trap_LinkEntity( self );
}

gentity_t *G_SpawnSpider( const vec3_t origin, float yaw, float healthScale ) {
	gentity_t *self = G_Spawn();
	spiderState_t *st = &spiderStates[self->s.number];

	memset( st, 0, sizeof( *st ) );
	st->nextLeap = level.time + 1500 + ( rand() % 1500 );
	st->nextChitter = level.time + ( rand() % 3000 );
	self->classname = "npc_spider";
	self->s.eType = ET_GENERAL;
	self->s.modelindex = G_ModelIndex( "models/enemies/spider/spider.md3" );
	self->health = (int)ceil( SPIDER_BASE_HEALTH * healthScale );
	self->damage = SPIDER_BITE_DAMAGE;
	self->speed = SPIDER_SPEED;
	self->clipmask = MASK_PLAYERSOLID;
	self->r.contents = CONTENTS_BODY;
	VectorSet( self->r.mins, -24, -24, 0 );
	VectorSet( self->r.maxs, 24, 24, 40 );
	self->takedamage = qtrue;
	self->pain = Spider_Pain;
	self->die = Spider_Die;
	self->think = Spider_Think;
	self->nextthink = level.time + SPIDER_THINK_MSEC + ( rand() % 200 );
	self->s.frame = SPIDER_ANIM_IDLE;
	self->s.time = level.time;
	self->s.apos.trType = TR_STATIONARY;
	self->s.apos.trBase[YAW] = yaw;
	VectorCopy( self->s.apos.trBase, self->r.currentAngles );
	G_SetOrigin( self, origin );
	self->s.pos.trType = TR_INTERPOLATE;
	trap_LinkEntity( self );
	return self;
}

void SP_npc_spider( gentity_t *ent ) {
	vec3_t origin;
	float yaw = ent->s.angles[YAW];

	VectorCopy( ent->s.origin, origin );
	G_FreeEntity( ent );
	G_SpawnSpider( origin, yaw, 1.0f );
}

int G_CountSpiders( void ) {
	int i, count = 0;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		if ( G_IsSpider( &g_entities[i] ) && g_entities[i].takedamage ) {
			count++;
		}
	}
	return count;
}

void G_RemoveAllSpiders( void ) {
	int i;

	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		if ( G_IsSpider( &g_entities[i] ) ) {
			G_FreeEntity( &g_entities[i] );
		}
	}
}
