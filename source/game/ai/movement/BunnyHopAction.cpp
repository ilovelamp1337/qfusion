#include "BunnyHopAction.h"
#include "MovementLocal.h"

bool BunnyHopAction::GenericCheckIsActionEnabled( Context *context, BaseMovementAction *suggestedAction ) {
	if( !BaseMovementAction::GenericCheckIsActionEnabled( context, suggestedAction ) ) {
		return false;
	}

	if( this->disabledForApplicationFrameIndex != context->topOfStackIndex ) {
		return true;
	}

	Debug( "Cannot apply action: the action has been disabled for application on frame %d\n", context->topOfStackIndex );
	context->sequenceStopReason = DISABLED;
	context->cannotApplyAction = true;
	context->actionSuggestedByAction = suggestedAction;
	return false;
}

bool BunnyHopAction::CheckCommonBunnyHopPreconditions( Context *context ) {
	int currAasAreaNum = context->CurrAasAreaNum();
	if( !currAasAreaNum ) {
		Debug( "Cannot apply action: curr AAS area num is undefined\n" );
		context->SetPendingRollback();
		return false;
	}

	int navTargetAasAreaNum = context->NavTargetAasAreaNum();
	if( !navTargetAasAreaNum ) {
		Debug( "Cannot apply action: nav target AAS area num is undefined\n" );
		context->SetPendingRollback();
		return false;
	}

	if( bot->ShouldKeepXhairOnEnemy() ) {
		const auto &selectedEnemies = bot->GetSelectedEnemies();
		if( selectedEnemies.AreValid() && selectedEnemies.ArePotentiallyHittable() ) {
			if( !context->MayHitWhileRunning().CanHit() ) {
				Debug( "Cannot apply action: cannot hit an enemy while keeping the crosshair on it is required\n" );
				context->SetPendingRollback();
				this->isDisabledForPlanning = true;
				return false;
			}
		}
	}

	// Cannot find a next reachability in chain while it should exist
	// (looks like the bot is too high above the ground)
	if( !context->IsInNavTargetArea() && !context->NextReachNum() ) {
		Debug( "Cannot apply action: next reachability is undefined and bot is not in the nav target area\n" );
		context->SetPendingRollback();
		return false;
	}

	if( !( context->currPlayerState->pmove.stats[PM_STAT_FEATURES] & PMFEAT_JUMP ) ) {
		Debug( "Cannot apply action: bot does not have the jump movement feature\n" );
		context->SetPendingRollback();
		this->isDisabledForPlanning = true;
		return false;
	}

	if( bot->ShouldBeSilent() ) {
		Debug( "Cannot apply action: bot should be silent\n" );
		context->SetPendingRollback();
		this->isDisabledForPlanning = true;
		return false;
	}

	return true;
}

void BunnyHopAction::SetupCommonBunnyHopInput( Context *context ) {
	const auto *pmoveStats = context->currPlayerState->pmove.stats;

	auto *botInput = &context->record->botInput;
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	botInput->SetForwardMovement( 1 );
	const auto &hitWhileRunningTestResult = context->MayHitWhileRunning();
	if( bot->ShouldKeepXhairOnEnemy() ) {
		const auto &selectedEnemies = bot->GetSelectedEnemies();
		if( selectedEnemies.AreValid() && selectedEnemies.ArePotentiallyHittable() ) {
			Assert( hitWhileRunningTestResult.CanHit() );
		}
	}

	botInput->canOverrideLookVec = hitWhileRunningTestResult.canHitAsIs;
	botInput->canOverridePitch = true;

	if( ( pmoveStats[PM_STAT_FEATURES] & PMFEAT_DASH ) && !pmoveStats[PM_STAT_DASHTIME] ) {
		bool shouldDash = false;
		if( entityPhysicsState.Speed() < context->GetDashSpeed() && entityPhysicsState.GroundEntity() ) {
			// Prevent dashing into obstacles
			auto &traceCache = context->TraceCache();
			auto query( EnvironmentTraceCache::Query::Front() );
			traceCache.TestForQuery( context, query );
			if( traceCache.ResultForQuery( query ).trace.fraction == 1.0f ) {
				shouldDash = true;
			}
		}

		if( shouldDash ) {
			botInput->SetSpecialButton( true );
			botInput->SetUpMovement( 0 );
			// Predict dash precisely
			context->predictionStepMillis = context->DefaultFrameTime();
		} else {
			botInput->SetUpMovement( 1 );
		}
	} else {
		if( entityPhysicsState.Speed() < context->GetRunSpeed() ) {
			botInput->SetUpMovement( 0 );
		} else {
			botInput->SetUpMovement( 1 );
		}
	}
}

bool BunnyHopAction::SetupBunnyHopping( const Vec3 &intendedLookVec, Context *context, float maxAccelDotThreshold ) {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	auto *botInput = &context->record->botInput;

	Vec3 toTargetDir2D( intendedLookVec );
	toTargetDir2D.Z() = 0;

	Vec3 velocityDir2D( entityPhysicsState.Velocity() );
	velocityDir2D.Z() = 0;

	float squareSpeed2D = entityPhysicsState.SquareSpeed2D();
	float toTargetDir2DSqLen = toTargetDir2D.SquaredLength();

	if( squareSpeed2D > 1.0f ) {
		SetupCommonBunnyHopInput( context );

		velocityDir2D *= 1.0f / entityPhysicsState.Speed2D();

		if( toTargetDir2DSqLen > 0.1f ) {
			const auto &oldPMove = context->oldPlayerState->pmove;
			const auto &newPMove = context->currPlayerState->pmove;
			// If not skimming
			if( !( newPMove.skim_time && newPMove.skim_time != oldPMove.skim_time ) ) {
				toTargetDir2D *= Q_RSqrt( toTargetDir2DSqLen );
				float velocityDir2DDotToTargetDir2D = velocityDir2D.Dot( toTargetDir2D );
				if( velocityDir2DDotToTargetDir2D > 0.0f ) {
					// Apply cheating acceleration.
					// maxAccelDotThreshold is usually 1.0f, so the "else" path gets executed.
					// If the maxAccelDotThreshold is lesser than the dot product,
					// a maximal possible acceleration is applied
					// (once the velocity and target dirs match conforming to the specified maxAccelDotThreshold).
					// This allows accelerate even faster if we have an a-priori knowledge that the action is reliable.
					Assert( maxAccelDotThreshold >= 0.0f );
					if( velocityDir2DDotToTargetDir2D >= maxAccelDotThreshold ) {
						context->CheatingAccelerate( 1.0f );
					} else {
						context->CheatingAccelerate( velocityDir2DDotToTargetDir2D );
					}
				}
				// Do not apply correction if this dot product is negative (looks like hovering in air and does not help)
				if( velocityDir2DDotToTargetDir2D > 0 && velocityDir2DDotToTargetDir2D < STRAIGHT_MOVEMENT_DOT_THRESHOLD ) {
					context->CheatingCorrectVelocity( velocityDir2DDotToTargetDir2D, toTargetDir2D );
				}
			}
		}
	}
	// Looks like the bot is in air falling vertically
	else if( !entityPhysicsState.GroundEntity() ) {
		// Release keys to allow full control over view in air without affecting movement
		if( bot->ShouldAttack() && CanFlyAboveGroundRelaxed( context ) ) {
			botInput->ClearMovementDirections();
			botInput->canOverrideLookVec = true;
		}
		return true;
	} else {
		SetupCommonBunnyHopInput( context );
		return true;
	}

	if( bot->ShouldAttack() && CanFlyAboveGroundRelaxed( context ) ) {
		botInput->ClearMovementDirections();
		botInput->canOverrideLookVec = true;
	}

	// Skip dash and WJ near triggers and nav targets to prevent missing a trigger/nav target
	const int nextReachNum = context->NextReachNum();
	if( !nextReachNum ) {
		// Preconditions check must not allow bunnying outside of nav target area having an empty reach. chain
		Assert( context->IsInNavTargetArea() );
		botInput->SetSpecialButton( false );
		botInput->canOverrideLookVec = false;
		botInput->canOverridePitch = false;
		return true;
	}

	switch( AiAasWorld::Instance()->Reachabilities()[nextReachNum].traveltype ) {
		case TRAVEL_TELEPORT:
		case TRAVEL_JUMPPAD:
		case TRAVEL_ELEVATOR:
		case TRAVEL_LADDER:
		case TRAVEL_BARRIERJUMP:
			botInput->SetSpecialButton( false );
			botInput->canOverrideLookVec = false;
			botInput->canOverridePitch = true;
			return true;
		default:
			if( context->IsCloseToNavTarget() ) {
				botInput->SetSpecialButton( false );
				botInput->canOverrideLookVec = false;
				botInput->canOverridePitch = false;
				return true;
			}
	}

	if( ShouldPrepareForCrouchSliding( context, 8.0f ) ) {
		botInput->SetUpMovement( -1 );
		context->predictionStepMillis = context->DefaultFrameTime();
	}

	TrySetWalljump( context );
	return true;
}

bool BunnyHopAction::CanFlyAboveGroundRelaxed( const Context *context ) const {
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( entityPhysicsState.GroundEntity() ) {
		return false;
	}

	float desiredHeightOverGround = 0.3f * AI_JUMPABLE_HEIGHT;
	return entityPhysicsState.HeightOverGround() >= desiredHeightOverGround;
}

void BunnyHopAction::TrySetWalljump( Context *context ) {
	if( !CanSetWalljump( context ) ) {
		return;
	}

	auto *botInput = &context->record->botInput;
	botInput->ClearMovementDirections();
	botInput->SetSpecialButton( true );
	// Predict a frame precisely for walljumps
	context->predictionStepMillis = context->DefaultFrameTime();
}

#define TEST_TRACE_RESULT_NORMAL( traceResult )                                   \
	do {                                                                          \
		if( traceResult.trace.fraction != 1.0f ) {                                \
			if( velocity2DDir.Dot( traceResult.trace.plane.normal ) < -0.3f ) {   \
				return false;                                                     \
			}                                                                     \
			hasGoodWalljumpNormal = true;                                         \
		}                                                                         \
	} while( 0 )

bool BunnyHopAction::CanSetWalljump( Context *context ) const {
	const short *pmoveStats = context->currPlayerState->pmove.stats;
	if( !( pmoveStats[PM_STAT_FEATURES] & PMFEAT_WALLJUMP ) ) {
		return false;
	}

	if( pmoveStats[PM_STAT_WJTIME] ) {
		return false;
	}

	if( pmoveStats[PM_STAT_STUN] ) {
		return false;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( entityPhysicsState.GroundEntity() ) {
		return false;
	}

	if( entityPhysicsState.HeightOverGround() < 8.0f && entityPhysicsState.Velocity()[2] <= 0 ) {
		return false;
	}

	float speed2D = entityPhysicsState.Speed2D();
	// The 2D speed is too low for walljumping
	if( speed2D < 400 ) {
		return false;
	}

	Vec3 velocity2DDir( entityPhysicsState.Velocity()[0], entityPhysicsState.Velocity()[1], 0 );
	velocity2DDir *= 1.0f / speed2D;

	auto &traceCache = context->TraceCache();
	auto query( EnvironmentTraceCache::Query::Front() );
	traceCache.TestForQuery( context, query );
	const auto &frontResult = traceCache.ResultForQuery( query );
	if( velocity2DDir.Dot( frontResult.traceDir ) < 0.7f ) {
		return false;
	}

	bool hasGoodWalljumpNormal = false;
	TEST_TRACE_RESULT_NORMAL( frontResult );

	// Do not force full-height traces for sides to be computed.
	// Walljump height rules are complicated, and full simulation of these rules seems to be excessive.
	// In worst case a potential walljump might be skipped.

	const auto leftQuery( EnvironmentTraceCache::Query::Left().JumpableHeight() );
	const auto rightQuery( EnvironmentTraceCache::Query::Right().JumpableHeight() );
	const auto frontLeftQuery( EnvironmentTraceCache::Query::FrontLeft().JumpableHeight() );
	const auto frontRightQuery( EnvironmentTraceCache::Query::FrontRight().JumpableHeight() );

	const unsigned mask = leftQuery.mask | rightQuery.mask | frontLeftQuery.mask | frontRightQuery.mask;
	traceCache.TestForResultsMask( context, mask );

	TEST_TRACE_RESULT_NORMAL( traceCache.ResultForQuery( leftQuery ) );
	TEST_TRACE_RESULT_NORMAL( traceCache.ResultForQuery( rightQuery ) );
	TEST_TRACE_RESULT_NORMAL( traceCache.ResultForQuery( frontLeftQuery ) );
	TEST_TRACE_RESULT_NORMAL( traceCache.ResultForQuery( frontRightQuery ) );

	return hasGoodWalljumpNormal;
}

#undef TEST_TRACE_RESULT_NORMAL

bool BunnyHopAction::CheckStepSpeedGainOrLoss( Context *context ) {
	const auto *oldPMove = &context->oldPlayerState->pmove;
	const auto *newPMove = &context->currPlayerState->pmove;
	// Make sure this test is skipped along with other ones while skimming
	Assert( !( newPMove->skim_time && newPMove->skim_time != oldPMove->skim_time ) );

	const auto &newEntityPhysicsState = context->movementState->entityPhysicsState;
	const auto &oldEntityPhysicsState = context->PhysicsStateBeforeStep();

	// Test for a huge speed loss in case of hitting of an obstacle
	const float *oldVelocity = oldEntityPhysicsState.Velocity();
	const float *newVelocity = newEntityPhysicsState.Velocity();
	const float oldSquare2DSpeed = oldEntityPhysicsState.SquareSpeed2D();
	const float newSquare2DSpeed = newEntityPhysicsState.SquareSpeed2D();


	bool hasChangedZ = false;
	bool continueOnFailure = false;
	unsigned penalty = 0;
	// Skip any further tests if the bot has changed Z substantially or has marked "may stop at origin".
	// Put cheaper tests first in outer conditions.
	if( HasSubstantiallyChangedZ( newEntityPhysicsState ) ) {
		if( originAtSequenceStart.SquareDistance2DTo( newEntityPhysicsState.Origin() ) > SQUARE( 72.0f ) ) {
			continueOnFailure = true;
			hasChangedZ = true;
		}
	}
	if( !continueOnFailure && mayStopAtAreaNum ) {
		float squareDistance = Distance2DSquared( newEntityPhysicsState.Origin(), mayStopAtOrigin );
		if( squareDistance > SQUARE( 16.0f ) ) {
			continueOnFailure = true;
			// Apply an additional penalty if the square distance from "may stop at origin" is not really sufficient
			constexpr float threshold = 96.0f;
			if( squareDistance < SQUARE( threshold ) ) {
				// Penalty units are supposed to be millis.
				// Add 50 penalty units for every insufficient distance unit
				penalty += 50U * (unsigned)( threshold - Q_Sqrt( squareDistance ) );
			}
		}
		if( continueOnFailure ) {
			// Also apply a penalty for bumping in obstacles being high above ground
			float heightOverGround = newEntityPhysicsState.HeightOverGround();
			if( std::isfinite( heightOverGround ) ) {
				constexpr float threshold = 32.0f;
				if( heightOverGround > threshold ) {
					penalty += 50U * ( unsigned )( heightOverGround - threshold );
				}
			} else {
				penalty += 10000;
			}
		}
	}

	// Check for unintended bouncing back (starting from some speed threshold)
	if( oldSquare2DSpeed > 100 * 100 && newSquare2DSpeed > 1 * 1 ) {
		Vec3 oldVelocity2DDir( oldVelocity[0], oldVelocity[1], 0 );
		oldVelocity2DDir *= 1.0f / oldEntityPhysicsState.Speed2D();
		Vec3 newVelocity2DDir( newVelocity[0], newVelocity[1], 0 );
		newVelocity2DDir *= 1.0f / newEntityPhysicsState.Speed2D();
		if( oldVelocity2DDir.Dot( newVelocity2DDir ) < 0.3f ) {
			if( !continueOnFailure ) {
				Debug( "A prediction step has lead to an unintended bouncing back\n" );
				return false;
			}
			// Walljumping is fine but in this environment it might hide bouncing of walls of a pit
			if( hasChangedZ ) {
				EnsurePathPenalty( 1000 + penalty );
			}
		}
	}

	// Avoid bumping into walls.
	// Note: the lower speed limit is raised to actually trigger this check.
	if( newSquare2DSpeed < 50 * 50 && oldSquare2DSpeed > 100 * 100 ) {
		if( continueOnFailure ) {
			EnsurePathPenalty( 1000 + penalty );
			return true;
		}
		Debug( "A prediction step has lead to close to zero 2D speed while it was significant\n" );
		this->shouldTryObstacleAvoidance = true;
		return false;
	}

	// Check for regular speed loss
	const float oldSpeed = oldEntityPhysicsState.Speed();
	const float newSpeed = newEntityPhysicsState.Speed();

	Assert( context->predictionStepMillis );
	float actualSpeedGainPerSecond = ( newSpeed - oldSpeed ) / ( 0.001f * context->predictionStepMillis );
	if( actualSpeedGainPerSecond >= minDesiredSpeedGainPerSecond || context->IsInNavTargetArea() ) {
		// Reset speed loss timer
		currentSpeedLossSequentialMillis = 0;
		return true;
	}

	const char *format = "Actual speed gain per second %.3f is lower than the desired one %.3f\n";
	Debug( "oldSpeed: %.1f, newSpeed: %1.f, speed gain per second: %.1f\n", oldSpeed, newSpeed, actualSpeedGainPerSecond );
	Debug( format, actualSpeedGainPerSecond, minDesiredSpeedGainPerSecond );

	currentSpeedLossSequentialMillis += context->predictionStepMillis;
	if( tolerableSpeedLossSequentialMillis > currentSpeedLossSequentialMillis ) {
		return true;
	}

	// Let actually interrupt it if the new speed is less than this threshold.
	// Otherwise many trajectories that look feasible get rejected.
	// We should not however completely eliminate this interruption
	// as sometimes it prevents bumping in obstacles pretty well.
	const float speed2D = newEntityPhysicsState.Speed2D();
	const float threshold = 0.5f * ( context->GetRunSpeed() + context->GetDashSpeed() );
	if( speed2D >= threshold ) {
		return true;
	}

	if( continueOnFailure ) {
		EnsurePathPenalty( 750 + penalty );
		return true;
	}

	// Stop in this seemingly unrecoverable case
	if( speed2D < 100 ) {
		const char *format_ = "A sequential speed loss interval of %d millis exceeds the tolerable one of %d millis\n";
		Debug( format_, currentSpeedLossSequentialMillis, tolerableSpeedLossSequentialMillis );
		this->shouldTryObstacleAvoidance = true;
		return false;
	}

	// If the area is not a "skip collision" area
	if( !( AiAasWorld::Instance()->AreaSettings()[context->CurrAasAreaNum()].areaflags & AREA_SKIP_COLLISION_16 ) ) {
		const float frac = ( threshold - speed2D ) * Q_Rcp( threshold );
		penalty += (unsigned)( 250 + 1250 * Q_Sqrt( frac ) );
	}

	EnsurePathPenalty( penalty );
	return true;
}

inline bool BunnyHopAction::WasOnGroundThisFrame( const Context *context ) const {
	return context->movementState->entityPhysicsState.GroundEntity() || context->frameEvents.hasJumped;
}

inline bool BunnyHopAction::HasSubstantiallyChangedZ( const AiEntityPhysicsState &state ) const {
	if( !std::isfinite( groundZAtSequenceStart ) ) {
		return false;
	}
	const float heightOverGround = state.HeightOverGround();
	if( !std::isfinite( heightOverGround ) ) {
		return false;
	}
	const float newGroundZ = state.Origin()[2] - heightOverGround + playerbox_stand_mins[2];
	return std::fabs( groundZAtSequenceStart - newGroundZ ) > 48.0f;
}

bool BunnyHopAction::CheckForActualCompletionOnGround( MovementPredictionContext *context ) {
	// TODO: provide wrappers that eliminate this awkward invocation
	if( context->TraceCache().CanSkipPMoveCollision( context ) ) {
		return true;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	// We should not end up landing having speed 2D this low
	if( entityPhysicsState.Speed2D() < 100.0f ) {
		return false;
	}

	// Make sure we do not land/jump just in front of an obstacle (12 units ahead of the bot origin).
	// Using a zero-width (ray) test is intentional (otherwise handling of sliding along a wall is complicated).
	// Also a Z-offset is applied so the ray is at the "head" height to prevent producing false negatives on stairs/ramps

	Vec3 velocityDir( entityPhysicsState.Velocity() );
	velocityDir *= 1.0f / entityPhysicsState.Speed();

	Vec3 xerpOrigin( velocityDir );
	xerpOrigin *= playerbox_stand_maxs[0] + 12.0f;
	xerpOrigin += entityPhysicsState.Origin();
	xerpOrigin.Z() += playerbox_stand_maxs[2] - 1.0f;

	trace_t trace;
	StaticWorldTrace( &trace, entityPhysicsState.Origin(), xerpOrigin.Data(), MASK_SOLID | MASK_WATER );
	if( trace.fraction == 1.0f ) {
		return true;
	}

	// Check bumping angle. Otherwise this test is way too restrictive in practice.
	return velocityDir.Dot( trace.plane.normal ) > -0.3f;
}

inline void BunnyHopAction::MarkForTruncation( Context *context ) {
	int currGroundedAreaNum = context->CurrGroundedAasAreaNum();
	Assert( currGroundedAreaNum );
	mayStopAtAreaNum = currGroundedAreaNum;

	int travelTimeToTarget = context->TravelTimeToNavTarget();
	Assert( travelTimeToTarget );
	mayStopAtTravelTime = travelTimeToTarget;

	mayStopAtStackFrame = (int)context->topOfStackIndex;
	VectorCopy( context->movementState->entityPhysicsState.Origin(), mayStopAtOrigin );
}

void BunnyHopAction::TryMarkingForTruncation( Context *context ) {
	const auto &physicsState = context->movementState->entityPhysicsState;
	if( physicsState.Velocity()[2] / physicsState.Speed() < -0.1f ) {
		MarkForTruncation( context );
	} else if( WasOnGroundThisFrame( context ) ) {
		MarkForTruncation( context );
	}
}

void BunnyHopAction::CompleteOrSaveGoodEnoughPath( Context *context, unsigned additionalPenalty ) {
	// Let the penalty be a sum of an accumulated path penalty and a penalty specified at invocation of this method.
	context->CompleteOrSaveGoodEnoughPath( minTravelTimeToNavTargetSoFar, additionalPenalty + sequencePathPenalty );
}

void BunnyHopAction::HandleSameOrBetterTravelTimeToTarget( Context *context,
														   int currTravelTimeToTarget,
														   float squareDistanceFromStart,
														   int groundedAreaNum ) {
	minTravelTimeToNavTargetSoFar = currTravelTimeToTarget;
	minTravelTimeAreaNumSoFar = context->CurrAasAreaNum();

	// Try setting "may stop at area num" if it has not been set yet
	if( mayStopAtAreaNum ) {
		return;
	}

	// Can't say much in this case
	if( !groundedAreaNum || !travelTimeAtSequenceStart ) {
		return;
	}

	// This is a very lenient condition, just check whether we are a bit closer to the target
	if( travelTimeAtSequenceStart > currTravelTimeToTarget + 5 ) {
		if( squareDistanceFromStart > SQUARE( 72 ) ) {
			TryMarkingForTruncation( context );
		}
		return;
	}

	// Passing this condition also implies the area is really huge
	if( squareDistanceFromStart < SQUARE( 96 ) ) {
		return;
	}

	const auto &newEntityPhysicsState = context->movementState->entityPhysicsState;

	Vec3 velocityDir2D( newEntityPhysicsState.Velocity() );
	velocityDir2D.Z() = 0;
	velocityDir2D *= Q_Rcp( newEntityPhysicsState.Speed2D() );
	const auto &reach = AiAasWorld::Instance()->Reachabilities()[reachAtSequenceStart];

	// The next reachability must be relatively far
	// (a reachability following the next one might have completely different direction)
	if( Distance2DSquared( reach.start, newEntityPhysicsState.Origin() ) < SQUARE( 48 ) ) {
		return;
	}

	Vec3 reachDir2D = Vec3( reach.end );
	reachDir2D -= reach.start;
	reachDir2D.Z() = 0;
	reachDir2D.NormalizeFast();

	// Check whether we conform to the next reachability direction
	if( velocityDir2D.Dot( reachDir2D ) < 0.9f ) {
		return;
	}

	TryMarkingForTruncation( context );
}

bool BunnyHopAction::TryHandlingWorseTravelTimeToTarget( Context *context,
														 int currTravelTimeToTarget,
														 int groundedAreaNum ) {
	constexpr const char *format = "A prediction step has lead to increased travel time to nav target\n";
	// Convert minTravelTimeToNavTargetSoFar to millis to have the same units for comparison
	int maxTolerableTravelTimeMillis = 10 * minTravelTimeToNavTargetSoFar;
	maxTolerableTravelTimeMillis += tolerableWalkableIncreasedTravelTimeMillis;
	// Use more lenient checks if we've marked mayStopAtAreaNum
	if( mayStopAtAreaNum ) {
		maxTolerableTravelTimeMillis += 1000;
	}

	// Convert currTravelTime from seconds^-2 to millis to have the same units for comparison
	if( 10 * currTravelTimeToTarget > maxTolerableTravelTimeMillis ) {
		Debug( format );
		return false;
	}

	// Can't say much in this case. Continue prediction.
	if( !groundedAreaNum || !minTravelTimeAreaNumSoFar ) {
		return true;
	}

	const auto *aasWorld = AiAasWorld::Instance();

	// Allow further prediction if we're still in the same floor cluster
	if( const int clusterNum = aasWorld->FloorClusterNum( minTravelTimeAreaNumSoFar ) ) {
		if( clusterNum == aasWorld->FloorClusterNum( groundedAreaNum ) ) {
			return true;
		}
	}

	// Allow further prediction if we're in a NOFALL area.
	if( aasWorld->AreaSettings()[groundedAreaNum].areaflags & AREA_NOFALL ) {
		const auto *aasAreas = aasWorld->Areas();
		// Delta Z relative to the best area so far must be positive
		if( aasAreas[groundedAreaNum].mins[2] > aasAreas[minTravelTimeAreaNumSoFar].mins[2] ) {
			EnsurePathPenalty( 250 );
			return true;
		}
		// Allow negative Z while being in a stairs cluster
		if( aasWorld->StairsClusterNum( groundedAreaNum ) ) {
			EnsurePathPenalty( 350 );
			return true;
		}
	}

	// Disallow moving into an area if the min travel time area cannot be reached by walking from the area.
	// Use a simple reverse reach. test instead of router calls (that turned out to be expensive/non-scalable).
	if( CheckDirectReachWalkingOrFallingShort( groundedAreaNum, minTravelTimeAreaNumSoFar ) ) {
		return true;
	}

	// Allow an increased travel time if the bot is far from "may stop at" area.
	// The path beginning should be good and the rest gets truncated/never used.
	if( mayStopAtAreaNum ) {
		const float *origin = context->movementState->entityPhysicsState.Origin();
		if( DistanceSquared( origin, mayStopAtOrigin ) > SQUARE( 96 ) ) {
			EnsurePathPenalty( 350 );
			return true;
		}
	}

	EnsurePathPenalty( 3000 );
	return true;
}

bool BunnyHopAction::CheckDirectReachWalkingOrFallingShort( int fromAreaNum, int toAreaNum ) {
	const auto *aasWorld = AiAasWorld::Instance();
	const auto *aasReach = aasWorld->Reachabilities();
	const auto &areaSettings = aasWorld->AreaSettings()[fromAreaNum];

	// Limit number of tested rev. reach.
	// TODO: Add and use reverse reach. table for this and many other purposes
	int maxReachNum = areaSettings.firstreachablearea + std::min( areaSettings.numreachableareas, 16 );
	for( int revReachNum = areaSettings.firstreachablearea; revReachNum != maxReachNum; revReachNum++ ) {
		const auto &reach = aasReach[revReachNum];
		if( reach.areanum != toAreaNum ) {
			continue;
		}
		const auto travelType = reach.traveltype & TRAVELTYPE_MASK;
		if( travelType == TRAVEL_WALK ) {
			EnsurePathPenalty( 300 );
			return true;
		}
		if( travelType == TRAVEL_WALKOFFLEDGE ) {
			// Make sure the fall distance is insufficient
			if( reach.start[2] - reach.end[2] < 64.0f ) {
				EnsurePathPenalty( 400 );
				return true;
			}
		}
		// We've found a rev. reach. (even if it did not pass additional tests). Avoid doing further tests.
		break;
	}

	return false;
}

bool BunnyHopAction::TryHandlingUnreachableTarget( Context *context ) {
	currentUnreachableTargetSequentialMillis += context->predictionStepMillis;
	if( currentUnreachableTargetSequentialMillis < tolerableUnreachableTargetSequentialMillis ) {
		context->SaveSuggestedActionForNextFrame( this );
		return true;
	}

	Debug( "A prediction step has lead to undefined travel time to the nav target\n" );
	return false;
}

inline bool BunnyHopAction::IsSkimmingInAGivenState( const Context *context ) const {
	const auto &newPMove = context->currPlayerState->pmove;
	if( !newPMove.skim_time ) {
		return true;
	}

	const auto &oldPMove = context->oldPlayerState->pmove;
	return newPMove.skim_time != oldPMove.skim_time;
}

bool BunnyHopAction::TryHandlingSkimmingState( Context *context ) {
	Assert( IsSkimmingInAGivenState( context ) );

	// Skip tests while skimming
	// The only exception is testing covered distance to prevent
	// jumping in front of wall contacting it forever updating skim timer
	if( this->SequenceDuration( context ) < 400 ) {
		context->SaveSuggestedActionForNextFrame( this );
		return true;
	}

	if( originAtSequenceStart.SquareDistance2DTo( context->movementState->entityPhysicsState.Origin() ) > SQUARE( 128 ) ) {
		context->SaveSuggestedActionForNextFrame( this );
		return true;
	}

	Debug( "Looks like the bot is stuck and is resetting the skim timer forever by jumping\n" );
	context->SaveSuggestedActionForNextFrame( this );
	return false;
}

bool BunnyHopAction::CheckNavTargetAreaTransition( Context *context ) {
	if( !context->IsInNavTargetArea() ) {
		// If the bot has left the nav target area
		if( hasEnteredNavTargetArea ) {
			if( !hasTouchedNavTarget ) {
				Debug( "The bot has left the nav target area without touching the nav target\n" );
				return false;
			}
			// Otherwise just save the action for next frame.
			// We do not want to fall in a gap after picking a nav target.
		}
		return true;
	}

	hasEnteredNavTargetArea = true;
	if( HasTouchedNavEntityThisFrame( context ) ) {
		hasTouchedNavTarget = true;
		// If there is no truncation frame set yet, we this frame is feasible to mark as one
		if( !mayStopAtAreaNum ) {
			mayStopAtAreaNum = context->NavTargetAasAreaNum();
			mayStopAtStackFrame = (int)context->topOfStackIndex;
			mayStopAtTravelTime = 1;
		}
	}

	if( hasTouchedNavTarget ) {
		return true;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	Vec3 toTargetDir( context->NavTargetOrigin() );
	toTargetDir -= entityPhysicsState.Origin();
	toTargetDir.NormalizeFast();

	Vec3 velocityDir( entityPhysicsState.Velocity() );
	velocityDir *= Q_Rcp( entityPhysicsState.Speed() );
	if( velocityDir.Dot( toTargetDir ) > 0.7f ) {
		return true;
	}

	Debug( "The bot is very likely going to miss the nav target\n" );
	return false;
}

void BunnyHopAction::CheckPredictionStepResults( Context *context ) {
	BaseMovementAction::CheckPredictionStepResults( context );
	if( context->cannotApplyAction || context->isCompleted ) {
		return;
	}

	if( IsSkimmingInAGivenState( context ) ) {
		if( !TryHandlingSkimmingState( context ) ) {
			context->SetPendingRollback();
		}
		return;
	}

	if( !CheckStepSpeedGainOrLoss( context ) ) {
		context->SetPendingRollback();
		return;
	}

	if( !CheckNavTargetAreaTransition( context ) ) {
		context->SetPendingRollback();
		return;
	}

	// This entity physics state has been modified after prediction step
	const auto &newEntityPhysicsState = context->movementState->entityPhysicsState;

	const int currTravelTimeToTarget = context->TravelTimeToNavTarget();
	if( !currTravelTimeToTarget ) {
		if( !TryHandlingUnreachableTarget( context ) ) {
			context->SetPendingRollback();
		}
		return;
	}

	// Reset unreachable target timer
	currentUnreachableTargetSequentialMillis = 0;

	const auto *aasWorld = AiAasWorld::Instance();
	const float squareDistanceFromStart = originAtSequenceStart.SquareDistanceTo( newEntityPhysicsState.Origin() );
	const int groundedAreaNum = context->CurrGroundedAasAreaNum();

	if( currTravelTimeToTarget <= minTravelTimeToNavTargetSoFar ) {
		HandleSameOrBetterTravelTimeToTarget( context, currTravelTimeToTarget, squareDistanceFromStart, groundedAreaNum );
	} else {
		if( !TryHandlingWorseTravelTimeToTarget( context, currTravelTimeToTarget, groundedAreaNum ) ) {
			context->SetPendingRollback();
			return;
		}
	}

	if( squareDistanceFromStart < SQUARE( 64 ) ) {
		if( SequenceDuration( context ) < 384 ) {
			context->SaveSuggestedActionForNextFrame( this );
			return;
		}

		// Prevent wasting CPU cycles on further prediction
		Debug( "The bot still has not covered 64 units yet in 384 millis\n" );
		context->SetPendingRollback();
		return;
	}

	if( TryTerminationOnStopAreaNum( context, currTravelTimeToTarget, groundedAreaNum ) ) {
		return;
	}

	// Try skipping further tests if we have passed an obstacle or have changed Z substantially.
	// This is proven to produce fairly good results.
	if( TryTerminationHavingPassedObstacleOrDeltaZ( context, currTravelTimeToTarget, groundedAreaNum ) ) {
		return;
	}

	// If the bot has not touched ground this frame
	if( !WasOnGroundThisFrame( context ) ) {
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	if( TryTerminationAtBestGroundPosition( context, currTravelTimeToTarget ) ) {
		return;
	}

	// If we have reached here, we are sure we have not:
	// 1) Landed in a "bad" area (BaseMovementAction::CheckPredictionStepResults())
	// 2) Lost a speed significantly, have bumped into wall or bounced back (CheckStepSpeedGainOrLoss())
	// 3) Has deviated significantly from the "best" path/falled down

	// If there were no area (and consequently, frame) marked as suitable for path truncation
	if( !mayStopAtAreaNum ) {
		if( !TryHandlingLackOfStopAreaNum( context, currTravelTimeToTarget, distanceToReachAtStart, groundedAreaNum ) ) {
			context->SetPendingRollback();
		}
		return;
	}

	// See notes for "if we are at the best reached position currently" branch
	if( Distance2DSquared( mayStopAtOrigin, newEntityPhysicsState.Origin() ) < SQUARE( 64 ) ) {
		if( !CheckForActualCompletionOnGround( context ) ) {
			// Looks like the bot is going to bump into a wall
			// without a substantial distance for trajectory correction
			context->SaveSuggestedActionForNextFrame( this );
			return;
		}
	}

	// Consider an attempt successful if we've landed in the same floor cluster and there is no gap to the best position
	if( const int clusterNum = aasWorld->FloorClusterNum( mayStopAtAreaNum ) ) {
		if( clusterNum == aasWorld->FloorClusterNum( groundedAreaNum ) ) {
			if( aasWorld->IsAreaWalkableInFloorCluster( groundedAreaNum, mayStopAtAreaNum ) ) {
				CompleteOrSaveGoodEnoughPath( context, 1000 );
				return;
			}
			// Floor clusters boundaries are not convex so checking whether
			// it's walkable in a straight line is a good idea.
			// However it turned out that we need more permissive checks.
			// This one allows tiny obstacles (that may be jumped over) be in-between these points.
			if( TraceArcInSolidWorld( mayStopAtOrigin, newEntityPhysicsState.Origin() ) ) {
				CompleteOrSaveGoodEnoughPath( context, 2000 );
				return;
			}
		}
	}

	if( CheckDirectReachWalkingOrFallingShort( groundedAreaNum, mayStopAtAreaNum ) ) {
		trace_t trace;
		SolidWorldTrace( &trace, mayStopAtOrigin, newEntityPhysicsState.Origin() );
		if( trace.fraction == 1.0f ) {
			CompleteOrSaveGoodEnoughPath( context, 1000 );
			return;
		}
	}

	// Allow termination under these conditions but apply a huge penalty
	// 1) The bot has initially advanced to target (only few frames of a trajectory really get used before its invalidation)
	// 2) The bot is in a NOFALL area at the moment of termination
	// 3) The initial area and the closest to target areas were NOFALL areas
	if( minTravelTimeToNavTargetSoFar && minTravelTimeToNavTargetSoFar != travelTimeAtSequenceStart ) {
		const auto *const aasAreaSettings = aasWorld->AreaSettings();
		const auto isBadArea = [=]( int num ) { return !( aasAreaSettings[num].areaflags & AREA_NOFALL ); };
		const int testedAreas[3] = { groundedAreaNum, groundedAreaAtSequenceStart, minTravelTimeAreaNumSoFar };
		if( std::find_if( std::begin( testedAreas ), std::end( testedAreas ), isBadArea ) == std::end( testedAreas ) ) {
			CompleteOrSaveGoodEnoughPath( context, 5000 );
			return;
		}
	}

	// Stop wasting CPU cycles at this.
	context->SetPendingRollback();
}

bool BunnyHopAction::TryTerminationOnStopAreaNum( Context *context, int currTravelTimeToTarget, int groundedAreaNum ) {
	if( !groundedAreaNum ) {
		return false;
	}

	auto iter = std::find( checkStopAtAreaNums.begin(), checkStopAtAreaNums.end(), groundedAreaNum );
	if( iter == checkStopAtAreaNums.end() ) {
		return false;
	}

	// Stop prediction having touched the ground this frame in this kind of area
	if( WasOnGroundThisFrame( context ) ) {
		// Ignore bumping into walls if we are very likely in stairs-like environment.
		// Bots have significant movement troubles in this case.
		if( HasSubstantiallyChangedZ( context->movementState->entityPhysicsState ) ) {
			CompleteOrSaveGoodEnoughPath( context );
			return true;
		}

		if( CheckForActualCompletionOnGround( context ) ) {
			CompleteOrSaveGoodEnoughPath( context );
			return true;
		}

		return false;
	}

	// Prevent termination in air unless we're currently at the best position
	if( groundedAreaNum && currTravelTimeToTarget && currTravelTimeToTarget == minTravelTimeToNavTargetSoFar ) {
		const auto *aasWorld = AiAasWorld::Instance();
		const auto *const aasAreaFloorClusterNums = aasWorld->AreaFloorClusterNums();
		// If the area is in a floor cluster, we can perform a cheap and robust 2D raycasting test
		// that should be preferred for AREA_NOFALL areas as well.
		if( const int floorClusterNum = aasAreaFloorClusterNums[groundedAreaNum] ) {
			if( CheckForPrematureCompletionInFloorCluster( context, groundedAreaNum, floorClusterNum ) ) {
				// Allow completion but apply an additional penalty (we're in air and the landing was not checked)
				CompleteOrSaveGoodEnoughPath( context, 300 );
				return true;
			}
		} else if( aasWorld->AreaSettings()[groundedAreaNum].areaflags & AREA_NOFALL ) {
			// We have decided still perform additional checks in this case.
			// (the bot is in a "check stop at area num" area and is in a "no-fall" area but is in air).
			// Bumping into walls on high speed is the most painful issue.
			if( GenericCheckForPrematureCompletion( context ) ) {
				// Allow completion but apply a substantial additional penalty (we're in air and the landing was not checked)
				CompleteOrSaveGoodEnoughPath( context, 600 );
				return true;
			}
			// Can't say much, lets continue prediction
		}
	}

	if( mayStopAtAreaNum ) {
		return false;
	}

	mayStopAtAreaNum = groundedAreaNum;
	mayStopAtStackFrame = (int) context->topOfStackIndex;
	mayStopAtTravelTime = context->TravelTimeToNavTarget();
	return false;
}

bool BunnyHopAction::TryTerminationHavingPassedObstacleOrDeltaZ( Context *context,
																 int currTravelTimeToTarget,
																 int groundedAreaNum ) {
	if( !groundedAreaNum ) {
		return false;
	}

	// Must be at the best reached area currently
	if( currTravelTimeToTarget != minTravelTimeToNavTargetSoFar ) {
		return false;
	}

	const auto *aasWorld = AiAasWorld::Instance();
	// The current grounded area must be a NOFALL area.
	const bool wasOnGround = WasOnGroundThisFrame( context );
	if( !( aasWorld->AreaSettings()[groundedAreaNum].areaflags & AREA_NOFALL ) ) {
		if( !wasOnGround ) {
			return false;
		}
	}

	// Check whether we have sufficiently advanced to target
	if( currTravelTimeToTarget + 15 > travelTimeAtSequenceStart ) {
		return false;
	}

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	if( HasSubstantiallyChangedZ( entityPhysicsState ) ) {
		// Allow completion in this case but apply a substantial penalty in addition to delta with min travel time
		CompleteOrSaveGoodEnoughPath( context, wasOnGround ? 300 : 1000 );
		return true;
	}

	// Try rejecting the expensive collision call by cheaper cluster walkability tests
	if( aasWorld->IsAreaWalkableInFloorCluster( groundedAreaAtSequenceStart, groundedAreaNum ) ) {
		return false;
	}

	if( !( aasWorld->AreaSettings()[groundedAreaNum].areaflags & AREA_NOFALL ) ) {
		return false;
	}

	trace_t trace;
	// This is intended to check for corners.
	// This turned out to detect small barriers and produce good bot behaviour results as well.
	SolidWorldTrace( &trace, originAtSequenceStart.Data(), entityPhysicsState.Origin() );
	if( trace.fraction == 1.0f ) {
		return false;
	}

	CompleteOrSaveGoodEnoughPath( context, 500 );
	return true;
}

bool BunnyHopAction::TryHandlingLackOfStopAreaNum( Context *context,
												   int currTravelTimeToTarget,
												   float squareDistanceFromStart,
												   int groundedAreaNum ) {
	constexpr unsigned maxStepsLimit = ( 7 * Context::MAX_PREDICTED_STATES ) / 8;
	static_assert( maxStepsLimit + 1 < Context::MAX_PREDICTED_STATES, "" );
	// If we have not reached prediction limits
	if( squareDistanceFromStart < SQUARE( 192 ) && context->topOfStackIndex < maxStepsLimit ) {
		context->SaveSuggestedActionForNextFrame( this );
		return true;
	}

	return false;
}

bool BunnyHopAction::TryTerminationAtBestGroundPosition( Context *context, int currTravelTimeToTarget ) {
	// Skip if the travel time at sequence start is undefined
	if( !travelTimeAtSequenceStart ) {
		return false;
	}
	// We must be at best reached position currently to use this
	if( currTravelTimeToTarget != minTravelTimeToNavTargetSoFar ) {
		return false;
	}
	// If we still are in the same start area
	if( currTravelTimeToTarget == travelTimeAtSequenceStart ) {
		return TryTerminationAtGroundInSameStartArea( context );
	}

	// This condition must be held if the curr travel time is the min travel time at start
	assert( currTravelTimeToTarget < travelTimeAtSequenceStart );

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	// Skip way too restrictive CheckForActualCompletionOnGround() call
	// if we are going to truncate trajectory at mayStopAtStackFrame
	// and we have a substantial distance for further trajectory correction
	// (if the distance to the trajectory truncation origin is above the threshold)
	if( mayStopAtAreaNum && Distance2DSquared( mayStopAtOrigin, entityPhysicsState.Origin() ) > SQUARE( 40 ) ) {
		CompleteOrSaveGoodEnoughPath( context );
		return true;
	}

	if( HasSubstantiallyChangedZ( entityPhysicsState ) ) {
		CompleteOrSaveGoodEnoughPath( context );
		return true;
	}

	if( CheckForActualCompletionOnGround( context ) ) {
		CompleteOrSaveGoodEnoughPath( context );
		return true;
	}

	return false;
}

bool BunnyHopAction::TryTerminationAtGroundInSameStartArea( Context *context ) {
	const int reachNum = context->NextReachNum();
	// Skip if we're seemingly in the nav target area
	if( !reachNum ) {
		return false;
	}

	const auto &reach = AiAasWorld::Instance()->Reachabilities()[reachNum];
	const auto &entityPhysicsState = context->movementState->entityPhysicsState;

	Vec3 dirToReach( reach.start );
	dirToReach -= entityPhysicsState.Origin();
	const float squareDistanceToReach = dirToReach.SquaredLength();
	if( squareDistanceToReach < 1 ) {
		return false;
	}

	const float invDistanceToReach = Q_RSqrt( squareDistanceToReach );
	const float distanceToReach = squareDistanceToReach * invDistanceToReach;
	// Check whether we have shortened a distance to a next reach. sufficiently.
	// This condition also consequently rejects termination being in tiny/junk areas.
	if( distanceToReach + 64.0f > this->distanceToReachAtStart ) {
		return false;
	}

	const float speed2D = entityPhysicsState.Speed2D();
	if( speed2D < 100 ) {
		return false;
	}

	Vec3 velocityDir2D( entityPhysicsState.Velocity()[0], entityPhysicsState.Velocity()[1], 0.0f );
	velocityDir2D *= Q_Rcp( speed2D );
	dirToReach *= invDistanceToReach;
	// The dir to reach is approximately is in XY plane for "good" reachabilities. Don't bother projecting it.
	const float dot = velocityDir2D.Dot( dirToReach );
	// We are in the same area so these conditions hold:
	// Being far from reach means the dot product only has to be positive
	// Being close to reach means we should aiming straight at its beginning
	const float proximityFactor = 1.0f - BoundedFraction( distanceToReach, 48.0f );
	if( dot < 0.1f + 0.8f * proximityFactor ) {
		return false;
	}

	// Apply a penalty varying of dot product
	CompleteOrSaveGoodEnoughPath( context, (unsigned)( 500.0f * ( 1.0f - dot ) ) );
	return true;
}

bool BunnyHopAction::GenericCheckForPrematureCompletion( Context *context ) {
	const auto &newEntityPhysicsState = context->movementState->entityPhysicsState;

	// Interpolate origin using full (non-2D) velocity
	Vec3 velocityDir( newEntityPhysicsState.Velocity() );
	velocityDir *= 1.0f / newEntityPhysicsState.Speed();
	Vec3 xerpPoint( velocityDir );
	float checkDistanceLimit = 48.0f + 72.0f * BoundedFraction( newEntityPhysicsState.Speed2D(), 750.0f );
	xerpPoint *= 2.0f * checkDistanceLimit;
	float timeSeconds = sqrtf( Distance2DSquared( xerpPoint.Data(), vec3_origin ) );
	timeSeconds /= newEntityPhysicsState.Speed2D();
	xerpPoint += newEntityPhysicsState.Origin();
	xerpPoint.Z() -= 0.5f * level.gravity * timeSeconds * timeSeconds;

	trace_t trace;
	SolidWorldTrace( &trace, newEntityPhysicsState.Origin(), xerpPoint.Data() );
	// Check also contents for sanity
	const auto badContents = CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_DONOTENTER;
	if( trace.fraction == 1.0f || ( trace.contents & badContents ) ) {
		return false;
	}

	const float minPermittedZ = newEntityPhysicsState.Origin()[2] - newEntityPhysicsState.HeightOverGround() - 16.0f;
	if( trace.endpos[2] < minPermittedZ ) {
		return false;
	}

	if( ISWALKABLEPLANE( &trace.plane ) ) {
		return true;
	}

	Vec3 firstHitPoint( trace.endpos );
	Vec3 firstHitNormal( trace.plane.normal );

	// Check ground below. AREA_NOFALL detection is still very lenient.

	Vec3 start( trace.endpos );
	start += trace.plane.normal;
	Vec3 end( start );
	end.Z() -= 64.0f;
	SolidWorldTrace( &trace, start.Data(), end.Data() );
	if( trace.fraction == 1.0f || ( trace.contents & badContents ) ) {
		return false;
	}

	if( trace.endpos[2] < minPermittedZ ) {
		return false;
	}

	// We surely have some time for maneuvering in this case
	if( firstHitPoint.SquareDistance2DTo( newEntityPhysicsState.Origin() ) > SQUARE( checkDistanceLimit ) ) {
		return true;
	}

	return firstHitNormal.Dot( velocityDir ) > -0.3f;
}

bool BunnyHopAction::CheckForPrematureCompletionInFloorCluster( Context *context,
																int currGroundedAreaNum,
																int floorClusterNum ) {
	const auto &newEntityPhysicsState = context->movementState->entityPhysicsState;
	const Vec3 currVelocity( newEntityPhysicsState.Velocity() );

	const float heightOverGround = newEntityPhysicsState.HeightOverGround();
	if( !std::isfinite( heightOverGround ) ) {
		return false;
	}
	// Almost landed in the "good" area
	if( heightOverGround < 1.0f ) {
		return true;
	}
	// The bot is going to land in the target area
	const float speed2D = newEntityPhysicsState.Speed2D();
	if( speed2D < 1.0f ) {
		return true;
	}

	const float gravity = level.gravity;
	const float currVelocityZ = currVelocity.Z();
	// Assuming the 2D velocity remains the same (this is not true but is an acceptable approximation)
	// the quadratic equation is
	// (0.5 * gravity) * timeTillLanding^2 - currVelocityZ * timeTillLanding - heightOverGround = 0
	// A = 0.5 * gravity (the gravity conforms to the landing direction)
	// B = -currVelocityZ (the current Z velocity contradicts the landing direction if positive)
	// C = -heightOverGround
	const float d = currVelocityZ * currVelocityZ + 4.0f * ( 0.5f * gravity ) * heightOverGround;
	// The bot must always land on the floor cluster plane
	assert( d >= 0 );

	const float sqd = std::sqrt( d );
	float timeTillLanding = ( currVelocityZ - sqd ) / ( 2 * ( 0.5f * gravity ) );
	if( timeTillLanding < 0 ) {
		timeTillLanding = ( currVelocityZ + sqd ) / ( 2 * ( 0.5f * gravity ) );
	}
	assert( timeTillLanding >= 0 );
	// Don't extrapolate more than for 1 second
	if( timeTillLanding > 1.0f ) {
		return false;
	}

	Vec3 landingPoint( currVelocity );
	landingPoint.Z() = 0;
	// Now "landing point" contains a 2D velocity.
	// Scale it by the time to get the shift.
	landingPoint *= timeTillLanding;
	// Convert the spatial shift to an absolute origin.
	landingPoint += newEntityPhysicsState.Origin();
	// The heightOverGround is a height of bot feet over ground
	// Lower the landing point to the ground
	landingPoint.Z() += playerbox_stand_mins[2];
	// Add few units above the ground plane for AAS sampling
	landingPoint.Z() += 4.0f;

	const auto *aasWorld = AiAasWorld::Instance();
	const int landingAreaNum = aasWorld->PointAreaNum( landingPoint.Data() );
	// If it's the same area
	if( landingAreaNum == currGroundedAreaNum ) {
		return true;
	}

	// If the extrapolated origin is in another floor cluster (this condition cuts off being in solid too)
	if( aasWorld->AreaFloorClusterNums()[landingAreaNum] != floorClusterNum ) {
		return false;
	}

	// Perform 2D raycast in a cluster to make sure we don't leave it/hit solid (a cluster is not a convex poly)
	return aasWorld->IsAreaWalkableInFloorCluster( currGroundedAreaNum, landingAreaNum );
}

void BunnyHopAction::OnApplicationSequenceStarted( Context *context ) {
	BaseMovementAction::OnApplicationSequenceStarted( context );
	context->MarkSavepoint( this, context->topOfStackIndex );

	minTravelTimeToNavTargetSoFar = std::numeric_limits<int>::max();
	minTravelTimeAreaNumSoFar = 0;

	checkStopAtAreaNums.clear();

	mayStopAtAreaNum = 0;
	mayStopAtStackFrame = -1;
	mayStopAtTravelTime = 0;

	travelTimeAtSequenceStart = 0;
	reachAtSequenceStart = 0;
	groundedAreaAtSequenceStart = context->CurrGroundedAasAreaNum();

	sequencePathPenalty = 0;

	const auto &entityPhysicsState = context->movementState->entityPhysicsState;
	originAtSequenceStart.Set( entityPhysicsState.Origin() );

	distanceToReachAtStart = std::numeric_limits<float>::infinity();

	if( context->NavTargetAasAreaNum() ) {
		int reachNum, travelTime;
		context->NextReachNumAndTravelTimeToNavTarget( &reachNum, &travelTime );
		if( travelTime ) {
			minTravelTimeToNavTargetSoFar = travelTime;
			travelTimeAtSequenceStart = travelTime;
			reachAtSequenceStart = reachNum;
			if( reachNum ) {
				const auto &reach = AiAasWorld::Instance()->Reachabilities()[reachNum];
				distanceToReachAtStart = originAtSequenceStart.DistanceTo( reach.start );
			}
		}
	}

	const float heightOverGround = entityPhysicsState.HeightOverGround();
	if( std::isfinite( heightOverGround ) ) {
		groundZAtSequenceStart = originAtSequenceStart.Z() - heightOverGround + playerbox_stand_mins[2];
	} else {
		groundZAtSequenceStart = std::numeric_limits<float>::infinity();
	}

	currentSpeedLossSequentialMillis = 0;
	currentUnreachableTargetSequentialMillis = 0;

	hasEnteredNavTargetArea = false;
	hasTouchedNavTarget = false;
}

void BunnyHopAction::OnApplicationSequenceStopped( Context *context,
												   SequenceStopReason reason,
												   unsigned stoppedAtFrameIndex ) {
	BaseMovementAction::OnApplicationSequenceStopped( context, reason, stoppedAtFrameIndex );

	if( reason != FAILED ) {
		ResetObstacleAvoidanceState();
		if( reason != DISABLED ) {
			this->disabledForApplicationFrameIndex = std::numeric_limits<unsigned>::max();
		}
		return;
	}

	// If the action has been disabled due to prediction stack overflow
	if( this->isDisabledForPlanning ) {
		return;
	}

	if( !supportsObstacleAvoidance ) {
		// However having shouldTryObstacleAvoidance flag is legal (it should be ignored in this case).
		// Make sure THIS method logic (that sets isTryingObstacleAvoidance) works as intended.
		Assert( !isTryingObstacleAvoidance );
		// Disable applying this action after rolling back to the savepoint
		this->disabledForApplicationFrameIndex = context->savepointTopOfStackIndex;
		return;
	}

	if( !isTryingObstacleAvoidance && shouldTryObstacleAvoidance ) {
		// Try using obstacle avoidance after rolling back to the savepoint
		// (We rely on skimming for the first try).
		isTryingObstacleAvoidance = true;
		// Make sure this action will be chosen again after rolling back
		context->SaveSuggestedActionForNextFrame( this );
		return;
	}

	// Disable applying this action after rolling back to the savepoint
	this->disabledForApplicationFrameIndex = context->savepointTopOfStackIndex;
	this->ResetObstacleAvoidanceState();
}

void BunnyHopAction::BeforePlanning() {
	BaseMovementAction::BeforePlanning();
	this->disabledForApplicationFrameIndex = std::numeric_limits<unsigned>::max();
	ResetObstacleAvoidanceState();
}