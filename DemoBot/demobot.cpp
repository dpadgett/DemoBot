#include <chrono>
#include <stdio.h>

#include <curl/curl.h>
#include <vector>
#include <sstream>
#include <functional>

#include "deps.h"
#include "utils.h"
#include "client/client.h"
#include "cJSON.h"

static byte		sys_packetReceived[MAX_MSGLEN] = {};

extern qboolean Sys_GetPacket( netadr_t *net_from, msg_t *net_message );  // in net_ip.cpp

qboolean playerActive( int playerIdx ) {
	// player's configstring is set to emptystring if they are not connected
	return *( cl.gameState.stringData + cl.gameState.stringOffsets[CS_PLAYERS + playerIdx] ) != 0 ? qtrue : qfalse;
}

int playerSkill( int playerIdx ) {
	if ( playerIdx > MAX_CLIENTS ) {
		return -1;
	}
	char *skillStr = Info_ValueForKey( cl.gameState.stringData + cl.gameState.stringOffsets[CS_PLAYERS + playerIdx], "skill" );
	if ( skillStr && skillStr[0] ) {
		return atoi( skillStr );
	}
	else {
		return -1;
	}
}

char *NewClientCommand( void ) {
	clc.reliableSequence++;
	return clc.reliableCommands[clc.reliableSequence & ( MAX_RELIABLE_COMMANDS - 1 )];
}

/*
==================
SV_GetPlayerByHandle

Returns the player id with player id or name from handle param
==================
*/
static int SV_GetPlayerByHandle( const char *handle ) {
	int			i;
	char		cleanName[64];

	// Check whether this is a numeric player handle
	for ( i = 0; handle[i] >= '0' && handle[i] <= '9'; i++ );

	if ( !handle[i] )
	{
		int plid = atoi( handle );

		// Check for numeric playerid match
		if ( plid >= 0 && plid < MAX_CLIENTS && playerActive( plid ) )
		{
			return plid;
		}
	}

	// check for a name match
	for ( i = 0; i < MAX_CLIENTS; i++ ) {
		if ( !playerActive( i ) ) {
			continue;
		}
		const char *cs = cl.gameState.stringData + cl.gameState.stringOffsets[CS_PLAYERS + i];
		const char *name = Info_ValueForKey( cs, "n" );
		if ( !Q_stricmp( name, handle ) ) {
			return i;
		}

		Q_strncpyz( cleanName, name, sizeof( cleanName ) );
		StripColor( cleanName );
		//Q_CleanStr( cleanName );
		if ( !Q_stricmp( cleanName, handle ) ) {
			return i;
		}
	}

	Com_Printf( "Player %s is not on the server\n", handle );

	return -1;
}

void UrlEscape( const char *string, char *buf, int bufsize ) {
	CURL *curl;

	curl = curl_easy_init();
	if ( !curl ) {
		return;
	}

	char *result = curl_easy_escape( curl, string, 0 );
	if ( result == nullptr ) {
		return;
	}
	Q_strncpyz( buf, result, bufsize );
	curl_free( result );

	/* always cleanup */
	curl_easy_cleanup( curl );
}

bool HttpPost( const char* url, const char* data, std::string* payload ) {
	CURL *curl;
	CURLcode res;

	curl = curl_easy_init();
	if ( !curl ) {
		return false;
	}

	Com_Printf( "Url: %s\n", url );
	curl_easy_setopt( curl, CURLOPT_URL, url );
	if ( data && *data ) {
		Com_Printf( "Post data: %s\n", data );
		curl_easy_setopt( curl, CURLOPT_POSTFIELDS, data );
	}
	curl_easy_setopt( curl, CURLOPT_FOLLOWLOCATION, 1L );
        // No call should take longer than 30 seconds.
	curl_easy_setopt( curl, CURLOPT_TIMEOUT, 30L );

	std::stringstream buf;
	curl_easy_setopt( curl, CURLOPT_WRITEDATA, (void *) &buf ); // Passing our BufferStruct to LC
	curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, static_cast<size_t( QDECL * )( void*, size_t, size_t, void* )>( []( void *ptr, size_t size, size_t nmemb, void *data ) {
		const char *bdata = static_cast<const char *>( ptr );
		std::stringstream *buf = static_cast<std::stringstream *>( data );
		*buf << std::string( bdata, size * nmemb );
		return size * nmemb;
	} ) ); // Passing the function pointer to LC

	/* Perform the request, res will get the return code */
	res = curl_easy_perform( curl );
	/* Check for errors */
	if ( res != CURLE_OK ) {
		fprintf( stderr, "curl_easy_perform() failed: %s\n",
			curl_easy_strerror( res ) );
		curl_easy_cleanup( curl );
		return false;
	}

	long http_code = 0;
	curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &http_code );
	if ( http_code != 200 && http_code != 204 ) {
		return false;
	}

	*payload = buf.str();

	/* always cleanup */
	curl_easy_cleanup( curl );

	return true;
}

bool HttpGet( const char* url, std::string* payload ) {
	return HttpPost( url, nullptr, payload );
}

bool getUniqueId( int clientIdx, uint64_t* uniqueId ) {
	const char *cs = cl.gameState.stringData + cl.gameState.stringOffsets[CS_PLAYERS + clientIdx];
	const char *uniqueIdStr = Info_ValueForKey( cs, "id" );
	if ( uniqueIdStr == NULL || uniqueIdStr[0] == '\0' ) {
		return false;
	}
#ifdef WIN32
	*uniqueId = _strtoui64( uniqueIdStr, NULL, 10 );
#else
	*uniqueId = strtoull( uniqueIdStr, NULL, 10 );
#endif
	return true;
}

const char *getName( int clientIdx ) {
	const char *cs = cl.gameState.stringData + cl.gameState.stringOffsets[CS_PLAYERS + clientIdx];
	return Info_ValueForKey( cs, "n" );
}

void Cmd_Who_f( const char *name, const char *msg ) {
	const char *playerName = name;
	if ( strlen( msg ) > strlen( "!who " ) ) {
		playerName = &msg[5];
	}
	int clientIdx = SV_GetPlayerByHandle( playerName );
	if ( clientIdx == -1 ) {
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Unknown client id for player %s^2\"", playerName );
		return;
	}

	uint64_t uniqueId;
	if ( !getUniqueId( clientIdx, &uniqueId ) ) {
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Unknown unique id for player %d\"", clientIdx );
		return;
	}

	std::string payload;
	char url[256];
	uint64_t mask = ( ( ( (uint64_t) 1 ) << 32 ) - 1 );
	// TESTING ONLY - sets it to ceasar's id
	//uniqueId = ( 1560836804LL << 32LL ) | ( 8614LL );
	Com_sprintf( url, sizeof( url ), "http://demos.jactf.com/playerrpc.php?rpc=searchplayer&id1=%d&id2=%d", static_cast<int>( ( uniqueId >> 32 ) & mask ), static_cast<int>( uniqueId & mask ) );
	if ( !HttpGet( url, &payload ) ) {
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Failed to connect to player database\"" );
		return;
	}

	cJSON *root = cJSON_Parse( payload.c_str() );
	if ( root == nullptr ) {
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Communication with player database failed!\"" );
		return;
	}
	cJSON *results = cJSON_GetArrayItem( root, 0 );
	cJSON *resultArr = cJSON_GetObjectItem( results, "result" );
	if ( resultArr == nullptr ) {
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Communication with player database failed!\"" );
		cJSON_Delete( root );
		return;
	}
	if ( cJSON_GetArraySize( resultArr ) == 0 ) {
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Unknown player %s\"", playerName );
		cJSON_Delete( root );
		return;
	}
	cJSON *result = cJSON_GetArrayItem( resultArr, 0 );
	cJSON *nameJson = cJSON_GetObjectItem( result, "name" );
	if ( nameJson == nullptr ) {
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Communication with player database failed!\"" );
		cJSON_Delete( root );
		return;
	}
	const char *realname = nameJson->valuestring;
	const char *decodedRealName = UTF8toCP1252( realname );
	cJSON *friendlyRatingJson = nullptr;
	cJSON *ratingJson = cJSON_GetObjectItem( result, "rating" );
	if ( ratingJson != nullptr ) {
		friendlyRatingJson = cJSON_GetObjectItem( ratingJson, "friendly" );
	}
	if ( friendlyRatingJson != nullptr ) {
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Player ^7%s^2: ^7%s^2, rating %d\"", playerName, decodedRealName, (int) ( friendlyRatingJson->valuedouble * 100 ) );
	} else {
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Player ^7%s^2: ^7%s^2\"", playerName, decodedRealName );
	}
	cJSON_Delete( root );
	free( (void *) decodedRealName );
}

typedef struct {
	team_t team;
	int elo;
	int clientIdx;
	char eloStr[MAX_STRING_CHARS];
	char playerName[MAX_STRING_CHARS];
} eloStruct_t;

const int kEloInvalid = INT_MIN >> 1;

int getEloStr( cJSON *results, eloStruct_t *elo ) {
	char *eloStr = elo->eloStr;
	int eloStrLen = sizeof( elo->eloStr );
	elo->elo = kEloInvalid;
	cJSON *resultArr = cJSON_GetObjectItem( results, "result" );
	if ( resultArr == nullptr ) {
		return Com_sprintf( eloStr, eloStrLen, "?" );
	}
	if ( cJSON_GetArraySize( resultArr ) == 0 ) {
		return Com_sprintf( eloStr, eloStrLen, "?" );
	}
	cJSON *result = cJSON_GetArrayItem( resultArr, 0 );
	cJSON *nameJson = cJSON_GetObjectItem( result, "name" );
	if ( nameJson == nullptr ) {
		return Com_sprintf( eloStr, eloStrLen, "?" );
	}
	const char *realname = nameJson->valuestring;
	const char *decodedRealName = UTF8toCP1252( realname );
	int ret = 0;
	cJSON *friendlyRatingJson = nullptr;
	cJSON *ratingJson = cJSON_GetObjectItem( result, "rating" );
	if ( ratingJson != nullptr ) {
		friendlyRatingJson = cJSON_GetObjectItem( ratingJson, "friendly" );
	}
	if ( friendlyRatingJson != nullptr ) {
		//ret = Com_sprintf( eloStr, eloStrLen, "^7%s^2, %d", decodedRealName, (int) ( friendlyRatingJson->valuedouble * 100 ) );
		elo->elo = (int) ( friendlyRatingJson->valuedouble * 100 );
		ret = Com_sprintf( eloStr, eloStrLen, "%d", (int) ( friendlyRatingJson->valuedouble * 100 ) );
	}
	else {
		//ret = Com_sprintf( eloStr, eloStrLen, "^7%s^2", decodedRealName );
		ret = Com_sprintf( eloStr, eloStrLen, "?" );
	}
	free( (void *) decodedRealName );
	return ret;
}

const char *teamColor( team_t team ) {
	switch ( team ) {
	case TEAM_RED:
		return "^1";
	case TEAM_BLUE:
		return "^4";
	case TEAM_FREE:
		return "^2";
	default:
		return "^7";
	}
}

const char *teamName( team_t team ) {
	switch ( team ) {
	case TEAM_RED:
		return "red";
	case TEAM_BLUE:
		return "blue";
	case TEAM_FREE:
		return "free";
	case TEAM_SPECTATOR:
		return "spec";
	default:
		return "unknown";
	}
}

int eloSort( const void *a, const void *b ) {
	const eloStruct_t *left = (const eloStruct_t *) a;
	const eloStruct_t *right = (const eloStruct_t *) b;
	if ( left->team != right->team ) {
		return left->team - right->team;
	}
	if ( left->elo != right->elo ) {
		return right->elo - left->elo;
	}
	return left->clientIdx - right->clientIdx;
}

void PrintElos( eloStruct_t *elos, int count ) {
	char *buf = NewClientCommand();
	Com_sprintf( buf, MAX_STRING_CHARS, "say \"" );
	int maxLineLen = strlen( "say \"\"" ) + 100;
	for ( int eloIdx = 0; eloIdx < count; eloIdx++ ) {
		eloStruct_t *elo = &elos[eloIdx];
		char playerName[MAX_STRING_CHARS];
		if ( !*elo->playerName ) {
			Q_strncpyz( playerName, getName( elo->clientIdx ), sizeof( playerName ) );
		} else {
			Q_strncpyz( playerName, elo->playerName, sizeof( playerName ) );
		}
		StripColor( playerName );
		char playerStr[MAX_STRING_CHARS];
		int len = Com_sprintf( playerStr, sizeof( playerStr ), "%s%s (%s) ", teamColor( elo->team ), playerName, elo->eloStr );
		if ( strlen( buf ) + len > maxLineLen || ( eloIdx > 0 && elo->team != elos[eloIdx - 1].team ) ) {
			Q_strcat( buf, MAX_STRING_CHARS, "\"" );
			buf = NewClientCommand();
			Com_sprintf( buf, MAX_STRING_CHARS, "say \"" );
		}
		Q_strcat( buf, MAX_STRING_CHARS, playerStr );
	}
	Q_strcat( buf, MAX_STRING_CHARS, "\"" );
}

int lastElosTime = -30 * 1000;
void Cmd_Elos_f( const char *name, const char *msg ) {

	if ( cls.realtime - lastElosTime < 10000 ) {
		int ago = cls.realtime - lastElosTime;
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Elos reported %d.%03ds ago\"", ago / 1000, ago % 1000 );
		//lastElosTime = cls.realtime;
		return;
	}

	lastElosTime = cls.realtime;

	uint64_t mask = ( ( ( (uint64_t) 1 ) << 32 ) - 1 );

	cJSON *players = cJSON_CreateArray();
	for ( int clientIdx = 0; clientIdx < MAX_CLIENTS; clientIdx++ ) {
		uint64_t uniqueId = -1;
		if ( !getUniqueId( clientIdx, &uniqueId ) ) {
			continue;
		}
		// TESTING ONLY - sets it to ceasar's id
		//uniqueId = ( 1560836804LL << 32LL ) | ( 8614LL );
		cJSON *player = cJSON_CreateArray();
		cJSON_AddItemToArray( player, cJSON_CreateNumber( static_cast<int>( ( uniqueId >> 32 ) & mask ) ) );
		cJSON_AddItemToArray( player, cJSON_CreateNumber( static_cast<int>( uniqueId & mask ) ) );
		cJSON_AddItemToArray( players, player );
	}

	std::string payload;
	const char *url = "http://demos.jactf.com/playerrpc.php?rpc=searchplayer";
	char *data = cJSON_Print( players );
	cJSON_Delete( players );
	if ( !HttpPost( url, data, &payload ) ) {
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Failed to connect to player database\"" );
		free( data );
		return;
	}
	free( data );

	//printf( "Response: %s\n", payload.c_str() );
	cJSON *root = cJSON_Parse( payload.c_str() );
	if ( root == nullptr ) {
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Communication with player database failed!\"" );
		printf( "Reply: %s\n", payload.c_str() );
		return;
	}
	int resultIdx = 0;
	eloStruct_t elos[MAX_CLIENTS];
	for ( int clientIdx = 0; clientIdx < MAX_CLIENTS; clientIdx++ ) {
		uint64_t uniqueId = -1;
		if ( !getUniqueId( clientIdx, &uniqueId ) ) {
			continue;
		}
		cJSON *results = cJSON_GetArrayItem( root, resultIdx );
		eloStruct_t *elo = &elos[resultIdx];
		*elo->playerName = '\0';
		elo->clientIdx = clientIdx;
		elo->team = getPlayerTeam( clientIdx );
		getEloStr( results, elo );
		resultIdx++;
	}
	// sort
	qsort( &elos[0], resultIdx, sizeof( elos[0] ), eloSort );
	PrintElos( elos, resultIdx );
	cJSON_Delete( root );
	return;
}

typedef struct {
	int redIdx;
	int blueIdx;
	qboolean redAccepted;
	qboolean blueAccepted;
} teamSwitch_t;

teamSwitch_t teamSwitch;

void Cmd_Teams_f( const char *name, const char *msg ) {
	std::stringstream url;
	url << "http://demos.jactf.com/playerrpc.php?rpc=teams";
	std::vector<int> redPlayers, bluePlayers;
	for ( int clientIdx = 0; clientIdx < MAX_CLIENTS; clientIdx++ ) {
		if ( !playerActive( clientIdx ) ) {
			continue;
		}
		team_t team = getPlayerTeam( clientIdx );
		switch ( team ) {
		case TEAM_FREE:
		case TEAM_SPECTATOR:
		case TEAM_NUM_TEAMS:
			continue;
		case TEAM_RED:
			redPlayers.push_back( clientIdx );
			break;
		case TEAM_BLUE:
			bluePlayers.push_back( clientIdx );
			break;
		}

		uint64_t uniqueId = -1;
		if ( !getUniqueId( clientIdx, &uniqueId ) ) {
			Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Unknown unique id for player %d\"", clientIdx );
			return;
		}

		uint64_t mask = ( ( ( (uint64_t) 1 ) << 32 ) - 1 );
		url << "&" << getPlayerTeamName( clientIdx ) << "_id1[]=" << static_cast<int>( ( uniqueId >> 32 ) & mask );
		url << "&" << getPlayerTeamName( clientIdx ) << "_id2[]=" << static_cast<int>( uniqueId & mask );
	}

	std::string payload;
	if ( !HttpGet( url.str().c_str(), &payload ) ) {
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Failed to connect to player database\"" );
		return;
	}

	cJSON *root = cJSON_Parse( payload.c_str() );
	if ( root == nullptr ) {
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Communication with player database failed!\"" );
		printf( "Reply: %s\n", payload.c_str() );
		return;
	}
	cJSON *red = cJSON_GetObjectItem( root, "red" );
	if ( red == nullptr ) {
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Communication with player database failed!\"" );
		printf( "Reply: %s\n", payload.c_str() );
		cJSON_Delete( root );
		return;
	}
	cJSON *redRating = cJSON_GetObjectItem( red, "rating" );
	if ( redRating == nullptr ) {
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Communication with player database failed!\"" );
		printf( "Reply: %s\n", payload.c_str() );
		cJSON_Delete( root );
		return;
	}
	cJSON *blue = cJSON_GetObjectItem( root, "blue" );
	if ( blue == nullptr ) {
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Communication with player database failed!\"" );
		printf( "Reply: %s\n", payload.c_str() );
		cJSON_Delete( root );
		return;
	}
	cJSON *blueRating = cJSON_GetObjectItem( blue, "rating" );
	if ( blueRating == nullptr ) {
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Communication with player database failed!\"" );
		printf( "Reply: %s\n", payload.c_str() );
		cJSON_Delete( root );
		return;
	}
	char winProbability[512] = "\0";
	cJSON *redWinProbability = cJSON_GetObjectItem( red, "win_probability" );
	if ( redWinProbability->valuedouble >= 0.5 ) {
		Com_sprintf( winProbability, sizeof( winProbability ), "^1%.2f^2", redWinProbability->valuedouble );
	} else {
		Com_sprintf( winProbability, sizeof( winProbability ), "^4%.2f^2", 1 - redWinProbability->valuedouble );
	}
	Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"^1Red^2: ^1%d^2, ^4Blue^2: ^4%d^2, Win probability %s\"",
		(int) (redRating->valuedouble * 100), 
		(int) (blueRating->valuedouble * 100),
		winProbability );
	cJSON *playerSwitch = cJSON_GetObjectItem( root, "switch" );
	if ( cJSON_GetArraySize( playerSwitch ) == 2 ) {
		cJSON *redPlayer = cJSON_GetArrayItem( playerSwitch, 0 );
		cJSON *bluePlayer = cJSON_GetArrayItem( playerSwitch, 1 );
		int redClient = redPlayers[redPlayer->valueint];
		int blueClient = bluePlayers[bluePlayer->valueint];
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Suggestion: switch ^7%s^2 with ^7%s^2\"",
			getPlayerName( redClient ), getPlayerName( blueClient ) );
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Named players can say !a to execute\"" );
		if ( redClient != teamSwitch.redIdx ) {
			teamSwitch.redAccepted = qfalse;
		}
		teamSwitch.redIdx = redClient;
		if ( blueClient != teamSwitch.blueIdx ) {
			teamSwitch.blueAccepted = qfalse;
		}
		teamSwitch.blueIdx = blueClient;
	} else {
		teamSwitch.redAccepted = qfalse;
		teamSwitch.redIdx = MAX_CLIENTS;
		teamSwitch.blueAccepted = qfalse;
		teamSwitch.blueIdx = MAX_CLIENTS;
	}
	cJSON_Delete( root );
}

#define MAX_RCON_MESSAGE 1024

/*
=====================
CL_Rcon_f

Send the rest of the command line over as
an unconnected command.
=====================
*/
void CL_Rcon_f( const char *cmd ) {
	char	message[MAX_RCON_MESSAGE];

	cvar_t *rcon_client_password = Cvar_Get( "rconpassword", "", CVAR_ARCHIVE );
	if ( !rcon_client_password->string ) {
		Com_Printf( "You must set 'rconpassword' before\n"
			"issuing an rcon command.\n" );
		return;
	}

	message[0] = -1;
	message[1] = -1;
	message[2] = -1;
	message[3] = -1;
	message[4] = 0;

	Q_strcat( message, MAX_RCON_MESSAGE, "rcon " );

	Q_strcat( message, MAX_RCON_MESSAGE, rcon_client_password->string );
	Q_strcat( message, MAX_RCON_MESSAGE, " " );

	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=543
	Q_strcat( message, MAX_RCON_MESSAGE, cmd );

	netadr_t rcon_address;
	if ( cls.state >= CA_CONNECTED ) {
		rcon_address = clc.netchan.remoteAddress;
	} else {
		Com_Printf( "You must be connected to issue rcon commands\n" );
		return;
	}

	NET_SendPacket( NS_CLIENT, strlen( message ) + 1, message, rcon_address );
}

void Cmd_AcceptSwitch_f( const char *name, const char *msg ) {
	const char *playerName = name;
	if ( strlen( msg ) > strlen( "!who " ) ) {
		playerName = &msg[5];
	}
	int clientIdx = SV_GetPlayerByHandle( playerName );
	if ( clientIdx == -1 ) {
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Unknown client id for player %s^2\"", playerName );
		return;
	}
	if ( getPlayerTeam( clientIdx ) == TEAM_BLUE && clientIdx == teamSwitch.blueIdx ) {
		teamSwitch.blueAccepted = qtrue;
	} else if ( getPlayerTeam( clientIdx ) == TEAM_RED && clientIdx == teamSwitch.redIdx ) {
		teamSwitch.redAccepted = qtrue;
	}
	if ( teamSwitch.blueAccepted && teamSwitch.redAccepted ) {
		char cmd[MAX_RCON_MESSAGE];
		Com_sprintf( cmd, sizeof( cmd ), "forceteam %d r", teamSwitch.blueIdx );
		CL_Rcon_f( cmd );
		Com_sprintf( cmd, sizeof( cmd ), "forceteam %d b", teamSwitch.redIdx );
		CL_Rcon_f( cmd );
		teamSwitch.redAccepted = qfalse;
		teamSwitch.redIdx = MAX_CLIENTS;
		teamSwitch.blueAccepted = qfalse;
		teamSwitch.blueIdx = MAX_CLIENTS;
	}
}

void getEloUpdateStr( cJSON *player, eloStruct_t *elo ) {
	cJSON *rating = cJSON_GetObjectItem( player, "rating" );
	cJSON *start = cJSON_GetObjectItem( rating, "start" );
	cJSON *friendly = cJSON_GetObjectItem( start, "friendly" );
	int startElo = friendly->valuedouble * 100;
	cJSON *updated = cJSON_GetObjectItem( rating, "updated" );
	friendly = cJSON_GetObjectItem( updated, "friendly" );
	int updatedElo = friendly->valuedouble * 100;
	const char *sign = "";
	if ( updatedElo > startElo ) {
		sign = "+";
	}
	Com_sprintf( elo->eloStr, sizeof( elo->eloStr ), "%d (%s%d)",
		updatedElo, sign, updatedElo - startElo );
	elo->elo = updatedElo;
	cJSON *name = cJSON_GetObjectItem( player, "name" );
	const char *decodedName = UTF8toCP1252( name->valuestring );
	Q_strncpyz( elo->playerName, decodedName, sizeof( elo->playerName ) );
	free( (void *)decodedName );
}

bool stopRecord = false;
void CG_ParseScores_f( void ) {
	int redScore = atoi( Cmd_Argv( 2 ) );
	int blueScore = atoi( Cmd_Argv( 3 ) );

	printf( "Red: %d Blue: %d\n", redScore, blueScore );
	const char *cs = cl.gameState.stringData + cl.gameState.stringOffsets[CS_INTERMISSION];
	if ( atoi( cs ) != 0 ) {
		//Cmd_EndMatch_f( redScore, blueScore );
		// stop the demo at this point so that we can get the metadata before map changes
		//CL_StopRecord_f();
		// can't stop record right now since this packet hasn't been written to demo yet.
		stopRecord = true;
	}
}

typedef struct {
	const char *command;
	void (*commandFunc)( const char *name, const char *msg );
} chatCommand_t;

chatCommand_t chatCommands[] = {
	{ "!who", Cmd_Who_f },
	{ "!elos", Cmd_Elos_f },
	{ "!teams", Cmd_Teams_f },
	{ "!a", Cmd_AcceptSwitch_f },
};

/*
=================
CG_SplitChat
=================
*/
static void CG_SplitChat( const char *text, char *name, int namelen, char *message, int msglen ) {
	int i;
	const char *sep = strstr( text, "\x19:" );
	if ( sep == NULL ) {
		*name = '\0';
		*message = '\0';
		return;
	}
	int sepidx = sep - text;
	int nameidx = 0, messageidx = 0;
	for ( i = 0; text[i]; i++ ) {
		if ( i >= sepidx + 5 ) {
			if ( messageidx < msglen - 1 ) {
				message[messageidx++] = text[i];
			}
		}
		if ( i < sepidx ) {
			if ( nameidx < namelen - 1 ) {
				name[nameidx++] = text[i];
			}
		}
	}
	message[messageidx++] = '\0';
	if ( nameidx >= 2 ) {
		nameidx -= 2;
	}
	name[nameidx++] = '\0';
}

void chatCommand( void ) {
	// call original callback so it gets logged
	Cmd_Chat_f();
	// scan for any commands
	char name[MAX_STRING_CHARS], msg[MAX_STRING_CHARS];
	CG_SplitChat( Cmd_Argv( 1 ), name, sizeof( name ), msg, sizeof( msg ) );
	for ( chatCommand_t cmd : chatCommands ) {
		if ( !strncmp( msg, cmd.command, strlen( cmd.command ) ) ) {
			cmd.commandFunc( name, msg );
		}
	}
}

void CG_MapRestart( void ) {
	// hack, since bot doesn't parse snapshots it can't generate a gamestate
	// sending donedl triggers game to send a new gamestate
	Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "donedl" );
	// that will trigger a new demo automatically in gamestate parsing
	//CL_StopRecord_f();
	//CL_Record_f();
}

void CG_OnDemoFinish( const char *filename ) {
	Com_Printf( "Demo index finished\n" );

	char demoName[MAX_STRING_CHARS]; // = "/cygdrive/U/demos/demobot/mpctf_manaan 2016-03-22_14-55-21.dm_26";
	Com_sprintf( demoName, sizeof( demoName ), "/cygdrive/%c%s", filename[0], &filename[2] );

	UrlEscape( demoName, demoName, sizeof( demoName ) );

	char url[MAX_STRING_CHARS];
	Com_sprintf( url, sizeof( url ), "http://demos.jactf.com/minrpc.php?rpc=endmatch&demo=%s", demoName );

	std::string payload;
	if ( !HttpGet( url, &payload ) ) {
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Failed to connect to player database\"" );
		return;
	}

	//Com_Printf( "Reply: %s\n", payload.c_str() );

	cJSON *root = cJSON_Parse( payload.c_str() );
	if ( root == nullptr ) {
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Communication with player database failed!\"" );
		printf( "Reply: %s\n", payload.c_str() );
		cJSON_Delete( root );
		return;
	}

	cJSON *elapsed = cJSON_GetObjectItem( root, "elapsed" );
	Com_Printf( "Elapsed: %lf\n", elapsed->valuedouble );

	cJSON *log = cJSON_GetObjectItem( root, "log" );
	Com_Printf( "%s", log->valuestring );

	cJSON *is_match = cJSON_GetObjectItem( root, "is_match" );
	if ( !is_match->valueint ) {
		cJSON_Delete( root );
		return;
	}

	cJSON *elosJson = cJSON_GetObjectItem( root, "elos" );
	int eloCount = 0;
	eloStruct_t elos[MAX_CLIENTS];
	for ( int idx = 0; idx < cJSON_GetArraySize( elosJson ); idx++ ) {
		cJSON *player = cJSON_GetArrayItem( elosJson, idx );
		cJSON *rating = cJSON_GetObjectItem( player, "rating" );
		cJSON *client = cJSON_GetObjectItem( player, "client_num" );
		if ( rating != nullptr && client != nullptr ) {
			eloStruct_t *elo = &elos[eloCount++];
			elo->clientIdx = client->valueint;
			elo->team = TEAM_SPECTATOR;
			if ( playerActive( client->valueint ) ) {
				elo->team = getPlayerTeam( client->valueint );
			}
			getEloUpdateStr( player, elo );
		}
	}
	// sort
	qsort( &elos[0], eloCount, sizeof( elos[0] ), eloSort );
	if ( eloCount > 0 ) {
		Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "say \"Player elo updates:\"" );
		PrintElos( elos, eloCount );
	}
	cJSON_Delete( root );
}

void CG_DemoIndexFinished( void ) {
	CG_OnDemoFinish( clc.demoName );
}

CURLM *curlm;
int handle_count;

typedef struct demoUpload_s {
	CURL *curl;
	const char *buf;
	int buflen;
	bool eos;
	std::stringstream obuf;
} demoUpload_t;

std::vector<demoUpload_t *> uploads;

bool CG_StartDemo( const char *name ) {
	uploads.push_back( new demoUpload_t{} );
	demoUpload_t *upload = uploads.back();

	upload->curl = curl_easy_init();
	if ( !upload->curl ) {
		uploads.pop_back();
		return false;
	}

	char url[MAX_STRING_CHARS];
	char demoName[MAX_STRING_CHARS];
	UrlEscape( name, demoName, sizeof( demoName ) );
	Com_sprintf( url, sizeof( url ), "http://demos.jactf.com/upload.php/%s", demoName );
	Com_Printf( "Url: %s\n", url );
	curl_easy_setopt( upload->curl, CURLOPT_URL, url );
	curl_easy_setopt( upload->curl, CURLOPT_UPLOAD, 1L );
	curl_easy_setopt( upload->curl, CURLOPT_PUT, 1L );
	curl_easy_setopt( upload->curl, CURLOPT_FOLLOWLOCATION, 1L );
	upload->eos = false;
	curl_easy_setopt( upload->curl, CURLOPT_READDATA, (void *) upload );
	curl_easy_setopt( upload->curl, CURLOPT_READFUNCTION, static_cast<size_t( QDECL * )( char*, size_t, size_t, void* )>( []( char *ptr, size_t size, size_t nmemb, void *data ) {
		demoUpload_t *upload = static_cast<demoUpload_t *>( data );
		if ( upload->buflen == 0 ) {
			if ( upload->eos ) {
				return (size_t) 0;
			}
			//fprintf( stderr, "Pausing...\n" );
			return (size_t) CURL_READFUNC_PAUSE;
		}
		int len = Q_min( upload->buflen, size * nmemb );
		//fprintf( stderr, "Sending %d bytes\n", len );
		memcpy( ptr, upload->buf, len );
		upload->buf = &upload->buf[len];
		upload->buflen -= len;
		return (size_t) len;
	} ) );
	//curl_easy_setopt( upload->curl, CURLOPT_VERBOSE, 1L );

	curl_easy_setopt( upload->curl, CURLOPT_WRITEDATA, (void *) upload ); // Passing our BufferStruct to LC
	curl_easy_setopt( upload->curl, CURLOPT_WRITEFUNCTION, static_cast<size_t( QDECL * )( void*, size_t, size_t, void* )>( []( void *ptr, size_t size, size_t nmemb, void *data ) {
		const char *bdata = static_cast<const char *>( ptr );
		demoUpload_t *upload = static_cast<demoUpload_t *>( data );
		upload->obuf << std::string( bdata, size * nmemb );
		//fprintf( stderr, "Read so far:\n%s\n", upload->obuf.str().c_str() );
		return size * nmemb;
	} ) ); // Passing the function pointer to LC

	curl_multi_add_handle( curlm, upload->curl );
	handle_count++;

	return true;
}

bool CG_WriteDemoPacket( const char *buf, int buflen ) {
	demoUpload_t *upload = uploads.back();
	// first unpause the active upload
	curl_easy_pause( upload->curl, 0 );
	// set the new buffer
	upload->buf = buf;
	upload->buflen = buflen;

	// flush until buffer is sent
	int extraloops = 0;
	while ( handle_count > 0 && (upload->buflen > 0 || extraloops > 0) ) {
		//fprintf( stderr, "Running multi perform...\n" );
		/* Perform the request, res will get the return code */
		//res = curl_easy_perform( curl );
		CURLMcode mres = curl_multi_perform( curlm, &handle_count );

		/* Check for errors */
		if ( mres != CURLE_OK ) {
			fprintf( stderr, "curl_multi_perform() failed: %s\n",
				curl_multi_strerror( mres ) );
			curl_easy_cleanup( upload->curl );
			return false;
		}

		if ( upload->buflen == 0 ) {
			extraloops--;
		}
	}

	return true;
}

bool CG_FinishSendingDemo() {
	demoUpload_t *upload = uploads.back();

	// first fully flush
	curl_easy_pause( upload->curl, 0 );
	upload->eos = true;
	while ( handle_count > 0 ) {
		//fprintf( stderr, "Running multi perform...\n" );
		/* Perform the request, res will get the return code */
		//res = curl_easy_perform( curl );
		CURLMcode mres = curl_multi_perform( curlm, &handle_count );

		/* Check for errors */
		if ( mres != CURLE_OK ) {
			fprintf( stderr, "curl_multi_perform() failed: %s\n",
				curl_multi_strerror( mres ) );
			curl_easy_cleanup( upload->curl );
			return false;
		}
	}

	long http_code = 0;
	curl_easy_getinfo( upload->curl, CURLINFO_RESPONSE_CODE, &http_code );
	if ( http_code != 200 && http_code != 204 ) {
		return false;
	}

	fprintf( stderr, "Payload: %s\n", upload->obuf.str().c_str() );
	cJSON *root = cJSON_Parse( upload->obuf.str().c_str() );
	if ( root == nullptr ) {
		Com_Printf( "Couldn't parse response: %s\n", upload->obuf.str().c_str() );
	} else {
		cJSON *file = cJSON_GetObjectItem( root, "file" );
		const char *filename = file->valuestring;
		CG_OnDemoFinish( filename );
	}


	/* always cleanup */
	curl_easy_cleanup( upload->curl );

	uploads.pop_back();
	delete upload;

	return true;
}

void CG_InitDemoSaving() {
	curlm = curl_multi_init();
}

int main( int argc, char **argv ) {
	if ( argc < 2 ) {
		printf( "Usage: %s ip:port\n", argv[0] );
		return 0;
	}
	
	//indexDemo = qtrue;
	indexFinished = CG_DemoIndexFinished;

	curl_global_init( CURL_GLOBAL_ALL );

	CG_InitDemoSaving();

	demoFolder = "";
	demoStart = CG_StartDemo;
	demoStop = CG_FinishSendingDemo;
	demoWrite = CG_WriteDemoPacket;

	cls.realtime = Com_Milliseconds();
	int seed = 0;
	{
		using std::chrono::duration_cast;
		using std::chrono::microseconds;
		using std::chrono::steady_clock;

		seed = duration_cast<microseconds>(
			steady_clock::now().time_since_epoch() ).count();
	}
	Rand_Init( seed );
	int port = Q_irand( 29070, 29970 );
	Com_Printf( "Port: %d\n", port );
	Cvar_Set( "net_port", va( "%i", port ) );
	NET_Init();
	Netchan_Init( port );

	// userinfo
	Cvar_Get( "name", "Padawan", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get( "rate", "25000", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get( "snaps", "40", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get( "model", DEFAULT_MODEL"/default", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get( "forcepowers", "7-1-032330000000001333", CVAR_USERINFO | CVAR_ARCHIVE );
	//	Cvar_Get ("g_redTeam", DEFAULT_REDTEAM_NAME, CVAR_SERVERINFO | CVAR_ARCHIVE);
	//	Cvar_Get ("g_blueTeam", DEFAULT_BLUETEAM_NAME, CVAR_SERVERINFO | CVAR_ARCHIVE);
	Cvar_Get( "color1", "4", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get( "color2", "4", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get( "handicap", "100", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get( "sex", "1150862", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get( "password", "", CVAR_USERINFO );
	Cvar_Get( "cg_predictItems", "1", CVAR_USERINFO | CVAR_ARCHIVE );

	//default sabers
	Cvar_Get( "saber1", DEFAULT_SABER, CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get( "saber2", "none", CVAR_USERINFO | CVAR_ARCHIVE );

	//skin color
	Cvar_Get( "char_color_red", "255", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get( "char_color_green", "255", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get( "char_color_blue", "255", CVAR_USERINFO | CVAR_ARCHIVE );

	Cvar_Set( "name", /*"spec"*/ "elo BOT" );
	if ( argc >= 3 ) {
		Cvar_Set( "password", argv[2] );
	} else {
		Cvar_Set( "password", "ctfpug" );
	}
	Com_Printf( "Connecting with password %s\n", Cvar_VariableString( "password" ) );

	Cvar_Get( "rconpassword", "", CVAR_ARCHIVE );

	if ( argc >= 4 ) {
		Cvar_Set( "rconpassword", argv[3] );
	}

	Cmd_AddCommand( "connect", CL_Connect_f );
	Cmd_AddCommand( "chat", chatCommand );
	Cmd_AddCommand( "cp", Cmd_Chat_f );
	Cmd_AddCommand( "cs", CL_ConfigstringModified );
	Cmd_AddCommand( "disconnect", CL_Disconnect_f );
	Cmd_AddCommand( "scores", CG_ParseScores_f );
	Cmd_AddCommand( "map_restart", CG_MapRestart );
	Cmd_ExecuteString( va( "connect %s", argv[1] ) );
	int lastPovChangeTime = cls.realtime - 88 * 1000;  // first pov switch will happen 2s after connect
	int lastServerPacketTime = cls.realtime;
	while ( cls.state != CA_DISCONNECTED ) {
		cls.realtime = Com_Milliseconds();
		NET_Sleep( 1000 );
		CL_CheckForResend();

		netadr_t	adr;
		msg_t		netmsg;
		// check for network packets
		MSG_Init( &netmsg, sys_packetReceived, sizeof( sys_packetReceived ) );
		if ( Sys_GetPacket( &adr, &netmsg ) ) {
			lastServerPacketTime = cls.realtime;
			CL_PacketEvent( adr, &netmsg );
		}
		if ( stopRecord ) {
			CL_StopRecord_f();
			stopRecord = false;
		}
		if ( cls.state == CA_CONNECTED ) {
			qboolean disconnect = qfalse;
			if ( cl.cmdNumber == 0 || cl.serverTime != cl.cmds[cl.cmdNumber & CMD_MASK].serverTime ) {
				// send a new usercmd to server
				cl.cmdNumber++;
				cl.cmds[cl.cmdNumber & CMD_MASK] = {};
				cl.cmds[cl.cmdNumber & CMD_MASK].serverTime = cl.serverTime;
				//Com_Printf( "Sending usercmd\n" );
				if ( cls.realtime - lastPovChangeTime > 90 * 1000 ) {
					cl.cmds[cl.cmdNumber & CMD_MASK].buttons |= BUTTON_ATTACK;
					Com_Printf( "Sending POV change\n" );
					lastPovChangeTime = cls.realtime;
				}
				team_t curTeam = getPlayerTeam( clc.clientNum );
				if ( curTeam != TEAM_SPECTATOR ) {
					Com_Printf( "Not on spectator - on team %d, trying to switch!\n", curTeam );
					Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "team s" );
				}
				if ( cl.cmdNumber > 10 ) {  // allow some settling time
					int humans = 0;
					for ( int i = 0; i < MAX_CLIENTS; i++ ) {
						if ( playerActive( i ) && playerSkill( i ) == -1 ) {
							humans++;
						}
					}
					if ( humans <= 1 ) {
						// we are the only human, so quit
						Com_Printf( "Only %d humans remaining, quit\n", humans );
						Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "disconnect" );
						disconnect = qtrue;
					}
				}
				CL_WritePacket();
				if ( disconnect ) {
					CL_Disconnect_f();
				}
			}
		}
		if ( cls.realtime - lastServerPacketTime > 60 * 1000 ) {
			// no server packet in 60 seconds, assume we lost connection
			Com_Printf( "Server packet timeout\n" );
			CL_Disconnect_f();
		}
	}
	//system( "PAUSE" );
	return 0;
}
