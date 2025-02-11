#include "snd_presets_registry.h"
#include "snd_local.h"
#include "snd_env_effects.h"

#include <algorithm>

// This is a workaround for macOS...
// TODO: Switch to using OpenAL SOFT headers across the entire codebase?
#include "../../third-party/openal-soft/include/AL/efx-presets.h"

#include "../qalgo/hash.h"

/**
 * Must be kept structurally-compatible with preset braced declarations from efx-presets.h
 * (We try to avoid using that crooked code style).
 */
struct EfxReverbPreset {
	float density;
	float diffusion;
	float gain;
	float gainHF;
	float gainLF;
	float decayTime;
	float decayHFRatio;
	float decayLFRatio;
	float reflectionsGain;
	float reflectionsDelay;
	float _unusedPan1[3];
	float lateReverbGain;
	float lateReverbDelay;
	float _unusedPan2[3];
	float echoTime;
	float echoDepth;
	float modulationTime;
	float modulationDepth;
	float airAbsorptionGainHF;
	float referenceHF;
	float referenceLF;
	float roomRolloffFactor;
	int decayHFLimit;
};

/**
 * This is a common supertype for all presets that are declared globally
 * and link themselves to the global presets cache
 * (so we do not have to maintain an array of presets and its length manually)
 */
struct EfxPresetEntry {
	const char *const name;
	EfxPresetEntry *nextInHashBin { nullptr };
	uint32_t nameHash { ~(uint32_t)0 };
	uint32_t nameLength { ~(uint32_t)0 };
	EfxReverbPreset preset;

	explicit EfxPresetEntry( const char *presetMacroName )
		: name( presetMacroName + sizeof( "EFX_REVERB_PRESET_" ) - 1 ) {
		assert( !Q_strnicmp( presetMacroName, "EFX_REVERB_PRESET_", name - presetMacroName ) );
		std::tie( nameHash, nameLength ) = ::GetHashAndLength( name );
	}

	void RegisterSelf() {
		EfxPresetsRegistry::instance.Register( this );
	}
};

EfxPresetsRegistry EfxPresetsRegistry::instance;

EfxPresetsRegistry::EfxPresetsRegistry() {
	std::fill_n( hashBins, sizeof( hashBins ) / sizeof( *hashBins ), nullptr );
}

void EfxPresetsRegistry::Register( EfxPresetEntry *entry ) {
	int binIndex = entry->nameHash % (int)( sizeof( hashBins ) / sizeof( *hashBins ) );
	entry->nextInHashBin = hashBins[binIndex];
	hashBins[binIndex] = entry;
}

const EfxPresetEntry *EfxPresetsRegistry::FindByName( const char *name ) const {
	uint32_t hash, length;
	std::tie( hash, length ) = GetHashAndLength( name );
	int binIndex = hash % (int)( sizeof( hashBins ) / sizeof( *hashBins ) );
	for( EfxPresetEntry *entry = hashBins[binIndex]; entry; entry = entry->nextInHashBin ) {
		if( entry->nameLength == length && !Q_strnicmp( name, entry->name, length ) ) {
			return entry;
		}
	}
	return nullptr;
}

void ReverbEffect::ReusePreset( const EfxPresetEntry *presetHandle ) {
	density = presetHandle->preset.density;
	diffusion = presetHandle->preset.diffusion;
	gain = presetHandle->preset.gain;
	gainHf = presetHandle->preset.gainHF;
	decayTime = presetHandle->preset.decayTime;
	reflectionsGain = presetHandle->preset.reflectionsGain;
	reflectionsDelay = presetHandle->preset.reflectionsDelay;
	lateReverbGain = presetHandle->preset.lateReverbGain;
	lateReverbDelay = presetHandle->preset.lateReverbDelay;
}

void EaxReverbEffect::ReusePreset( const EfxPresetEntry *presetHandle ) {
	ReverbEffect::ReusePreset( presetHandle );
	hfReference = presetHandle->preset.referenceHF;
	echoTime = presetHandle->preset.echoTime;
	echoDepth = presetHandle->preset.echoDepth;
}

#define DEFINE_PRESET( presetMacroName )                    \
	static struct EfxPresetsEntry_##presetMacroName         \
		: public EfxPresetEntry {                           \
		EfxPresetsEntry_##presetMacroName()                 \
			: EfxPresetEntry( #presetMacroName ) {          \
			this->preset = presetMacroName;                 \
			RegisterSelf();                                 \
    	}                                                   \
	} presetEntry_##presetMacroName;

// cat efx-presets.h | grep EFX_REV | awk '{b = sprintf("DEFINE_PRESET( %s );", $2); print b}'

DEFINE_PRESET( EFX_REVERB_PRESET_GENERIC );
DEFINE_PRESET( EFX_REVERB_PRESET_PADDEDCELL );
DEFINE_PRESET( EFX_REVERB_PRESET_ROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_BATHROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_LIVINGROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_STONEROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_AUDITORIUM );
DEFINE_PRESET( EFX_REVERB_PRESET_CONCERTHALL );
DEFINE_PRESET( EFX_REVERB_PRESET_CAVE );
DEFINE_PRESET( EFX_REVERB_PRESET_ARENA );
DEFINE_PRESET( EFX_REVERB_PRESET_HANGAR );
DEFINE_PRESET( EFX_REVERB_PRESET_CARPETEDHALLWAY );
DEFINE_PRESET( EFX_REVERB_PRESET_HALLWAY );
DEFINE_PRESET( EFX_REVERB_PRESET_STONECORRIDOR );
DEFINE_PRESET( EFX_REVERB_PRESET_ALLEY );
DEFINE_PRESET( EFX_REVERB_PRESET_FOREST );
DEFINE_PRESET( EFX_REVERB_PRESET_CITY );
DEFINE_PRESET( EFX_REVERB_PRESET_MOUNTAINS );
DEFINE_PRESET( EFX_REVERB_PRESET_QUARRY );
DEFINE_PRESET( EFX_REVERB_PRESET_PLAIN );
DEFINE_PRESET( EFX_REVERB_PRESET_PARKINGLOT );
DEFINE_PRESET( EFX_REVERB_PRESET_SEWERPIPE );
DEFINE_PRESET( EFX_REVERB_PRESET_UNDERWATER );
DEFINE_PRESET( EFX_REVERB_PRESET_DRUGGED );
DEFINE_PRESET( EFX_REVERB_PRESET_DIZZY );
DEFINE_PRESET( EFX_REVERB_PRESET_PSYCHOTIC );
DEFINE_PRESET( EFX_REVERB_PRESET_CASTLE_SMALLROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_CASTLE_SHORTPASSAGE );
DEFINE_PRESET( EFX_REVERB_PRESET_CASTLE_MEDIUMROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_CASTLE_LARGEROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_CASTLE_LONGPASSAGE );
DEFINE_PRESET( EFX_REVERB_PRESET_CASTLE_HALL );
DEFINE_PRESET( EFX_REVERB_PRESET_CASTLE_CUPBOARD );
DEFINE_PRESET( EFX_REVERB_PRESET_CASTLE_COURTYARD );
DEFINE_PRESET( EFX_REVERB_PRESET_CASTLE_ALCOVE );
DEFINE_PRESET( EFX_REVERB_PRESET_FACTORY_SMALLROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_FACTORY_SHORTPASSAGE );
DEFINE_PRESET( EFX_REVERB_PRESET_FACTORY_MEDIUMROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_FACTORY_LARGEROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_FACTORY_LONGPASSAGE );
DEFINE_PRESET( EFX_REVERB_PRESET_FACTORY_HALL );
DEFINE_PRESET( EFX_REVERB_PRESET_FACTORY_CUPBOARD );
DEFINE_PRESET( EFX_REVERB_PRESET_FACTORY_COURTYARD );
DEFINE_PRESET( EFX_REVERB_PRESET_FACTORY_ALCOVE );
DEFINE_PRESET( EFX_REVERB_PRESET_ICEPALACE_SMALLROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_ICEPALACE_SHORTPASSAGE );
DEFINE_PRESET( EFX_REVERB_PRESET_ICEPALACE_MEDIUMROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_ICEPALACE_LARGEROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_ICEPALACE_LONGPASSAGE );
DEFINE_PRESET( EFX_REVERB_PRESET_ICEPALACE_HALL );
DEFINE_PRESET( EFX_REVERB_PRESET_ICEPALACE_CUPBOARD );
DEFINE_PRESET( EFX_REVERB_PRESET_ICEPALACE_COURTYARD );
DEFINE_PRESET( EFX_REVERB_PRESET_ICEPALACE_ALCOVE );
DEFINE_PRESET( EFX_REVERB_PRESET_SPACESTATION_SMALLROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_SPACESTATION_SHORTPASSAGE );
DEFINE_PRESET( EFX_REVERB_PRESET_SPACESTATION_MEDIUMROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_SPACESTATION_LARGEROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_SPACESTATION_LONGPASSAGE );
DEFINE_PRESET( EFX_REVERB_PRESET_SPACESTATION_HALL );
DEFINE_PRESET( EFX_REVERB_PRESET_SPACESTATION_CUPBOARD );
DEFINE_PRESET( EFX_REVERB_PRESET_SPACESTATION_ALCOVE );
DEFINE_PRESET( EFX_REVERB_PRESET_WOODEN_SMALLROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_WOODEN_SHORTPASSAGE );
DEFINE_PRESET( EFX_REVERB_PRESET_WOODEN_MEDIUMROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_WOODEN_LARGEROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_WOODEN_LONGPASSAGE );
DEFINE_PRESET( EFX_REVERB_PRESET_WOODEN_HALL );
DEFINE_PRESET( EFX_REVERB_PRESET_WOODEN_CUPBOARD );
DEFINE_PRESET( EFX_REVERB_PRESET_WOODEN_COURTYARD );
DEFINE_PRESET( EFX_REVERB_PRESET_WOODEN_ALCOVE );
DEFINE_PRESET( EFX_REVERB_PRESET_SPORT_EMPTYSTADIUM );
DEFINE_PRESET( EFX_REVERB_PRESET_SPORT_SQUASHCOURT );
DEFINE_PRESET( EFX_REVERB_PRESET_SPORT_SMALLSWIMMINGPOOL );
DEFINE_PRESET( EFX_REVERB_PRESET_SPORT_LARGESWIMMINGPOOL );
DEFINE_PRESET( EFX_REVERB_PRESET_SPORT_GYMNASIUM );
DEFINE_PRESET( EFX_REVERB_PRESET_SPORT_FULLSTADIUM );
DEFINE_PRESET( EFX_REVERB_PRESET_SPORT_STADIUMTANNOY );
DEFINE_PRESET( EFX_REVERB_PRESET_PREFAB_WORKSHOP );
DEFINE_PRESET( EFX_REVERB_PRESET_PREFAB_SCHOOLROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_PREFAB_PRACTISEROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_PREFAB_OUTHOUSE );
DEFINE_PRESET( EFX_REVERB_PRESET_PREFAB_CARAVAN );
DEFINE_PRESET( EFX_REVERB_PRESET_DOME_TOMB );
DEFINE_PRESET( EFX_REVERB_PRESET_PIPE_SMALL );
DEFINE_PRESET( EFX_REVERB_PRESET_DOME_SAINTPAULS );
DEFINE_PRESET( EFX_REVERB_PRESET_PIPE_LONGTHIN );
DEFINE_PRESET( EFX_REVERB_PRESET_PIPE_LARGE );
DEFINE_PRESET( EFX_REVERB_PRESET_PIPE_RESONANT );
DEFINE_PRESET( EFX_REVERB_PRESET_OUTDOORS_BACKYARD );
DEFINE_PRESET( EFX_REVERB_PRESET_OUTDOORS_ROLLINGPLAINS );
DEFINE_PRESET( EFX_REVERB_PRESET_OUTDOORS_DEEPCANYON );
DEFINE_PRESET( EFX_REVERB_PRESET_OUTDOORS_CREEK );
DEFINE_PRESET( EFX_REVERB_PRESET_OUTDOORS_VALLEY );
DEFINE_PRESET( EFX_REVERB_PRESET_MOOD_HEAVEN );
DEFINE_PRESET( EFX_REVERB_PRESET_MOOD_HELL );
DEFINE_PRESET( EFX_REVERB_PRESET_MOOD_MEMORY );
DEFINE_PRESET( EFX_REVERB_PRESET_DRIVING_COMMENTATOR );
DEFINE_PRESET( EFX_REVERB_PRESET_DRIVING_PITGARAGE );
DEFINE_PRESET( EFX_REVERB_PRESET_DRIVING_INCAR_RACER );
DEFINE_PRESET( EFX_REVERB_PRESET_DRIVING_INCAR_SPORTS );
DEFINE_PRESET( EFX_REVERB_PRESET_DRIVING_INCAR_LUXURY );
DEFINE_PRESET( EFX_REVERB_PRESET_DRIVING_FULLGRANDSTAND );
DEFINE_PRESET( EFX_REVERB_PRESET_DRIVING_EMPTYGRANDSTAND );
DEFINE_PRESET( EFX_REVERB_PRESET_DRIVING_TUNNEL );
DEFINE_PRESET( EFX_REVERB_PRESET_CITY_STREETS );
DEFINE_PRESET( EFX_REVERB_PRESET_CITY_SUBWAY );
DEFINE_PRESET( EFX_REVERB_PRESET_CITY_MUSEUM );
DEFINE_PRESET( EFX_REVERB_PRESET_CITY_LIBRARY );
DEFINE_PRESET( EFX_REVERB_PRESET_CITY_UNDERPASS );
DEFINE_PRESET( EFX_REVERB_PRESET_CITY_ABANDONED );
DEFINE_PRESET( EFX_REVERB_PRESET_DUSTYROOM );
DEFINE_PRESET( EFX_REVERB_PRESET_CHAPEL );
DEFINE_PRESET( EFX_REVERB_PRESET_SMALLWATERROOM );
