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
// cg_effects.c -- these functions generate localentities, usually as a result
// of event processing

#include "cg_local.h"


/*
==================
CG_BubbleTrail

Bullets shot underwater
==================
*/
void CG_BubbleTrail( vec3_t start, vec3_t end, float spacing ) {
	vec3_t		move;
	vec3_t		vec;
	float		len;
	int			i;

	if ( cg_noProjectileTrail.integer ) {
		return;
	}

	VectorCopy (start, move);
	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);

	// advance a random amount first
	i = rand() % (int)spacing;
	VectorMA( move, i, vec, move );

	VectorScale (vec, spacing, vec);

	for ( ; i < len; i += spacing ) {
		localEntity_t	*le;
		refEntity_t		*re;

		le = CG_AllocLocalEntity();
		le->leFlags = LEF_PUFF_DONT_SCALE;
		le->leType = LE_MOVE_SCALE_FADE;
		le->startTime = cg.time;
		le->endTime = cg.time + 1000 + random() * 250;
		le->lifeRate = 1.0 / ( le->endTime - le->startTime );

		re = &le->refEntity;
		re->shaderTime = cg.time / 1000.0f;

		re->reType = RT_SPRITE;
		re->rotation = 0;
		re->radius = 3;
		re->customShader = cgs.media.waterBubbleShader;
		re->shaderRGBA[0] = 0xff;
		re->shaderRGBA[1] = 0xff;
		re->shaderRGBA[2] = 0xff;
		re->shaderRGBA[3] = 0xff;

		le->color[3] = 1.0;

		le->pos.trType = TR_LINEAR;
		le->pos.trTime = cg.time;
		VectorCopy( move, le->pos.trBase );
		le->pos.trDelta[0] = crandom()*5;
		le->pos.trDelta[1] = crandom()*5;
		le->pos.trDelta[2] = crandom()*5 + 6;

		VectorAdd (move, vec, move);
	}
}

/*
=====================
CG_SmokePuff

Adds a smoke puff or blood trail localEntity.
=====================
*/
localEntity_t *CG_SmokePuff( const vec3_t p, const vec3_t vel, 
				   float radius,
				   float r, float g, float b, float a,
				   float duration,
				   int startTime,
				   int fadeInTime,
				   int leFlags,
				   qhandle_t hShader ) {
	static int	seed = 0x92;
	localEntity_t	*le;
	refEntity_t		*re;
//	int fadeInTime = startTime + duration / 2;

	le = CG_AllocLocalEntity();
	le->leFlags = leFlags;
	le->radius = radius;

	re = &le->refEntity;
	re->rotation = Q_random( &seed ) * 360;
	re->radius = radius;
	re->shaderTime = startTime / 1000.0f;

	le->leType = LE_MOVE_SCALE_FADE;
	le->startTime = startTime;
	le->fadeInTime = fadeInTime;
	le->endTime = startTime + duration;
	if ( fadeInTime > startTime ) {
		le->lifeRate = 1.0 / ( le->endTime - le->fadeInTime );
	}
	else {
		le->lifeRate = 1.0 / ( le->endTime - le->startTime );
	}
	le->color[0] = r;
	le->color[1] = g; 
	le->color[2] = b;
	le->color[3] = a;


	le->pos.trType = TR_LINEAR;
	le->pos.trTime = startTime;
	VectorCopy( vel, le->pos.trDelta );
	VectorCopy( p, le->pos.trBase );

	VectorCopy( p, re->origin );
	re->customShader = hShader;

	// rage pro can't alpha fade, so use a different shader
	if ( cgs.glconfig.hardwareType == GLHW_RAGEPRO ) {
		re->customShader = cgs.media.smokePuffRageProShader;
		re->shaderRGBA[0] = 0xff;
		re->shaderRGBA[1] = 0xff;
		re->shaderRGBA[2] = 0xff;
		re->shaderRGBA[3] = 0xff;
	} else {
		re->shaderRGBA[0] = le->color[0] * 0xff;
		re->shaderRGBA[1] = le->color[1] * 0xff;
		re->shaderRGBA[2] = le->color[2] * 0xff;
		re->shaderRGBA[3] = 0xff;
	}

	re->reType = RT_SPRITE;
	re->radius = le->radius;

	return le;
}

/*
==================
CG_SpawnEffect

Player teleporting in or out
==================
*/
void CG_SpawnEffect( vec3_t org ) {
	localEntity_t	*le;
	refEntity_t		*re;

	le = CG_AllocLocalEntity();
	le->leFlags = 0;
	le->leType = LE_FADE_RGB;
	le->startTime = cg.time;
	le->endTime = cg.time + 500;
	le->lifeRate = 1.0 / ( le->endTime - le->startTime );

	le->color[0] = le->color[1] = le->color[2] = le->color[3] = 1.0;

	re = &le->refEntity;

	re->reType = RT_MODEL;
	re->shaderTime = cg.time / 1000.0f;

#ifndef MISSIONPACK
	re->customShader = cgs.media.teleportEffectShader;
#endif
	re->hModel = cgs.media.teleportEffectModel;
	AxisClear( re->axis );

	VectorCopy( org, re->origin );
#ifdef MISSIONPACK
	re->origin[2] += 16;
#else
	re->origin[2] -= 24;
#endif
}


#ifdef MISSIONPACK
/*
===============
CG_LightningBoltBeam
===============
*/
void CG_LightningBoltBeam( vec3_t start, vec3_t end ) {
	localEntity_t	*le;
	refEntity_t		*beam;

	le = CG_AllocLocalEntity();
	le->leFlags = 0;
	le->leType = LE_SHOWREFENTITY;
	le->startTime = cg.time;
	le->endTime = cg.time + 50;

	beam = &le->refEntity;

	VectorCopy( start, beam->origin );
	// this is the end point
	VectorCopy( end, beam->oldorigin );

	beam->reType = RT_LIGHTNING;
	beam->customShader = cgs.media.lightningShader;
}

/*
==================
CG_KamikazeEffect
==================
*/
void CG_KamikazeEffect( vec3_t org ) {
	localEntity_t	*le;
	refEntity_t		*re;

	le = CG_AllocLocalEntity();
	le->leFlags = 0;
	le->leType = LE_KAMIKAZE;
	le->startTime = cg.time;
	le->endTime = cg.time + 3000;//2250;
	le->lifeRate = 1.0 / ( le->endTime - le->startTime );

	le->color[0] = le->color[1] = le->color[2] = le->color[3] = 1.0;

	VectorClear(le->angles.trBase);

	re = &le->refEntity;

	re->reType = RT_MODEL;
	re->shaderTime = cg.time / 1000.0f;

	re->hModel = cgs.media.kamikazeEffectModel;

	VectorCopy( org, re->origin );

}

/*
==================
CG_ObeliskExplode
==================
*/
void CG_ObeliskExplode( vec3_t org, int entityNum ) {
	localEntity_t	*le;
	vec3_t origin;

	// create an explosion
	VectorCopy( org, origin );
	origin[2] += 64;
	le = CG_MakeExplosion( origin, vec3_origin,
						   cgs.media.dishFlashModel,
						   cgs.media.rocketExplosionShader,
						   600, qtrue );
	le->light = 300;
	le->lightColor[0] = 1;
	le->lightColor[1] = 0.75;
	le->lightColor[2] = 0.0;
}

/*
==================
CG_ObeliskPain
==================
*/
void CG_ObeliskPain( vec3_t org ) {
	float r;
	sfxHandle_t sfx;

	// hit sound
	r = rand() & 3;
	if ( r < 2 ) {
		sfx = cgs.media.obeliskHitSound1;
	} else if ( r == 2 ) {
		sfx = cgs.media.obeliskHitSound2;
	} else {
		sfx = cgs.media.obeliskHitSound3;
	}
	trap_S_StartSound ( org, ENTITYNUM_NONE, CHAN_BODY, sfx );
}


/*
==================
CG_InvulnerabilityImpact
==================
*/
void CG_InvulnerabilityImpact( vec3_t org, vec3_t angles ) {
	localEntity_t	*le;
	refEntity_t		*re;
	int				r;
	sfxHandle_t		sfx;

	le = CG_AllocLocalEntity();
	le->leFlags = 0;
	le->leType = LE_INVULIMPACT;
	le->startTime = cg.time;
	le->endTime = cg.time + 1000;
	le->lifeRate = 1.0 / ( le->endTime - le->startTime );

	le->color[0] = le->color[1] = le->color[2] = le->color[3] = 1.0;

	re = &le->refEntity;

	re->reType = RT_MODEL;
	re->shaderTime = cg.time / 1000.0f;

	re->hModel = cgs.media.invulnerabilityImpactModel;

	VectorCopy( org, re->origin );
	AnglesToAxis( angles, re->axis );

	r = rand() & 3;
	if ( r < 2 ) {
		sfx = cgs.media.invulnerabilityImpactSound1;
	} else if ( r == 2 ) {
		sfx = cgs.media.invulnerabilityImpactSound2;
	} else {
		sfx = cgs.media.invulnerabilityImpactSound3;
	}
	trap_S_StartSound (org, ENTITYNUM_NONE, CHAN_BODY, sfx );
}

/*
==================
CG_InvulnerabilityJuiced
==================
*/
void CG_InvulnerabilityJuiced( vec3_t org ) {
	localEntity_t	*le;
	refEntity_t		*re;
	vec3_t			angles;

	le = CG_AllocLocalEntity();
	le->leFlags = 0;
	le->leType = LE_INVULJUICED;
	le->startTime = cg.time;
	le->endTime = cg.time + 10000;
	le->lifeRate = 1.0 / ( le->endTime - le->startTime );

	le->color[0] = le->color[1] = le->color[2] = le->color[3] = 1.0;

	re = &le->refEntity;

	re->reType = RT_MODEL;
	re->shaderTime = cg.time / 1000.0f;

	re->hModel = cgs.media.invulnerabilityJuicedModel;

	VectorCopy( org, re->origin );
	VectorClear(angles);
	AnglesToAxis( angles, re->axis );

	trap_S_StartSound (org, ENTITYNUM_NONE, CHAN_BODY, cgs.media.invulnerabilityJuicedSound );
}

#endif

/*
==================
CG_ScorePlum
==================
*/
void CG_ScorePlum( int client, vec3_t org, int score ) {
	localEntity_t	*le;
	refEntity_t		*re;
	vec3_t			angles;
	static vec3_t lastPos;

	// only visualize for the client that scored
	if (client != cg.predictedPlayerState.clientNum || cg_scorePlum.integer == 0) {
		return;
	}

	le = CG_AllocLocalEntity();
	le->leFlags = 0;
	le->leType = LE_SCOREPLUM;
	le->startTime = cg.time;
	le->endTime = cg.time + 4000;
	le->lifeRate = 1.0 / ( le->endTime - le->startTime );

	
	le->color[0] = le->color[1] = le->color[2] = le->color[3] = 1.0;
	le->radius = score;
	
	VectorCopy( org, le->pos.trBase );
	if (org[2] >= lastPos[2] - 20 && org[2] <= lastPos[2] + 20) {
		le->pos.trBase[2] -= 20;
	}

	//CG_Printf( "Plum origin %i %i %i -- %i\n", (int)org[0], (int)org[1], (int)org[2], (int)Distance(org, lastPos));
	VectorCopy(org, lastPos);


	re = &le->refEntity;

	re->reType = RT_SPRITE;
	re->radius = 16;

	VectorClear(angles);
	AnglesToAxis( angles, re->axis );
}


/*
====================
CG_MakeExplosion
====================
*/
localEntity_t *CG_MakeExplosion( vec3_t origin, vec3_t dir, 
								qhandle_t hModel, qhandle_t shader,
								int msec, qboolean isSprite ) {
	float			ang;
	localEntity_t	*ex;
	int				offset;
	vec3_t			tmpVec, newOrigin;

	if ( msec <= 0 ) {
		CG_Error( "CG_MakeExplosion: msec = %i", msec );
	}

	// skew the time a bit so they aren't all in sync
	offset = rand() & 63;

	ex = CG_AllocLocalEntity();
	if ( isSprite ) {
		ex->leType = LE_SPRITE_EXPLOSION;

		// randomly rotate sprite orientation
		ex->refEntity.rotation = rand() % 360;
		VectorScale( dir, 16, tmpVec );
		VectorAdd( tmpVec, origin, newOrigin );
	} else {
		ex->leType = LE_EXPLOSION;
		VectorCopy( origin, newOrigin );

		// set axis with random rotate
		if ( !dir ) {
			AxisClear( ex->refEntity.axis );
		} else {
			ang = rand() % 360;
			VectorCopy( dir, ex->refEntity.axis[0] );
			RotateAroundDirection( ex->refEntity.axis, ang );
		}
	}

	ex->startTime = cg.time - offset;
	ex->endTime = ex->startTime + msec;

	// bias the time so all shader effects start correctly
	ex->refEntity.shaderTime = ex->startTime / 1000.0f;

	ex->refEntity.hModel = hModel;
	ex->refEntity.customShader = shader;

	// set origin
	VectorCopy( newOrigin, ex->refEntity.origin );
	VectorCopy( newOrigin, ex->refEntity.oldorigin );

	ex->color[0] = ex->color[1] = ex->color[2] = 1.0;

	return ex;
}


/*
=================
CG_LaunchBloodDrop

A single droplet that arcs under gravity and fades. Drops read as wet and
physical where the old sprite read as a red star.
=================
*/
static void CG_LaunchBloodDrop( const vec3_t origin, const vec3_t velocity, float radius, int duration, int renderfx ) {
	localEntity_t	*le;

	le = CG_SmokePuff( origin, velocity, radius, 1, 1, 1, 1, duration, cg.time, 0,
		LEF_PUFF_DONT_SCALE, cgs.media.bloodTrailShader );
	le->pos.trType = TR_GRAVITY;
	le->refEntity.renderfx |= renderfx;
}

/*
=================
CG_Bleed

This is the spurt of blood when a character gets hit: a small puff of mist
and a few droplets, with the odd drip landing on the floor below.
=================
*/
void CG_Bleed( vec3_t origin, int entityNum ) {
	localEntity_t	*mist;
	vec3_t			velocity;
	vec3_t			end;
	trace_t			trace;
	int				renderfx;
	int				i;

	if ( !cg_blood.integer ) {
		return;
	}

	// don't show player's own blood in view
	renderfx = ( entityNum == cg.snap->ps.clientNum ) ? RF_THIRD_PERSON : 0;

	VectorSet( velocity, crandom() * 12, crandom() * 12, 10 );
	mist = CG_SmokePuff( origin, velocity, 18 + random() * 6, 1, 1, 1, 0.85f,
		380 + random() * 120, cg.time, 0, 0, cgs.media.bloodExplosionShader );
	mist->refEntity.renderfx |= renderfx;

	for ( i = 0; i < 3; i++ ) {
		VectorSet( velocity, crandom() * 110, crandom() * 110, 60 + random() * 120 );
		CG_LaunchBloodDrop( origin, velocity, 1.6f + random() * 1.2f, 450 + random() * 250, renderfx );
	}

	// an occasional small spatter on the floor below sells the hit without carpeting the map
	if ( ( rand() & 3 ) == 0 ) {
		VectorCopy( origin, end );
		end[2] -= 96;
		CG_Trace( &trace, origin, NULL, NULL, end, entityNum, MASK_SOLID );
		if ( !trace.startsolid && trace.fraction < 1.0f ) {
			CG_ImpactMark( cgs.media.bloodMarkShader, trace.endpos, trace.plane.normal,
				random() * 360, 1, 1, 1, 1, qtrue, 6 + random() * 5, qfalse );
		}
	}
}


/*
=================
CG_RocketExplosion

Layered rocket blast: a burst of fireball billows that swell and burn out
fast, darker smoke that rises and lingers after them, and embers that arc
out under gravity. Close blasts also shake the view a little.
=================
*/
#define	EXPLOSION_SHAKE_RANGE	640.0f

void CG_RocketExplosion( const vec3_t origin, const vec3_t dir ) {
	localEntity_t	*le;
	vec3_t			center, org, velocity, delta;
	float			dist, shake;
	int				i;

	// lift the effect off the wall so the billows are not cut in half by it
	VectorMA( origin, 14, dir, center );

	for ( i = 0; i < 5; i++ ) {
		VectorSet( org, center[0] + crandom() * 10, center[1] + crandom() * 10, center[2] + crandom() * 10 );
		VectorScale( dir, 70, velocity );
		velocity[0] += crandom() * 80;
		velocity[1] += crandom() * 80;
		velocity[2] += crandom() * 80 + 30;
		CG_SmokePuff( org, velocity, 54 + random() * 22, 1, 0.9f + random() * 0.1f, 0.85f, 1,
			340 + random() * 160, cg.time, 0, 0, cgs.media.rocketExplosionShader );
	}

	for ( i = 0; i < 4; i++ ) {
		VectorSet( org, center[0] + crandom() * 16, center[1] + crandom() * 16, center[2] + crandom() * 8 );
		VectorScale( dir, 30, velocity );
		velocity[0] += crandom() * 30;
		velocity[1] += crandom() * 30;
		velocity[2] += 40 + random() * 30;
		CG_SmokePuff( org, velocity, 54 + random() * 24, 0.3f, 0.29f, 0.28f, 0.7f,
			1400 + random() * 700, cg.time, 0, 0, cgs.media.smokePuffShader );
	}

	for ( i = 0; i < 12; i++ ) {
		VectorScale( dir, 200, velocity );
		velocity[0] += crandom() * 300;
		velocity[1] += crandom() * 300;
		velocity[2] += crandom() * 200 + 160;
		le = CG_SmokePuff( center, velocity, 2.5f + random() * 2, 1, 0.75f + random() * 0.25f, 0.4f, 1,
			450 + random() * 550, cg.time, 0, LEF_PUFF_DONT_SCALE, cgs.media.explosionEmberShader );
		le->pos.trType = TR_GRAVITY;
	}

	VectorSubtract( origin, cg.refdef.vieworg, delta );
	dist = VectorLength( delta );
	if ( dist < EXPLOSION_SHAKE_RANGE ) {
		shake = 1.0f - dist / EXPLOSION_SHAKE_RANGE;
		// a second blast mid-shake only takes over if it is the bigger one
		if ( cg.explosionShakeTime && cg.time - cg.explosionShakeTime < EXPLOSION_SHAKE_TIME ) {
			float remaining = cg.explosionShakeScale *
				( 1.0f - ( cg.time - cg.explosionShakeTime ) / (float)EXPLOSION_SHAKE_TIME );
			if ( remaining > shake ) {
				return;
			}
		}
		cg.explosionShakeTime = cg.time;
		cg.explosionShakeScale = shake;
	}
}


/*
=================
CG_DeathBloodPool

Projects a persistent pool onto the walkable world surface below a corpse.
=================
*/
void CG_DeathBloodPool( const vec3_t playerOrigin ) {
	trace_t	trace;
	vec3_t	start;
	vec3_t	end;

	if ( !cg_blood.integer || !cgs.media.bloodPoolShader ) {
		return;
	}

	VectorCopy( playerOrigin, start );
	start[2] += 24;
	VectorCopy( playerOrigin, end );
	end[2] -= 80;
	CG_Trace( &trace, start, NULL, NULL, end, -1, MASK_SOLID );

	if ( trace.startsolid || trace.fraction == 1.0f || trace.plane.normal[2] < 0.7f ) {
		return;
	}

	CG_ImpactMark( cgs.media.bloodPoolShader, trace.endpos, trace.plane.normal,
		random() * 360.0f, 1, 1, 1, 0.92f, qtrue,
		42.0f + random() * 8.0f, qfalse );
}



/*
==================
CG_LaunchGib
==================
*/
void CG_LaunchGib( vec3_t origin, vec3_t velocity, qhandle_t hModel ) {
	localEntity_t	*le;
	refEntity_t		*re;

	le = CG_AllocLocalEntity();
	re = &le->refEntity;

	le->leType = LE_FRAGMENT;
	le->startTime = cg.time;
	le->endTime = le->startTime + 5000 + random() * 3000;

	VectorCopy( origin, re->origin );
	AxisCopy( axisDefault, re->axis );
	re->hModel = hModel;

	le->pos.trType = TR_GRAVITY;
	VectorCopy( origin, le->pos.trBase );
	VectorCopy( velocity, le->pos.trDelta );
	le->pos.trTime = cg.time;

	le->bounceFactor = 0.6f;

	le->angles.trType = TR_LINEAR;
	le->angles.trTime = cg.time;
	le->angles.trBase[0] = rand()&31;
	le->angles.trBase[1] = rand()&31;
	le->angles.trBase[2] = rand()&31;
	le->angles.trDelta[0] = 180 + random() * 220;
	le->angles.trDelta[1] = 180 + random() * 220;
	le->angles.trDelta[2] = 180 + random() * 220;

	le->leFlags = LEF_TUMBLE;
	le->leBounceSoundType = LEBS_BLOOD;
	le->leMarkType = LEMT_BLOOD;
}

#define	GIB_VELOCITY	250
#define	GIB_JUMP		250

static void CG_LaunchGibPiece( vec3_t playerOrigin, qhandle_t hModel, const vec3_t offset, const vec3_t direction ) {
	vec3_t	origin, velocity;

	VectorAdd( playerOrigin, offset, origin );
	velocity[0] = direction[0] * GIB_VELOCITY + crandom() * 160;
	velocity[1] = direction[1] * GIB_VELOCITY + crandom() * 160;
	velocity[2] = GIB_JUMP + direction[2] * 120 + random() * 120;

	CG_LaunchGib( origin, velocity, hModel );
}

/*
===================
CG_GibPlayer

Generated six gibs launching out from the body's location. cg_gibs scales
the whole burst: 0 turns gibs off, 1 is the normal six pieces, and each step
above that adds another full set of pieces, mist and droplets.
===================
*/
#define	GIB_MAX_SCALE	4

void CG_GibPlayer( vec3_t playerOrigin ) {
	static const vec3_t pieceOffsets[6] = {
		{ 0, 0, 42 },
		{ 0, 0, 28 },
		{ 0, 0, 14 },
		{ 0, 12, 24 },
		{ 0, -12, 24 },
		{ 0, 0, 4 }
	};
	static const vec3_t pieceDirections[6] = {
		{ 0.00f, 0.00f, 1.00f },
		{ 0.92f, 0.22f, 0.35f },
		{ -0.85f, -0.30f, 0.25f },
		{ 0.32f, 0.95f, 0.15f },
		{ -0.32f, -0.95f, 0.15f },
		{ 0.15f, -0.35f, 0.05f }
	};
	qhandle_t	gibModels[6];
	vec3_t		origin, velocity;
	int			i;
	int			scale;
	int			pieces = 0;
	int			loadedModels = 0;

	if ( !cg_blood.integer ) {
		return;
	}

	// allow gibs to be turned off for speed; the cap keeps a big setting from
	// draining the shared local entity pool in a busy match
	scale = cg_gibs.integer;
	if ( scale <= 0 ) {
		return;
	}
	if ( scale > GIB_MAX_SCALE ) {
		scale = GIB_MAX_SCALE;
	}

	gibModels[0] = cgs.media.gibSkull;
	gibModels[1] = cgs.media.gibChest;
	gibModels[2] = cgs.media.gibAbdomen;
	gibModels[3] = cgs.media.gibArm;
	gibModels[4] = cgs.media.gibForearm;
	gibModels[5] = cgs.media.gibLeg;

	// a quick wet "pop" where the body was: one mist cloud and a ring of droplets
	for ( i = 0; i < 3 * scale; i++ ) {
		VectorSet( origin, playerOrigin[0] + crandom() * 8, playerOrigin[1] + crandom() * 8, playerOrigin[2] + 16 + ( i % 3 ) * 10 );
		VectorSet( velocity, crandom() * 30, crandom() * 30, 20 + random() * 20 );
		CG_SmokePuff( origin, velocity, 30 + random() * 10, 1, 1, 1, 0.8f,
			550 + random() * 200, cg.time, 0, 0, cgs.media.bloodExplosionShader );
	}
	for ( i = 0; i < 14 * scale; i++ ) {
		VectorSet( origin, playerOrigin[0], playerOrigin[1], playerOrigin[2] + 24 );
		VectorSet( velocity, crandom() * 240, crandom() * 240, 120 + random() * 200 );
		CG_LaunchBloodDrop( origin, velocity, 2.2f + random() * 1.8f, 600 + random() * 400, 0 );
	}

	for ( i = 0; i < 6; i++ ) {
		if ( gibModels[i] ) {
			loadedModels++;
		}
	}
	// extra sets reuse the same launch directions; the random spread in
	// CG_LaunchGibPiece keeps the copies from flying off as a stacked pair
	for ( ; scale > 0; scale-- ) {
		for ( i = 0; i < 6; i++ ) {
			CG_LaunchGibPiece( playerOrigin, gibModels[i], pieceOffsets[i], pieceDirections[i] );
			pieces++;
		}
	}
	if ( cg_debugEvents.integer ) {
		CG_Printf( "Gib effect: pieces=%i models=%i\n", pieces, loadedModels );
	}
}

/*
==================
CG_LaunchExplode
==================
*/
void CG_LaunchExplode( vec3_t origin, vec3_t velocity, qhandle_t hModel ) {
	localEntity_t	*le;
	refEntity_t		*re;

	le = CG_AllocLocalEntity();
	re = &le->refEntity;

	le->leType = LE_FRAGMENT;
	le->startTime = cg.time;
	le->endTime = le->startTime + 10000 + random() * 6000;

	VectorCopy( origin, re->origin );
	AxisCopy( axisDefault, re->axis );
	re->hModel = hModel;

	le->pos.trType = TR_GRAVITY;
	VectorCopy( origin, le->pos.trBase );
	VectorCopy( velocity, le->pos.trDelta );
	le->pos.trTime = cg.time;

	le->bounceFactor = 0.1f;

	le->leBounceSoundType = LEBS_BRASS;
	le->leMarkType = LEMT_NONE;
}

#define	EXP_VELOCITY	100
#define	EXP_JUMP		150
/*
===================
CG_BigExplode

Generated a bunch of gibs launching out from the bodies location
===================
*/
void CG_BigExplode( vec3_t playerOrigin ) {
	vec3_t	origin, velocity;

	if ( !cg_blood.integer ) {
		return;
	}

	VectorCopy( playerOrigin, origin );
	velocity[0] = crandom()*EXP_VELOCITY;
	velocity[1] = crandom()*EXP_VELOCITY;
	velocity[2] = EXP_JUMP + crandom()*EXP_VELOCITY;
	CG_LaunchExplode( origin, velocity, cgs.media.smoke2 );

	VectorCopy( playerOrigin, origin );
	velocity[0] = crandom()*EXP_VELOCITY;
	velocity[1] = crandom()*EXP_VELOCITY;
	velocity[2] = EXP_JUMP + crandom()*EXP_VELOCITY;
	CG_LaunchExplode( origin, velocity, cgs.media.smoke2 );

	VectorCopy( playerOrigin, origin );
	velocity[0] = crandom()*EXP_VELOCITY*1.5;
	velocity[1] = crandom()*EXP_VELOCITY*1.5;
	velocity[2] = EXP_JUMP + crandom()*EXP_VELOCITY;
	CG_LaunchExplode( origin, velocity, cgs.media.smoke2 );

	VectorCopy( playerOrigin, origin );
	velocity[0] = crandom()*EXP_VELOCITY*2.0;
	velocity[1] = crandom()*EXP_VELOCITY*2.0;
	velocity[2] = EXP_JUMP + crandom()*EXP_VELOCITY;
	CG_LaunchExplode( origin, velocity, cgs.media.smoke2 );

	VectorCopy( playerOrigin, origin );
	velocity[0] = crandom()*EXP_VELOCITY*2.5;
	velocity[1] = crandom()*EXP_VELOCITY*2.5;
	velocity[2] = EXP_JUMP + crandom()*EXP_VELOCITY;
	CG_LaunchExplode( origin, velocity, cgs.media.smoke2 );
}

