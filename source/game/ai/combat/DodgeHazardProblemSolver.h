#ifndef QFUSION_DODGEHAZARDPROBLEMSOLVER_H
#define QFUSION_DODGEHAZARDPROBLEMSOLVER_H

#include "TacticalSpotsProblemSolver.h"

class DodgeHazardProblemSolver: public TacticalSpotsProblemSolver {
public:
	class ProblemParams : public BaseProblemParams {
		friend class DodgeHazardProblemSolver;
		const Vec3 &hazardHitPoint;
		const Vec3 &hazardDirection;
		const bool avoidSplashDamage;
	public:
		ProblemParams( const Vec3 &hazardHitPoint_, const Vec3 &hazardDirection_, bool avoidSplashDamage_ )
			: hazardHitPoint( hazardHitPoint_ )
			, hazardDirection( hazardDirection_ )
			, avoidSplashDamage( avoidSplashDamage_ ) {}
	};
private:
	const ProblemParams &problemParams;

	/**
	 * Makes a dodge hazard direction for given {@code ProblemParams}
	 * @return a pair of a direction and a flag indicating whether the direction is allowed to be negated.
	 */
	std::pair<Vec3, bool> MakeDodgeHazardDir() const;

	SpotsAndScoreVector &SelectCandidateSpots( const SpotsQueryVector &spotsFromQuery ) override;
	OriginAndScoreVector &SelectFallbackSpotLikeOrigins( const SpotsQueryVector &spotsFromQuery );

	template <typename VectorWithScores>
	void ModifyScoreByVelocityConformance( VectorWithScores &input );
public:
	DodgeHazardProblemSolver( const OriginParams &originParams_, const ProblemParams &problemParams_ )
		: TacticalSpotsProblemSolver( originParams_, problemParams_ ), problemParams( problemParams_ ) {}

	int FindMany( vec3_t *spots, int numSpots ) override;
};

#endif
