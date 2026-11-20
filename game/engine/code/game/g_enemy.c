/*
===========================================================================
Standalone test-game enemy AI.

This intentionally uses ordinary game entities instead of Arena bot clients.
The result has no bot personality or baseq3 data dependency, while still using
the engine's traces, damage system, events, snapshots, and collision model.
===========================================================================
*/

#include "g_local.h"

#define ENEMY_THINK_MSEC       50
#define ENEMY_DETECT_RANGE     1500.0f
#define ENEMY_CHASE_DISTANCE   420.0f
#define ENEMY_RETREAT_DISTANCE 210.0f
#define ENEMY_APPROACH_DISTANCE 340.0f
#define ENEMY_STEP_HEIGHT      24.0f
#define ENEMY_EYE_HEIGHT       52.0f

typedef enum {
	ENEMY_IDLE,
	ENEMY_WANDER,
	ENEMY_CHASE,
	ENEMY_ATTACK,
	ENEMY_PAIN,
	ENEMY_DEAD
} enemyState_t;

static const char *Enemy_StateName( int state ) {
	switch ( state ) {
	case ENEMY_IDLE: return "idle";
	case ENEMY_WANDER: return "wander";
	case ENEMY_CHASE: return "chase";
	case ENEMY_ATTACK: return "attack";
	case ENEMY_PAIN: return "pain";
	case ENEMY_DEAD: return "dead";
	default: return "unknown";
	}
}

qboolean G_IsCombatEnemy( const gentity_t *ent ) {
	return ent && ent->classname && ( !Q_stricmp( ent->classname, "npc_enemy" ) ||
		!Q_stricmp( ent->classname, "npc_spider" ) );
}

static void Enemy_SetState( gentity_t *self, enemyState_t state ) {
	if ( self->count == state ) {
		return;
	}
	self->count = state;
	if ( trap_Cvar_VariableIntegerValue( "g_debugEnemies" ) ) {
		G_Printf( "enemy %i -> %s\n", self->s.number, Enemy_StateName( state ) );
	}
}

static qboolean Enemy_TargetValid( gentity_t *target ) {
	return target && target->inuse && target->client &&
		target->client->pers.connected == CON_CONNECTED && target->health > 0 &&
		target->client->sess.sessionTeam != TEAM_SPECTATOR &&
		!( target->flags & FL_NOTARGET );
}

static qboolean Enemy_CanSee( gentity_t *self, gentity_t *target ) {
	trace_t tr;
	vec3_t start, end;

	VectorCopy( self->r.currentOrigin, start );
	start[2] += ENEMY_EYE_HEIGHT;
	VectorCopy( target->r.currentOrigin, end );
	end[2] += target->client->ps.viewheight * 0.75f;
	trap_Trace( &tr, start, NULL, NULL, end, self->s.number, MASK_SHOT );
	return tr.fraction == 1.0f || tr.entityNum == target->s.number;
}

static gentity_t *Enemy_FindTarget( gentity_t *self ) {
	int i;
	float bestDistance = ENEMY_DETECT_RANGE;
	gentity_t *best = NULL;

	for ( i = 0; i < level.maxclients; i++ ) {
		gentity_t *candidate = &g_entities[i];
		vec3_t delta;
		float distance;

		if ( !Enemy_TargetValid( candidate ) || ( candidate->r.svFlags & SVF_BOT ) ) {
			continue;
		}
		VectorSubtract( candidate->r.currentOrigin, self->r.currentOrigin, delta );
		distance = VectorLength( delta );
		if ( distance < bestDistance && Enemy_CanSee( self, candidate ) ) {
			bestDistance = distance;
			best = candidate;
		}
	}
	return best;
}

void Enemy_FaceDirection( gentity_t *self, const vec3_t direction ) {
	vec3_t angles;
	vectoangles( direction, angles );
	self->s.apos.trType = TR_STATIONARY;
	self->s.apos.trBase[PITCH] = 0;
	self->s.apos.trBase[YAW] = angles[YAW];
	self->s.apos.trBase[ROLL] = 0;
	VectorCopy( self->s.apos.trBase, self->r.currentAngles );
}

static qboolean Enemy_GroundAt( gentity_t *self, const vec3_t position, vec3_t grounded ) {
	trace_t tr;
	vec3_t start, end;

	VectorCopy( position, start );
	start[2] += 8;
	VectorCopy( position, end );
	end[2] -= 56;
	trap_Trace( &tr, start, self->r.mins, self->r.maxs, end,
		self->s.number, MASK_PLAYERSOLID );
	if ( tr.startsolid || tr.allsolid || tr.fraction == 1.0f || tr.plane.normal[2] < 0.55f ) {
		return qfalse;
	}
	VectorCopy( tr.endpos, grounded );
	return qtrue;
}

static qboolean Enemy_TryMove( gentity_t *self, const vec3_t direction ) {
	trace_t tr;
	vec3_t dir, end, grounded, raised;
	float distance = self->speed * ( ENEMY_THINK_MSEC / 1000.0f );

	VectorCopy( direction, dir );
	dir[2] = 0;
	if ( VectorNormalize( dir ) < 0.01f ) {
		return qfalse;
	}
	VectorMA( self->r.currentOrigin, distance, dir, end );
	trap_Trace( &tr, self->r.currentOrigin, self->r.mins, self->r.maxs, end,
		self->s.number, MASK_PLAYERSOLID );
	if ( tr.fraction == 1.0f && Enemy_GroundAt( self, end, grounded ) ) {
		G_SetOrigin( self, grounded );
		self->s.pos.trType = TR_INTERPOLATE;
		self->s.pos.trTime = level.time;
		VectorCopy( dir, self->movedir );
		trap_LinkEntity( self );
		return qtrue;
	}

	VectorCopy( self->r.currentOrigin, raised );
	raised[2] += ENEMY_STEP_HEIGHT;
	trap_Trace( &tr, self->r.currentOrigin, self->r.mins, self->r.maxs, raised,
		self->s.number, MASK_PLAYERSOLID );
	if ( tr.fraction < 1.0f ) {
		return qfalse;
	}
	VectorMA( raised, distance, dir, end );
	trap_Trace( &tr, raised, self->r.mins, self->r.maxs, end,
		self->s.number, MASK_PLAYERSOLID );
	if ( tr.fraction < 1.0f || !Enemy_GroundAt( self, end, grounded ) ) {
		return qfalse;
	}
	G_SetOrigin( self, grounded );
	self->s.pos.trType = TR_INTERPOLATE;
	self->s.pos.trTime = level.time;
	VectorCopy( dir, self->movedir );
	trap_LinkEntity( self );
	return qtrue;
}

qboolean Enemy_MoveWithAvoidance( gentity_t *self, const vec3_t desired ) {
	static const float offsets[] = { 0, 32, -32, 68, -68, 105, -105, 180 };
	vec3_t forward, candidate;
	float side = ( self->s.number & 1 ) ? 1.0f : -1.0f;
	int i;

	VectorCopy( desired, forward );
	forward[2] = 0;
	if ( VectorNormalize( forward ) < 0.01f ) return qfalse;
	for ( i = 0; i < ARRAY_LEN( offsets ); i++ ) {
		float radians = DEG2RAD( offsets[i] * side );
		candidate[0] = forward[0] * cos( radians ) - forward[1] * sin( radians );
		candidate[1] = forward[0] * sin( radians ) + forward[1] * cos( radians );
		candidate[2] = 0;
		if ( Enemy_TryMove( self, candidate ) ) return qtrue;
	}
	VectorClear( self->movedir );
	return qfalse;
}

static void Enemy_ChooseCombatMove( gentity_t *self, const vec3_t targetDelta,
	float distance, vec3_t movement ) {
	vec3_t toward;

	VectorCopy( targetDelta, toward );
	toward[2] = 0;
	VectorNormalize( toward );
	if ( distance < ENEMY_RETREAT_DISTANCE ) {
		VectorScale( toward, -1, movement );
		return;
	}
	if ( distance > ENEMY_APPROACH_DISTANCE ) {
		VectorCopy( toward, movement );
		return;
	}
	if ( level.time >= self->aiMoveChangeTime ||
		VectorLengthSquared( self->aiMoveDirection ) < 0.1f ) {
		float side = ( rand() & 1 ) ? 1.0f : -1.0f;
		VectorSet( self->aiMoveDirection, -toward[1] * side, toward[0] * side, 0 );
		self->aiMoveChangeTime = level.time + 850 + ( rand() % 650 );
	}
	VectorCopy( self->aiMoveDirection, movement );
}

static void Enemy_ChooseWanderTarget( gentity_t *self ) {
	float angle = random() * M_PI * 2.0f;
	float radius = 140.0f + random() * 240.0f;

	self->pos2[0] = self->pos1[0] + cos( angle ) * radius;
	self->pos2[1] = self->pos1[1] + sin( angle ) * radius;
	self->pos2[2] = self->r.currentOrigin[2];
	self->last_move_time = level.time + 1800 + (int)( random() * 2200 );
}

static void Enemy_SendImpact( gentity_t *self, trace_t *tr ) {
	gentity_t *impact;
	gentity_t *hit = &g_entities[tr->entityNum];

	SnapVectorTowards( tr->endpos, self->r.currentOrigin );
	if ( hit->takedamage ) {
		impact = G_TempEntity( tr->endpos, EV_BULLET_HIT_FLESH );
		impact->s.otherEntityNum2 = hit->s.number;
	} else {
		impact = G_TempEntity( tr->endpos, EV_BULLET_HIT_WALL );
		impact->s.eventParm = DirToByte( tr->plane.normal );
	}
	impact->s.otherEntityNum = self->s.number;
}

static void Enemy_Fire( gentity_t *self, gentity_t *target ) {
	trace_t tr;
	vec3_t muzzle, aim, forward, right, up, end;
	float spread = self->random > 0 ? self->random : 0.055f;
	gentity_t *hit;

	VectorSubtract( target->r.currentOrigin, self->r.currentOrigin, forward );
	forward[2] += target->client->ps.viewheight * 0.65f;
	Enemy_FaceDirection( self, forward );
	AngleVectors( self->s.apos.trBase, forward, right, up );
	VectorCopy( self->r.currentOrigin, muzzle );
	muzzle[2] += 46;
	VectorMA( muzzle, 26, forward, muzzle );

	VectorCopy( target->r.currentOrigin, aim );
	aim[2] += target->client->ps.viewheight * 0.65f;
	VectorMA( aim, crandom() * spread * 700.0f, right, aim );
	VectorMA( aim, crandom() * spread * 500.0f, up, aim );
	VectorSubtract( aim, muzzle, forward );
	VectorNormalize( forward );
	VectorMA( muzzle, 8192, forward, end );

	G_AddEvent( self, EV_FIRE_WEAPON, 0 );
	trap_Trace( &tr, muzzle, NULL, NULL, end, self->s.number, MASK_SHOT );
	if ( tr.surfaceFlags & SURF_NOIMPACT ) {
		return;
	}
	Enemy_SendImpact( self, &tr );
	hit = &g_entities[tr.entityNum];
	if ( hit->takedamage ) {
		G_Damage( hit, self, self, forward, tr.endpos, self->damage,
			0, MOD_MACHINEGUN );
	}
}

static void Enemy_Pain( gentity_t *self, gentity_t *attacker, int damage ) {
	if ( attacker && attacker->client ) {
		self->enemy = attacker;
	}
	Enemy_SetState( self, ENEMY_PAIN );
	self->s.frame = 3;
	self->pain_debounce_time = level.time + 220;
	if ( trap_Cvar_VariableIntegerValue( "g_debugEnemies" ) ) {
		G_Printf( "enemy %i hit for %i, health %i\n", self->s.number, damage, self->health );
	}
	if ( level.time >= self->fly_sound_debounce_time ) {
		G_Sound( self, CHAN_VOICE, G_SoundIndex( "sound/enemies/sentry/pain.wav" ) );
		self->fly_sound_debounce_time = level.time + 450;
	}
}

static void Enemy_Die( gentity_t *self, gentity_t *inflictor, gentity_t *attacker,
	int damage, int mod ) {
	self->takedamage = qfalse;
	self->r.contents = 0;
	self->s.frame = 4;
	self->s.eFlags |= EF_DEAD;
	self->timestamp = level.time + 8000;
	Enemy_SetState( self, ENEMY_DEAD );
	G_Sound( self, CHAN_VOICE, G_SoundIndex( "sound/enemies/sentry/death.wav" ) );
	if ( attacker && attacker->client ) {
		AddScore( attacker, self->r.currentOrigin, 1 );
	}
	trap_LinkEntity( self );
}

static void Enemy_Think( gentity_t *self ) {
	vec3_t delta, movement;
	float distance;
	qboolean visible;

	self->nextthink = level.time + ENEMY_THINK_MSEC;
	if ( self->count == ENEMY_DEAD ) {
		if ( level.time >= self->timestamp ) {
			G_FreeEntity( self );
		}
		return;
	}
	if ( self->count == ENEMY_PAIN && level.time < self->pain_debounce_time ) {
		return;
	}

	if ( !Enemy_TargetValid( self->enemy ) ) {
		self->enemy = Enemy_FindTarget( self );
	}
	if ( !self->enemy ) {
		Enemy_SetState( self, ENEMY_WANDER );
		if ( level.time >= self->last_move_time ||
			DistanceSquared( self->r.currentOrigin, self->pos2 ) < 48.0f * 48.0f ) {
			Enemy_ChooseWanderTarget( self );
		}
		VectorSubtract( self->pos2, self->r.currentOrigin, movement );
		if ( Enemy_MoveWithAvoidance( self, movement ) ) {
			self->s.frame = 1 + ( ( level.time / 180 ) & 1 );
			Enemy_FaceDirection( self, self->movedir );
		} else {
			self->last_move_time = 0;
			self->s.frame = 0;
		}
		return;
	}

	VectorSubtract( self->enemy->r.currentOrigin, self->r.currentOrigin, delta );
	distance = VectorLength( delta );
	visible = Enemy_CanSee( self, self->enemy );
	if ( !visible && distance > ENEMY_DETECT_RANGE * 1.15f ) {
		self->enemy = NULL;
		Enemy_SetState( self, ENEMY_IDLE );
		self->s.frame = 0;
		return;
	}

	if ( visible && distance <= ENEMY_CHASE_DISTANCE ) {
		qboolean moved;
		Enemy_SetState( self, ENEMY_ATTACK );
		Enemy_ChooseCombatMove( self, delta, distance, movement );
		moved = Enemy_MoveWithAvoidance( self, movement );
		self->s.frame = moved ? 1 + ( ( level.time / 180 ) & 1 ) : 0;
		Enemy_FaceDirection( self, delta );
		if ( level.time >= self->timestamp ) {
			Enemy_Fire( self, self->enemy );
			self->timestamp = level.time + (int)( self->wait * 1000.0f ) + ( rand() % 180 );
		}
		return;
	}

	Enemy_SetState( self, ENEMY_CHASE );
	if ( Enemy_MoveWithAvoidance( self, delta ) ) {
		self->s.frame = 1 + ( ( level.time / 180 ) & 1 );
		Enemy_FaceDirection( self, self->movedir );
	} else {
		self->s.frame = 0;
	}
}

void SP_npc_enemy( gentity_t *self ) {
	trace_t tr;
	vec3_t start, end;

	if ( self->health <= 0 ) self->health = 70;
	if ( self->damage <= 0 ) self->damage = 7;
	if ( self->speed <= 0 ) self->speed = 145;
	if ( self->wait <= 0 ) self->wait = 0.72f;

	self->classname = "npc_enemy";
	self->s.eType = ET_GENERAL;
	self->s.modelindex = G_ModelIndex( "models/players/sarge/lower.md3" );
	self->s.modelindex2 = G_ModelIndex( "models/players/sarge/upper.md3" );
	self->s.generic1 = G_ModelIndex( "models/players/sarge/head.md3" );
	self->s.weapon = WP_MACHINEGUN;
	self->clipmask = MASK_PLAYERSOLID;
	self->r.contents = CONTENTS_BODY;
	VectorSet( self->r.mins, -22, -22, 0 );
	VectorSet( self->r.maxs, 22, 22, 72 );
	self->takedamage = qtrue;
	self->pain = Enemy_Pain;
	self->die = Enemy_Die;
	self->think = Enemy_Think;
	self->nextthink = level.time + 250 + ( rand() % 300 );
	self->count = ENEMY_IDLE;
	self->s.frame = 0;
	self->s.apos.trType = TR_STATIONARY;
	VectorCopy( self->s.angles, self->s.apos.trBase );
	VectorCopy( self->s.angles, self->r.currentAngles );

	VectorCopy( self->s.origin, start );
	start[2] += 32;
	VectorCopy( self->s.origin, end );
	end[2] -= 256;
	trap_Trace( &tr, start, self->r.mins, self->r.maxs, end,
		self->s.number, MASK_PLAYERSOLID );
	if ( !tr.startsolid && !tr.allsolid && tr.fraction < 1.0f ) {
		G_SetOrigin( self, tr.endpos );
	} else {
		G_SetOrigin( self, self->s.origin );
	}
	self->s.pos.trType = TR_INTERPOLATE;
	self->s.pos.trTime = level.time;
	VectorCopy( self->r.currentOrigin, self->pos1 );
	VectorCopy( self->r.currentOrigin, self->pos2 );
	trap_LinkEntity( self );
}

gentity_t *G_SpawnCombatEnemy( const vec3_t origin, float yaw ) {
	gentity_t *enemy = G_Spawn();
	enemy->classname = "npc_enemy";
	VectorCopy( origin, enemy->s.origin );
	enemy->s.angles[YAW] = yaw;
	SP_npc_enemy( enemy );
	return enemy;
}

void G_KillAllCombatEnemies( void ) {
	gentity_t *enemy = NULL;
	while ( ( enemy = G_Find( enemy, FOFS(classname), "npc_enemy" ) ) != NULL ) {
		if ( enemy->takedamage ) {
			Enemy_Die( enemy, enemy, NULL, 999, MOD_UNKNOWN );
		}
	}
}

void G_PrintCombatEnemies( void ) {
	gentity_t *enemy = NULL;
	int total = 0;
	while ( ( enemy = G_Find( enemy, FOFS(classname), "npc_enemy" ) ) != NULL ) {
		G_Printf( "enemy %i state=%s health=%i origin=(%.0f %.0f %.0f) moved=%.0f target=%i\n",
			enemy->s.number, Enemy_StateName( enemy->count ), enemy->health,
			enemy->r.currentOrigin[0], enemy->r.currentOrigin[1], enemy->r.currentOrigin[2],
			sqrt( DistanceSquared( enemy->r.currentOrigin, enemy->pos1 ) ),
			enemy->enemy ? enemy->enemy->s.number : -1 );
		total++;
	}
	G_Printf( "%i combat enemies\n", total );
}
