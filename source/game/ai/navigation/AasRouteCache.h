#ifndef QFUSION_AI_ROUTE_CACHE_H
#define QFUSION_AI_ROUTE_CACHE_H

#include "AasWorld.h"
#include "../ai_local.h"

//travel flags
#define TFL_INVALID             0x00000001  //traveling temporary not possible
#define TFL_WALK                0x00000002  //walking
#define TFL_CROUCH              0x00000004  //crouching
#define TFL_BARRIERJUMP         0x00000008  //jumping onto a barrier
#define TFL_JUMP                0x00000010  //jumping
#define TFL_LADDER              0x00000020  //climbing a ladder
#define TFL_WALKOFFLEDGE        0x00000080  //walking of a ledge
#define TFL_SWIM                0x00000100  //swimming
#define TFL_WATERJUMP           0x00000200  //jumping out of the water
#define TFL_TELEPORT            0x00000400  //teleporting
#define TFL_ELEVATOR            0x00000800  //elevator
#define TFL_ROCKETJUMP          0x00001000  //rocket jumping
#define TFL_BFGJUMP             0x00002000  //bfg jumping
#define TFL_GRAPPLEHOOK         0x00004000  //grappling hook
#define TFL_DOUBLEJUMP          0x00008000  //double jump
#define TFL_RAMPJUMP            0x00010000  //ramp jump
#define TFL_STRAFEJUMP          0x00020000  //strafe jump
#define TFL_JUMPPAD             0x00040000  //jump pad
#define TFL_AIR                 0x00080000  //travel through air
#define TFL_WATER               0x00100000  //travel through water
#define TFL_SLIME               0x00200000  //travel through slime
#define TFL_LAVA                0x00400000  //travel through lava
#define TFL_DONOTENTER          0x00800000  //travel through donotenter area
#define TFL_FUNCBOB             0x01000000  //func bobbing
#define TFL_FLIGHT              0x02000000  //flight
#define TFL_BRIDGE              0x04000000  //move over a bridge
//
#define TFL_NOTTEAM1            0x08000000  //not team 1
#define TFL_NOTTEAM2            0x10000000  //not team 2

//default travel flags
#define TFL_DEFAULT TFL_WALK | TFL_CROUCH | TFL_BARRIERJUMP | \
	TFL_JUMP | TFL_LADDER | \
	TFL_WALKOFFLEDGE | TFL_SWIM | TFL_WATERJUMP | \
	TFL_TELEPORT | TFL_ELEVATOR | \
	TFL_AIR | TFL_WATER | TFL_JUMPPAD | TFL_FUNCBOB

class AiAasRouteCache {
	/**
	 * An array of two elements: preferred and allowed travel flags that are used by default
	 */
	const int *const travelFlags;
	/**
	 * Used to provide a dummy writable address for several routing calls
	 * where we do not want to add extra branching for every call if an out parameter is unused.
	 */
	mutable int dummyIntPtr[1];

	template<typename T> friend T *Link( T *, T ** );
	template<typename T> friend T *Unlink( T *, T ** );

	/**
	 * Links for maintaining a list of all instances of the class.
	 */
	AiAasRouteCache *prev { nullptr }, *next { nullptr };

	/**
	 * An md5 digest of a boolean array of blocked areas status.
	 * If two instances have matching digest it's very likely
	 * that blocked areas vector is the same
	 * (so we can read from a routing cache of other instance).
	 * We don't care about collisions since they're harmless in this case.
	 */
	uint64_t blockedAreasDigest[2];

	/**
	 * A default digest for statically blocked areas in the AAS world
	 * (some areas may have AREA_DISABLED intrinsic flag from the beginning).
	 */
	static uint64_t defaultBlockedAreasDigest[2];

	static constexpr unsigned short CACHETYPE_PORTAL = 0;
	static constexpr unsigned short CACHETYPE_AREA = 1;

	struct AreaOrPortalCacheTable {
		// TODO:
		AreaOrPortalCacheTable *prev, *next;
		// TODO:
		AreaOrPortalCacheTable *time_prev, *time_next;

		/**
		 * Travel times to every area in cluster
		 */
		uint16_t *travelTimes;
		/**
		 * Offsets of reach. to every area in cluster
		 */
		uint8_t *reachOffsets;
		/**
		 * A total size of allocated data this struct serves as a header
		 */
		uint32_t size;
		/**
		 * AAS travel flags
		 */
		int travelFlags;
		/**
		 * Travel time to start with
		 */
		int startTravelTime;
		/**
		 * A number of cluster the cache is for
		 */
		uint16_t cluster;
		/**
		 * A global number of area the cache is for (not the number of area within cluster)
		 */
		uint16_t areaNum;
		/**
		 * A usage of this area
		 * @todo link area and cluster caches to different lists
		*/
		uint16_t type;

		/**
		 * A helper low-level method for setting {@code travelTimes}, {@code reachOffsets} refs relative to {@code this}.
		 * @param numTravelTimes a length of {@code travelTimes} (and {@code reachOffsets}) arrays that refs point to.
		 */
		inline void FixVarLenDataRefs( int numTravelTimes );
		/**
		 * A helper low-level method for setting fields of a partially constructed or copied object.
		 * @param cluster_ a value to set for {@code cluster} field.
		 * @param areaNum_ a value to set for {@code areaNum} field.
		 * @param travelFlags_ a value to set for {@code travelFlags} field.
		 * @note resets {@code startTravelTime} to {@code 1} as well.
		 */
		inline void SetPathFindingProps( int cluster_, int areaNum_, int travelFlags_ );
	};

	struct PathFinderNode {
		const uint16_t *areaTravelTimes;             // travel times within the area
		uint16_t cluster;
		uint16_t areaNum;                            // area number of the node
		uint16_t tmpTravelTime;                      // temporary travel time
		int8_t dijkstraLabel;
	};

	struct alignas( 2 )RevLink {
		/**
		 * An index of the next rev. link in its storage ({@code aasRevLinks}).
		 */
		uint16_t nextLink;
		/**
		 * An index of the corresponding reachability in its storage {@code AiAasWorld::Reachabilities()}
		 */
		uint16_t linkNum;
		/**
		 * An index of area reachable from the area this rev. link is associated with.
		 */
		uint16_t areaNum;
	};

	static_assert( alignof( RevLink ) == 2, "Alignment assumptions are broken" );
	static_assert( sizeof( RevLink ) == 6, "Size assumptions are broken" );

	struct alignas( 2 )RevReach {
		/**
		 * An index of the first RevLink in its storage ({@code aasRevLinks}).
		 * @note {@code aasRevLinks} have size equal to number of all reachabilities,
		 * and that number is not going to exceed 2^16 in supported AAS versions).
		 */
		uint16_t firstRevLink;
		/**
		 * A total number of rev. links in the rev. links chain
		 */
		uint16_t numLinks;
	};

	static_assert( alignof( RevReach ) == 2, "Alignment assumptions are broken" );
	static_assert( sizeof( RevReach ) == 4, "Size assumptions are broken" );

	const AiAasWorld &aasWorld;

	bool loaded { false };

	/**
	 * It is sufficient to fit all required info in 2 bits, but we should avoid using bitsets
	 * since variable shifts are required for access patterns used by implemented algorithms
	 * and variable shift instructions are usually microcoded.
	 */
	struct alignas( 1 )AreaDisabledStatus {
		uint8_t value;

		// We hope a compiler avoids using branches here
		bool OldStatus() const { return (bool)( ( value >> 1 ) & 1 ); }
		bool CurrStatus() const { return (bool)( ( value >> 0 ) & 1 ); }

		// Also we hope a compiler eliminates branches for a known constant status
		void SetOldStatus( bool status ) {
			status ? ( value |= 2 ) : ( value &= ~2 );
		}

		void SetCurrStatus( bool status ) {
			status ? ( value |= 1 ) : ( value &= ~1 );
		}

		// Copies curr status to old status and clears the curr status
		void ShiftCurrToOldStatus() {
			// Clear 6 high bits to avoid confusion
			value &= 3;
			// Promote the curr bit to the old bit position
			value <<= 1;
		}
	};

	/**
	 * A compact representation of all AAS area related fields needed for a routing algorithm.
	 * This allows to keep and address all area related data together during the routing algorithm execution.
	 */
	struct alignas( 4 )AreaPathFindingData {
		/**
		 * A computed result of ClusterAreaNum() call
		 */
		uint16_t clusterAreaNum;
		/**
		 * A copy of {@code firstreachablearea} from {@code areasettings_t} for an area
		 */
		uint16_t firstReachNum;
		/**
		 * 	Usually there is something like 5-6 clusters/portals in AAS world.
		 * 	The sign of the value determines whether it refers to a cluster or a portal.
		 */
		int8_t clusterOrPortalNum;
		/**
		 * Indicates whether this area should be temporarily excluded from routing.
		 * @note inlining this field here is not that efficient for {@code SetDisabledZones()}
		 * but routing algorithm is much more stressful so it has a priority.
		 */
		AreaDisabledStatus disabledStatus;
	};

	// Note: Using Int32Align2 won't give a substantial win (only 2 bytes that are wasted for alignment)
	static_assert( sizeof( AreaPathFindingData ) == 8, "The struct size assumptions are broken" );

	/**
	 * As the areas exclusion status is individual for a bot and it must be preserved between frames,
	 * every bot must have its own copy of this data.
	 */
	AreaPathFindingData *areaPathFindingData;

	/**
	 * A compact representation of all AAS reachability related fields needed for routing algorithm.
	 * This allows to keep and address all reachability related data together during the routing algorithm execution.
	 */
	struct alignas( 2 )ReachPathFindingData {
		/**
		 * Travel flags corresponding to a travel type of this reachability.
		 * Precomputation of these travel flags allows to eliminate necessity in
		 * accessing old "travel flag for type" lookup table during the routing algorithm execution
		 */
		Int32Align2 travelFlags;
		/**
		 * A copy of the travel time of this reachability.
		 */
		uint16_t travelTime;
	};

	// By using Int32Align2 we win 1/4 of the storage size, and it is fairly good reason to do that
	static_assert( sizeof( ReachPathFindingData ) == 6, "The struct size assumptions are broken" );

	/**
	 * See {@code ReachPathFindindData}
	 * This is a beginning of the buffer shared among bots.
	 * The data is either immutable or used for temporaries unused between frames.
	 */
	ReachPathFindingData *reachPathFindingData;

	mutable PathFinderNode *areaPathFindingNodes;
	mutable PathFinderNode *portalPathFindingNodes;

	RevReach *aasRevReach;
	// Allocated within aasRevReach, no need to free this
	RevLink *aasRevLinks;
	int maxReachAreas;

	uint16_t ***areaTravelTimes;

	AreaOrPortalCacheTable ***clusterAreaCache;
	AreaOrPortalCacheTable **portalCache;

	AreaOrPortalCacheTable *oldestCache;        // start of cache list sorted on time
	AreaOrPortalCacheTable *newestCache;        // end of cache list sorted on time

	int *portalMaxTravelTimes;

	// We have to waste 8 bytes for the ref count since blocks should be at least 8-byte aligned
	inline static const int64_t RefCountOf( const void *chunk ) { return *( ( (int64_t *)chunk ) - 1 ); }
	inline static int64_t &RefCountOf( void *chunk ) { return *( ( (int64_t *)chunk ) - 1 ); }

	template <typename T>
	inline static T *AddRef( T *chunk ) {
		RefCountOf( chunk )++;
		return chunk;
	}

	inline void *GetClearedRefCountedMemory( size_t size ) {
		void *mem = ( (int64_t *)GetClearedMemory( size + 8 ) ) + 1;
		RefCountOf( mem ) = 1;
		return mem;
	}

	inline void FreeRefCountedMemory( void *ptr ) {
		--RefCountOf( ptr );
		if( !RefCountOf( ptr ) ) {
			FreeMemory( ( (int64_t *)ptr ) - 1 );
		}
	}

	// A linked list for bins of relatively large size
	class AreaAndPortalCacheAllocatorBin *areaAndPortalCacheHead { nullptr };
	// A table of small size bins addressed by bin size
	class AreaAndPortalCacheAllocatorBin *areaAndPortalSmallBinsTable[128] { nullptr };

	class ResultCache {
public:
		static constexpr unsigned MAX_CACHED_RESULTS = 512;
		/**
		 * A prime number. We have increased it since bin pointers have been replaced by short integers
		 * but not very much since we would not win in space and thus in CPU cache efficiency)
		 */
		static constexpr unsigned NUM_HASH_BINS = 1181;

		struct alignas( 8 )Node {
			// Indices for ::Link() and ::Unlink().
			// Now they are put first so if links are touched the key is more likely to be on the same cache line.
			int16_t prev[2];
			int16_t next[2];

			enum { BIN_LINKS, LIST_LINKS };

			// Links consume 8 bytes, so there should not be an alignment gap for the key
			uint64_t key;

			uint16_t reachability;
			uint16_t travelTime;
			uint16_t binIndex;
		};

		static_assert( sizeof( Node ) <= 24, "The struct size assumptions are broken" );

		// Assuming that area nums are limited by 16 bits, all parameters can be composed in a single integer
		static inline uint64_t Key( int fromAreaNum, int toAreaNum, int travelFlags ) {
			assert( fromAreaNum >= 0 && fromAreaNum <= 0xFFFF );
			assert( toAreaNum >= 0 && toAreaNum <= 0xFFFF );
			return ( (uint64_t)travelFlags << 32 ) | ( (uint16_t)fromAreaNum << 16 ) | ( (uint16_t)toAreaNum );
		}

		static inline uint16_t BinIndexForKey( uint64_t key ) {
			// Convert a 64-bit key to 32-bit hash trying to preserve bits entropy.
			// The primary purpose of it is avoiding 64-bit division in modulo computation
			constexpr uint32_t mask32 = 0xFFFFFFFFu;
			uint32_t loPart32 = (uint32_t)( key & mask32 );
			uint32_t hiPart32 = (uint32_t)( ( key >> 32 ) & mask32 );
			uint32_t hash = loPart32 * 17 + hiPart32;
			static_assert( NUM_HASH_BINS < 0xFFFF, "Bin indices are assumed to be short" );
			return (uint16_t)( hash % NUM_HASH_BINS );
		}
private:
		Node nodes[MAX_CACHED_RESULTS];
		// We could keep these links as pointers since they do not require a compact storage,
		// but its better to stay uniform and use common link/unlink methods
		int16_t freeNode;
		int16_t newestUsedNode;
		int16_t oldestUsedNode;

		int16_t bins[NUM_HASH_BINS];

		static inline bool IsValidLink( int16_t link ) { return link >= 0; }
		inline int16_t LinkOf( const Node *node ) { return (int16_t)( node - nodes ); }

		// Constexpr usage leads to "symbol not found" crash on library loading.
		enum { NULL_LINK = -1 };

		inline void LinkToHashBin( uint16_t binIndex, Node *node );
		inline void LinkToUsedList( Node *node );

		inline Node *UnlinkOldestUsedNode();
public:
		inline ResultCache() { Clear(); }

		void Clear();

		// The key and bin index must be computed by callers using Key() and BinIndexForKey().
		// This is a bit ugly but encourages efficient usage patterns.
		const Node *GetCachedResultForKey( uint16_t binIndex, uint64_t key ) const;
		Node *AllocAndRegisterForKey( uint16_t binIndex, uint64_t key );
	};

	ResultCache resultCache;

	void LinkCache( AreaOrPortalCacheTable *cache );
	void UnlinkCache( AreaOrPortalCacheTable *cache );

	void FreeRoutingCache( AreaOrPortalCacheTable *cache );

	void *GetClearedMemory( size_t size );
	void FreeMemory( void *ptr );

	void *AllocAreaAndPortalCacheMemory( size_t size );
	void FreeAreaAndPortalCacheMemory( void *ptr );

	void FreeAreaAndPortalMemoryPools();

	bool FreeOldestCache();

	AreaOrPortalCacheTable *AllocRoutingCache( int numTravelTimes, bool zeroMemory = true );

	void UpdateAreaRoutingCache( const aas_areasettings_t *aasAreaSettings,
								 const aas_portal_t *aasPortals,
								 AreaOrPortalCacheTable *areaCache ) const;

	AreaOrPortalCacheTable *GetAreaRoutingCache( const aas_areasettings_t *aasAreaSettings,
												 const aas_portal_t *aasPortals,
												 int clusterNum, int areaNum, int travelFlags );

	const AreaOrPortalCacheTable *FindSiblingCache( int clusterNum, int clusterAreaNum, int travelFlags ) const;

	void UpdatePortalRoutingCache( AreaOrPortalCacheTable *portalCache );

	AreaOrPortalCacheTable *GetPortalRoutingCache( const aas_areasettings_t *aasAreaSettings,
												   const aas_portal_t *aasPortals,
												   int clusterNum, int areaNum, int travelFlags );

	struct RoutingRequest {
		int areaNum;
		int goalAreaNum;
		int travelFlags;

		inline RoutingRequest( int areaNum_, int goalAreaNum_, int travelFlags_ )
			: areaNum( areaNum_ ), goalAreaNum( goalAreaNum_ ), travelFlags( travelFlags_ ) {}
	};

	struct RoutingResult {
		int reachNum;
		int travelTime;
	};

	bool RoutingResultToGoalArea( int fromAreaNum, int toAreaNum, int travelFlags, RoutingResult *result ) const;

	bool RouteToGoalArea( const RoutingRequest &request, RoutingResult *result );
	bool RouteToGoalPortal( const RoutingRequest &request, AreaOrPortalCacheTable *portalCache, RoutingResult *result );

	void InitCompactReachDataAreaDataAndHelpers();
	AreaPathFindingData *CloneAreaPathFindingData();

	void InitPathFindingNodes();
	void CreateReversedReach();
	void InitClusterAreaCache();
	void InitPortalCache();
	void CalculateAreaTravelTimes();
	void InitPortalMaxTravelTimes();

	void ResetAllClusterAreaCache();
	void ResetAllPortalCache();

	void FreeAllClusterAreaCache();
	void FreeAllPortalCache();

	// Should be used only for shared route cache initialization
	explicit AiAasRouteCache( const AiAasWorld &aasWorld_ );
	// Should be used for creation of new instances based on shared one
	AiAasRouteCache( AiAasRouteCache *parent, const int *newTravelFlags );

	static AiAasRouteCache *shared;
	static AiAasRouteCache *instancesHead;

	static void InitTravelFlagFromType();
	static void InitDefaultBlockedAreasDigest( const AiAasWorld &aasWorld );
public:
	// AiRoutingCache should be init and shutdown explicitly
	// (a game library is not unloaded when a map changes)
	static void Init( const AiAasWorld &aasWorld );
	static void Shutdown();

	static AiAasRouteCache *Shared() { return shared; }
	static AiAasRouteCache *NewInstance( const int *travelFlags_ );
	static void ReleaseInstance( AiAasRouteCache *instance );

	// A helper for emplace_back() calls on instances of this class
	//AiAasRouteCache( AiAasRouteCache &&that );
	~AiAasRouteCache();

	inline int ReachabilityToGoalArea( int fromAreaNum, int toAreaNum, int travelFlags ) const {
		RoutingResult result;
		if( RoutingResultToGoalArea( fromAreaNum, toAreaNum, travelFlags, &result ) ) {
			return result.reachNum;
		}
		return 0;
	}

	inline int TravelTimeToGoalArea( int fromAreaNum,int toAreaNum, int travelFlags ) const {
		RoutingResult result;
		if( RoutingResultToGoalArea( fromAreaNum, toAreaNum, travelFlags, &result ) ) {
			return result.travelTime;
		}
		return 0;
	}

	/**
	 * Finds a reachability/travel time to goal area testing preferred and allowed travel flags for the owner
	 * starting from preferred travel flags for the owner and stopping at first feasible result.
	 * Returns non-zero travel time on success and a reachability via the out parameter.
	 */
	int PreferredRouteToGoalArea( int fromAreaNum, int toAreaNum, int *reachNum ) const;
	/**
	 * Tests all specified area nums for each flag before moving to the next one.
	 */
	int PreferredRouteToGoalArea( const int *fromAreaNums, int numFromAreas, int toAreaNum, int *reachNum ) const;
	/**
	 * Finds a reachability/travel time to goal area testing preferred and allowed travel flags for the owner
	 * starting from preferred travel flags for the owner and choosing a best result.
	 * Returns non-zero travel time on success and a reachability via the out parameter.
	 */
	int FastestRouteToGoalArea( int fromAreaNum, int toAreaNum, int *reachNum ) const;
	/**
	 * Returns best results for every combination of "from area" / travel flags.
	 */
	int FastestRouteToGoalArea( const int *fromAreaNums, int numFromAreas, int toAreaNum, int *reachNum ) const;

	// It's better to add separate prototypes than set out pointers to null by default and use branching on every call.
	// We could also set these parameters to an address of some static variable, but it could lead to extra cache misses
	// since all these variables are likely to be scattered in memory.
	// The underlying calls read flags pointer that is very likely on the same cache line the dummyIntPtr is.

	inline int PreferredRouteToGoalArea( int fromAreaNum, int toAreaNum ) const {
		return PreferredRouteToGoalArea( fromAreaNum, toAreaNum, dummyIntPtr );
	}
	inline int PreferredRouteToGoalArea( const int *fromAreaNums, int numFromAreas, int toAreaNum ) const {
		return PreferredRouteToGoalArea( fromAreaNums, numFromAreas, toAreaNum, dummyIntPtr );
	}
	inline int FastestRouteToGoalArea( int fromAreaNum, int toAreaNum ) const {
		return FastestRouteToGoalArea( fromAreaNum, toAreaNum, dummyIntPtr );
	}
	inline int FastestRouteToGoalArea( const int *fromAreaNums, int numFromAreas, int toAreaNum ) const {
		return FastestRouteToGoalArea( fromAreaNums, numFromAreas, toAreaNum, dummyIntPtr );
	}

	inline bool AreaDisabled( int areaNum ) const {
		return areaPathFindingData[areaNum].disabledStatus.CurrStatus();
	}

	struct DisableZoneRequest {
		virtual ~DisableZoneRequest() = default;

		/**
		 * Should mark all areas that considered to be blocked in the supplied buffer.
		 * @warning results are accumulated for multiple requests. Don't clear the buffer.
		 * @param table a buffer that is addressed by area numbers.
		 */
		virtual void FillBlockedAreasTable( bool *__restrict table ) = 0;
	};

	inline void ClearDisabledZones() {
		SetDisabledZones( nullptr, 0 );
	}

	// Pass an array of object references since they are generic non-POD objects having different size/vtbl
	void SetDisabledZones( DisableZoneRequest **requests, int numRequests );
};

#endif
