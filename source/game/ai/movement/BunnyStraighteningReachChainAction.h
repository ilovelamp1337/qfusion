#ifndef QFUSION_BUNNYSTRAIGHTENINGREACHCHAINACTION_H
#define QFUSION_BUNNYSTRAIGHTENINGREACHCHAINACTION_H

#include "BunnyTestingMultipleLookDirsAction.h"

class BunnyStraighteningReachChainAction final : public BunnyTestingSavedLookDirsAction {
	static constexpr const char *NAME = "BunnyStraighteningReachChainAction";

	friend class BunnyToBestNavMeshPointAction;

	void SaveSuggestedLookDirs( MovementPredictionContext *context ) override;
public:
	explicit BunnyStraighteningReachChainAction( BotMovementModule *module_ );

	void BeforePlanning() override;
};

#endif
