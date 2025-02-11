#ifndef QFUSION_BOT_MOVEMENT_H
#define QFUSION_BOT_MOVEMENT_H

// It would be better if we avoid inclusion of the implementation headers
// but we do not want to lose some performance on indirect access.
// Let offsets of all members be known statically.

#include "MovementPredictionContext.h"

#include "LandOnSavedAreasAction.h"
#include "RidePlatformAction.h"
#include "BunnyToStairsOrRampExitAction.h"
#include "BunnyInterpolatingReachChainAction.h"
#include "BunnyStraighteningReachChainAction.h"
#include "BunnyToBestClusterPointAction.h"
#include "BunnyToBestNavMeshPointAction.h"
#include "CampASpotAction.h"
#include "CombatDodgeToTargetAction.h"
#include "FallbackMovementAction.h"
#include "WalkOrSlideInterpolatingAction.h"
#include "WeaponJumpActions.h"

#include "FallDownScript.h"
#include "JumpOverBarrierScript.h"
#include "JumpToSpotScript.h"
#include "UseWalkableNodeScript.h"
#include "UseWalkableTriggerScript.h"
#include "UseStairsExitScript.h"
#include "UseRampExitScript.h"

class Bot;

// Roughly based on token buckets algorithm
class alignas( 4 )RateLimiter {
	Int64Align4 refilledAt;
	float refillRatePerMillis;
	unsigned intervalMillis;
	const int size;
	int value;

	int GetNewValue( int64_t millisNow ) const {
		int64_t diff = millisNow - refilledAt;
		auto tokensToAdd = (int)(diff * refillRatePerMillis);
		if( tokensToAdd <= 0 ) {
			return value;
		}

		int newValue = value;
		if( value <= 0 ) {
			newValue = tokensToAdd;
			if( newValue > size ) {
				newValue = size;
			}
		} else {
			newValue += tokensToAdd;
			if( newValue > size ) {
				newValue = 0;
			}
		}
		return newValue;
	}

	void Refill( int64_t millisNow ) {
		int newValue = GetNewValue( millisNow );
		if( value != newValue ) {
			value = newValue;
			refilledAt = millisNow - ( millisNow - refilledAt ) % intervalMillis;
		}
	}
public:
	explicit RateLimiter( int actionsPerSecond )
		: refilledAt( 0 )
		, refillRatePerMillis( actionsPerSecond / 1000.0f )
		, intervalMillis( (unsigned)( 1000.0f / actionsPerSecond ) )
		, size( actionsPerSecond )
		, value( 1 ) {
		// Note: initializing the value by 1 is important.
		// Otherwise the first TryAcquire() attempt fails.
		// This algorithm converges to theoretical values well according to tests.
	}

	bool TryAcquire( int64_t levelTime ) {
		Refill( levelTime );
		value -= 1;
		return value >= 0;
	}
};

class BotMovementModule {
	friend class Bot;
	friend struct BotMovementState;
	friend class MovementPredictionContext;
	friend class BaseMovementAction;
	friend class FallbackMovementAction;
	friend class HandleTriggeredJumppadAction;
	friend class LandOnSavedAreasAction;
	friend class RidePlatformAction;
	friend class SwimMovementAction;
	friend class FlyUntilLandingAction;
	friend class CampASpotMovementAction;
	friend class WalkCarefullyAction;
	friend class GenericRunBunnyingAction;
	friend class BunnyToStairsOrRampExitAction;
	friend class BunnyStraighteningReachChainAction;
	friend class BunnyToBestFloorClusterPointAction;
	friend class BunnyInterpolatingChainAtStartAction;
	friend class BunnyInterpolatingReachChainAction;
	friend class BunnyToBestNavMeshPointAction;
	friend class WalkOrSlideInterpolatingReachChainAction;
	friend class CombatDodgeSemiRandomlyToTargetAction;
	friend class ScheduleWeaponJumpAction;
	friend class TryTriggerWeaponJumpAction;
	friend class CorrectWeaponJumpAction;

	friend class GenericGroundMovementScript;
	friend class UseWalkableNodeScript;
	friend class UseRampExitScript;
	friend class UseStairsExitScript;
	friend class UseWalkableTriggerScript;
	friend class FallDownScript;
	friend class JumpOverBarrierScript;

	Bot *const bot;

	static constexpr unsigned MAX_SAVED_AREAS = MovementPredictionContext::MAX_SAVED_LANDING_AREAS;
	StaticVector<int, MAX_SAVED_AREAS> savedLandingAreas;
	StaticVector<int, MAX_SAVED_AREAS> savedPlatformAreas;

	// Limits weapon jumps attempts per second
	// (consequential attempts are allowed but no more than several frames,
	// otherwise a bot might loop attempts forever)
	RateLimiter weaponJumpAttemptsRateLimiter;
	// Is not for rate limiting but for preventing instant weapon switch for shooting after a failed attempt
	Int64Align4 lastWeaponJumpTriggeringFailedAt { 0 };

	// Must be initialized before any of movement actions constructors is called
	StaticVector<BaseMovementAction *, 20> movementActions;

	FallbackMovementAction fallbackMovementAction;
	HandleTriggeredJumppadAction handleTriggeredJumppadAction;
	LandOnSavedAreasAction landOnSavedAreasAction;
	RidePlatformAction ridePlatformAction;
	SwimMovementAction swimMovementAction;
	FlyUntilLandingAction flyUntilLandingAction;
	CampASpotMovementAction campASpotMovementAction;
	WalkCarefullyAction walkCarefullyAction;
	BunnyToStairsOrRampExitAction bunnyToStairsOrRampExitAction;
	BunnyStraighteningReachChainAction bunnyStraighteningReachChainAction;
	BunnyToBestFloorClusterPointAction bunnyToBestFloorClusterPointAction;
	BunnyInterpolatingChainAtStartAction bunnyInterpolatingChainAtStartAction;
	BunnyInterpolatingReachChainAction bunnyInterpolatingReachChainAction;
	BunnyToBestNavMeshPointAction bunnyToBestNavMeshPointAction;
	WalkOrSlideInterpolatingReachChainAction walkOrSlideInterpolatingReachChainAction;
	CombatDodgeSemiRandomlyToTargetAction combatDodgeSemiRandomlyToTargetAction;
	ScheduleWeaponJumpAction scheduleWeaponJumpAction;
	TryTriggerWeaponJumpAction tryTriggerWeaponJumpAction;
	CorrectWeaponJumpAction correctWeaponJumpAction;

	BotMovementState movementState;

	MovementPredictionContext predictionContext;

	UseWalkableNodeScript useWalkableNodeScript;
	UseRampExitScript useRampExitScript;
	UseStairsExitScript useStairsExitScript;
	UseWalkableTriggerScript useWalkableTriggerScript;

	JumpToSpotScript jumpToSpotScript;
	FallDownScript fallDownScript;
	JumpOverBarrierScript jumpOverBarrierScript;

	MovementScript *activeMovementScript { nullptr };

	int64_t nextRotateInputAttemptAt { 0 };
	int64_t inputRotationBlockingTimer { 0 };
	int64_t lastInputRotationFailureAt { 0 };

	void CheckGroundPlatform();

	void ApplyPendingTurnToLookAtPoint( BotInput *input, MovementPredictionContext *context = nullptr );
	inline void InvertInput( BotInput *input, MovementPredictionContext *context = nullptr );
	inline void TurnInputToSide( vec3_t sideDir, int sign, BotInput *input, MovementPredictionContext *context = nullptr );
	inline bool TryRotateInput( BotInput *input, MovementPredictionContext *context = nullptr );
	void CheckBlockingDueToInputRotation();

	void ResetFailedWeaponJumpAttempt( MovementPredictionContext *context ) {
		assert( context->movementState->weaponJumpMovementState.IsActive() );
		context->movementState->weaponJumpMovementState.Deactivate();
		this->lastWeaponJumpTriggeringFailedAt = level.time;
	}
public:
	explicit BotMovementModule( Bot *bot_ );

	inline void OnInterceptedPredictedEvent( int ev, int parm ) {
		predictionContext.OnInterceptedPredictedEvent( ev, parm );
	}

	inline void OnInterceptedPMoveTouchTriggers( pmove_t *pm, const vec3_t previousOrigin ) {
		predictionContext.OnInterceptedPMoveTouchTriggers( pm, previousOrigin );
	}

	inline void SetCampingSpot( const AiCampingSpot &campingSpot ) {
		movementState.campingSpotState.Activate( campingSpot );
	}

	inline void ResetCampingSpot() {
		movementState.campingSpotState.Deactivate();
	}

	inline bool HasActiveCampingSpot() const {
		return movementState.campingSpotState.IsActive();
	}

	inline void SetPendingLookAtPoint( const AiPendingLookAtPoint &lookAtPoint, unsigned timeoutPeriod ) {
		movementState.pendingLookAtPointState.Activate( lookAtPoint, timeoutPeriod );
	}

	inline void ResetPendingLookAtPoint() {
		movementState.pendingLookAtPointState.Deactivate();
	}

	inline bool HasPendingLookAtPoint() const {
		return movementState.pendingLookAtPointState.IsActive();
	}

	inline void ActivateJumppadState( const edict_t *jumppadEnt ) {
		movementState.jumppadMovementState.Activate( jumppadEnt );
	}

	bool CanChangeWeapons() const;

	void Reset() {
		movementState.Reset();
		activeMovementScript = nullptr;
	}

	bool CanInterruptMovement() const;

	void Frame( BotInput *input );
	void ApplyInput( BotInput *input, MovementPredictionContext *context = nullptr );
};

#endif
