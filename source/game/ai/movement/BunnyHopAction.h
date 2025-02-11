#ifndef QFUSION_BUNNYHOPACTION_H
#define QFUSION_BUNNYHOPACTION_H

#include "BaseMovementAction.h"

class BunnyHopAction : public BaseMovementAction {
	friend class MovementPredictionContext;
protected:
	// If the current grounded area matches one of these areas, we can mark mayStopAtAreaNum
	StaticVector<int, 8> checkStopAtAreaNums;

	int travelTimeAtSequenceStart { 0 };
	int reachAtSequenceStart { 0 };
	int groundedAreaAtSequenceStart { 0 };
	float groundZAtSequenceStart { 0.0f };
	// Best results so far achieved in the action application sequence
	int minTravelTimeToNavTargetSoFar { 0 };
	int minTravelTimeAreaNumSoFar { 0 };

	// If this is not a valid area num, try set it to a current grounded area if several conditions are met.
	// If this area is set, we can truncate the built path later at mayStopAtStackFrame
	// since this part of a trajectory is perfectly valid even if it has diverged after this frame.
	// Thus further planning is deferred once the bot actually reaches it (or prediction results give a mismatch).
	// This is a workaround for strict results tests and imperfect input suggested by various movement actions.
	// We still have to continue prediction until the bot hits ground
	// to ensure the bot is not going to land in a "bad" area in all possible cases.
	int mayStopAtAreaNum { 0 };
	int mayStopAtTravelTime { 0 };
	int mayStopAtStackFrame { -1 };
	vec3_t mayStopAtOrigin { 0, 0, 0 };

	float distanceToReachAtStart { std::numeric_limits<float>::infinity() };

	// A fraction of speed gain per frame time.
	// Might be negative, in this case it limits allowed speed loss
	float minDesiredSpeedGainPerSecond { 0.0f };
	unsigned currentSpeedLossSequentialMillis { 0 };
	unsigned tolerableSpeedLossSequentialMillis { 300 };

	// When bot bunnies over a gap, its target either becomes unreachable
	// or travel time is calculated from the bottom of the pit.
	// These timers allow to temporarily skip targer reachability/travel time tests.
	unsigned currentUnreachableTargetSequentialMillis { 0 };
	unsigned tolerableUnreachableTargetSequentialMillis { 700 };

	// Allow increased final travel time if the min travel time area is reachable by walking
	// from the final area and walking travel time is lower than this limit.
	// It allows to follow the reachability chain less strictly while still being close to it.
	unsigned tolerableWalkableIncreasedTravelTimeMillis { 3000 };

	// There is a mechanism for completely disabling an action for further planning by setting isDisabledForPlanning flag.
	// However we need a more flexible way of disabling an action after an failed application sequence.
	// A sequence started from different frame that the failed one might succeed.
	// An application sequence will not start at the frame indexed by this value.
	unsigned disabledForApplicationFrameIndex { std::numeric_limits<unsigned>::max() };

	// This should be set if we want to continue prediction
	// but give a path built by a current sequence an additional penalty
	// accounted by MovementPredictionContext::CompleteOrSaveGoodEnoughPath()
	unsigned sequencePathPenalty { 0 };

	bool hasEnteredNavTargetArea { false };
	bool hasTouchedNavTarget { false };

	bool supportsObstacleAvoidance { false };
	bool shouldTryObstacleAvoidance { false };
	bool isTryingObstacleAvoidance { false };

	inline void ResetObstacleAvoidanceState() {
		shouldTryObstacleAvoidance = false;
		isTryingObstacleAvoidance = false;
	}

	void SetupCommonBunnyHopInput( MovementPredictionContext *context );
	// TODO: Mark as virtual in base class and mark as final here to avoid a warning about hiding parent member?
	bool GenericCheckIsActionEnabled( MovementPredictionContext *context, BaseMovementAction *suggestedAction );
	bool CheckCommonBunnyHopPreconditions( MovementPredictionContext *context );
	bool SetupBunnyHopping( const Vec3 &intendedLookVec,
							MovementPredictionContext *context,
							float maxAccelDotThreshold = 1.0f );
	bool CanFlyAboveGroundRelaxed( const MovementPredictionContext *context ) const;
	bool CanSetWalljump( MovementPredictionContext *context ) const;
	void TrySetWalljump( MovementPredictionContext *context );

	// Can be overridden for finer control over tests
	virtual bool CheckStepSpeedGainOrLoss( MovementPredictionContext *context );

	bool GenericCheckForPrematureCompletion( MovementPredictionContext *context );
	bool CheckForPrematureCompletionInFloorCluster( MovementPredictionContext *context,
													int currGroundedAreaNum,
													int floorClusterNum );

	bool CheckForActualCompletionOnGround( MovementPredictionContext *context );

	bool TryTerminationAtBestGroundPosition( MovementPredictionContext *context, int currTravelTimeToTarget );
	bool TryTerminationAtGroundInSameStartArea( MovementPredictionContext *context );

	bool CheckNavTargetAreaTransition( MovementPredictionContext *context );

	inline bool IsSkimmingInAGivenState( const MovementPredictionContext *context ) const;

	bool TryHandlingSkimmingState( MovementPredictionContext *context );

	bool TryHandlingUnreachableTarget( MovementPredictionContext *context );

	void HandleSameOrBetterTravelTimeToTarget( MovementPredictionContext *context,
											   int currTravelTimeToTarget,
											   float squareDistanceFromStart,
											   int groundedAreaNum );

	bool TryHandlingWorseTravelTimeToTarget( MovementPredictionContext *context,
		                                     int currTravelTimeToTarget,
		                                     int groundedAreaNum );

	void TryMarkingForTruncation( MovementPredictionContext *context );

	bool TryTerminationOnStopAreaNum( MovementPredictionContext *context, int currTravelTimeToTarget, int groundedAreaNum );

	bool TryTerminationHavingPassedObstacleOrDeltaZ( MovementPredictionContext *context,
													 int currTravelTimeToTarget,
													 int groundedAreaNum );

	bool TryHandlingLackOfStopAreaNum( MovementPredictionContext *context,
									   int currTravelTimeToTarget,
									   float squareDistanceFromStart,
									   int groundedAreaNum );

	inline bool WasOnGroundThisFrame( const MovementPredictionContext *context ) const;

	inline bool HasSubstantiallyChangedZ( const AiEntityPhysicsState &entityPhysicsState ) const;

	inline void MarkForTruncation( MovementPredictionContext *context );

	void EnsurePathPenalty( unsigned penalty ) {
		assert( penalty < 30000 );
		sequencePathPenalty = std::max( sequencePathPenalty, penalty );
	}

	void CompleteOrSaveGoodEnoughPath( MovementPredictionContext *context, unsigned additionalPenalty = 0 );

	bool CheckDirectReachWalkingOrFallingShort( int fromAreaNum, int toAreaNum );
public:
	BunnyHopAction( BotMovementModule *module_, const char *name_, int debugColor_ = 0 )
		: BaseMovementAction( module_, name_, debugColor_ ) {
		ResetObstacleAvoidanceState();
		// Do NOT stop prediction on this! We have to check where the bot is going to land!
		BaseMovementAction::stopPredictionOnTouchingNavEntity = false;
	}

	void CheckPredictionStepResults( MovementPredictionContext *context ) override;
	void OnApplicationSequenceStarted( MovementPredictionContext *context ) override;
	void OnApplicationSequenceStopped( MovementPredictionContext *context,
									   SequenceStopReason reason,
									   unsigned stoppedAtFrameIndex ) override;
	void BeforePlanning() override;
};

#endif
