#include "TacticalSpotsProblemSolver.h"
#include "SpotsProblemSolversLocal.h"
#include "../navigation/AasElementsMask.h"
#include "../ai_ground_trace_cache.h"

SpotsAndScoreVector &TacticalSpotsProblemSolver::SelectCandidateSpots( const SpotsQueryVector &spotsFromQuery ) {
	const float minHeightAdvantageOverOrigin = problemParams.minHeightAdvantageOverOrigin;
	const float heightOverOriginInfluence = problemParams.heightOverOriginInfluence;
	const float searchRadius = originParams.searchRadius;
	const float originZ = originParams.origin[2];
	const auto *spots = tacticalSpotsRegistry->spots;
	// Copy to stack for faster access
	Vec3 origin( originParams.origin );

	SpotsAndScoreVector &result = tacticalSpotsRegistry->temporariesAllocator.GetNextCleanSpotsAndScoreVector();

	for( auto spotNum: spotsFromQuery ) {
		const TacticalSpot &spot = spots[spotNum];

		float heightOverOrigin = spot.absMins[2] - originZ;
		if( heightOverOrigin < minHeightAdvantageOverOrigin ) {
			continue;
		}

		float squareDistanceToOrigin = DistanceSquared( origin.Data(), spot.origin );
		if( squareDistanceToOrigin > searchRadius * searchRadius ) {
			continue;
		}

		float score = 1.0f;
		float factor = BoundedFraction( heightOverOrigin - minHeightAdvantageOverOrigin, searchRadius );
		score = ApplyFactor( score, factor, heightOverOriginInfluence );

		result.push_back( SpotAndScore( spotNum, score ) );
	}

	return result;
}

SpotsAndScoreVector &TacticalSpotsProblemSolver::FilterByReachTablesFromOrigin( SpotsAndScoreVector &spotsAndScores ) {
	// AAS uses travel time in centiseconds
	const int maxFeasibleTravelTimeCentis = problemParams.maxFeasibleTravelTimeMillis / 10;
	const int originAreaNum = originParams.originAreaNum;
	const auto *const routeCache = originParams.routeCache;
	const auto *const spots = tacticalSpotsRegistry->spots;

	unsigned numFilteredSpots = 0;
	// Filter spots in-place
	for( unsigned i = 0; i < spotsAndScores.size(); ++i ) {
		const int spotNum = spotsAndScores[i].spotNum;
		const int tableTravelTime = tacticalSpotsRegistry->TravelTimeFromAreaToSpot( originAreaNum, spotNum );
		if( !tableTravelTime || tableTravelTime > maxFeasibleTravelTimeCentis ) {
			continue;
		}
		// Cut off blocked spots early without draining the router cache by making requests
		if( routeCache->AreaDisabled( spots[spotNum].aasAreaNum ) ) {
			continue;
		}

		spotsAndScores[numFilteredSpots++] = spotsAndScores[i];
	}

	spotsAndScores.truncate( numFilteredSpots );
	return spotsAndScores;
}

SpotsAndScoreVector &TacticalSpotsProblemSolver::CheckSpotsReachFromOrigin( SpotsAndScoreVector &candidateSpots,
																			int maxResultSpots ) {
	const auto *const routeCache = originParams.routeCache;
	const float *const origin = originParams.origin;
	const auto *const spots = tacticalSpotsRegistry->spots;

	const int originAreaNum = originParams.originAreaNum;
	const float searchRadius = originParams.searchRadius;
	// AAS uses travel time in centiseconds
	const int maxFeasibleTravelTimeCentis = problemParams.maxFeasibleTravelTimeMillis / 10;
	const float weightFalloffDistanceRatio = problemParams.originWeightFalloffDistanceRatio;
	const float distanceInfluence = problemParams.originDistanceInfluence;
	const float travelTimeInfluence = problemParams.travelTimeInfluence;
	const auto travelFlags = Bot::ALLOWED_TRAVEL_FLAGS;

	SpotsAndScoreVector &result = tacticalSpotsRegistry->temporariesAllocator.GetNextCleanSpotsAndScoreVector();

	// The outer index of the table corresponds to an area to aid cache-friendly iteration in these checks
	for( const SpotAndScore &spotAndScore: candidateSpots ) {
		if( (int)result.size() >= maxResultSpots ) {
			return result;
		}

		const TacticalSpot &spot = spots[spotAndScore.spotNum];
		const int travelTime = routeCache->TravelTimeToGoalArea( originAreaNum, spot.aasAreaNum, travelFlags );
		if( !travelTime || travelTime > maxFeasibleTravelTimeCentis ) {
			continue;
		}

		const float travelTimeFactor = 1.0f - ComputeTravelTimeFactor( travelTime, maxFeasibleTravelTimeCentis );
		const float distanceFactor = ComputeDistanceFactor( spot.origin, origin, weightFalloffDistanceRatio, searchRadius );
		float newScore = spotAndScore.score;
		newScore = ApplyFactor( newScore, distanceFactor, distanceInfluence );
		newScore = ApplyFactor( newScore, travelTimeFactor, travelTimeInfluence );
		result.push_back( SpotAndScore( spotAndScore.spotNum, newScore ) );
	}

	return result;
}

SpotsAndScoreVector &TacticalSpotsProblemSolver::FilterByReachTablesFromOriginAndBack( SpotsAndScoreVector &spotsAndScores ) {
	// A round trip time can't be 2x larger
	const int maxFeasibleSumTravelTimeCentis = 2 * ( problemParams.maxFeasibleTravelTimeMillis / 10 );
	const int originAreaNum = originParams.originAreaNum;
	const auto *const routeCache = originParams.routeCache;
	const auto *const spots = tacticalSpotsRegistry->spots;

	unsigned numFilteredSpots = 0;
	// Filter spots in-place
	for( unsigned i = 0; i < spotsAndScores.size(); ++i ) {
		const auto spotNum = spotsAndScores[i].spotNum;
		const int tableToTravelTime = tacticalSpotsRegistry->TravelTimeFromAreaToSpot( originAreaNum, spotNum );
		if( !tableToTravelTime ) {
			continue;
		}
		const int tableBackTravelTime = tacticalSpotsRegistry->TravelTimeFromSpotToArea( spotNum, originAreaNum );
		if( !tableBackTravelTime ) {
			continue;
		}
		if( tableToTravelTime + tableBackTravelTime > maxFeasibleSumTravelTimeCentis ) {
			continue;
		}
		// Cut off blocked spots early without draining the router cache by making requests
		if( routeCache->AreaDisabled( spots[spotNum].aasAreaNum ) ) {
			continue;
		}
		spotsAndScores[numFilteredSpots++] = spotsAndScores[i];
	}

	spotsAndScores.truncate( numFilteredSpots );
	return spotsAndScores;
}

SpotsAndScoreVector &TacticalSpotsProblemSolver::CheckSpotsReachFromOriginAndBack( SpotsAndScoreVector &candidateSpots,
																				   int maxResultSpots ) {
	const auto *const routeCache = originParams.routeCache;
	const float *const origin = originParams.origin;
	const auto *const spots = tacticalSpotsRegistry->spots;

	const int originAreaNum = originParams.originAreaNum;
	const float searchRadius = originParams.searchRadius;
	// AAS uses time in centiseconds
	const int maxFeasibleTravelTimeCentis = problemParams.maxFeasibleTravelTimeMillis / 10;
	const float weightFalloffDistanceRatio = problemParams.originWeightFalloffDistanceRatio;
	const float distanceInfluence = problemParams.originDistanceInfluence;
	const float travelTimeInfluence = problemParams.travelTimeInfluence;
	const auto travelFlags = Bot::ALLOWED_TRAVEL_FLAGS;

	SpotsAndScoreVector &result = tacticalSpotsRegistry->temporariesAllocator.GetNextCleanSpotsAndScoreVector();

	// The outer index of the table corresponds to an area to aid cache-friendly iteration in these checks
	for( const SpotAndScore &spotAndScore : candidateSpots ) {
		if( (int)result.size() >= maxResultSpots ) {
			return result;
		}

		const auto spotNum = spotAndScore.spotNum;
		const TacticalSpot &spot = spots[spotNum];
		const int toTravelTime = routeCache->TravelTimeToGoalArea( originAreaNum, spot.aasAreaNum, travelFlags );
		// If `to` travel time is apriori greater than maximum allowed one (and thus the sum would be), reject early.
		if( !toTravelTime || toTravelTime > maxFeasibleTravelTimeCentis ) {
			continue;
		}

		const int backTravelTime = routeCache->TravelTimeToGoalArea( spot.aasAreaNum, originAreaNum, travelFlags );
		if( !backTravelTime || toTravelTime + backTravelTime > 2 * maxFeasibleTravelTimeCentis ) {
			continue;
		}

		const int totalTravelTimeCentis = toTravelTime + backTravelTime;
		const float travelTimeFactor = ComputeTravelTimeFactor( totalTravelTimeCentis, maxFeasibleTravelTimeCentis );
		const float distanceFactor = ComputeDistanceFactor( spot.origin, origin, weightFalloffDistanceRatio, searchRadius );
		float newScore = spotAndScore.score;
		newScore = ApplyFactor( newScore, distanceFactor, distanceInfluence );
		newScore = ApplyFactor( newScore, travelTimeFactor, travelTimeInfluence );
		result.push_back( SpotAndScore( spotAndScore.spotNum, newScore ) );
	}

	return result;
}

SpotsAndScoreVector &TacticalSpotsProblemSolver::ApplyEnemiesInfluence( SpotsAndScoreVector &candidateSpots ) {
	if( candidateSpots.empty() ) {
		return candidateSpots;
	}
	if( !problemParams.enemiesListHead ) {
		return candidateSpots;
	}
	if( problemParams.enemiesInfluence <= 0.0f ) {
		return candidateSpots;
	}

	// Precompute some enemy parameters that are going to be used in an inner loop.

	struct CachedEnemyData {
		const bool *areaVisRow;
		vec3_t viewOrigin;
		vec3_t lookDir;
		vec3_t velocityDir2D;
		float speed2D;
		int groundedAreaNum;
	};

	// Pick at most as many enemies as the number of AAS tmp rows we can allocate
	StaticVector<CachedEnemyData, AasElementsMask::TMP_ROW_REDUNDANCY_SCALE> cachedEnemyData;

	const auto *aasWorld = AiAasWorld::Instance();
	const int64_t levelTime = level.time;

	for( const TrackedEnemy *enemy = problemParams.enemiesListHead; enemy; enemy = enemy->NextInTrackedList() ) {
		if( levelTime - enemy->LastSeenAt() > problemParams.lastSeenEnemyMillisThreshold ) {
			continue;
		}
		// If the enemy has been invalidated but not unlinked yet (todo: is it reachable?)
		if( !enemy->IsValid() ) {
			continue;
		}
		// If it seems to be a primary enemy
		if( enemy == problemParams.ignoredEnemy ) {
			continue;
		}

		bool *const areaVisRow = AasElementsMask::TmpAreasVisRow( (int)cachedEnemyData.size() );
		CachedEnemyData *const enemyData = cachedEnemyData.unsafe_grow_back();

		Vec3 enemyOrigin( enemy->LastSeenOrigin() );
		enemyOrigin.CopyTo( enemyData->viewOrigin );
		enemyData->viewOrigin[2] += playerbox_stand_viewheight;
		enemy->LookDir().CopyTo( enemyData->lookDir );
		enemy->LastSeenVelocity().CopyTo( enemyData->velocityDir2D );
		enemyData->velocityDir2D[2] = 0;
		enemyData->speed2D = VectorLengthSquared( enemyData->velocityDir2D );
		if( enemyData->speed2D > 0.001f ) {
			enemyData->speed2D = std::sqrt( enemyData->speed2D );
			float scale = 1.0f / enemyData->speed2D;
			VectorScale( enemyData->velocityDir2D, scale, enemyData->velocityDir2D );
		}

		if( enemy->ent->ai && enemy->ent->ai->botRef ) {
			int areaNums[2] = { 0, 0 };
			enemy->ent->ai->botRef->EntityPhysicsState()->PrepareRoutingStartAreas( areaNums );
			// TODO: PrepareRoutingStartAreas() should always put grounded area first.
			// The currently saved data is a valid input for further tests but could lead to false negatives.
			enemyData->groundedAreaNum = areaNums[0];
		} else {
			vec3_t tmpOrigin;
			const float *testedOrigin = enemyOrigin.Data();
			if( AiGroundTraceCache::Instance()->TryDropToFloor( enemy->ent, 64.0f, tmpOrigin ) ) {
				testedOrigin = tmpOrigin;
			}
			enemyData->groundedAreaNum = aasWorld->FindAreaNum( testedOrigin );
		}

		enemyData->areaVisRow = aasWorld->DecompressAreaVis( enemyData->groundedAreaNum, areaVisRow );

		// Interrupt if the capacity is exceeded. This is not really correct
		// since the enemies are not sorted starting from the most dangerous one
		// but fits realistic situations well. The gameplay is a mess otherwise anyway.
		if( cachedEnemyData.size() == cachedEnemyData.capacity() ) {
			break;
		}
	}

	if( cachedEnemyData.empty() ) {
		return candidateSpots;
	}

	const auto *const spots = tacticalSpotsRegistry->spots;
	for( auto &spotAndScore: candidateSpots ) {
		const auto &__restrict spot = spots[spotAndScore.spotNum];
		const int spotFloorClusterNum = aasWorld->AreaFloorClusterNums()[spot.aasAreaNum];
		float spotVisScore = 0.0f;
		for( const CachedEnemyData &enemyData: cachedEnemyData ) {
			Vec3 toSpotDir( spot.origin );
			toSpotDir -= enemyData.viewOrigin;
			float squareDistanceToSpot = toSpotDir.SquaredLength();
			// Skip far enemies
			if( squareDistanceToSpot > 1000 * 1000 ) {
				continue;
			}
			// Skip not very close enemies that are seemingly running away from spot
			if( squareDistanceToSpot > 384 * 384 ) {
				toSpotDir *= Q_RSqrt( squareDistanceToSpot );
				if( toSpotDir.Dot( enemyData.lookDir ) < 0 ) {
					if( enemyData.speed2D >= DEFAULT_PLAYERSPEED ) {
						if( toSpotDir.Dot( enemyData.velocityDir2D ) < 0 ) {
							continue;
						}
					}
				}
			}

			// If the spot is very unlikely to be in enemy view
			// Reasons why it still may be (but we don't care):
			// 1) A spot can really occupy > 1 area
			// 2) An enemy can really occupy > 1 area
			// 3) AAS vis table is built using fairly coarse tests.
			if( !enemyData.areaVisRow[spot.aasAreaNum] ) {
				continue;
			}

			// If the spot and the enemy are in the same floor cluster
			if( spotFloorClusterNum && spotFloorClusterNum == enemyData.groundedAreaNum ) {
				if( !aasWorld->IsAreaWalkableInFloorCluster( enemyData.groundedAreaNum, spotFloorClusterNum ) ) {
					continue;
				}
			} else {
				trace_t trace;
				SolidWorldTrace( &trace, enemyData.viewOrigin, spot.origin );
				if( trace.fraction != 1.0f ) {
					continue;
				}
			}

			// Just add a unit on influence for every enemy.
			// We can't fully predict enemy future state
			// (e.g. whether it can become very dangerous by picking something).
			// Even a weak enemy can do a substantial damage since unless we take it into account.
			// We just select spots that are less visible for other enemies for proper positioning.
			spotVisScore += 1.0f;
		}

		// We have computed a vis score testing every enemy.
		// Now modify the original score
		const float enemyInfluenceFactor = Q_Rcp( 1.0f + spotVisScore );
		spotAndScore.score = ApplyFactor( spotAndScore.score, enemyInfluenceFactor, problemParams.enemiesInfluence );
	}

	return candidateSpots;
}

template <typename SpotsAndScores>
int TacticalSpotsProblemSolver::MakeResultsFilteringByProximity_( const SpotsAndScores &spotsAndScores,
																  vec3_t *origins, int maxSpots ) {
	const auto resultsSize = spotsAndScores.size();
	if( maxSpots == 0 || resultsSize == 0 ) {
		return 0;
	}

	// Its a common case so give it an optimized branch
	if( maxSpots == 1 ) {
		const float *origin = ::SpotOriginOf( spotsAndScores[0] );
		VectorCopy( origin, origins[0] );
		return 1;
	}

	const float squareProximityThreshold = problemParams.spotProximityThreshold * problemParams.spotProximityThreshold;
	bool *const isSpotExcluded = tacticalSpotsRegistry->temporariesAllocator.GetCleanExcludedSpotsMask();

	int numSpots_ = 0;
	unsigned keptSpotIndex = 0;
	for(;; ) {
		if( keptSpotIndex >= resultsSize ) {
			return numSpots_;
		}
		if( numSpots_ >= maxSpots ) {
			return numSpots_;
		}

		// Spots are sorted by score.
		// So first spot not marked as excluded yet has higher priority and should be kept.
		// The condition that terminates the outer loop ensures we have a valid kept spot.
		const float *__restrict keptSpotOrigin = ::SpotOriginOf( spotsAndScores[keptSpotIndex] );
		VectorCopy( keptSpotOrigin, origins[numSpots_] );
		++numSpots_;

		// Start from the next spot of the kept one
		unsigned testedSpotIndex = keptSpotIndex + 1;
		// Reset kept spot index so the loop is going to terminate next step by default
		keptSpotIndex = std::numeric_limits<unsigned>::max();
		// For every remaining spot in results left
		for(; testedSpotIndex < resultsSize; testedSpotIndex++ ) {
			// Skip already excluded spots
			if( isSpotExcluded[testedSpotIndex] ) {
				continue;
			}

			const float *__restrict testedSpotOrigin = ::SpotOriginOf( spotsAndScores[testedSpotIndex] );
			if( DistanceSquared( keptSpotOrigin, testedSpotOrigin ) < squareProximityThreshold ) {
				isSpotExcluded[testedSpotIndex] = true;
			} else if( keptSpotIndex > testedSpotIndex ) {
				// Mark the first non-excluded next spot for the outer loop
				keptSpotIndex = testedSpotIndex;
			}
		}
	}
}

int TacticalSpotsProblemSolver::MakeResultsFilteringByProximity( const SpotsAndScoreVector &spotsAndScores,
																 vec3_t *origins, int maxSpots ) {
	return MakeResultsFilteringByProximity_( spotsAndScores, origins, maxSpots );
}

int TacticalSpotsProblemSolver::MakeResultsFilteringByProximity( const OriginAndScoreVector &originsAndScores,
																 vec3_t *origins, int maxSpots ) {
	return MakeResultsFilteringByProximity_( originsAndScores, origins, maxSpots );
}