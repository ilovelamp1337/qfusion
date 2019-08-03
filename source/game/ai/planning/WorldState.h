#ifndef QFUSION_WORLD_STATE_H
#define QFUSION_WORLD_STATE_H

#include "../ai_local.h"

float DamageToKill( float health, float armor, float armorProtection, float armorDegradation );

inline float DamageToKill( float health, float armor ) {
	return DamageToKill( health, armor, g_armor_protection->value, g_armor_degradation->value );
}

class WorldState;

template <typename T, typename This>
class ComparableVar {
	friend class WorldState;
	template <typename> friend struct ZippedIterator;
public:
	enum class SatisfyOp : uint8_t {
		EQ,
		NE,
		GT,
		GE,
		LS,
		LE
	};
protected:
	This *next { nullptr };
	T value { T( 0 ) };
	SatisfyOp satisfyOp { SatisfyOp::EQ };
	bool ignore { false };
public:
	const T &Value() const { return value; }
	operator const T &() const { return value; }
	bool Ignore() const { return ignore; }

	This &SetValue( const T &value_ ) {
		this->value = value_;
		return static_cast<This &>( *this );
	}

	This &SetIgnore( bool ignore_ ) {
		this->ignore = ignore_;
		return static_cast<This &>( *this );
	}

	This &SetSatisfyOp( SatisfyOp satisfyOp_ ) {
		this->satisfyOp = satisfyOp_;
		return static_cast<This &>( *this );
	}

	bool IsSatisfiedBy( const This &that ) const {
		if( ignore ) {
			return true;
		}
		if( that.ignore ) {
			return false;
		}
		switch( satisfyOp ) {
			case SatisfyOp::EQ: return value == that.value;
			case SatisfyOp::NE: return value != that.value;
			case SatisfyOp::GT: return value > that.value;
			case SatisfyOp::GE: return value >= that.value;
			case SatisfyOp::LS: return value < that.value;
			case SatisfyOp::LE: return value <= that.value;
		}
	}

	template <typename P>
	bool IgnoredOr( P predicate ) const {
		return ignore || predicate( value );
	}

	template <typename P>
	bool ImportantAnd( P predicate ) const {
		return !ignore && predicate( value );
	}

	bool IgnoredOrDifferent( const T &value_ ) const {
		return ignore || value != value;
	}

	bool ImportantAndSame( const T &value_ ) const {
		return !ignore && value == value_;
	}

	void CopyFromThat( const This &that ) {
		this->value = that.value;
		this->satisfyOp = that.satisfyOp;
		this->ignore = that.ignore;
	}

	bool operator==( const This &that ) const {
		if( this->ignore ) {
			return that.ignore;
		}
		if( that.ignore ) {
			return false;
		}
		return satisfyOp == that.satisfyOp && value == that.value;
	}
};

class ShortVar : public ComparableVar<short, ShortVar> {
	friend class WorldState;
	explicit ShortVar( WorldState *parent_ );
public:
	void DebugPrint( const char *tag, const char *nameOfThis ) const;

	uint32_t Hash() const {
		if( ignore ) {
			return 0;
		}
		return ( (uint32_t)satisfyOp << 16 ) | (uint32_t)value;
	}
};

class UnsignedVar : public ComparableVar<unsigned, UnsignedVar> {
	friend class WorldState;
	explicit UnsignedVar( WorldState *parent_ );
public:
	void DebugPrint( const char *tag, const char *nameOfThis ) const;

	uint32_t Hash() const {
		static_assert( sizeof( value ) == sizeof( uint32_t ), "The value type is assumed to be the same as hash type" );
		if( ignore ) {
			return 0;
		}
		return (uint32_t)satisfyOp ^ (uint32_t)value;
	}
};

class BoolVar {
	friend class WorldState;
	template <typename> friend struct ZippedIterator;

	BoolVar *next { nullptr };
	bool value { false };
	bool ignore { true };

	explicit BoolVar( WorldState *parent_ );
public:

	bool Value() const { return value; }
	operator bool() const { return value; }
	bool Ignore() const { return ignore; }

	BoolVar &SetValue( bool value_ ) {
		this->value = value_;
		return *this;
	}

	BoolVar &SetIgnore( bool ignore_ ) {
		this->ignore = ignore_;
		return *this;
	}

	bool IsSatisfiedBy( const BoolVar &that ) const {
		if( this->ignore ) {
			return true;
		}
		if( that.ignore ) {
			return false;
		}
		return this->value == that.value;
	}

	bool IgnoredOrTrue() const { return ignore || value; }
	bool IgnoredOrFalse() const { return ignore || !value; }

	bool ImportantAndTrue() const { return !ignore && value; }
	bool ImportantAndFalse() const { return !ignore && !value; }

	void CopyFromThat( const BoolVar &that ) {
		ignore = that.ignore;
		value = that.value;
	}

	void DebugPrint( const char *tag, const char *nameOfThis ) const;

	uint32_t Hash() const {
		return ignore ? 0 : (uint32_t)value + 17;
	}

	bool operator==( const BoolVar &that ) const {
		if( this->ignore ) {
			return that.ignore;
		}
		if( that.ignore ) {
			return false;
		}
		return this->value == that.value;
	}
};

// Stores a 3-dimensional world space origin vector. Dimensions are rounded up to 4 units.
class OriginVar {
	friend class WorldState;
	template <typename> friend struct ZippedIterator;
public:
	enum class SatisfyOp { EQ, NE };
private:
	OriginVar *next { nullptr };
	int16_t data[4] { 0, 0, 0, 0 };

	struct alignas( 2 )PackedFields {
		bool ignore : 1;
		uint8_t satisfyOp : 5;
		uint8_t epsilon : 8;

		bool operator==( const PackedFields &that ) const {
			return *( (const int16_t *)this ) == *( (const int16_t *)&that );
		}
	};

	static_assert( sizeof( PackedFields ) == sizeof( short ), "" );
	static_assert( alignof( PackedFields ) == alignof( short ), "" );

	PackedFields &Packed() {
		return *( (PackedFields *)( &data[3] ) );
	}
	const PackedFields &Packed() const {
		return *( (const PackedFields *)( &data[3] ) );
	}

	explicit OriginVar( WorldState *parent_ );
public:
	// Each coordinate is rounded up to 4 units
	// Thus maximal rounding distance error = sqrt(dx*dx + dy*dy + dz*dz) = sqrt(4*4 + 4*4 + 4*4)
	static constexpr float MAX_ROUNDING_SQUARE_DISTANCE_ERROR = 3 * 4 * 4;

	float DistanceTo( const OriginVar &that ) const;

	Vec3 Value() const {
		return GetUnpacked4uVec( data );
	}

	OriginVar &SetValue( float x, float y, float z ) {
		data[0] = (short)( ( (int)x ) / 4 );
		data[1] = (short)( ( (int)y ) / 4 );
		data[2] = (short)( ( (int)z ) / 4 );
		return *this;
	}

	OriginVar &SetValue( const Vec3 &value ) {
		return SetValue( value.X(), value.Y(), value.Z() );
	}

	OriginVar &SetValue( const vec3_t value ) {
		return SetValue( value[0], value[1], value[2] );
	}

	bool Ignore() const {
		return Packed().ignore;
	}

	OriginVar &SetIgnore( bool ignore ) {
		Packed().ignore = ignore;
		return *this;
	}

	OriginVar &SetSatisfyOp( SatisfyOp op, float epsilon );

	void CopyFromThat( const OriginVar &that ) {
		Vector4Copy( that.data, this->data );
	}

	bool operator==( const OriginVar &that ) const;

	void DebugPrint( const char *tag, const char *nameOfThis ) const;

	uint32_t Hash() const {
		if( Ignore() ) {
			return 0;
		}
		auto *p = (uint32_t *)data;
		// Check the actual alignment as well
		assert( ( ( (uintptr_t)p ) % 4 ) == 0 );
		return p[0] ^ p[1];
	}

	static bool IsOriginSatisfiedBy( int sign, int epsilon, const int16_t *thisData, const int16_t *thatData ) {
		// This is a branch-less (CMOV-friendly) boolean expression for these cases:
		// EQ -> squareDistance <= epsilon * epsilon
		// NE -> squareDistance >= epsilon * epsilon <=> -squareDistance <= -epsilon * epsilon
		int squareDistance = DistanceSquared( thisData, thatData );
		// Epsilon and both origins are packed but their order is preserved under all these transformations (scale, x*x)
		return sign * squareDistance <= sign * epsilon * epsilon;
	}

	bool IsSatisfiedBy( const OriginVar &that ) const {
		if( Ignore() ) {
			return true;
		}
		if( that.Ignore() ) {
			return false;
		}
		int sign = ( (SatisfyOp)Packed().satisfyOp == SatisfyOp::EQ ) ? +1 : -1;
		assert( (SatisfyOp)Packed().satisfyOp == SatisfyOp::EQ || (SatisfyOp)Packed().satisfyOp == SatisfyOp::NE );
		return IsOriginSatisfiedBy( sign, Packed().epsilon, data, that.data );
	}

	bool ImportantAndSame( const Vec3 &value ) const {
		return !Packed().ignore && Value().SquareDistanceTo( value ) <= MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
	}

	template <typename P>
	bool ImportantAnd( P predicate ) const {
		return !Packed().ignore && predicate( Value() );
	}

	bool IgnoredOrDifferent( const Vec3 &value ) const {
		return Packed().ignore || Value().SquareDistanceTo( value ) > MAX_ROUNDING_SQUARE_DISTANCE_ERROR;
	}

	template <typename P>
	bool IgnoredOr( P predicate ) const {
		return Packed().ignore || predicate( Value() );
	}
};

template <typename This>
class OriginLazyVarBase {
	friend class OriginVar;
	friend class WorldState;
	template <typename> friend struct ZippedIterator;
public:
	enum SatisfyOp { EQ, NE };

	typedef const short *(WorldState::*ValueSupplier)();
protected:
	This *next { nullptr };
	WorldState *const parent;
	ValueSupplier supplier;
	short *const varsData;

	OriginLazyVarBase( WorldState *parent_, ValueSupplier supplier_, short *varsData_ )
		: parent( parent_ ), supplier( supplier_ ), varsData( varsData_ ) {}

	short *Data() { return varsData; }
	const short *Data() const { return varsData; }

	struct alignas( 2 )PackedFields {
		bool ignore : 1;
		uint8_t stateBits : 4;
		uint8_t satisfyOp : 1;
		uint8_t epsilon : 8;

		bool operator==( const PackedFields &that ) const {
			return *( (const short *)this ) == *( (const short *)&that );
		}
	};

	static_assert( sizeof( PackedFields ) == sizeof( short ), "" );
	static_assert( alignof( PackedFields ) == alignof( short ), "" );

	PackedFields &Packed() {
		return *( (PackedFields *)( varsData + 3 ) );
	}

	const PackedFields &Packed() const {
		return *( (const PackedFields *)( varsData + 3 ) );
	}

	uint8_t StateBits() const {
		return (uint8_t)Packed().stateBits;
	}

	// It gets called from a const function, thats why it is const too
	void SetStateBits( uint8_t stateBits ) const {
		const_cast<OriginLazyVarBase *>( this )->Packed().stateBits = stateBits;
	}

	// This values are chosen in this way to allow zero-cost conversion to bool from ABSENT/PRESENT state.
	static constexpr uint8_t ABSENT = 0;
	static constexpr uint8_t PRESENT = 1;
	static constexpr uint8_t PENDING = 2;
public:
	// Each coordinate is rounded up to 4 units
	// Thus maximal rounding distance error = sqrt(dx*dx + dy*dy + dz*dz) = sqrt(4*4 + 4*4 + 4*4)
	static constexpr float MAX_ROUNDING_SQUARE_DISTANCE_ERROR = 3 * 4 * 4;

	Vec3 Value() const;

	void Reset() {
		SetStateBits( PENDING );
		Packed().ignore = false;
	}

	bool Ignore() const {
		return Packed().ignore;
	}

	This &SetIgnore( bool ignore ) {
		Packed().ignore = ignore;
		return static_cast<This &>( *this );
	}

	This &SetSatisfyOp( SatisfyOp op, float epsilon );

	float DistanceTo( const OriginVar &that ) const;
};

class OriginLazyVar : public OriginLazyVarBase<OriginLazyVar> {
	friend class WorldState;
	short actualData[4] { 0, 0, 0, 0 };

	OriginLazyVar( WorldState *parent_, ValueSupplier supplier_ );
public:
	bool IsPresent() const;

	bool IgnoreOrAbsent() const {
		return Ignore() || !IsPresent();
	}

	bool operator==( const OriginLazyVar &that ) const;

	uint32_t Hash() const;

	void CopyFromThat( const OriginLazyVar &that ) {
		Vector4Copy( that.actualData, this->actualData );
	}

	bool IsSatisfiedBy( const OriginLazyVar &that ) const;
	void DebugPrint( const char *tag, const char *nameOfThis ) const;
};

class DualOriginLazyVar : public OriginLazyVarBase<DualOriginLazyVar> {
	friend class WorldState;

	short actualData[7] { 0, 0, 0, 0, 0, 0, 0 };

	DualOriginLazyVar( WorldState *parent_, ValueSupplier supplier_ );

	const short *Data2() const { return Data() + 4; }
public:
	Vec3 Value2() const;
	bool IsPresent() const;

	bool IgnoreOrAbsent() const {
		return Ignore() || !IsPresent();
	}

	bool operator==( const DualOriginLazyVar &that ) const;
	uint32_t Hash() const;

	void CopyFromThat( const DualOriginLazyVar &that ) {
		VectorCopy( that.actualData + 0, this->actualData + 0 );
		VectorCopy( that.actualData + 3, this->actualData + 3 );
		this->actualData[6] = that.actualData[6];
	}

	bool IsSatisfiedBy( const DualOriginLazyVar &that ) const;
	void DebugPrint( const char *tag, const char *nameOfThis ) const;
};

#define VAR_NAME_FORMAT "%-32.32s"

#define DECLARE_VAR( VarType, varName ) \
	private: VarType var##varName { this }; \
	public: VarType &varName##Var() { return var##varName; } \
	public: const VarType &varName##Var() const { return var##varName; } \
	private:

#define DECLARE_UNSIGNED_VAR( varName ) DECLARE_VAR( UnsignedVar, varName );
#define DECLARE_SHORT_VAR( varName ) DECLARE_VAR( ShortVar, varName );
#define DECLARE_BOOL_VAR( varName ) DECLARE_VAR( BoolVar, varName );
#define DECLARE_ORIGIN_VAR( varName ) DECLARE_VAR( OriginVar, varName );

#define DECLARE_LAZY_VAR( VarType, varName ) \
	private: VarType var##varName { this, &WorldState::Get##varName }; \
	public: VarType &varName##Var() { return var##varName; } \
	public: const VarType &varName##Var() const { return var##varName; } \
	private:

#define DECLARE_ORIGIN_LAZY_VAR( varName ) DECLARE_LAZY_VAR( OriginLazyVar, varName )
#define DECLARE_DUAL_ORIGIN_LAZY_VAR( varName ) DECLARE_LAZY_VAR( DualOriginLazyVar, varName )

class WorldState {
	friend class ShortVar;
	friend class UnsignedVar;
	friend class BoolVar;
	friend class OriginVar;
	friend class OriginLazyVar;
	friend class DualOriginLazyVar;

	Ai *const self;
#ifndef PUBLIC_BUILD
	bool isCopiedFromOtherWorldState { false };
#endif

	inline Bot *Self();
	inline const Bot *Self() const;

	ShortVar *shortVarsHead { nullptr };
	UnsignedVar *unsignedVarsHead { nullptr };
	BoolVar *boolVarsHead { nullptr };
	OriginVar *originVarsHead { nullptr };
	OriginLazyVar *originLazyVarsHead { nullptr };
	DualOriginLazyVar *dualOriginLazyVarsHead { nullptr };

	void Link( ShortVar *var ) {
		var->next = shortVarsHead;
		shortVarsHead = var;
	}

	void Link( UnsignedVar *var ) {
		var->next = unsignedVarsHead;
		unsignedVarsHead = var;
	}

	void Link( BoolVar *var ) {
		var->next = boolVarsHead;
		boolVarsHead = var;
	}

	void Link( OriginVar *var ) {
		var->next = originVarsHead;
		originVarsHead = var;
	}

	void Link( OriginLazyVar *var ) {
		var->next = originLazyVarsHead;
		originLazyVarsHead = var;
	}

	void Link( DualOriginLazyVar *var ) {
		var->next = dualOriginLazyVarsHead;
		dualOriginLazyVarsHead = var;
	}

	const short *BotOriginData() const {
		auto &var = BotOriginVar();
		assert( !var.Ignore() );
		return var.data;
	}

	const short *EnemyOriginData() const {
		auto &var = EnemyOriginVar();
		assert( !var.Ignore() );
		return var.data;
	}

	const short *GetSniperRangeTacticalSpot();
	const short *GetFarRangeTacticalSpot();
	const short *GetMiddleRangeTacticalSpot();
	const short *GetCloseRangeTacticalSpot();
	const short *GetCoverSpot();

	const short *GetRunAwayTeleportOrigin();
	const short *GetRunAwayJumppadOrigin();
	const short *GetRunAwayElevatorOrigin();

	void CopyFromOtherWorldState( const WorldState &that );

	void MoveFromOtherWorldState( WorldState &&that ) {
		// TODO: Implement an actual moving version if non-POD types are used for some vars
		CopyFromOtherWorldState( that );
	}
public:
#ifndef PUBLIC_BUILD
	bool IsCopiedFromOtherWorldState() { return isCopiedFromOtherWorldState; }
#endif
	WorldState &operator=( const WorldState &that ) {
		assert( self == that.self );
		CopyFromOtherWorldState( that );
		return *this;
	}

	WorldState( const WorldState &that ) : self( that.self ) {
		CopyFromOtherWorldState( that );
	}

	WorldState &operator=( WorldState &&that ) {
		assert( self == that.self );
		CopyFromOtherWorldState( that );
		return *this;
	}

	WorldState( WorldState &&that ) : self( that.self ) {
		CopyFromOtherWorldState( that );
	}

	explicit WorldState( Ai *self_ ): self( self_ ) {
#ifndef PUBLIC_BUILD
		this->isCopiedFromOtherWorldState = false;
#endif
	}

	template <typename Var>
	static inline void CopyVarsFromThat( Var *thisVars, const Var *thatVars );

	template <typename Var>
	static inline bool CheckSatisfiedBy( const Var *thisVars, const Var *thatVars );

	template <typename Var>
	static inline void SetIgnore( Var *varsHead, bool ignore );

	template <typename Var>
	static inline uint32_t ComputeHash( const Var *varsHead );

	template <typename Var>
	static inline bool CheckEquality( const Var *thisVars, const Var *thatVars );

	bool IsSatisfiedBy( const WorldState &that ) const;

	uint32_t Hash() const;
	bool operator==( const WorldState &that ) const;

	void SetIgnoreAll( bool ignore );

	DECLARE_UNSIGNED_VAR( GoalItemWaitTime )
	DECLARE_UNSIGNED_VAR( SimilarWorldStateInstanceId )

	DECLARE_SHORT_VAR( Health )
	DECLARE_SHORT_VAR( Armor )
	DECLARE_SHORT_VAR( RawDamageToKill )
	DECLARE_SHORT_VAR( PotentialHazardDamage )
	DECLARE_SHORT_VAR( ThreatInflictedDamage )

	DECLARE_BOOL_VAR( HasQuad )
	DECLARE_BOOL_VAR( HasShell )
	DECLARE_BOOL_VAR( EnemyHasQuad )
	DECLARE_BOOL_VAR( HasThreateningEnemy )
	DECLARE_BOOL_VAR( HasJustPickedGoalItem )

	DECLARE_BOOL_VAR( IsRunningAway )
	DECLARE_BOOL_VAR( HasRunAway )

	DECLARE_BOOL_VAR( HasReactedToHazard )
	DECLARE_BOOL_VAR( HasReactedToThreat )

	DECLARE_BOOL_VAR( IsReactingToEnemyLost )
	DECLARE_BOOL_VAR( HasReactedToEnemyLost )
	DECLARE_BOOL_VAR( MightSeeLostEnemyAfterTurn )

	DECLARE_BOOL_VAR( HasJustTeleported )
	DECLARE_BOOL_VAR( HasJustTouchedJumppad )
	DECLARE_BOOL_VAR( HasJustEnteredElevator )

	DECLARE_BOOL_VAR( HasPendingCoverSpot )
	DECLARE_BOOL_VAR( HasPendingRunAwayTeleport )
	DECLARE_BOOL_VAR( HasPendingRunAwayJumppad )
	DECLARE_BOOL_VAR( HasPendingRunAwayElevator )

	DECLARE_BOOL_VAR( HasPositionalAdvantage )
	DECLARE_BOOL_VAR( CanHitEnemy )
	DECLARE_BOOL_VAR( EnemyCanHit )
	DECLARE_BOOL_VAR( HasJustKilledEnemy )

	DECLARE_BOOL_VAR( HasGoodSniperRangeWeapons )
	DECLARE_BOOL_VAR( HasGoodFarRangeWeapons )
	DECLARE_BOOL_VAR( HasGoodMiddleRangeWeapons )
	DECLARE_BOOL_VAR( HasGoodCloseRangeWeapons )

	DECLARE_BOOL_VAR( EnemyHasGoodSniperRangeWeapons )
	DECLARE_BOOL_VAR( EnemyHasGoodFarRangeWeapons )
	DECLARE_BOOL_VAR( EnemyHasGoodMiddleRangeWeapons )
	DECLARE_BOOL_VAR( EnemyHasGoodCloseRangeWeapons )

	DECLARE_ORIGIN_VAR( BotOrigin )
	DECLARE_ORIGIN_VAR( EnemyOrigin )
	DECLARE_ORIGIN_VAR( NavTargetOrigin )
	DECLARE_ORIGIN_VAR( PendingOrigin )

	DECLARE_ORIGIN_VAR( HazardHitPoint )
	DECLARE_ORIGIN_VAR( HazardDirection )
	DECLARE_ORIGIN_VAR( DodgeHazardSpot )
	DECLARE_ORIGIN_VAR( ThreatPossibleOrigin )
	DECLARE_ORIGIN_VAR( LostEnemyLastSeenOrigin )

	DECLARE_ORIGIN_LAZY_VAR( SniperRangeTacticalSpot )
	DECLARE_ORIGIN_LAZY_VAR( FarRangeTacticalSpot )
	DECLARE_ORIGIN_LAZY_VAR( MiddleRangeTacticalSpot )
	DECLARE_ORIGIN_LAZY_VAR( CloseRangeTacticalSpot )
	DECLARE_ORIGIN_LAZY_VAR( CoverSpot )

	DECLARE_DUAL_ORIGIN_LAZY_VAR( RunAwayTeleportOrigin )
	DECLARE_DUAL_ORIGIN_LAZY_VAR( RunAwayJumppadOrigin )
	DECLARE_DUAL_ORIGIN_LAZY_VAR( RunAwayElevatorOrigin )
public:
	float DistanceToEnemy() const { return BotOriginVar().DistanceTo( EnemyOriginVar() ); }
	float DistanceToNavTarget() const { return BotOriginVar().DistanceTo( NavTargetOriginVar() ); }

	void ResetTacticalSpots() {
		SniperRangeTacticalSpotVar().Reset();
		FarRangeTacticalSpotVar().Reset();
		MiddleRangeTacticalSpotVar().Reset();
		CloseRangeTacticalSpotVar().Reset();
		CoverSpotVar().Reset();

		RunAwayTeleportOriginVar().Reset();
		RunAwayJumppadOriginVar().Reset();
		RunAwayElevatorOriginVar().Reset();
	}

	constexpr static float FAR_RANGE_MAX = 2.5f * 900.0f;
	constexpr static float MIDDLE_RANGE_MAX = 900.0f;
	constexpr static float CLOSE_RANGE_MAX = 175.0f;

	bool EnemyIsOnSniperRange() const {
		return DistanceToEnemy() > FAR_RANGE_MAX;
	}

	bool EnemyIsOnFarRange() const {
		return DistanceToEnemy() > MIDDLE_RANGE_MAX && DistanceToEnemy() <= FAR_RANGE_MAX;
	}

	bool EnemyIsOnMiddleRange() const {
		return DistanceToEnemy() > CLOSE_RANGE_MAX && DistanceToEnemy() <= MIDDLE_RANGE_MAX;
	}

	bool EnemyIsOnCloseRange() const {
		return DistanceToEnemy() <= CLOSE_RANGE_MAX;
	}

	float DamageToBeKilled() const {
		float damageToBeKilled = ::DamageToKill( HealthVar(), ArmorVar() );
		if( HasShellVar() ) {
			damageToBeKilled *= 4.0f;
		}
		if( EnemyHasQuadVar() ) {
			damageToBeKilled /= 4.0f;
		}
		return damageToBeKilled;
	}

	float DamageToKill() const {
		float damageToKill = RawDamageToKillVar();
		if( HasQuadVar() ) {
			damageToKill /= 4.0f;
		}
		return damageToKill;
	}

	float KillToBeKilledDamageRatio() const {
		return DamageToKill() / DamageToBeKilled();
	}

	void DebugPrint( const char *tag ) const;

	void DebugPrintDiff( const WorldState &that, const char *oldTag, const char *newTag ) const;
};

#undef DECLARE_UNSIGNED_VAR
#undef DECLARE_SHORT_VAR
#undef DECLARE_BOOL_VAR
#undef DECLARE_ORIGIN_VAR
#undef DECLARE_ORIGIN_LAZY_VAR
#undef DECLARE_DUAL_ORIGIN_LAZY_VAR

inline ShortVar::ShortVar( WorldState *parent_ ) {
	parent_->Link( this );
}

inline UnsignedVar::UnsignedVar( WorldState *parent_ ) {
	parent_->Link( this );
}

inline BoolVar::BoolVar( WorldState *parent_ ) {
	parent_->Link( this );
}

inline OriginVar::OriginVar( WorldState *parent_ ) {
	parent_->Link( this );
}

inline OriginLazyVar::OriginLazyVar( WorldState *parent_, ValueSupplier supplier_ )
	: OriginLazyVarBase( parent_, supplier_, actualData ) {
	parent_->Link( this );
}

inline DualOriginLazyVar::DualOriginLazyVar( WorldState *parent_, ValueSupplier supplier_ )
	: OriginLazyVarBase( parent_, supplier_, actualData ) {
	parent->Link( this );
}

inline void UnsignedVar::DebugPrint( const char *tag, const char *nameOfThis ) const {
	if( Ignore() ) {
		AI_Debug( tag, VAR_NAME_FORMAT ": (ignored)\n", nameOfThis );
	} else {
		AI_Debug( tag, VAR_NAME_FORMAT ": %u\n", nameOfThis, Value() );
	}
}

inline void ShortVar::DebugPrint( const char *tag, const char *nameOfThis ) const {
	if( Ignore() ) {
		AI_Debug( tag, VAR_NAME_FORMAT ": (ignored)\n", nameOfThis );
	} else {
		AI_Debug( tag, VAR_NAME_FORMAT ": %hi\n", nameOfThis, Value() );
	}
}

inline void BoolVar::DebugPrint( const char *tag, const char *nameOfThis ) const {
	if( Ignore() ) {
		AI_Debug( tag, VAR_NAME_FORMAT ": (ignored)\n", nameOfThis );
	} else {
		AI_Debug( tag, VAR_NAME_FORMAT ": %s\n", nameOfThis, Value() ? "true" : "false" );
	}
}

inline void OriginVar::DebugPrint( const char *tag, const char *nameOfThis ) const {
	if( Ignore() ) {
		AI_Debug( tag, VAR_NAME_FORMAT ": (ignored)\n", nameOfThis );
		return;
	}
	Vec3 value( Value() );
	AI_Debug( tag, VAR_NAME_FORMAT ": %f %f %f\n", nameOfThis, value.X(), value.Y(), value.Z() );
}

inline float OriginVar::DistanceTo( const OriginVar &that ) const {
#ifdef _DEBUG
	if( this->Ignore() ) {
		AI_FailWith( "OriginVar::GetDistance()", "`this` var is ignored\n" );
	}
	if( that.Ignore() ) {
		AI_FailWith( "OriginVar::GetDistance()", "`that` var is ignored\n" );
	}
#endif

	float result = GetUnpacked4uVec( data ).FastDistanceTo( that.Value() );
	assert( std::isfinite( result ) );
	return result;
}

inline OriginVar &OriginVar::SetSatisfyOp( SatisfyOp op, float epsilon ) {
#ifdef _DEBUG
	if( op != SatisfyOp::EQ && op != SatisfyOp::NE ) {
		AI_FailWith( "OriginVar::SetSatisfyOp()", "Illegal satisfy op %d for this kind of var\n", (int)op );
	}
	if( epsilon < 4.0f || epsilon >= 1024.0f ) {
		AI_FailWith( "OriginVar::SetSatisfyOp()", "An epsilon %f is out of valid [4, 1024] range\n", epsilon );
	}
#endif
	// Up to 8 bits
	auto packedEpsilon = (uint8_t)( (unsigned)epsilon / 4 );
	auto packedOp = (uint8_t)op;
	Packed().epsilon = packedEpsilon;
	Packed().satisfyOp = packedOp;
	return *this;
}

inline bool OriginVar::operator==( const OriginVar &that ) const {
	if( !Packed().ignore ) {
		if( !( Packed() == that.Packed() ) ) {
			return false;
		}

		return VectorCompare( data, that.data );
	}

	return that.Packed().ignore;
}

template <typename This>
inline Vec3 OriginLazyVarBase<This>::Value() const {
	if( StateBits() == PRESENT ) {
		return GetUnpacked4uVec( Data() );
	}

	AI_FailWith( "OriginLazyVar::Value()", "Attempt to get a value of a var that is not in PRESENT state\n" );
}

template <typename This>
inline This &OriginLazyVarBase<This>::SetSatisfyOp( SatisfyOp op, float epsilon ) {
#ifdef _DEBUG
	if( op != SatisfyOp::EQ && op != SatisfyOp::NE ) {
		AI_FailWith( "OriginLazyVarBase::SetSatisfyOp()", "Illegal satisfy op %d for this kind of var\n", (int)op );
	}
	if( epsilon < 4.0f || epsilon >= 1024.0f ) {
		AI_FailWith( "OriginLazyVarBase::SetSatisfyOp()", "An epsilon %f is out of valid [4, 1024] range\n", epsilon );
	}
#endif
	// Up to 8 bits
	auto packedEpsilon = (uint8_t)( (unsigned)epsilon / 4 );
	// A single bit
	auto packedOp = (uint8_t)op;
	static_assert( (unsigned)SatisfyOp::EQ == 0, "SatisfyOp can't be packed in a single bit" );
	static_assert( (unsigned)SatisfyOp::NE == 1, "SatisfyOp can't be packed in a single bit" );
	Packed().epsilon = packedEpsilon;
	Packed().satisfyOp = packedOp;
	return *this;
}

template <typename This>
inline float OriginLazyVarBase<This>::DistanceTo( const OriginVar &that ) const {
#ifdef _DEBUG
	if( this->Ignore() ) {
		AI_FailWith( "OriginLazyVar::GetDistance(const OriginVar &)", "`this` var is ignored\n" );
	}
	if( that.Ignore() ) {
		AI_FailWith( "OriginLazyVar::GetDistance(const OriginVar &)", "`that` var is ignored\n" );
	}
#endif
	return GetUnpacked4uVec( Data() ).DistanceTo( that.Value() );
}

inline bool OriginLazyVar::IsPresent() const {
	unsigned char stateBits = StateBits();
	if( stateBits != PENDING ) {
		return stateBits != ABSENT;
	}

	const short *packedValues = ( parent->*supplier )();
	if( packedValues ) {
		short *data = const_cast<short*>( Data() );
		VectorCopy( packedValues, data );
		SetStateBits( PRESENT );
		return true;
	}
	SetStateBits( ABSENT );
	return false;
}

inline bool OriginLazyVar::operator==( const OriginLazyVar &that ) const {
	if( !Packed().ignore ) {
		if( that.Packed().ignore ) {
			return false;
		}
		auto stateBits = StateBits();
		if( stateBits != that.StateBits() ) {
			return false;
		}
		if( stateBits != PRESENT ) {
			return true;
		}

		if( !( Packed() == that.Packed() ) ) {
			return false;
		}

		return VectorCompare( Data(), that.Data() );
	}
	// `that` should be ignored too
	return that.Packed().ignore;
}

inline uint32_t OriginLazyVar::Hash() const {
	auto stateBits = StateBits();
	if( stateBits != PRESENT ) {
		return stateBits;
	}
	const unsigned short *data = (const unsigned short*)Data();
	return ( data[0] | ( data[1] << 16 ) ) ^ ( data[2] | ( data[3] << 16 ) );
}

inline void OriginLazyVar::DebugPrint( const char *tag, const char *nameOfThis ) const {
	if( Ignore() ) {
		AI_Debug( tag, VAR_NAME_FORMAT ": (ignored)\n", nameOfThis );
		return;
	}
	switch( StateBits() ) {
		case PENDING:
			AI_Debug( tag, VAR_NAME_FORMAT ": (pending)\n", nameOfThis );
			break;
		case ABSENT:
			AI_Debug( tag, VAR_NAME_FORMAT ": (absent)\n", nameOfThis );
			break;
		case PRESENT:
			Vec3 value( Value() );
			AI_Debug( tag, VAR_NAME_FORMAT ": %f %f %f\n", nameOfThis, value.X(), value.Y(), value.Z() );
			break;
	}
}

inline bool OriginLazyVar::IsSatisfiedBy( const OriginLazyVar &that ) const {
	if( Packed().ignore ) {
		return true;
	}
	if( that.Packed().ignore ) {
		return false;
	}

	auto stateBits = this->StateBits();
	// Do not force a lazy value to be computed
	if( stateBits != that.StateBits() ) {
		return false;
	}
	if( stateBits != PRESENT ) {
		return true;
	}

	const int sign = ( (SatisfyOp)Packed().satisfyOp == SatisfyOp::EQ ) ? +1 : -1;
	assert( (SatisfyOp)Packed().satisfyOp == SatisfyOp::EQ || (SatisfyOp)Packed().satisfyOp == SatisfyOp::NE );
	return OriginVar::IsOriginSatisfiedBy( sign, Packed().epsilon, Data(), that.Data() );
}

inline Vec3 DualOriginLazyVar::Value2() const {
	if( StateBits() == PRESENT ) {
		return GetUnpacked4uVec( Data2() );
	}

	AI_FailWith( "OriginLazyVar::Value2()", "Attempt to get a 2nd value of a var that is not in PRESENT state\n" );
}

inline bool DualOriginLazyVar::IsSatisfiedBy( const DualOriginLazyVar &that ) const {
	if( Packed().ignore ) {
		return true;
	}
	if( that.Packed().ignore ) {
		return false;
	}

	auto stateBits = this->StateBits();
	// Do not force a lazy value to be computed
	if( stateBits != that.StateBits() ) {
		return false;
	}
	if( stateBits != PRESENT ) {
		return true;
	}

	if( !( Packed() == that.Packed() ) ) {
		return false;
	}

	assert( (SatisfyOp)Packed().satisfyOp == SatisfyOp::EQ || (SatisfyOp)Packed().satisfyOp == SatisfyOp::NE );

	const int sign = (SatisfyOp)Packed().satisfyOp == SatisfyOp::EQ ? +1 : -1;
	const int epsilon = Packed().epsilon;
	if( !OriginVar::IsOriginSatisfiedBy( sign, epsilon, Data(), that.Data() ) ) {
		return false;
	}
	return OriginVar::IsOriginSatisfiedBy( sign, epsilon, Data2(), that.Data2() );
}

inline bool DualOriginLazyVar::operator==( const DualOriginLazyVar &that ) const {
	if( !Packed().ignore ) {
		if( that.Packed().ignore ) {
			return false;
		}
		auto stateBits = StateBits();
		if( stateBits != that.StateBits() ) {
			return false;
		}
		if( stateBits != PRESENT ) {
			return true;
		}
		return VectorCompare( Data(), that.Data() ) && VectorCompare( Data2(), that.Data2() );
	}
	// `that` should be ignored too
	return that.Packed().ignore;
}

inline void DualOriginLazyVar::DebugPrint( const char *tag, const char *nameOfThis ) const {
	if( Ignore() ) {
		AI_Debug( tag, VAR_NAME_FORMAT ": (ignored)\n", nameOfThis );
		return;
	}
	switch( StateBits() ) {
		case PENDING:
			AI_Debug( tag, VAR_NAME_FORMAT ": (pending)\n", nameOfThis );
			break;
		case ABSENT:
			AI_Debug( tag, VAR_NAME_FORMAT ": (absent)\n", nameOfThis );
			break;
		case PRESENT:
			Vec3 v( Value() ), v2( Value2() );
			constexpr const char *format = VAR_NAME_FORMAT ": %f %f %f, %f %f %f\n";
			AI_Debug( tag, format, nameOfThis, v.X(), v.Y(), v.Z(), v2.X(), v2.Y(), v2.Z() );
			break;
	}
}

inline bool DualOriginLazyVar::IsPresent() const {
	uint8_t stateBits = StateBits();
	if( stateBits != PENDING ) {
		return stateBits != ABSENT;
	}

	const int16_t *packedValues = ( parent->*supplier )();
	if( !packedValues ) {
		SetStateBits( ABSENT );
		return false;
	}

	auto *const data = const_cast<int16_t*>( Data() );
	auto *const data2 = const_cast<int16_t*>( Data2() );
	VectorCopy( packedValues, data );
	VectorCopy( packedValues + 3, data2 );
	SetStateBits( PRESENT );
	return true;
}

inline uint32_t DualOriginLazyVar::Hash() const {
	auto stateBits = StateBits();
	if( stateBits != PRESENT ) {
		return stateBits;
	}
	unsigned originHash = ( 17U * Data()[0] + ( Data()[1] | ( Data()[2] << 16 ) ) );
	unsigned originHash2 = ( 17U * Data2()[0] + ( Data2()[1] | ( Data2()[2] << 16 ) ) );
	uint32_t result = 17u * ( *(short *)&Packed() );
	result = result * 31 + originHash;
	result = result * 31 + originHash2;
	return result;
}

#undef VAR_NAME_FORMAT

#endif
