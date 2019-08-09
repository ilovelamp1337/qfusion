/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "ServerList.h"
#include "../qalgo/hash.h"
#include "../qalgo/Links.h"
#include "../qalgo/SingletonHolder.h"
#include "../qcommon/qcommon.h"
#include "client.h"

#include <cinttypes>
#include <cstdlib>
#include <limits>

uint32_t NET_AddressHash( const netadr_t & );

static qmutex_t *resolverMutex;
// An additional helper for the resolver thread
static std::atomic<bool> initialized;
static std::atomic<int> numActiveResolvers;
static SingletonHolder<ServerList> serverListHolder;

void ServerList::Init() {
	serverListHolder.Init();
	initialized = true;

	const char *masterServersStr = Cvar_String( "masterservers" );
	if( !*masterServersStr ) {
		return;
	}

	// count the number of master servers
	int numMasters = 0;
	for( const char *ptr = masterServersStr; ptr; ) {
		char *masterAddress = COM_Parse( &ptr );
		if( !*masterAddress ) {
			break;
		}
		numMasters++;
	}

	if( !( resolverMutex = QMutex_Create() ) ) {
		return;
	}

	// Set this first as some resolvers may return almost immediately
	::numActiveResolvers = std::min( (int)MAX_MASTER_SERVERS, numMasters );

	int numSpawnedResolvers = 0;
	for( const char *ptr = masterServersStr; ptr; ) {
		if( numSpawnedResolvers == MAX_MASTER_SERVERS ) {
			break;
		}
		char *addressString = COM_Parse( &ptr );
		if( !*addressString ) {
			break;
		}
		size_t len = ::strlen( addressString );
		char *s = new char[len + 1];
		memcpy( s, addressString, len );
		s[len] = '\0';
		QThread_Create( &ServerList::ResolverThreadFunc, s );
		numSpawnedResolvers++;
	}
}

void ServerList::Shutdown() {
	initialized = false;
	serverListHolder.Shutdown();
	// The mutex is not disposed intentionally
}

ServerList *ServerList::Instance() {
	return serverListHolder.Instance();
}

void *ServerList::ResolverThreadFunc( void *param ) {
	const char *string = (char *)param;
	netadr_t address;
	NET_StringToAddress( string, &address );
	delete string;

	if( address.type != NA_IP && address.type != NA_IP6 ) {
		Com_Printf( "Failed to resolve master server address: %s\n", string );
		return nullptr;
	}

	if( NET_GetAddressPort( &address ) == 0 ) {
		NET_SetAddressPort( &address, PORT_MASTER );
	}

	int numResolversLeft = 9999;
	QMutex_Lock( resolverMutex );
	if( initialized ) {
		Instance()->AddMasterServer( address );
		numResolversLeft = numActiveResolvers.fetch_sub( 1 );
	}
	QMutex_Unlock( resolverMutex );

	if( !numResolversLeft ) {
		QMutex_Destroy( &resolverMutex );
	}

	return nullptr;
}

class ServerInfoParser {
	// These fields are used to pass info during parsing
	ServerInfo *info { nullptr };
	uint64_t lastAcknowledgedChallenge { 0 };

	// This field is parsed along with info KV pairs
	uint64_t parsedChallenge { 0 };

	wsw::HashedStringView keyView;
	wsw::StringView valueView;

	const char *chars { nullptr };
	unsigned index { 0 };
	unsigned bytesLeft { 0 };

	typedef bool ( ServerInfoParser::*HandlerMethod )( const wsw::StringView & );

	struct TokenHandler {
		wsw::HashedStringView key;
		TokenHandler *nextInHashBin { nullptr };
		HandlerMethod method { nullptr };

		TokenHandler() = default;

		TokenHandler( const char *key_, HandlerMethod handler_ )
			: key( key_ ), method( handler_ ) {}

		bool CanHandle( const wsw::HashedStringView &key_ ) const {
			return key.EqualsIgnoreCase( key_ );
		}

		bool Handle( ServerInfoParser *parser, const wsw::StringView &value ) const {
			return ( parser->*method )( value );
		}
	};

	static constexpr auto HASH_MAP_SIZE = 17;
	TokenHandler *handlersHashMap[HASH_MAP_SIZE];
	static constexpr auto MAX_HANDLERS = 16;
	TokenHandler handlersStorage[MAX_HANDLERS];
	unsigned numHandlers { 0 };

	void AddHandler( const char *command, HandlerMethod handler );
	void LinkHandlerEntry( TokenHandler *handlerEntry );

	bool HandleChallenge( const wsw::StringView &value );
	bool HandleHostname( const wsw::StringView & );
	bool HandleMaxClients( const wsw::StringView & );
	bool HandleMapname( const wsw::StringView & );
	bool HandleMatchTime( const wsw::StringView & );
	bool HandleMatchScore( const wsw::StringView & );
	bool HandleGameFS(const wsw::StringView &);
	bool HandleGametype( const wsw::StringView & );
	bool HandleNumBots( const wsw::StringView & );
	bool HandleNumClients( const wsw::StringView & );
	bool HandleNeedPass( const wsw::StringView & );

	template<typename T>
	inline bool HandleInteger( const wsw::StringView &, T *result ) const;
	template<typename T, typename I>
	inline bool ParseInteger( const char *value, T *result, I ( *func )( const char *, char **, int ) ) const;

	template<unsigned N>
	inline bool HandleString( const wsw::StringView &, StaticString<N> *result ) const;

	bool ScanForKey();
	bool ScanForValue();
public:
	ServerInfoParser();

	bool Parse( msg_t *msg_, ServerInfo *info_, uint64_t lastAcknowledgedChallenge_ );
	bool HandleKVPair();

	inline uint64_t ParsedChallenge() const { return parsedChallenge; }
};

void ServerList::ParseGetServersExtResponse( const socket_t *socket, const netadr_t &address, msg_t *msg ) {
	if( !listener ) {
		return;
	}

	constexpr const char *function = "ServerList::ParseGetServersExtResponse()";

	// TODO: Check whether the packet came from an actual master server
	// TODO: Is it possible at all? (We're talking about UDP packets).

	MSG_BeginReading( msg );
	(void)MSG_ReadInt32( msg );

	static const auto prefixLen = sizeof( "getserversExtResponse" ) - 1;
	if( !MSG_SkipData( msg, prefixLen ) ) {
		return;
	}

	for(;; ) {
		if( !MSG_BytesLeft( msg ) ) {
			return;
		}

		netadr_t readAddress;
		int numAddressBytes;
		netadrtype_t addressType;
		uint8_t *destBytes;
		uint16_t *destPort;

		const char startPrefix = (char)MSG_ReadInt8( msg );
		if( startPrefix == '\\' ) {
			numAddressBytes = 4;
			addressType = NA_IP;
			destBytes = readAddress.address.ipv4.ip;
			destPort = &readAddress.address.ipv4.port;
		} else if( startPrefix == '/' ) {
			numAddressBytes = 16;
			addressType = NA_IP6;
			destBytes = readAddress.address.ipv6.ip;
			destPort = &readAddress.address.ipv6.port;
		} else {
			Com_DPrintf( "%s: Warning: Illegal address prefix `%c`\n", function, startPrefix );
			return;
		}

		if( MSG_BytesLeft( msg ) < numAddressBytes + 2 ) {
			Com_DPrintf( "%s: Warning: Too few bytes in message for an address\n", function );
			return;
		}

		const uint8_t *addressBytes = msg->data + msg->readcount;
		const uint8_t *portBytes = addressBytes + numAddressBytes;

		// Stop parsing on a zero port. Its weird but that's what actual engine sources do.
		// Note: the comment in the old code says "both endians need this swapped"
		const uint16_t port = ( (uint16_t)portBytes[1] << 8 ) | portBytes[0];
		if( !( portBytes[0] | portBytes[1] ) ) {
			return;
		}

		NET_InitAddress( &readAddress, addressType );
		::memcpy( destBytes, addressBytes, numAddressBytes );
		*destPort = port;
		OnServerAddressReceived( readAddress );
		MSG_SkipData( msg, numAddressBytes + 2 );
	}
}

void ServerList::ParseGetInfoResponse( const socket_t *socket, const netadr_t &address, msg_t *msg ) {
	if( !listener ) {
		return;
	}

	PolledGameServer *const server = FindServerByAddress( address );
	if( !server ) {
		// Be silent in this case, it can legally occur if a server times out and a packet arrives then
		return;
	}

	ServerInfo *const parsedServerInfo = ParseServerInfo( msg, server );
	if( !parsedServerInfo ) {
		return;
	}

	if( MSG_BytesLeft( msg ) > 0 ) {
		Com_Printf( "ServerList::ParseGetInfoResponse(): There are extra bytes in the message\n" );
		delete parsedServerInfo;
		return;
	}

	parsedServerInfo->hasPlayerInfo = false;
	OnNewServerInfo( server, parsedServerInfo );
}

ServerInfo *ServerList::ParseServerInfo( msg_t *msg, PolledGameServer *server ) {
	auto *const info = new ServerInfo;
	if( serverInfoParser->Parse( msg, info, server->lastAcknowledgedChallenge ) ) {
		server->lastAcknowledgedChallenge = serverInfoParser->ParsedChallenge();
		return info;
	}

	return nullptr;
}

ServerInfoParser::ServerInfoParser() {
	memset( handlersHashMap, 0, sizeof( handlersHashMap ) );

	AddHandler( "challenge", &ServerInfoParser::HandleChallenge );
	AddHandler( "sv_hostname", &ServerInfoParser::HandleHostname );
	AddHandler( "sv_maxclients", &ServerInfoParser::HandleMaxClients );
	AddHandler( "mapname", &ServerInfoParser::HandleMapname );
	AddHandler( "g_match_time", &ServerInfoParser::HandleMatchTime );
	AddHandler( "g_match_score", &ServerInfoParser::HandleMatchScore );
	AddHandler( "fs_game", &ServerInfoParser::HandleGameFS );
	AddHandler( "gametype", &ServerInfoParser::HandleGametype );
	AddHandler( "bots", &ServerInfoParser::HandleNumBots );
	AddHandler( "clients", &ServerInfoParser::HandleNumClients );
	AddHandler( "g_needpass", &ServerInfoParser::HandleNeedPass );
}

bool ServerInfoParser::ScanForKey() {
	uint32_t hash = 0;
	unsigned start = index;
	while( index < bytesLeft && chars[index] != '\\' ) {
		hash = NextHashStep( hash, chars[index] );
		index++;
	}

	// If no '\\' has been found before end of data
	if( index >= bytesLeft ) {
		return false;
	}

	// Otherwise we have met a '\\'
	keyView = wsw::HashedStringView( chars + start, index - start, hash );
	index++;
	return true;
}

bool ServerInfoParser::ScanForValue() {
	unsigned start = index;
	while( index < bytesLeft && chars[index] != '\\' && chars[index] != '\n' ) {
		index++;
	}

	// If we have ran out of range without stopping at termination characters
	if( index >= bytesLeft ) {
		return false;
	}

	valueView = wsw::StringView( chars + start, index - start );
	return true;
}

bool ServerInfoParser::Parse( msg_t *msg_, ServerInfo *info_, uint64_t lastAcknowledgedChallenge_ ) {
	this->info = info_;
	this->lastAcknowledgedChallenge = lastAcknowledgedChallenge_;
	this->parsedChallenge = 0;
	this->index = 0;
	this->chars = (const char *)( msg_->data + msg_->readcount );
	this->bytesLeft = MSG_BytesLeft( msg_ );

	constexpr const char *missingChallenge = "Warning: ServerList::ServerInfoParser::Parse(): missing a challenge\n";

	for(;; ) {
		if( index >= bytesLeft ) {
			msg_->readcount += index;
			if( !parsedChallenge ) {
				Com_DPrintf( missingChallenge );
				return false;
			}
			return true;
		}

		// Expect new '\\'
		if( chars[index] != '\\' ) {
			return false;
		}
		index++;

		// Expect a key
		if( !ScanForKey() ) {
			return false;
		}

		// Expect a value
		if( !ScanForValue() ) {
			return false;
		}

		// Now try handling the pair matched in the character input
		if( !HandleKVPair() ) {
			return false;
		}

		// If we have stopped at \n while scanning for value
		if( chars[index] == '\n' ) {
			msg_->readcount += index;
			if( !parsedChallenge ) {
				Com_DPrintf( missingChallenge );
				return false;
			}
			return true;
		}
	}
}

void ServerInfoParser::AddHandler( const char *command, HandlerMethod handler ) {
	if( numHandlers < MAX_HANDLERS ) {
		void *mem = &handlersStorage[numHandlers++];
		LinkHandlerEntry( new( mem )TokenHandler( command, handler ) );
		return;
	}
	Com_Printf( "ServerList::ServerInfoParser::AddHandler(): too many handlers\n" );
	abort();
}

void ServerInfoParser::LinkHandlerEntry( TokenHandler *handlerEntry ) {
	unsigned hashBinIndex = handlerEntry->key.Hash() % HASH_MAP_SIZE;

	handlerEntry->nextInHashBin = handlersHashMap[hashBinIndex];
	handlersHashMap[hashBinIndex] = handlerEntry;
}

bool ServerInfoParser::HandleKVPair() {
	unsigned hashBinIndex = this->keyView.Hash() % HASH_MAP_SIZE;
	for( TokenHandler *entry = handlersHashMap[hashBinIndex]; entry; entry = entry->nextInHashBin ) {
		if( entry->CanHandle( this->keyView ) ) {
			return entry->Handle( this, this->valueView );
		}
	}

	// If the key is unknown, return with success.
	// Only parsing errors for known keys should terminate parsing.
	return true;
}

template <typename T>
inline bool ServerInfoParser::HandleInteger( const wsw::StringView &value, T *result ) const {
	if( sizeof( T ) > 4 ) {
		if( ( ( T )-1 ) != std::numeric_limits<T>::max() ) {
			return ParseInteger<T, long long>( value.Data(), result, strtoll );
		}
		return ParseInteger<T, unsigned long long>( value.Data(), result, strtoull );
	}

	if( ( ( T )-1 ) != std::numeric_limits<T>::max() ) {
		return ParseInteger<T, long>( value.Data(), result, strtol );
	}
	return ParseInteger<T, unsigned long>( value.Data(), result, strtoul );
}

template <typename T, typename I>
bool ServerInfoParser::ParseInteger( const char *value, T *result, I ( *func )( const char *, char **, int ) ) const {
	char *endptr;
	I parsed = func( value, &endptr, 10 );

	if( parsed == std::numeric_limits<I>::min() || parsed == std::numeric_limits<I>::max() ) {
		if( errno == ERANGE ) {
			return false;
		}
	}

	*result = (T)parsed;
	return true;
}

template<unsigned N>
bool ServerInfoParser::HandleString( const wsw::StringView &value, StaticString<N> *result ) const {
	// Its better to pass a caller name but we do not really want adding extra parameters to this method
	constexpr const char *function = "ServerList::ServerInfoParser::HandleString()";

	const char *s = value.Data();
	if( value.Size() > std::numeric_limits<uint8_t>::max() ) {
		Com_Printf( "Warning: %s: the value `%s` exceeds result size limits\n", function, s );
		return false;
	}

	if( value.Size() >= result->Capacity() ) {
		Com_Printf( "Warning: %s: the value `%s` exceeds a result capacity %d\n", function, s, (int)result->Capacity() );
		return false;
	}

	result->SetFrom( value );
	return true;
}

bool ServerInfoParser::HandleChallenge( const wsw::StringView &value ) {
	if( !HandleInteger( value, &parsedChallenge ) ) {
		return false;
	}
	return parsedChallenge > lastAcknowledgedChallenge;
}

bool ServerInfoParser::HandleHostname( const wsw::StringView &value ) {
	return HandleString( value, &info->serverName );
}

bool ServerInfoParser::HandleMaxClients( const wsw::StringView &value ) {
	return HandleInteger( value, &info->maxClients );
}

bool ServerInfoParser::HandleMapname( const wsw::StringView &value ) {
	return HandleString( value, &info->mapname );
}

static inline bool ScanInt( const char *s, char **endptr, int *result ) {
	long maybeResult = strtol( s, endptr, 10 );

	if( maybeResult == std::numeric_limits<long>::min() || maybeResult == std::numeric_limits<long>::max() ) {
		if( errno == ERANGE ) {
			return false;
		}
	}
	*result = (int)maybeResult;
	return true;
}

static inline bool ScanMinutesAndSeconds( const char *s, char **endptr, int *minutes, int8_t *seconds ) {
	int minutesValue, secondsValue;

	if( !ScanInt( s, endptr, &minutesValue ) ) {
		return false;
	}

	s = *endptr;

	if( *s != ':' ) {
		return false;
	}
	s++;

	if( !ScanInt( s, endptr, &secondsValue ) ) {
		return false;
	}

	if( minutesValue < 0 ) {
		return false;
	}

	if( secondsValue < 0 || secondsValue > 60 ) {
		return false;
	}
	*minutes = minutesValue;
	*seconds = (int8_t)secondsValue;
	return true;
}

#define DECLARE_MATCH_FUNC( funcName, flagString )            \
	static inline bool funcName( const char *s, char **endptr ) { \
		static const size_t length = strlen( flagString );        \
		if( !strncmp( s, flagString, length ) ) {                 \
			*endptr = const_cast<char *>( s + length );           \
			return true;                                          \
		}                                                         \
		return false;                                             \
	}

DECLARE_MATCH_FUNC( MatchOvertime, "overtime" )
DECLARE_MATCH_FUNC( MatchSuddenDeath, "suddendeath" )
DECLARE_MATCH_FUNC( MatchInTimeout, "(in timeout)" )

static wsw::StringView WARMUP( "Warmup" );
static wsw::StringView FINISHED( "Finished" );
static wsw::StringView COUNTDOWN( "Countdown" );

bool ServerInfoParser::HandleMatchTime( const wsw::StringView &value ) {
	// TODO: Should EqualsIgnoreCase be used?
	if( WARMUP.Equals( value ) ) {
		info->time.isWarmup = true;
		return true;
	}

	if( FINISHED.Equals( value ) ) {
		info->time.isFinished = true;
		return true;
	}

	if( COUNTDOWN.Equals( value ) ) {
		info->time.isCountdown = true;
		return true;
	}

	char *ptr;
	if( !ScanMinutesAndSeconds( value.Data(), &ptr, &info->time.timeMinutes, &info->time.timeSeconds ) ) {
		return false;
	}

	if( ptr - value.Data() == value.Size() ) {
		return true;
	}

	if( *ptr != ' ' ) {
		return false;
	}
	ptr++;

	if( *ptr == '/' ) {
		ptr++;

		if( *ptr != ' ' ) {
			return false;
		}
		ptr++;

		if( !ScanMinutesAndSeconds( value.Data(), &ptr, &info->time.limitMinutes, &info->time.limitSeconds ) ) {
			return false;
		}

		if( !*ptr ) {
			return true;
		}

		if( *ptr == ' ' ) {
			ptr++;
		}
	}

	for(;; ) {
		if( *ptr == 'o' && MatchOvertime( ptr, &ptr ) ) {
			info->time.isOvertime = true;
			continue;
		}

		if( *ptr == 's' && MatchSuddenDeath( ptr, &ptr ) ) {
			info->time.isSuddenDeath = true;
			continue;
		}

		if( *ptr == '(' && MatchInTimeout( ptr, &ptr ) ) {
			info->time.isTimeout = true;
			continue;
		}

		if( *ptr == ' ' ) {
			ptr++;
			continue;
		}

		if( *ptr == '/' || *ptr == '\n' ) {
			return true;
		}

		if( ptr - value.Data() >= value.Size() ) {
			return false;
		}
	}
}

bool ServerInfoParser::HandleMatchScore( const wsw::StringView &value ) {
	info->score.Clear();

	const auto valueLength = value.Size();
	if( !valueLength ) {
		return true;
	}

	int scores[2] = { 0, 0 };
	unsigned offsets[2] = { 0, 0 };
	unsigned lengths[2] = { 0, 0 };
	const char *const valueData = value.Data();
	const char *s = valueData;
	for( int i = 0; i < 2; ++i ) {
		while( *s == ' ' && ( s - valueData ) < valueLength ) {
			s++;
		}
		offsets[i] = (unsigned)( s - valueData );
		// Should not use strchr here (there is no zero terminator at the end of the value)
		while( *s != ':' && ( s - valueData ) < valueLength ) {
			s++;
		}

		if( ( s - valueData ) >= valueLength ) {
			return false;
		}
		lengths[i] = (unsigned)( s - valueData ) - offsets[i];

		if( lengths[i] >= info->score.scores[0].name.Capacity() ) {
			return false;
		}
		s++;

		if( *s != ' ' ) {
			return false;
		}
		s++;

		char *endptr;
		if( !ScanInt( s, &endptr, &scores[i] ) ) {
			return false;
		}
		s = endptr;
	}

	for( int i = 0; i < 2; ++i ) {
		auto *teamScore = &info->score.scores[i];
		teamScore->score = scores[i];
		teamScore->name.SetFrom( valueData + offsets[i], lengths[i] );
	}

	return true;
}

bool ServerInfoParser::HandleGameFS( const wsw::StringView &value ) {
	return HandleString( value, &info->modname );
}

bool ServerInfoParser::HandleGametype( const wsw::StringView &value ) {
	return HandleString( value, &info->gametype );
}

bool ServerInfoParser::HandleNumBots( const wsw::StringView &value ) {
	return HandleInteger( value, &info->numBots );
}

bool ServerInfoParser::HandleNumClients( const wsw::StringView &value ) {
	return HandleInteger( value, &info->numClients );
}

bool ServerInfoParser::HandleNeedPass( const wsw::StringView &value ) {
	return HandleInteger( value, &info->needPassword );
}

void ServerList::ParseGetStatusResponse( const socket_t *socket, const netadr_t &address, msg_t *msg ) {
	if( !listener ) {
		return;
	}

	PolledGameServer *const server = FindServerByAddress( address );
	if( !server ) {
		return;
	}

	ServerInfo *const parsedServerInfo = ParseServerInfo( msg, server );
	if( !parsedServerInfo ) {
		return;
	}

	PlayerInfo *parsedPlayerInfo = nullptr;

	// ParsePlayerInfo() returns a null pointer if there is no clients.
	// Avoid qualifying this case as a parsing failure, do an actual parsing only if there are clients.
	if( parsedServerInfo->numClients ) {
		if( !( parsedPlayerInfo = ParsePlayerInfo( msg ) ) ) {
			delete parsedServerInfo;
			return;
		}
		parsedServerInfo->playerInfoHead = parsedPlayerInfo;
	}

	parsedServerInfo->hasPlayerInfo = true;
	OnNewServerInfo( server, parsedServerInfo );
}

PlayerInfo *ServerList::ParsePlayerInfo( msg_t *msg ) {
	PlayerInfo *listHead = nullptr;

	if( ParsePlayerInfo( msg, &listHead ) ) {
		return listHead;
	}

	PlayerInfo *nextInfo;
	for( PlayerInfo *info = listHead; info; info = nextInfo ) {
		nextInfo = info->next;
		delete info;
	}

	return nullptr;
}

// TODO: Generalize and lift to Links.h
static PlayerInfo *LinkToTail( PlayerInfo *item, PlayerInfo **listTailRef ) {
	if( *listTailRef ) {
		( *listTailRef )->next = item;
	}
	item->next = nullptr;
	item->prev = *listTailRef;
	*listTailRef = item;
	return item;
}

bool ServerList::ParsePlayerInfo( msg_t *msg_, PlayerInfo **listHead ) {
	const char *chars = (const char *)( msg_->data + msg_->readcount );

	const unsigned currSize = msg_->cursize;
	const unsigned readCount = msg_->readcount;
	assert( currSize >= readCount );
	unsigned bytesLeft = currSize - readCount;
	const char *s = chars;

	PlayerInfo *listTail = nullptr;

	// Skip '\n' at the beginning (if any)
	if( *s == '\n' ) {
		s++;
	}

	int score, ping, team;
	char *endptr;

	for(;; ) {
		if( s - chars >= bytesLeft ) {
			break;
		}

		if( *s == '\n' ) {
			break;
		}

		if( !ScanInt( s, &endptr, &score ) ) {
			return false;
		}
		s = endptr + 1;

		if( s - chars >= bytesLeft ) {
			return false;
		}

		if( !ScanInt( s, &endptr, &ping ) ) {
			return false;
		}
		s = endptr + 1;

		if( s - chars >= bytesLeft ) {
			return false;
		}

		if( *s != '"' ) {
			return false;
		}
		s++;

		const auto nameStart = (unsigned)( s - chars );
		unsigned nameLength = 0;
		for( ;; ) {
			if( s - chars >= bytesLeft ) {
				return false;
			}

			if( *s == '"' ) {
				nameLength = (unsigned)( s - chars ) - nameStart;
				break;
			}
			s++;
		}
		static_assert( sizeof( PlayerInfo::name ) < std::numeric_limits<uint8_t>::max(), "" );

		if( nameLength >= sizeof( PlayerInfo::name ) ) {
			return false;
		}
		s++;

		if( s - chars >= bytesLeft ) {
			return false;
		}

		if( !ScanInt( s, &endptr, &team ) ) {
			return false;
		}
		s = endptr;

		if( *s != '\n' ) {
			return false;
		}

		auto *playerInfo = new PlayerInfo;
		playerInfo->score = score;
		playerInfo->name.SetFrom( chars + nameStart, nameLength );
		playerInfo->ping = (uint16_t)ping;
		playerInfo->team = (uint8_t)team;

		if( !*listHead ) {
			*listHead = playerInfo;
		}

		::LinkToTail( playerInfo, &listTail );
		s++;
	}

	return true;
}

PolledGameServer *ServerList::FindServerByAddress( const netadr_t &address ) {
	return FindServerByAddress( address, ::NET_AddressHash( address ) % HASH_MAP_SIZE );
}

PolledGameServer *ServerList::FindServerByAddress( const netadr_t &address, unsigned binIndex ) {
	for( PolledGameServer *server = serversHashBins[binIndex]; server; server = server->NextInBin() ) {
		if( NET_CompareAddress( &server->networkAddress, &address ) ) {
			return server;
		}
	}
	return nullptr;
}

void ServerList::OnServerAddressReceived( const netadr_t &address ) {
	const uint32_t hash = NET_AddressHash( address );
	const auto binIndex = hash % HASH_MAP_SIZE;
	if( FindServerByAddress( address, binIndex ) ) {
		// TODO: Touch the found server?
		return;
	}

	auto *const server = new PolledGameServer;
	server->networkAddress = address;
	::Link( server, &serversHead, PolledGameServer::LIST_LINKS );
	server->addressHash = hash;
	server->hashBinIndex = binIndex;
	::Link( server, &serversHashBins[binIndex], PolledGameServer::BIN_LINKS );
}

ServerInfo::ServerInfo() {
	time.Clear();
	score.Clear();
	hasPlayerInfo = false;
	playerInfoHead = nullptr;
	maxClients = 0;
	numClients = 0;
	numBots = 0;
}

ServerList::ServerList() {
	memset( serversHashBins, 0, sizeof( serversHashBins ) );
	this->serverInfoParser = new ServerInfoParser;
}

ServerList::~ServerList() {
	ClearExistingServerList();
	delete serverInfoParser;
}

void ServerList::Frame() {
	if( !listener ) {
		return;
	}

	DropTimedOutServers();

	EmitPollMasterServersPackets();
	EmitPollGameServersPackets();
}

void ServerList::ClearExistingServerList() {
	PolledGameServer *nextServer;
	for( PolledGameServer *server = serversHead; server; server = nextServer ) {
		nextServer = server->NextInList();
		delete server;
	}

	serversHead = nullptr;
	memset( serversHashBins, 0, sizeof( serversHashBins ) );

	lastMasterServerIndex = 0;
	lastMasterServersPollAt = 0;
}

void ServerList::StartPushingUpdates( ServerListListener *listener_, bool showEmptyServers_, bool showPlayerInfo_ ) {
	if( !listener_ ) {
		Com_Error( ERR_FATAL, "The listener is not specified" );
	}

	if( this->listener == listener_ ) {
		if( this->showEmptyServers == showEmptyServers_ && this->showPlayerInfo == showPlayerInfo_ ) {
			return;
		}
	}

	if( this->listener ) {
		ClearExistingServerList();
	}

	this->listener = listener_;
	this->showEmptyServers = showEmptyServers_;
	this->showPlayerInfo = showPlayerInfo_;
}

void ServerList::StopPushingUpdates() {
	ClearExistingServerList();
	this->listener = nullptr;
}

void ServerList::EmitPollMasterServersPackets() {
	const auto millisNow = Sys_Milliseconds();

	if( millisNow - lastMasterServersPollAt < 750 ) {
		return;
	}

	// Make the warning affected by the timer too (do not spam in console way too often), do not return prematurely
	if( numMasterServers ) {
		lastMasterServerIndex = ( lastMasterServerIndex + 1 ) % numMasterServers;
		SendPollMasterServerPacket( masterServers[lastMasterServerIndex] );
	}

	lastMasterServersPollAt = millisNow;
}

void ServerList::EmitPollGameServersPackets() {
	const auto millisNow = Sys_Milliseconds();
	for( PolledGameServer *server = serversHead; server; server = server->NextInList() ) {
		if( millisNow - server->lastInfoRequestSentAt < 300 ) {
			continue;
		}
		SendPollGameServerPacket( server );
		server->lastInfoRequestSentAt = millisNow;
	}
}

void ServerList::DropTimedOutServers() {
	const auto millisNow = Sys_Milliseconds();
	PolledGameServer *nextServer;
	for( PolledGameServer *server = serversHead; server; server = nextServer ) {
		nextServer = server->NextInList();
		if( millisNow - server->lastInfoRequestSentAt < 1000 ) {
			// Wait for the first info received...
			if( server->lastInfoReceivedAt && millisNow - server->lastInfoReceivedAt > 5000 ) {
				DropServer( server );
			}
		}
	}
}

void ServerList::DropServer( PolledGameServer *server ) {
	listener->OnServerRemoved( *server );
	Unlink( server, &serversHead, PolledGameServer::LIST_LINKS );
	Unlink( server, &serversHashBins[server->hashBinIndex], PolledGameServer::BIN_LINKS );
	delete server;
}

void ServerList::SendPollMasterServerPacket( const netadr_t &address ) {
	socket_t *socket = ( address.type == NA_IP ) ? &cls.socket_udp : &cls.socket_udp6;
	const char *empty = showEmptyServers ? "empty" : "";
	Netchan_OutOfBandPrint( socket, &address, "getserversExt Warsow %d full%s", 22, empty );
}

void ServerList::SendPollGameServerPacket( PolledGameServer *server ) {
	uint64_t challenge = Sys_Milliseconds();
	socket_t *socket = ( server->networkAddress.type == NA_IP ) ? &cls.socket_udp : &cls.socket_udp6;
	if( showPlayerInfo ) {
		Netchan_OutOfBandPrint( socket, &server->networkAddress, "getstatus %" PRIu64, challenge );
	} else {
		Netchan_OutOfBandPrint( socket, &server->networkAddress, "getinfo %" PRIu64, challenge );
	}
}

void ServerList::OnNewServerInfo( PolledGameServer *server, ServerInfo *newServerInfo ) {
	if( server->oldInfo ) {
		delete server->oldInfo;
		assert( server->currInfo );
		server->oldInfo = server->currInfo;
	}

	server->oldInfo = server->currInfo;
	server->currInfo = newServerInfo;
	server->lastInfoReceivedAt = Sys_Milliseconds();

	if( !newServerInfo->MatchesOld( server->oldInfo ) ) {
		if( server->oldInfo ) {
			listener->OnServerUpdated( *server );
		} else {
			// Defer server addition until a first info arrives.
			// Otherwise there is just nothing to show in a server browser.
			// If there is no old info, the listener has not been notified about a new server yet.
			listener->OnServerAdded( *server );
		}
	}
}

void MatchTime::Clear() {
	memset( this, 0, sizeof( MatchTime ) );
}

bool MatchTime::operator==( const MatchTime &that ) const {
	return !memcmp( this, &that, sizeof( MatchTime ) );
}

void MatchScore::Clear() {
	scores[0].Clear();
	scores[1].Clear();
}

bool MatchScore::operator==( const MatchScore &that ) const {
	// Its better to do integer comparisons first, thats why there are no individual TeamScore::Equals() methods
	for( int i = 0; i < 2; ++i ) {
		if( this->scores[i].score != that.scores[i].score ) {
			return false;
		}
	}

	for( int i = 0; i < 2; ++i ) {
		if( this->scores[i].name != that.scores[i].name ) {
			return false;
		}
	}
	return true;
}

bool PlayerInfo::operator==( const PlayerInfo &that ) const {
	// Do these cheap comparisons first
	if( this->score != that.score || this->ping != that.ping || this->team != that.team ) {
		return false;
	}
	return this->name == that.name;
}

ServerInfo::~ServerInfo() {
	PlayerInfo *nextInfo;
	for( PlayerInfo *info = playerInfoHead; info; info = nextInfo ) {
		nextInfo = info->next;
		delete info;
	}
}

bool ServerInfo::MatchesOld( ServerInfo *oldInfo ) {
	if( !oldInfo ) {
		return false;
	}

	// Test fields that are likely to change often first

	if( this->time != oldInfo->time ) {
		return false;
	}

	if( this->numClients != oldInfo->numClients ) {
		return false;
	}

	if( this->hasPlayerInfo && oldInfo->hasPlayerInfo ) {
		PlayerInfo *thisInfo = this->playerInfoHead;
		PlayerInfo *thatInfo = oldInfo->playerInfoHead;

		for(;; ) {
			if( !thisInfo ) {
				if( !thatInfo ) {
					break;
				}
				return false;
			}

			if( !thatInfo ) {
				return false;
			}

			if( *thisInfo != *thatInfo ) {
				return false;
			}

			thisInfo++, thatInfo++;
		}
	} else if( this->hasPlayerInfo != oldInfo->hasPlayerInfo ) {
		return false;
	}

	if( this->score != oldInfo->score ) {
		return false;
	}

	if( mapname != oldInfo->mapname ) {
		return false;
	}

	if( gametype != oldInfo->gametype ) {
		return false;
	}

	if( this->numBots != oldInfo->numBots ) {
		return false;
	}

	// Never changes until server restart

	if( serverName != oldInfo->serverName ) {
		return false;
	}

	if( modname != oldInfo->modname ) {
		return false;
	}

	return this->maxClients == oldInfo->maxClients && this->needPassword == oldInfo->needPassword;
}