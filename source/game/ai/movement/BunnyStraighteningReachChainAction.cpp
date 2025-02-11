#include "BunnyStraighteningReachChainAction.h"
#include "MovementLocal.h"
#include "FloorClusterAreasCache.h"
#include "../ai_manager.h"

BunnyStraighteningReachChainAction::BunnyStraighteningReachChainAction( BotMovementModule *module_ )
	: BunnyTestingSavedLookDirsAction( module_, NAME, COLOR_RGB( 0, 192, 0 ) ) {
	supportsObstacleAvoidance = false;
	// The constructor cannot be defined in the header due to this bot member access
	suggestedAction = &module->bunnyToBestNavMeshPointAction;
	maxSuggestedLookDirs = MAX_SUGGESTED_LOOK_DIRS;
}

void BunnyStraighteningReachChainAction::BeforePlanning() {
	BunnyTestingSavedLookDirsAction::BeforePlanning();
	// We plan to allow varying bot skill dynamically.
	// This value should be recomputed every planning frame.

	const float skill = bot->Skill();
	// Bunny-hopping is enabled for easy bots under certain conditions. Allow only up to 2 dirs in this case.
	if( skill <= 0.33f ) {
		maxSuggestedLookDirs = 2;
		return;
	}

	maxSuggestedLookDirs = MAX_SUGGESTED_LOOK_DIRS;
	// Use the maximum possible number of suggested dirs for hard bots.
	if( skill >= 0.66f ) {
		return;
	}

	// TODO: All these decisions should not be made at movement module level

	// Check whether the bot is carrier. Use the maximal possible number of look dirs in this case.
	const edict_t *self = game.edicts + bot->EntNum();
	if( ( ( self->s.effects & EF_CARRIER ) || self->s.modelindex2 ) || bot->ShouldRushHeadless() ) {
		return;
	}

	const auto *inventory = self->r.client->ps.inventory;
	// Check whether the bot has a powerup. Ensure a best behaviour in this case.
	if( ( inventory[POWERUP_QUAD] | inventory[POWERUP_SHELL] | inventory[POWERUP_REGEN] ) ) {
		return;
	}

	// Grow quadratic starting from a weakest mid-skill bot
	float skillFrac = ( skill - 0.33f ) / ( 0.66f - 0.33f );
	Assert( skillFrac > 0.0f && skillFrac < 1.0f );
	maxSuggestedLookDirs = (unsigned)( 2 + ( skillFrac * skillFrac ) * MAX_SUGGESTED_LOOK_DIRS );
	maxSuggestedLookDirs = std::min( maxSuggestedLookDirs, (unsigned)MAX_SUGGESTED_LOOK_DIRS );
}

class StraighteningReachChainWalker final : public ReachChainWalker {
	friend class BunnyStraighteningReachChainAction;

	Context *const context;
	const AiAasWorld *const aasWorld;
	const aas_area_t *const aasAreas;
	const aas_areasettings_t *const aasAreaSettings;
	const uint16_t *const aasFloorClusterNums;
	const uint16_t *const aasStairsClusterNums;
	const Hazard *hazardToEvade;
	AreaAndScore *const candidatesBegin;
	Vec3 traceStartPoint;
	int metStairsClusterNum { 0 };
	int stairsExitAreaNum { 0 };
	int groundedStartAreaNum { 0 };
	int startFloorClusterNum { 0 };
	unsigned numCandidates { 0 };
	const unsigned maxCandidates;

	inline bool CheckForStairsCluster( int areaNum );
public:
	StraighteningReachChainWalker( const Bot *bot_, Context *context_, AreaAndScore *candidates_, unsigned maxCandidates_ )
		: ReachChainWalker( context_->RouteCache() )
		, context( context_ )
		, aasWorld( AiAasWorld::Instance() )
		, aasAreas( aasWorld->Areas() )
		, aasAreaSettings( aasWorld->AreaSettings() )
		, aasFloorClusterNums( aasWorld->AreaFloorClusterNums() )
		, aasStairsClusterNums( aasWorld->AreaStairsClusterNums() )
		, candidatesBegin( candidates_ )
		, traceStartPoint( context->movementState->entityPhysicsState.Origin() )
		, maxCandidates( maxCandidates_ ) {
		const auto &physicsState = context->movementState->entityPhysicsState;
		SetAreaNums( physicsState, context->NavTargetAasAreaNum() );
		traceStartPoint.Z() += playerbox_stand_viewheight;

		hazardToEvade = bot_->PrimaryHazard();
		if( bot_->ShouldRushHeadless() || ( hazardToEvade && !hazardToEvade->SupportsImpactTests() ) ) {
			hazardToEvade = nullptr;
		}

		if( ( groundedStartAreaNum = context->CurrGroundedAasAreaNum() ) ) {
			startFloorClusterNum = aasWorld->FloorClusterNum( groundedStartAreaNum );
			// Non-grounded areas cannot belong to a stairs cluster
			CheckForStairsCluster( groundedStartAreaNum );
		}
	}

	bool Accept( int, const aas_reachability_t &reach, int ) override;
};

void BunnyStraighteningReachChainAction::SaveSuggestedLookDirs( Context *context ) {
	Assert( suggestedLookDirs.empty() );

	if( context->IsInNavTargetArea() ) {
		return;
	}

	AreaAndScore candidates[MAX_SUGGESTED_LOOK_DIRS];
	StraighteningReachChainWalker walker( bot, context, candidates, MAX_SUGGESTED_LOOK_DIRS );
	if( !walker.Exec() ) {
		Debug( "Can't find areas for straightening a look dir\n" );
	}

	SaveCandidateAreaDirs( context, candidates, candidates + walker.numCandidates );
	Assert( suggestedLookDirs.size() <= maxSuggestedLookDirs );

	if( !walker.lastReachNum ) {
		return;
	}

	const auto &stoppedAtReach = AiAasWorld::Instance()->Reachabilities()[walker.lastReachNum];
	const auto travelType = stoppedAtReach.traveltype & TRAVELTYPE_MASK;
	if( travelType != TRAVEL_TELEPORT && travelType != TRAVEL_JUMPPAD && travelType != TRAVEL_ELEVATOR ) {
		return;
	}

	// TODO: Check a trace/an coarse arc trace to the trigger and ensure we do not hit anything besides maybe the trigger

	// If there is a trigger entity in the reach chain, try keep looking at it
	Assert( maxSuggestedLookDirs > 0 );
	// Evict the last dir, the trigger should have a priority over it
	if( suggestedLookDirs.size() == maxSuggestedLookDirs ) {
		suggestedLookDirs.pop_back();
	}

	Vec3 toTriggerDir( stoppedAtReach.start );
	toTriggerDir -= context->movementState->entityPhysicsState.Origin();
	toTriggerDir.Normalize();
	// The target area of reachStoppedAt is the area "behind" trigger.
	// The prediction gets always interrupted on touching trigger.
	// Just supply a dummy value and rely on touching the trigger during prediction.
	suggestedLookDirs.emplace_back( DirAndArea( toTriggerDir, 0 ) );
}

inline bool StraighteningReachChainWalker::CheckForStairsCluster( int areaNum ) {
	// Skip if a stairs cluster has been already met
	// (limiting to a single handled stairs cluster is satisfactory)
	if( metStairsClusterNum ) {
		return true;
	}

	const int stairsClusterNum = aasStairsClusterNums[areaNum];
	if( !stairsClusterNum ) {
		return true;
	}

	if( stairsExitAreaNum == areaNum ) {
		return true;
	}

	if( const auto *exitAreaNum = TryFindBestStairsExitArea( context, stairsClusterNum ) ) {
		stairsExitAreaNum = *exitAreaNum;
		return true;
	}

	// Exclude all other areas for this
	return false;
}

bool StraighteningReachChainWalker::Accept( int, const aas_reachability_t &reach, int ) {
	const auto travelType = ( reach.traveltype & TRAVELTYPE_MASK );
	if( travelType != TRAVEL_WALK && travelType != TRAVEL_WALKOFFLEDGE ) {
		// Interrupt walking reach chain at this
		return false;
	}

	const int areaNum = reach.areanum;
	if( !CheckForStairsCluster( areaNum ) ) {
		// Skip the area and continue walking
		return true;
	}

	const auto &__restrict areaSettings = aasAreaSettings[areaNum];
	// Skip the area and continue walking if these conditions are unmet
	if( areaSettings.contents & AREACONTENTS_DONOTENTER ) {
		return true;
	}
	if( !( areaSettings.areaflags & AREA_GROUNDED ) || ( areaSettings.areaflags & AREA_DISABLED ) ) {
		return true;
	}

	const auto &__restrict area = aasAreas[areaNum];
	Vec3 areaPoint( area.center[0], area.center[1], area.mins[2] - playerbox_stand_mins[2] );

	const float squareDistanceToArea = areaPoint.SquareDistanceTo( traceStartPoint );
	// Skip way too close areas (otherwise the bot might fall into endless looping)
	if( squareDistanceToArea < SQUARE( 128 ) ) {
		return true;
	}

	// Skip way too far areas (this is mainly an optimization for the following SolidWorldTrace() call)
	if( squareDistanceToArea > SQUARE( 1024 + 512 ) ) {
		return true;
	}

	// Skip "junk" areas if they are far.
	// Avoid wasting CPU cycles for these areas.
	// They might still be useful being close to a bot, e.g. if a bot is trapped at unrecognized stairs cluster.
	if( squareDistanceToArea > SQUARE( 256 + 64 ) && ( areaSettings.areaflags & AREA_JUNK ) ) {
		return true;
	}

	if( hazardToEvade && hazardToEvade->HasImpactOnPoint( areaPoint ) ) {
		return true;
	}

	// Make sure the bot can see the ground
	// On failure, restore minScore (it might have been set to the value of the rejected area score on this loop step)
	if( startFloorClusterNum && startFloorClusterNum == aasFloorClusterNums[areaNum] ) {
		if( !aasWorld->IsAreaWalkableInFloorCluster( groundedStartAreaNum, areaNum ) ) {
			return true;
		}
	} else {
		if( !TraceArcInSolidWorld( traceStartPoint.Data(), areaPoint.Data() ) ) {
			return true;
		}

		// This is very likely to indicate a significant elevation of the area over the bot area.
		// TODO: This test leads to a failure if the target area is direct-reachable via falling
		if( !TravelTimeWalkingOrFallingShort( routeCache, areaNum, groundedStartAreaNum ) ) {
			return true;
		}
	}

	if( numCandidates == maxCandidates ) {
		// Interrupt walking at this
		return false;
	}

	// Give next-in-chain areas greater scores
	new( candidatesBegin + numCandidates )AreaAndScore( areaNum, numCandidates );
	numCandidates++;
	return true;
}