#ifndef QFUSION_AI_BASE_ENEMY_POOL_H
#define QFUSION_AI_BASE_ENEMY_POOL_H

#include "../AIComponent.h"
#include "../static_deque.h"
#include "../static_vector.h"
#include "../vec3.h"
#include "../../../gameshared/q_comref.h"
#include <limits>

template <int Weapon>
struct WeaponAmmo {
	static constexpr int strongAmmoTag = AMMO_NONE;
	static constexpr int weakAmmoTag = AMMO_NONE;
};

template<>
struct WeaponAmmo<WEAP_NONE>{
	static constexpr int strongAmmoTag = AMMO_NONE;
	static constexpr int weakAmmoTag = AMMO_NONE;
};

template<>
struct WeaponAmmo<WEAP_GUNBLADE>{
	static constexpr int strongAmmoTag = AMMO_GUNBLADE;
	static constexpr int weakAmmoTag = AMMO_WEAK_GUNBLADE;
};

template<>
struct WeaponAmmo<WEAP_RIOTGUN>{
	static constexpr int strongAmmoTag = AMMO_SHELLS;
	static constexpr int weakAmmoTag = AMMO_WEAK_SHELLS;
};

template<>
struct WeaponAmmo<WEAP_GRENADELAUNCHER>{
	static constexpr int strongAmmoTag = AMMO_GRENADES;
	static constexpr int weakAmmoTag = AMMO_WEAK_GRENADES;
};

template<>
struct WeaponAmmo<WEAP_ROCKETLAUNCHER>{
	static constexpr int strongAmmoTag = AMMO_ROCKETS;
	static constexpr int weakAmmoTag = AMMO_WEAK_ROCKETS;
};

template<>
struct WeaponAmmo<WEAP_PLASMAGUN>{
	static constexpr int strongAmmoTag = AMMO_PLASMA;
	static constexpr int weakAmmoTag = AMMO_WEAK_PLASMA;
};

template<>
struct WeaponAmmo<WEAP_LASERGUN>{
	static constexpr int strongAmmoTag = AMMO_LASERS;
	static constexpr int weakAmmoTag = AMMO_WEAK_LASERS;
};

template<>
struct WeaponAmmo<WEAP_MACHINEGUN>{
	static constexpr int strongAmmoTag = AMMO_BULLETS;
	static constexpr int weakAmmoTag = AMMO_WEAK_BULLETS;
};

template<>
struct WeaponAmmo<WEAP_ELECTROBOLT>{
	static constexpr int strongAmmoTag = AMMO_BOLTS;
	static constexpr int weakAmmoTag = AMMO_WEAK_BOLTS;
};

template<>
struct WeaponAmmo<WEAP_SHOCKWAVE>{
	static constexpr int strongAmmoTag = AMMO_WAVES;
	static constexpr int weakAmmoTag = AMMO_WEAK_WAVES;
};

template<>
struct WeaponAmmo<WEAP_INSTAGUN>{
	static constexpr int strongAmmoTag = AMMO_INSTAS;
	static constexpr int weakAmmoTag = AMMO_WEAK_INSTAS;
};

inline bool HasQuad( const edict_t *ent ) {
	return ent && ent->r.client && ent->r.client->ps.inventory[POWERUP_QUAD];
}

inline bool HasShell( const edict_t *ent ) {
	return ent && ent->r.client && ent->r.client->ps.inventory[POWERUP_SHELL];
}

inline bool HasPowerups( const edict_t *ent ) {
	if( !ent || !ent->r.client ) {
		return false;
	}
	return ent->r.client->ps.inventory[POWERUP_QUAD] && ent->r.client->ps.inventory[POWERUP_SHELL];
}

inline bool IsCarrier( const edict_t *ent ) {
	return ent && ent->r.client && ent->s.effects & EF_CARRIER;
}

float DamageToKill( const edict_t *ent, float armorProtection, float armorDegradation );

float DamageToKill( float health, float armor, float armorProtection, float armorDegradation );

class TrackedEnemy {
	friend class AiEnemiesTracker;
	class AiEnemiesTracker *parent { nullptr };

public:
	enum class HitFlags: int {
		NONE = 0,
		RAIL = 1,
		ROCKET = 2,
		SHAFT = 4,
		ALL = 7
	};
private:

	// Intrusive list links (an instance can be linked to many lists at the same time)
	struct Links {
		TrackedEnemy *next, *prev;

		void Clear() {
			next = prev = nullptr;
		}
	};

	enum { TRACKED_LIST_INDEX, ACTIVE_LIST_INDEX };
	Links listLinks[2];

	float weight;
	float avgPositiveWeight;
	float maxPositiveWeight;
	unsigned positiveWeightsCount;

	int entNum { -1 };
	float scoreAsActiveEnemy;

	int64_t registeredAt;

	// Same as front() of lastSeenTimestamps, used for faster access
	int64_t lastSeenAt;
	// Same as front() of lastSeenOrigins, used for faster access
	Vec3 lastSeenOrigin { 0, 0, 0 };
	// Same as front() of lastSeenVelocities, used for faster access
	Vec3 lastSeenVelocity { 0, 0, 0 };

	// Some intermediates that should be cached for consequent MightBlockArea() flags
	mutable int64_t lookDirComputedAt;
	mutable int64_t weaponHitFlagsComputedAt;

	mutable HitFlags checkForWeaponHitFlags;
	mutable float checkForWeaponHitKillDamage;

	mutable vec3_t lookDir;

	TrackedEnemy *NextInTrackedList() { return listLinks[TRACKED_LIST_INDEX].next; }
	TrackedEnemy *NextInActiveList() { return listLinks[ACTIVE_LIST_INDEX].next; }

	inline bool IsInList( int listIndex ) const;

	HitFlags ComputeCheckForWeaponHitFlags( float damageToKillTarget ) const;
public:
	const edict_t *ent;  // If null, the enemy slot is unused

	inline TrackedEnemy() {
		Clear();
	}

	const TrackedEnemy *NextInTrackedList() const { return listLinks[TRACKED_LIST_INDEX].next; }
	const TrackedEnemy *NextInActiveList() const { return listLinks[ACTIVE_LIST_INDEX].next; }

	inline bool IsInTrackedList() const { return IsInList( TRACKED_LIST_INDEX ); }

	inline bool IsInActiveList() const { return IsInList( ACTIVE_LIST_INDEX ); }

	static constexpr unsigned MAX_TRACKED_SNAPSHOTS = 16;

	void Clear();
	void OnViewed( const float *specifiedOrigin = nullptr );
	void InitAndLink( const edict_t *ent, const float *specifiedOrigin = nullptr );

	inline const char *Nick() const {
		if( !ent ) {
			return "???";
		}
		return ent->r.client ? ent->r.client->netname : ent->classname;
	}

	inline float AvgWeight() const { return avgPositiveWeight; }
	inline float MaxWeight() const { return maxPositiveWeight; }

	inline int EntNum() const { return entNum; }

	inline bool HasQuad() const { return ::HasQuad( ent ); }
	inline bool HasShell() const { return ::HasShell( ent ); }
	inline bool HasPowerups() const { return ::HasPowerups( ent ); }
	inline bool IsCarrier() const { return ::IsCarrier( ent ); }

	template<int Weapon>
	inline int AmmoReadyToFireCount() const {
		if( !ent->r.client ) {
			return 0;
		}
		const int *inventory = ent->r.client->ps.inventory;
		if( !inventory[Weapon] ) {
			return 0;
		}
		return inventory[WeaponAmmo < Weapon > ::strongAmmoTag] + inventory[WeaponAmmo < Weapon > ::weakAmmoTag];
	}

	inline int ShellsReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_RIOTGUN>(); }
	inline int GrenadesReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_GRENADELAUNCHER>(); }
	inline int RocketsReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_ROCKETLAUNCHER>(); }
	inline int PlasmasReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_PLASMAGUN>(); }
	inline int BulletsReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_MACHINEGUN>(); }
	inline int LasersReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_LASERGUN>(); }
	inline int BoltsReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_ELECTROBOLT>(); }
	inline int WavesReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_SHOCKWAVE>(); }
	inline int InstasReadyToFireCount() const { return AmmoReadyToFireCount<WEAP_INSTAGUN>(); }

	bool IsShootableCurrWeapon( int weapon ) const;
	bool IsShootableCurrOrPendingWeapon( int weapon ) const;

	bool TriesToKeepUnderXhair( const float *origin ) const;

	inline unsigned FireDelay() const {
		return ent->r.client ? ent->r.client->ps.stats[STAT_WEAPON_TIME] : 0u;
	}

	inline int64_t LastSeenAt() const { return lastSeenAt; }
	inline const Vec3 &LastSeenOrigin() const { return lastSeenOrigin; }
	inline const Vec3 &LastSeenVelocity() const { return lastSeenVelocity; }

	inline int64_t LastAttackedByTime() const;
	inline float TotalInflictedDamage() const;

	inline bool IsValid() const {
		return ent && !( G_ISGHOSTING( ent ) );
	}

	Vec3 LookDir() const;

	HitFlags GetCheckForWeaponHitFlags( float damageToKillTarget ) const;

	inline Vec3 Angles() const { return Vec3( ent->s.angles ); }

	class alignas ( 4 )Snapshot {
		Int64Align4 timestamp;
		int16_t packedOrigin[3];
		int16_t packedVelocity[3];
		int16_t angles[2];
	public:
		Snapshot( const vec3_t origin_, const vec3_t velocity_, const vec3_t angles_, int64_t timestamp_ ) {
			this->timestamp = timestamp_;
			SetPacked4uVec( origin_, this->packedOrigin );
			SetPacked4uVec( velocity_, this->packedVelocity );
			angles[0] = (int16_t)angles_[PITCH];
			angles[1] = (int16_t)angles_[YAW];
		}

		int64_t Timestamp() const { return timestamp; }
		Vec3 Origin() const { return GetUnpacked4uVec( packedOrigin ); }
		Vec3 Velocity() const { return GetUnpacked4uVec( packedVelocity ); }

		Vec3 Angles() const {
			vec3_t result;
			result[PITCH] = angles[0];
			result[YAW] = angles[1];
			result[ROLL] = 0;
			return Vec3( result );
		}
	};

	typedef StaticDeque<Snapshot, MAX_TRACKED_SNAPSHOTS> SnapshotsQueue;
	SnapshotsQueue lastSeenSnapshots;
};

class AttackStats {
	friend class AiEnemiesTracker;

	// Very close to 4 game seconds
	static constexpr unsigned MAX_KEPT_FRAMES = 64 * 4;

	static_assert( ( MAX_KEPT_FRAMES & ( MAX_KEPT_FRAMES - 1 ) ) == 0, "Should be a power of 2 for fast modulo computation" );

	// A damage is saturated up to 255 units.
	// Storing greater values not only does not make sense, but leads to non-efficient memory usage/cache access.
	uint8_t frameDamages[MAX_KEPT_FRAMES];

	unsigned frameIndex;
	unsigned totalAttacks;
	int64_t lastDamageAt;
	int64_t lastTouchAt;
	float totalDamage;

	const edict_t *ent;

	AttackStats() { Clear(); }

public:
	void Clear() {
		ent = nullptr;
		totalDamage = 0;
		totalAttacks = 0;
		lastDamageAt = 0;
		lastTouchAt = level.time;
		frameIndex = 0;
		memset( frameDamages, 0, sizeof( frameDamages ) );
	}

	// Call it once in a game frame
	void Frame() {
		auto overwrittenDamage = frameDamages[frameIndex];
		frameIndex = ( frameIndex + 1 ) % MAX_KEPT_FRAMES;
		totalDamage -= overwrittenDamage;
		frameDamages[frameIndex] = 0;
		if( overwrittenDamage > 0 ) {
			totalAttacks--;
		}
	}

	// Call it after Frame() in the same frame
	void OnDamage( float damage ) {
		frameDamages[frameIndex] = (uint8_t)std::min( damage, 255.0f );
		totalDamage += damage;
		totalAttacks++;
		lastDamageAt = level.time;
	}

	// Call it after Frame() in the same frame if damage is not registered
	// but you want to mark frame as a frame of activity anyway
	void Touch() { lastTouchAt = level.time; }

	int64_t LastActivityAt() const { return std::max( lastDamageAt, lastTouchAt ); }
};

class AiEnemiesTracker : public AiFrameAwareComponent {
	friend class TrackedEnemy;
	friend class BotAwarenessModule;
	friend class AiSquad;
public:
	static constexpr unsigned MAX_TRACKED_ATTACKERS = 5;
	static constexpr unsigned MAX_TRACKED_TARGETS = 5;

	// If an enemy has not been seen for this period, it gets unlinked and completely forgotten
	// and thus cannot block an area (in general and not AAS sense) anymore.
	static constexpr unsigned NOT_SEEN_UNLINK_TIMEOUT = 8000;
	// If an enemy has not been seen for this period, it gets unlinked.
	static constexpr unsigned NOT_SEEN_SUGGEST_TIMEOUT = 4000;
	// An attacker gets evicted/forgotten if there were no hits during this period.
	static constexpr unsigned ATTACKER_TIMEOUT = 4000;
	// A target gets evicted/forgotten if there were no hits/selection as a target during this period.
	static constexpr unsigned TARGET_TIMEOUT = 4000;

	static constexpr unsigned MAX_ACTIVE_ENEMIES = 3;

private:
	const float avgSkill; // (0..1)

	// An i-th element corresponds to i-th entity
	TrackedEnemy entityToEnemyTable[MAX_EDICTS];

	// List heads for tracked and active enemies lists
	TrackedEnemy *listHeads[2] { nullptr, nullptr };

	unsigned numTrackedEnemies;
	const unsigned maxTrackedAttackers;
	const unsigned maxTrackedTargets;
	const unsigned maxActiveEnemies;

	const unsigned reactionTime;

	int64_t prevThinkLevelTime { 0 };

	StaticVector<AttackStats, MAX_TRACKED_ATTACKERS> attackers;
	StaticVector<AttackStats, MAX_TRACKED_TARGETS> targets;

	void RemoveEnemy( TrackedEnemy *enemy );

	void UpdateEnemyWeight( TrackedEnemy *enemy );

	virtual float ComputeRawEnemyWeight( const edict_t *enemy );

	/**
	 * Modifies weight for an enemy that has been an attacker of the bot recently.
	 * @param enemy an enemy underlying entity (for a newly added or updated enemy)
	 * @param weightSoFar a weight of the enemy computed to the moment of this call
	 * @return a modified value of the enemy weight (can't be less than the supplied one)
	 */
	virtual float ModifyWeightForAttacker( const edict_t *enemy, float weightSoFar );

	/**
	 * Modifies weight for an enemy that has been hit by the bot recently.
	 * @param enemy an enemy underlying entity (for a newly added or updated enemy)
	 * @param weightSoFar a weight of the enemy computed to the moment of this call
	 * @return a modified value of the enemy weight (can't be less than the supplied one)
	 */
	virtual float ModifyWeightForHitTarget( const edict_t *enemy, float weightSoFar );

	/**
	 * Modifies weight for an enemy based on a "kill enemy/be killed by enemy" damage ratio
	 * @param enemy an enemy underlying entity (for a newly added or updated enemy)
	 * @param weightSoFar a weight of the enemy computed to the moment of this call
	 * @return a modified value of the enemy weight (can't be less that the supplied one)
	 */
	virtual float ModifyWeightForDamageRatio( const edict_t *enemy, float weightSoFar );

	// Returns attacker slot number
	int EnqueueAttacker( const edict_t *attacker, int damage );

	// Precache results of virtual Check* calls in these vars in PreThink()
	bool hasQuad { false };
	bool hasShell { false };
	float damageToBeKilled { 0.0f };

	enum {
		TRACKED_LIST_INDEX = TrackedEnemy::TRACKED_LIST_INDEX,
		ACTIVE_LIST_INDEX = TrackedEnemy::ACTIVE_LIST_INDEX
	};

	inline void Link( TrackedEnemy *enemy, int listIndex ) {
		assert( listIndex == TRACKED_LIST_INDEX || listIndex == ACTIVE_LIST_INDEX );

		// If there is an existing list head, set its prev link to the newly linked enemy
		if( TrackedEnemy *currHead = listHeads[listIndex] ) {
			currHead->listLinks[listIndex].prev = enemy;
		}

		TrackedEnemy::Links *enemyLinks = &enemy->listLinks[listIndex];
		// This is an "invariant" of list heads
		enemyLinks->prev = nullptr;
		// Set the next link of the newly linked enemy to the current head
		enemyLinks->next = this->listHeads[listIndex];
		// Set the list head to the newly linked enemy
		this->listHeads[listIndex] = enemy;
	}

	inline void Unlink( TrackedEnemy *enemy, int listIndex ) {
		assert( listIndex == TRACKED_LIST_INDEX || listIndex == ACTIVE_LIST_INDEX );

		TrackedEnemy::Links *enemyLinks = &enemy->listLinks[listIndex];
		// If a next enemy in list exists, set its prev link to the prev enemy of the unlinked enemy
		if( TrackedEnemy *nextInList = enemyLinks->next ) {
			nextInList->listLinks[listIndex].prev = enemyLinks->prev;
		}

		// If a prev enemy in list exists
		if( TrackedEnemy *prevInList = enemyLinks->prev ) {
			// The unlinked enemy must not be a list head
			assert( enemy != this->listHeads[listIndex] );
			// Set the prev enemy next link to the next enemy in list of the unlinked enemy
			prevInList->listLinks[listIndex].next = enemyLinks->next;
		} else {
			// Make sure this is the list head
			assert( enemy == this->listHeads[listIndex] );
			// Update the list head using the next enemy in list of the unlinked enemy
			this->listHeads[listIndex] = enemyLinks->next;
		}

		// Prevent using dangling links
		enemyLinks->Clear();
	}

protected:
	inline void LinkToTrackedList( TrackedEnemy *enemy ) {
		Link( enemy, TRACKED_LIST_INDEX );
	}

	inline void UnlinkFromTrackedList( TrackedEnemy *enemy ) {
		Unlink( enemy, TRACKED_LIST_INDEX );
	}

	inline void LinkToActiveList( TrackedEnemy *enemy ) {
		Link( enemy, ACTIVE_LIST_INDEX );
	}

	inline void UnlinkFromActiveList( TrackedEnemy *enemy ) {
		Unlink( enemy, ACTIVE_LIST_INDEX );
	}

	virtual void OnHurtByNewThreat( const edict_t *newThreat ) = 0;
	virtual bool CheckHasQuad() const = 0;
	virtual bool CheckHasShell() const = 0;
	virtual void OnEnemyRemoved( const TrackedEnemy *enemy ) = 0;
	// Used to compare enemy strength and pool owner
	virtual float ComputeDamageToBeKilled() const = 0;
	// Overridden method may give some additional weight to an enemy
	// (Useful for case when a bot should have some reinforcements)
	virtual float GetAdditionalEnemyWeight( const edict_t *bot, const edict_t *enemy ) const = 0;
	virtual void OnBotEnemyAssigned( const edict_t *bot, const TrackedEnemy *enemy ) = 0;

	inline bool HasQuad() const { return hasQuad; }
	inline bool HasShell() const { return hasShell; }

	inline float DamageToBeKilled() const { return damageToBeKilled; }

	inline static float DamageToKill( const edict_t *ent ) {
		return ::DamageToKill( ent, g_armor_protection->value, g_armor_degradation->value );
	}

	inline float AvgSkill() const { return avgSkill; }

	TrackedEnemy *TrackedEnemiesHead() { return listHeads[TRACKED_LIST_INDEX]; }
	TrackedEnemy *ActiveEnemiesHead() { return listHeads[ACTIVE_LIST_INDEX]; }
public:
	explicit AiEnemiesTracker( float avgSkill_ );
	virtual ~AiEnemiesTracker() = default;

	// If a weight is set > 0, this bot requires reinforcements
	virtual void SetBotRoleWeight( const edict_t *bot, float weight ) = 0;

	const TrackedEnemy *TrackedEnemiesHead() const { return listHeads[TRACKED_LIST_INDEX]; }
	const TrackedEnemy *ActiveEnemiesHead() const { return listHeads[ACTIVE_LIST_INDEX]; }

	unsigned NumTrackedEnemies() const { return numTrackedEnemies; }

	const TrackedEnemy *EnemyForEntity( const edict_t *ent ) const {
		return EnemyForEntity( ENTNUM( ent ) );
	}

	const TrackedEnemy *EnemyForEntity( int entNum ) const {
		assert( (unsigned)entNum < MAX_EDICTS );
		return entityToEnemyTable + entNum;
	}

	virtual void Frame() override;

	virtual void PreThink() override;
	virtual void Think() override;
	virtual void PostThink() override {
		prevThinkLevelTime = level.time;
	}

	void OnEnemyViewed( const edict_t *enemy );
	void OnEnemyOriginGuessed( const edict_t *enemy,
							   unsigned minMillisSinceLastSeen,
							   const float *guessedOrigin = nullptr );

	// Force the pool to forget the enemy (for example, when bot attitude to an enemy has been changed)
	void Forget( const edict_t *enemy );

	bool WillAssignAimEnemy() const;

	// Note that these methods modify this object state
	const TrackedEnemy *ChooseVisibleEnemy( const edict_t *challenger );
	const TrackedEnemy *ChooseLostOrHiddenEnemy( const edict_t *challenger, unsigned timeout = ( unsigned ) - 1 );

	void OnPain( const edict_t *bot, const edict_t *enemy, float kick, int damage );
	void OnEnemyDamaged( const edict_t *bot, const edict_t *target, int damage );

	void EnqueueTarget( const edict_t *target );

	// Returns zero if ent not found
	int64_t LastAttackedByTime( const edict_t *ent ) const;
	int64_t LastTargetTime( const edict_t *ent ) const;

	float TotalDamageInflictedBy( const edict_t *ent ) const;
};

inline int64_t TrackedEnemy::LastAttackedByTime() const { return parent->LastAttackedByTime( ent ); }
inline float TrackedEnemy::TotalInflictedDamage() const { return parent->TotalDamageInflictedBy( ent ); }

inline bool TrackedEnemy::IsInList( int listIndex ) const {
	return listLinks[listIndex].next || listLinks[listIndex].prev || this == parent->listHeads[listIndex];
}

#endif
