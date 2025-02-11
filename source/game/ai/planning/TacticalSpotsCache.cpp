#include "../bot.h"
#include "../combat/TacticalSpotsRegistry.h"
#include "../combat/AdvantageProblemSolver.h"
#include "../combat/CoverProblemSolver.h"

inline const AiAasRouteCache *BotTacticalSpotsCache::RouteCache() {
	return bot->RouteCache();
}

inline float BotTacticalSpotsCache::Skill() const {
	return bot->Skill();
}

inline bool BotTacticalSpotsCache::BotHasAlmostSameOrigin( const Vec3 &unpackedOrigin ) const {
	constexpr float squareDistanceError = OriginVar::MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
	return unpackedOrigin.SquareDistanceTo( bot->Origin() ) <= squareDistanceError;
}

template <typename ProblemParams>
inline bool BotTacticalSpotsCache::FindForOrigin( const ProblemParams &problemParams,
												  const Vec3 &origin, float searchRadius, vec3_t result ) {
	if( BotHasAlmostSameOrigin( origin ) ) {
		// Provide a bot entity to aid trace checks
		AdvantageProblemSolver::OriginParams originParams( game.edicts + bot->EntNum(), searchRadius, RouteCache() );
		return AdvantageProblemSolver( originParams, problemParams ).FindSingle( result );
	}
	TacticalSpotsRegistry::OriginParams originParams( origin.Data(), searchRadius, RouteCache() );
	return AdvantageProblemSolver( originParams, problemParams ).FindSingle( result );
}

const short *BotTacticalSpotsCache::GetSingleOriginSpot( SingleOriginSpotsCache *cache, const short *origin,
														 const short *enemyOrigin, SingleOriginFindMethod findMethod ) {
	short *cachedSpot;
	if( cache->TryGetCachedSpot( origin, enemyOrigin, &cachedSpot ) ) {
		return cachedSpot;
	}

	CachedSpot<short[3]> *newSpot = cache->Alloc();
	// Can't allocate a spot. It also means a limit of such tactical spots per think frame has been exceeded.
	if( !newSpot ) {
		return nullptr;
	}

	VectorCopy( origin, newSpot->origin );
	VectorCopy( enemyOrigin, newSpot->enemyOrigin );

	vec3_t foundSpotOrigin;
	if( !( this->*findMethod )( GetUnpacked4uVec( origin ), GetUnpacked4uVec( enemyOrigin ), foundSpotOrigin ) ) {
		newSpot->succeeded = false;
		return nullptr;
	}

	for( unsigned i = 0; i < 3; ++i )
		newSpot->spotData[i] = (short)( ( (int)foundSpotOrigin[i] ) / 4 );

	newSpot->succeeded = true;
	return newSpot->spotData;
}

const short *BotTacticalSpotsCache::GetDualOriginSpot( DualOriginSpotsCache *cache, const short *origin,
													   const short *enemyOrigin, DualOriginFindMethod findMethod ) {
	short *cachedSpot;
	if( cache->TryGetCachedSpot( origin, enemyOrigin, &cachedSpot ) ) {
		return cachedSpot;
	}

	CachedSpot<short[6]> *newSpot = cache->Alloc();
	// Can't allocate a spot. It also means a limit of such tactical spots per think frame has been exceeded.
	if( !newSpot ) {
		return nullptr;
	}

	VectorCopy( origin, newSpot->origin );
	VectorCopy( enemyOrigin, newSpot->enemyOrigin );

	vec3_t foundOrigins[2];
	if( !( this->*findMethod )( GetUnpacked4uVec( origin ), GetUnpacked4uVec( enemyOrigin ), foundOrigins ) ) {
		newSpot->succeeded = false;
		return nullptr;
	}

	for( unsigned i = 0; i < 3; ++i ) {
		newSpot->spotData[i + 0] = (short)( ( ( foundOrigins[0][i] ) / 4 ) );
		newSpot->spotData[i + 3] = (short)( ( ( foundOrigins[1][i] ) / 4 ) );
	}

	newSpot->succeeded = true;
	return newSpot->spotData;
}

template <typename ProblemParams>
inline void BotTacticalSpotsCache::TakeEnemiesIntoAccount( ProblemParams &problemParams ) {
	if( Skill() < 0.33f ) {
		return;
	}
	const auto &selectedEnemies = bot->GetSelectedEnemies();
	if( !selectedEnemies.AreValid() ) {
		return;
	}
	// TODO: Provide PrimaryEnemy() getter?
	assert( selectedEnemies.IsPrimaryEnemy( *selectedEnemies.begin() ) );
	problemParams.TakeEnemiesIntoAccount( bot->TrackedEnemiesHead(), *selectedEnemies.begin() );
}

bool BotTacticalSpotsCache::FindSniperRangeTacticalSpot( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result ) {
	AdvantageProblemSolver::ProblemParams problemParams( enemyOrigin.Data() );
	problemParams.SetMinSpotDistanceToEntity( WorldState::FAR_RANGE_MAX );
	problemParams.SetOriginDistanceInfluence( 0.0f );
	problemParams.SetTravelTimeInfluence( 0.8f );
	problemParams.SetMinHeightAdvantageOverOrigin( -1024.0f );
	problemParams.SetMinHeightAdvantageOverEntity( -1024.0f );
	problemParams.SetHeightOverOriginInfluence( 0.3f );
	problemParams.SetHeightOverEntityInfluence( 0.1f );
	problemParams.SetCheckToAndBackReach( false );
	problemParams.SetMaxFeasibleTravelTimeMillis( 12500 );
	problemParams.OptimizeAggressively( true );
	TakeEnemiesIntoAccount( problemParams );

	float searchRadius = 192.0f + 768.0f * Skill();
	float distanceToEnemy = ( origin - enemyOrigin ).LengthFast();
	// If bot is not on sniper range, increase search radius (otherwise a point in a sniper range can't be found).
	if( distanceToEnemy - searchRadius < WorldState::FAR_RANGE_MAX ) {
		searchRadius += WorldState::FAR_RANGE_MAX - distanceToEnemy + searchRadius;
	}

	return FindForOrigin( problemParams, origin, searchRadius, result );
}

bool BotTacticalSpotsCache::FindFarRangeTacticalSpot( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result ) {
	AdvantageProblemSolver::ProblemParams problemParams( enemyOrigin.Data() );
	problemParams.SetMinSpotDistanceToEntity( WorldState::MIDDLE_RANGE_MAX );
	problemParams.SetMaxSpotDistanceToEntity( WorldState::FAR_RANGE_MAX );
	problemParams.SetOriginDistanceInfluence( 0.0f );
	problemParams.SetEntityDistanceInfluence( 0.3f );
	problemParams.SetEntityWeightFalloffDistanceRatio( 0.25f );
	problemParams.SetTravelTimeInfluence( 0.8f );
	problemParams.SetMinHeightAdvantageOverOrigin( -192.0f );
	problemParams.SetMinHeightAdvantageOverEntity( -192.0f );
	problemParams.SetHeightOverOriginInfluence( 0.3f );
	problemParams.SetHeightOverEntityInfluence( 0.7f );
	problemParams.SetCheckToAndBackReach( false );
	problemParams.SetMaxFeasibleTravelTimeMillis( 7500 );
	problemParams.OptimizeAggressively( true );
	TakeEnemiesIntoAccount( problemParams );

	float searchRadius = 192.0f + 768.0f * Skill();
	float distanceToEnemy = ( origin - enemyOrigin ).LengthFast();
	float minSearchDistanceToEnemy = distanceToEnemy - searchRadius;
	float maxSearchDistanceToEnemy = distanceToEnemy + searchRadius;
	if( minSearchDistanceToEnemy < WorldState::MIDDLE_RANGE_MAX ) {
		searchRadius += WorldState::MIDDLE_RANGE_MAX - minSearchDistanceToEnemy;
	} else if( maxSearchDistanceToEnemy > WorldState::FAR_RANGE_MAX ) {
		searchRadius += maxSearchDistanceToEnemy - WorldState::FAR_RANGE_MAX;
	}

	return FindForOrigin( problemParams, origin, searchRadius, result );
}

bool BotTacticalSpotsCache::FindMiddleRangeTacticalSpot( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result ) {
	AdvantageProblemSolver::ProblemParams problemParams( enemyOrigin.Data() );
	problemParams.SetMinSpotDistanceToEntity( WorldState::CLOSE_RANGE_MAX );
	problemParams.SetMaxSpotDistanceToEntity( WorldState::MIDDLE_RANGE_MAX );
	problemParams.SetOriginDistanceInfluence( 0.3f );
	problemParams.SetEntityDistanceInfluence( 0.4f );
	problemParams.SetEntityWeightFalloffDistanceRatio( 0.5f );
	problemParams.SetTravelTimeInfluence( 0.7f );
	problemParams.SetMinHeightAdvantageOverOrigin( -64.0f );
	problemParams.SetMinHeightAdvantageOverEntity( +16.0f );
	problemParams.SetHeightOverOriginInfluence( 0.6f );
	problemParams.SetHeightOverEntityInfluence( 0.8f );
	problemParams.SetCheckToAndBackReach( false );
	problemParams.SetMaxFeasibleTravelTimeMillis( 4000 );
	TakeEnemiesIntoAccount( problemParams );

	float searchRadius = WorldState::MIDDLE_RANGE_MAX;
	float distanceToEnemy = ( origin - enemyOrigin ).LengthFast();
	if( distanceToEnemy < WorldState::CLOSE_RANGE_MAX ) {
		searchRadius += WorldState::CLOSE_RANGE_MAX;
	} else if( distanceToEnemy > WorldState::MIDDLE_RANGE_MAX ) {
		searchRadius += distanceToEnemy - WorldState::MIDDLE_RANGE_MAX;
	} else {
		searchRadius *= 1.0f + 0.5f * Skill();
	}

	return FindForOrigin( problemParams, origin, searchRadius, result );
}

bool BotTacticalSpotsCache::FindCloseRangeTacticalSpot( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result ) {
	AdvantageProblemSolver::ProblemParams problemParams( enemyOrigin.Data() );
	float meleeRange = GS_GetWeaponDef( WEAP_GUNBLADE )->firedef_weak.timeout;
	problemParams.SetMinSpotDistanceToEntity( meleeRange );
	problemParams.SetMaxSpotDistanceToEntity( WorldState::CLOSE_RANGE_MAX );
	problemParams.SetOriginDistanceInfluence( 0.0f );
	problemParams.SetEntityDistanceInfluence( 0.7f );
	problemParams.SetEntityWeightFalloffDistanceRatio( 0.8f );
	problemParams.SetTravelTimeInfluence( 0.0f );
	problemParams.SetMinHeightAdvantageOverOrigin( -64.0f );
	problemParams.SetMinHeightAdvantageOverEntity( +16.0f );
	problemParams.SetHeightOverOriginInfluence( 0.4f );
	problemParams.SetHeightOverEntityInfluence( 0.9f );
	// Bot should be able to retreat from close combat
	problemParams.SetCheckToAndBackReach( true );
	problemParams.SetMaxFeasibleTravelTimeMillis( 2000 );
	TakeEnemiesIntoAccount( problemParams );

	float searchRadius = WorldState::CLOSE_RANGE_MAX * 2;
	float distanceToEnemy = ( origin - enemyOrigin ).LengthFast();
	if( distanceToEnemy > WorldState::CLOSE_RANGE_MAX ) {
		searchRadius += distanceToEnemy - WorldState::CLOSE_RANGE_MAX;
		// On this range retreating to an old position makes little sense
		if( distanceToEnemy > 0.5f * WorldState::MIDDLE_RANGE_MAX ) {
			problemParams.SetCheckToAndBackReach( false );
		}
	}

	return FindForOrigin( problemParams, origin, searchRadius, result );
}

bool BotTacticalSpotsCache::FindCoverSpot( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result ) {
	const float searchRadius = 192.0f + 512.0f * Skill();
	CoverProblemSolver::ProblemParams problemParams( enemyOrigin.Data(), 32.0f );
	problemParams.SetOriginDistanceInfluence( 0.0f );
	problemParams.SetTravelTimeInfluence( 0.9f );
	problemParams.SetMinHeightAdvantageOverOrigin( -searchRadius );
	problemParams.SetHeightOverOriginInfluence( 0.3f );
	problemParams.SetCheckToAndBackReach( false );
	problemParams.SetMaxFeasibleTravelTimeMillis( 1250 );
	TakeEnemiesIntoAccount( problemParams );

	if( BotHasAlmostSameOrigin( origin ) ) {
		TacticalSpotsRegistry::OriginParams originParams( game.edicts + bot->EntNum(), searchRadius, RouteCache() );
		return CoverProblemSolver( originParams, problemParams ).FindSingle( result );
	}
	TacticalSpotsRegistry::OriginParams originParams( origin.Data(), searchRadius, RouteCache() );
	return CoverProblemSolver( originParams, problemParams ).FindSingle( result );
}

const BotTacticalSpotsCache::NearbyEntitiesCache::NearbyEntitiesCacheEntry
*BotTacticalSpotsCache::NearbyEntitiesCache::TryGetCachedEntities( const Vec3 &origin, float radius ) {
	for( unsigned i = 0; i < numEntries; ++i ) {
		NearbyEntitiesCacheEntry &entry = entries[i];
		if( !VectorCompare( origin.Data(), entry.botOrigin ) ) {
			continue;
		}
		if( radius != entry.radius ) {
			continue;
		}
		return &entry;
	}

	return nullptr;
}

int BotTacticalSpotsCache::FindNearbyEntities( const Vec3 &origin, float radius, int **entNums ) {
	if( const auto *nearbyEntitiesCacheEntry = nearbyEntitiesCache.TryGetCachedEntities( origin, radius ) ) {
		*entNums = (int *)nearbyEntitiesCacheEntry->entNums;
		return nearbyEntitiesCacheEntry->numEntities;
	}

	auto *nearbyEntitiesCacheEntry = nearbyEntitiesCache.Alloc();
	if( !nearbyEntitiesCacheEntry ) {
		return 0;
	}

	VectorCopy( origin.Data(), nearbyEntitiesCacheEntry->botOrigin );
	nearbyEntitiesCacheEntry->radius = radius;

	constexpr int maxCachedEntities = NearbyEntitiesCache::MAX_CACHED_NEARBY_ENTITIES;
	// Find more than maxCachedEntities entities in radius (most entities will usually be filtered out)
	constexpr int maxRadiusEntities = 2 * maxCachedEntities;

	int radiusEntNums[maxRadiusEntities];
	// Note that this value might be greater than maxRadiusEntities (an actual number of entities is returned)
	int numRadiusEntities = GClip_FindInRadius( const_cast<float *>( origin.Data() ), radius, radiusEntNums, maxRadiusEntities );

	int numEntities = 0;
	// Copy to locals for faster access (a compiler might be paranoid about aliasing)
	edict_t *gameEdicts = game.edicts;
	int *triggerEntNums = nearbyEntitiesCacheEntry->entNums;

	if( numRadiusEntities <= maxCachedEntities ) {
		// In this case we can avoid buffer capacity checks on each step
		for( int i = 0, end = std::min( numRadiusEntities, maxRadiusEntities ); i < end; ++i ) {
			edict_t *ent = gameEdicts + radiusEntNums[i];
			if( !ent->r.inuse ) {
				continue;
			}
			if( !ent->classname ) {
				continue;
			}

			triggerEntNums[numEntities++] = radiusEntNums[i];
		}
	} else {
		for( int i = 0, end = std::min( numRadiusEntities, maxRadiusEntities ); i < end; ++i ) {
			edict_t *ent = game.edicts + radiusEntNums[i];
			if( !ent->r.inuse ) {
				continue;
			}
			if( !ent->classname ) {
				continue;
			}

			triggerEntNums[numEntities++] = radiusEntNums[i];
			if( numEntities == maxCachedEntities ) {
				break;
			}
		}
	}

	nearbyEntitiesCacheEntry->numEntities = numEntities;
	*entNums = (int *)nearbyEntitiesCacheEntry->entNums;
	return numEntities;
}

void BotTacticalSpotsCache::FindReachableClassEntities( const Vec3 &origin, float radius, const char *classname,
														BotTacticalSpotsCache::ReachableEntities &result ) {
	int *triggerEntities;
	int numEntities = FindNearbyEntities( origin, radius, &triggerEntities );

	ReachableEntities candidateEntities;
	// Copy to locals for faster access (a compiler might be paranoid about aliasing)
	edict_t *gameEdicts = game.edicts;

	if( numEntities > (int)candidateEntities.capacity() ) {
		for( int i = 0; i < numEntities; ++i ) {
			edict_t *ent = gameEdicts + triggerEntities[i];
			// Specify expected strcmp() result explicitly to avoid misinterpreting the condition
			// (Strings are equal if an strcmp() result is zero)
			if( strcmp( ent->classname, classname ) != 0 ) {
				continue;
			}
			float distance = DistanceFast( origin.Data(), ent->s.origin );
			candidateEntities.push_back( EntAndScore( triggerEntities[i], radius - distance ) );
			if( candidateEntities.size() == candidateEntities.capacity() ) {
				break;
			}
		}
	} else {
		for( int i = 0; i < numEntities; ++i ) {
			edict_t *ent = gameEdicts + triggerEntities[i];
			if( strcmp( ent->classname, classname ) != 0 ) {
				continue;
			}
			float distance = DistanceFast( origin.Data(), ent->s.origin );
			candidateEntities.push_back( EntAndScore( triggerEntities[i], radius - distance ) );
		}
	}

	const auto *aasWorld = AiAasWorld::Instance();
	const auto *routeCache = RouteCache();

	int fromAreaNums[2] { 0, 0 };
	int numFromAreas;
	// If an origin matches actual bot origin
	if( BotHasAlmostSameOrigin( origin ) ) {
		numFromAreas = bot->EntityPhysicsState()->PrepareRoutingStartAreas( fromAreaNums );
	} else {
		fromAreaNums[0] = aasWorld->FindAreaNum( origin );
		numFromAreas = fromAreaNums[0] ? 1 : 0;
	}

	for( EntAndScore &candidate: candidateEntities ) {
		const edict_t *ent = gameEdicts + candidate.entNum;

		const int toAreaNum = FindMostFeasibleEntityAasArea( ent, aasWorld );
		if( !toAreaNum ) {
			continue;
		}

		const int travelTime = routeCache->PreferredRouteToGoalArea( fromAreaNums, numFromAreas, toAreaNum );
		if( !travelTime ) {
			continue;
		}

		// AAS travel time is in seconds^-2
		const float factor = Q_Sqrt( 1.01f - std::min( travelTime, 200 ) * Q_Rcp( 200 ) );
		result.push_back( EntAndScore( candidate.entNum, candidate.score * factor ) );
	}

	// Sort entities so best entities are first
	std::sort( result.begin(), result.end() );
}

int BotTacticalSpotsCache::FindMostFeasibleEntityAasArea( const edict_t *ent, const AiAasWorld *aasWorld ) const {
	int areaNums[24];
	const Vec3 boxMins( Vec3( -20, -20, -12 ) + ent->r.absmin );
	const Vec3 boxMaxs( Vec3( +20, +20, +12 ) + ent->r.absmax );
	int numAreas = aasWorld->BBoxAreas( boxMins.Data(), boxMaxs.Data(), areaNums, 24 );

	const auto *aasAreaSettings = aasWorld->AreaSettings();
	for( int i = 0; i < numAreas; ++i ) {
		int areaFlags = aasAreaSettings[areaNums[i]].areaflags;
		if( !( areaFlags & AREA_GROUNDED ) ) {
			continue;
		}
		if( areaFlags & AREA_DISABLED ) {
			continue;
		}
		return areaNums[i];
	}
	return 0;
}

bool BotTacticalSpotsCache::FindRunAwayTeleportOrigin( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result[2] ) {
	ReachableEntities reachableEntities;
	FindReachableClassEntities( origin, 128.0f + 384.0f * Skill(), "trigger_teleport", reachableEntities );

	trace_t trace;
	const auto *pvsCache = EntitiesPvsCache::Instance();
	edict_t *enemyEnt = const_cast<edict_t *>( bot->GetSelectedEnemies().Ent() );
	for( const auto &entAndScore: reachableEntities ) {
		edict_t *ent = game.edicts + entAndScore.entNum;
		if( !ent->target ) {
			continue;
		}
		edict_t *dest = G_Find( NULL, FOFS( targetname ), ent->target );
		if( !dest ) {
			continue;
		}

		if( !pvsCache->AreInPvs( enemyEnt, dest ) ) {
			continue;
		}

		G_Trace( &trace, enemyEnt->s.origin, nullptr, nullptr, dest->s.origin, enemyEnt, MASK_AISOLID );
		if( trace.fraction == 1.0f || trace.ent > 0 ) {
			continue;
		}

		// Copy trigger origin
		VectorCopy( ent->s.origin, result[0] );
		// Copy trigger destination
		VectorCopy( dest->s.origin, result[1] );
		return true;
	}

	return false;
}

bool BotTacticalSpotsCache::FindRunAwayJumppadOrigin( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result[2] ) {
	ReachableEntities reachableEntities;
	FindReachableClassEntities( origin, 128.0f + 384.0f * Skill(), "trigger_push", reachableEntities );

	trace_t trace;
	const auto *pvsCache = EntitiesPvsCache::Instance();
	edict_t *enemyEnt = const_cast<edict_t *>( bot->GetSelectedEnemies().Ent() );
	for( const auto &entAndScore: reachableEntities ) {
		edict_t *ent = game.edicts + entAndScore.entNum;
		if( !pvsCache->AreInPvs( enemyEnt, ent ) ) {
			continue;
		}

		G_Trace( &trace, enemyEnt->s.origin, nullptr, nullptr, ent->target_ent->s.origin, enemyEnt, MASK_AISOLID );
		if( trace.fraction == 1.0f || trace.ent > 0 ) {
			continue;
		}

		// Copy trigger origin
		VectorCopy( ent->s.origin, result[0] );
		// Copy trigger destination
		VectorCopy( ent->target_ent->s.origin, result[1] );
		return true;
	}

	return false;
}

bool BotTacticalSpotsCache::FindRunAwayElevatorOrigin( const Vec3 &origin, const Vec3 &enemyOrigin, vec3_t result[2] ) {
	ReachableEntities reachableEntities;
	FindReachableClassEntities( origin, 128.0f + 384.0f * Skill(), "func_plat", reachableEntities );

	trace_t trace;
	const auto *pvsCache = EntitiesPvsCache::Instance();
	edict_t *enemyEnt = const_cast<edict_t *>( bot->GetSelectedEnemies().Ent() );
	edict_t *gameEdicts = game.edicts;
	for( const auto &entAndScore: reachableEntities ) {
		edict_t *ent = gameEdicts + entAndScore.entNum;
		// Can't run away via elevator if the elevator has been always activated
		if( ent->moveinfo.state != STATE_BOTTOM ) {
			continue;
		}

		if( !pvsCache->AreInPvs( enemyEnt, ent ) ) {
			continue;
		}

		G_Trace( &trace, enemyEnt->s.origin, nullptr, nullptr, ent->moveinfo.end_origin, enemyEnt, MASK_AISOLID );
		if( trace.fraction == 1.0f || trace.ent > 0 ) {
			continue;
		}

		// Copy trigger origin
		VectorCopy( ent->s.origin, result[0] );
		// Drop origin to the elevator bottom
		result[0][2] = ent->r.absmin[2] + 16;
		// Copy trigger destination
		VectorCopy( ent->moveinfo.end_origin, result[1] );
		return true;
	}

	return false;
}