#include "snd_leaf_props_cache.h"
#include "snd_effect_sampler.h"
#include "snd_computation_host.h"
#include "snd_presets_registry.h"
#include "../qalgo/SingletonHolder.h"

#include <algorithm>
#include <new>
#include <limits>
#include <cstdlib>

class LeafPropsIOHelper {
protected:
	int leafCounter { 0 };
public:
	virtual ~LeafPropsIOHelper() = default;
};

struct EfxPresetEntry;

class LeafPropsReader final: public CachedComputationReader, public LeafPropsIOHelper {
public:
	using PresetHandle = const EfxPresetEntry *;
private:
	bool ParseLine( char *line, unsigned lineLength, LeafProps *props, PresetHandle *presetHandle );
public:
	explicit LeafPropsReader( const LeafPropsCache *parent_, int fileFlags )
		: CachedComputationReader( parent_, fileFlags, true ) {}

	enum Status {
		OK,
		DONE,
		ERROR
	};

	Status ReadNextProps( LeafProps *props, PresetHandle *presetRef );
};

class LeafPropsWriter final: public CachedComputationWriter, public LeafPropsIOHelper {
public:
	explicit LeafPropsWriter( const LeafPropsCache *parent_ )
		: CachedComputationWriter( parent_ ) {}

	bool WriteProps( const LeafProps &props );
};

LeafPropsReader::Status LeafPropsReader::ReadNextProps( LeafProps *props, PresetHandle *presetHandle ) {
	if( fsResult < 0 ) {
		return ERROR;
	}

	SkipWhiteSpace();
	if( !*dataPtr ) {
		return DONE;
	}

	char *nextLine = strchr( dataPtr, '\n' );
	if( nextLine ) {
		*nextLine = '\0';
	}

	size_t lineLength = nextLine - dataPtr;
	if( lineLength > std::numeric_limits<uint32_t>::max() ) {
		dataPtr = nullptr;
		return ERROR;
	}

	if( !ParseLine( dataPtr, (uint32_t)lineLength, props, presetHandle ) ) {
		dataPtr = nullptr;
		return ERROR;
	}

	if( nextLine ) {
		dataPtr = nextLine + 1;
	} else {
		fileData[0] = '\0';
		dataPtr = fileData;
	}

	leafCounter++;

	return OK;
}

bool LeafPropsReader::ParseLine( char *line, unsigned lineLength, LeafProps *props, PresetHandle *presetHandle ) {
	char *linePtr = line;
	char *endPtr = nullptr;

	// Parsing errors are caught implicitly by the counter comparison below
	const auto num = (int)std::strtol( linePtr, &endPtr, 10 );
	if( !*endPtr || !isspace( *endPtr ) ) {
		return false;
	}

	if( num != leafCounter ) {
		return false;
	}

	// Trim whitespace at start and end. This is mandatory for FindPresetByName() call.

	linePtr = endPtr;
	while( ( linePtr - line < lineLength ) && ::isspace( *linePtr ) ) {
		linePtr++;
	}

	endPtr = line + lineLength;
	while( endPtr > linePtr && ( !*endPtr || ::isspace( *endPtr ) ) ) {
		endPtr--;
	}

	if( endPtr <= linePtr ) {
		return false;
	}

	// Terminate after the character we have stopped at
	*++endPtr = '\0';

	// Save for recovery in case of leaf props scanning failure
	const char *maybePresetName = linePtr;
	int lastPartNum = 0;
	float parts[6];
	for(; lastPartNum < 6; ++lastPartNum ) {
		double v = ::strtod( linePtr, &endPtr );
		if( endPtr == linePtr ) {
			break;
		}
		// The value is numeric but is out of range.
		// It is not a preset for sure, return with failure immediately.
		// The range is [0, 1] for first 4 parts and [1000, 20000] for last 2 parts.
		if( lastPartNum <= 3 ) {
			if( v < 0.0 || v > 1.0 ) {
				return false;
			}
		} else if( v < 1000.0f || v > 20000.0f ) {
			return false;
		}
		if( lastPartNum != 5 ) {
			if( !isspace( *endPtr ) ) {
				return false;
			}
		} else if( *endPtr ) {
			return false;
		}
		parts[lastPartNum] = (float)v;
		linePtr = endPtr + 1;
	}

	if( !lastPartNum ) {
		if( ( *presetHandle = EfxPresetsRegistry::Instance()->FindByName( maybePresetName ) ) ) {
			return true;
		}
		// A verbose reporting of an illegal preset name is important as it is typed by a designer manually
		Com_Printf( S_COLOR_YELLOW "The token `%s` for leaf %d is not a valid preset name\n", maybePresetName, num );
		return false;
	}

	// There should be 6 parts
	if( lastPartNum != 6 ) {
		return false;
	}

	// The HF reference range must be valid
	if( parts[4] > parts[5] ) {
		return false;
	}

	props->SetRoomSizeFactor( parts[0] );
	props->SetSkyFactor( parts[1] );
	props->SetWaterFactor( parts[2] );
	props->SetMetalFactor( parts[3] );
	props->minHfRef = (uint16_t)parts[4];
	props->maxHfRef = (uint16_t)parts[5];
	return true;
}

bool LeafPropsWriter::WriteProps( const LeafProps &props ) {
	if( fsResult < 0 ) {
		return false;
	}

	char buffer[MAX_STRING_CHARS];
	int charsPrinted = Q_snprintfz( buffer, sizeof( buffer ),
									"%d %.2f %.2f %.2f %.2f %d %d\r\n", leafCounter,
									props.RoomSizeFactor(), props.SkyFactor(),
									props.WaterFactor(), props.MetalFactor(),
									(int)props.MinHfRef(), (int)props.MaxHfRef() );

	int charsWritten = FS_Write( buffer, (size_t)charsPrinted, fd );
	if( charsWritten != charsPrinted ) {
		fsResult = -1;
		return false;
	}

	leafCounter++;
	return true;
}

static SingletonHolder<LeafPropsCache> instanceHolder;

LeafPropsCache *LeafPropsCache::Instance() {
	return instanceHolder.Instance();
}

void LeafPropsCache::Init() {
	instanceHolder.Init();
}

void LeafPropsCache::Shutdown() {
	instanceHolder.Shutdown();
}

void LeafPropsCache::ResetExistingState() {
	if( leafProps ) {
		S_Free( leafProps );
	}
	if( leafPresets ) {
		S_Free( leafPresets );
	}

	leafProps = (LeafProps *)S_Malloc( sizeof( LeafProps ) * NumLeafs() );
	leafPresets = (PresetHandle *)S_Malloc( sizeof( PresetHandle ) * NumLeafs() );
}

bool LeafPropsCache::TryReadFromFile( int fsFlags ) {
	LeafPropsReader reader( this, fsFlags );
	return TryReadFromFile( &reader );
}

bool LeafPropsCache::TryReadFromFile( LeafPropsReader *reader ) {
	int numReadProps = 0;
	for(;; ) {
		LeafProps props;
		PresetHandle presetHandle = nullptr;
		switch( reader->ReadNextProps( &props, &presetHandle ) ) {
			case LeafPropsReader::OK:
				if( numReadProps + 1 > NumLeafs() ) {
					return false;
				}
				this->leafProps[numReadProps] = props;
				this->leafPresets[numReadProps] = presetHandle;
				numReadProps++;
				break;
			case LeafPropsReader::DONE:
				return numReadProps == NumLeafs();
			default:
				return false;
		}
	}
}

bool LeafPropsCache::SaveToCache() {
	LeafPropsWriter writer( this );
	for( int i = 0, end = NumLeafs(); i < end; ++i ) {
		if( !writer.WriteProps( this->leafProps[i] ) ) {
			return false;
		}
	}

	return true;
}

struct LeafPropsBuilder {
	float roomSizeFactor { 0.0f };
	float skyFactor { 0.0f };
	float waterFactor { 0.0f };
	float metalFactor { 0.0f };
	int numProps { 0 };

	void operator+=( const LeafProps &propsToAdd ) {
		roomSizeFactor += propsToAdd.RoomSizeFactor();
		skyFactor += propsToAdd.SkyFactor();
		waterFactor += propsToAdd.WaterFactor();
		metalFactor += propsToAdd.MetalFactor();
		++numProps;
	}

	LeafProps Result() {
		LeafProps result;
		if( numProps ) {
			float scale = 1.0f / numProps;
			roomSizeFactor *= scale;
			skyFactor *= scale;
			waterFactor *= scale;
			metalFactor *= scale;
		}
		result.SetRoomSizeFactor( roomSizeFactor );
		result.SetSkyFactor( skyFactor );
		result.SetWaterFactor( waterFactor );
		result.SetMetalFactor( metalFactor );
		return result;
	}
};

class LeafPropsSampler: public GenericRaycastSampler {
	static constexpr unsigned MAX_RAYS = 1024;

	// Inline buffers for algorithm intermediates.
	// Note that instances of this class should be allocated dynamically, so do not bother about arrays size.
	vec3_t dirs[MAX_RAYS];
	float distances[MAX_RAYS];
	const unsigned maxRays;
public:
	explicit LeafPropsSampler( bool fastAndCoarse = false )
		: maxRays( fastAndCoarse ? MAX_RAYS / 2 : MAX_RAYS ) {
		SetupSamplingRayDirs( dirs, maxRays );
	}

	bool ComputeLeafProps( const vec3_t origin, LeafProps *props );
};

class LeafPropsComputationTask: public ParallelComputationHost::PartialTask {
	friend class LeafPropsCache;

	LeafProps *const leafProps;
	LeafPropsSampler sampler;
	int leafsRangeBegin { -1 };
	int leafsRangeEnd { -1 };
	const bool fastAndCoarse;

	LeafProps ComputeLeafProps( int leafNum );
public:
	explicit LeafPropsComputationTask( LeafProps *leafProps_, bool fastAndCoarse_ )
		: leafProps( leafProps_ ), sampler( fastAndCoarse_ ), fastAndCoarse( fastAndCoarse_ ) {}

	void Exec() override;
};

struct {
	const char *map;
	float min, max;
} hardcodedHfReferenceBounds[] = {
	{ "wdm2",  4500.0f, 10000.0f },
	{ "wdm3",  3000.0f, 5000.0f },
	{ "wdm4",  5000.0f, 15000.0f },
	{ "wdm6",  4500.0f, 10000.0f },
	{ "wdm8",  5000.0f, 12500.0f },
	{ "wdm9",  4000.0f, 10000.0f },
	{ "wdm11", 3000.0f, 4000.0f },
	{ "wdm12", 3000.0f, 5000.0f },
	{ "wdm13", 4500.0f, 10000.0f },
	{ "wdm14", 4500.0f, 10000.0f },
	{ "wdm15", 5000.0f, 12500.0f },
	{ "wdm16", 5000.0f, 10000.0f },
	{ "wdm17", 5000.0f, 10000.0f },
	{ "wbomb1", 3000.0f, 4000.0f },
	{ "wbomb2", 4000.0f, 10000.0f },
	{ "wbomb3", 3000.0f, 5000.0f },
	{ "wca1", 5000.0f, 12500.0f },
	{ "wamphi1", 3500.0f, 7500.0f },
	{ "wda1", 3500.0f, 7500.0f },
	{ "wda2", 4500.0f, 7500.0f },
	{ "wda3", 3000.0f, 5000.0f },
	{ "wda4", 5000.0f, 9000.0f }
};

bool LeafPropsCache::ComputeNewState( bool fastAndCoarse ) {
	leafProps[0] = LeafProps();

	const int actualNumLeafs = NumLeafs();

	ComputationHostLifecycleHolder computationHostLifecycleHolder;
	auto *const computationHost = computationHostLifecycleHolder.Instance();

	// Up to 64 parallel tasks are supported.
	// Every task does not consume lots of memory compared to computation of the sound propagation table.
	LeafPropsComputationTask *submittedTasks[64];
	int actualNumTasks = 0;
	// Do not spawn more tasks than the actual number of leaves. Otherwise it fails for very small maps
	const int suggestedNumTasks = std::min( actualNumLeafs, std::min( computationHost->SuggestNumberOfTasks(), 64 ) );
	for( int i = 0; i < suggestedNumTasks; ++i ) {
		void *taskMem = S_Malloc( sizeof( LeafPropsComputationTask ) );
		// Never really happens with S_Malloc()... Use just malloc() instead?
		if( !taskMem ) {
			break;
		}
		auto *const task = new( taskMem )LeafPropsComputationTask( leafProps, fastAndCoarse );
		if( !computationHost->TryAddTask( task ) ) {
			break;
		}
		submittedTasks[actualNumTasks++] = task;
	}

	if( !actualNumTasks ) {
		return false;
	}

	const int step = actualNumLeafs / actualNumTasks;
	assert( step > 0 );
	int rangeBegin = 1;
	for( int i = 0; i < actualNumTasks; ++i ) {
		auto *const task = submittedTasks[i];
		task->leafsRangeBegin = rangeBegin;
		if( i + 1 != actualNumTasks ) {
			rangeBegin += step;
			task->leafsRangeEnd = rangeBegin;
		} else {
			task->leafsRangeEnd = actualNumLeafs;
		}
	}

	computationHost->Exec();

	// Should be derived from metalness/smoothness of surfaces for every leaf.
	// Currently just try using a global hardcoded value for a map or using a default one.
	char buffer[MAX_CONFIGSTRING_CHARS];
	memcpy( buffer, S_GetConfigString( CS_WORLDMODEL ), MAX_CONFIGSTRING_CHARS );
	COM_StripExtension( buffer );
	const char *map = COM_FileBase( buffer );
	std::pair<float, float> hfRefBounds { 3000.0f, 5500.0f };
	for( const auto &hardcoded: hardcodedHfReferenceBounds ) {
		if( !Q_stricmp( map, hardcoded.map ) ) {
			hfRefBounds = std::make_pair( hardcoded.min, hardcoded.max );
			break;
		}
	}

	// Set values for the zero leaf as well.
	// Otherwise the file loading fails at illegal value and we do not want to complicate the loader code.
	for( int i = 0; i < actualNumLeafs; ++i ) {
		float minHfRef = hfRefBounds.first - 1000.0f * leafProps->SkyFactor();
		float maxHfRef = hfRefBounds.second - 3000.0f * leafProps->SkyFactor();
		Q_clamp( minHfRef, 1500.0f, 5000.0f );
		Q_clamp( maxHfRef, minHfRef, 20000.0f );
		std::tie( leafProps[i].minHfRef, leafProps[i].maxHfRef ) = std::make_pair( minHfRef, maxHfRef );
	}

	return true;
}

void LeafPropsComputationTask::Exec() {
	// Check whether the range has been assigned
	assert( leafsRangeBegin > 0 );
	assert( leafsRangeEnd > leafsRangeBegin );

	for( int i = leafsRangeBegin; i < leafsRangeEnd; ++i ) {
		leafProps[i] = ComputeLeafProps( i );
	}
}

bool LeafPropsSampler::ComputeLeafProps( const vec3_t origin, LeafProps *props ) {
	ResetMutableState( dirs, nullptr, distances, origin );
	this->numPrimaryRays = maxRays;

	EmitPrimaryRays();
	// Happens mostly if rays outgoing from origin start in solid
	if( !numPrimaryHits ) {
		return false;
	}

	props->SetRoomSizeFactor( ComputeRoomSizeFactor() );
	props->SetWaterFactor( ComputeWaterFactor() );
	props->SetSkyFactor( ComputeSkyFactor() );
	props->SetMetalFactor( ComputeMetalFactor() );
	return true;
}

LeafProps LeafPropsComputationTask::ComputeLeafProps( int leafNum ) {
	const vec3_t *leafBounds = S_GetLeafBounds( leafNum );
	const float *leafMins = leafBounds[0];
	const float *leafMaxs = leafBounds[1];

	vec3_t extent;
	VectorSubtract( leafMaxs, leafMins, extent );
	const float squareExtent = VectorLengthSquared( extent );

	LeafProps tmpProps;
	LeafPropsBuilder propsBuilder;

	int maxSamples;
	if( fastAndCoarse ) {
		// Use a linear growth in this case
		maxSamples = (int)( 1 + ( sqrtf( squareExtent ) / 256.0f ) );
	} else {
		// Let maxSamples grow quadratic depending linearly of square extent value.
		// Cubic growth is more "natural" but leads to way too expensive computations.
		maxSamples = (int)( 2 + ( squareExtent / ( 256.0f * 256.0f ) ) );
	}

	int numSamples = 0;
	int numAttempts = 0;

	// Always start at the leaf center
	vec3_t point;
	VectorMA( leafMins, 0.5f, extent, point );
	bool hasValidPoint = true;

	while( numSamples < maxSamples ) {
		numAttempts++;
		// Attempts may fail for 2 reasons: can't pick a valid point and a point sampling has failed.
		// Use the shared loop termination condition for these cases.
		if( numAttempts > 7 + 2 * maxSamples ) {
			return propsBuilder.Result();
		}

		if( !hasValidPoint ) {
			for( int i = 0; i < 3; ++i ) {
				point[i] = leafMins[i] + ( 0.1f + 0.9f * EffectSamplers::SamplingRandom() ) * extent[i];
			}

			// Check whether the point is really in leaf (the leaf is not a box but is inscribed in the bounds box)
			// Some bogus leafs (that are probably degenerate planes) lead to infinite looping otherwise
			if( S_PointLeafNum( point ) != leafNum ) {
				continue;
			}

			hasValidPoint = true;
		}

		// Might fail if the rays outgoing from the point start in solid
		if( sampler.ComputeLeafProps( point, &tmpProps ) ) {
			propsBuilder += tmpProps;
			numSamples++;
			// Invalidate previous point used for sampling
			hasValidPoint = false;
			continue;
		}
	}

	return propsBuilder.Result();
}
