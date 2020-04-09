#include <chrono>

#include "qcommon/q_shared.h"
#include "qcommon/qcommon.h"
#include "server/server.h"
#include "client/client.h"

cvar_t cl_shownet_concrete;
cvar_t *cl_shownet = &cl_shownet_concrete;

cvar_t sv_blockJumpSelect_concrete;
cvar_t *sv_blockJumpSelect = &sv_blockJumpSelect_concrete;

server_t sv;

clientStatic_t		cls;
clientConnection_t clc;
clientActive_t cl;

sharedEntity_t *SV_GentityNum( int num ) {
	Com_Error( ERR_FATAL, "SV_GentityNum not supported" );
	return NULL;
}

ping_t	cl_pinglist[MAX_PINGREQUESTS];

typedef struct serverStatus_s
{
	char string[BIG_INFO_STRING];
	netadr_t address;
	int time, startTime;
	qboolean pending;
	qboolean print;
	qboolean retrieved;
} serverStatus_t;

#undef MAX_SERVERSTATUSREQUESTS
#define MAX_SERVERSTATUSREQUESTS MAX_GLOBAL_SERVERS

serverStatus_t cl_serverStatusList[MAX_SERVERSTATUSREQUESTS];
int serverStatusCount;

#define	MAXPRINTMSG	4096
static char com_errorMessage[MAXPRINTMSG] = { 0 };
void QDECL Com_Error( int code, const char *fmt, ... ) {
	va_list		argptr;

	va_start( argptr, fmt );
	Q_vsnprintf( com_errorMessage, sizeof( com_errorMessage ), fmt, argptr );
	va_end( argptr );

	Com_Printf( "Com_Error code %d: %s", code, com_errorMessage );
	//system( "pause" );
	if ( code == ERR_FATAL ) {
		exit( code );
	}
	else {
		throw code;
	}
}

void QDECL Com_Printf( const char *fmt, ... ) {
	va_list argptr;

	va_start( argptr, fmt );
	vfprintf( stderr, fmt, argptr );
	va_end( argptr );
}

void QDECL Com_DPrintf( const char *fmt, ... ) {
	/*va_list argptr;

	va_start( argptr, fmt );
	vfprintf( stderr, fmt, argptr );
	va_end( argptr );*/
}

void *Z_Malloc( int iSize, memtag_t eTag, qboolean bZeroit, int unusedAlign ) {
	if ( bZeroit ) {
		return calloc( 1, iSize );
	}
	else {
		return malloc( iSize );
	}
}

// Frees a block of memory...
//
void Z_Free(void *pvAddress)
{
	free( pvAddress );
}

#ifdef WIN32
#include <io.h>
#include <fcntl.h>
#endif

static FILE *fileHandles[1024];
static int fileHandleCount = 1;
static long bytesRead[1024];
long QDECL FS_FOpenFileRead( const char *filename, fileHandle_t *fileHandle, qboolean uniqueFILE ) {
	*fileHandle = fileHandleCount++;
	if ( !Q_strncmp( filename, "-", 2 ) ) {
#ifdef WIN32
		setmode( fileno( stdin ), O_BINARY );
#else
		freopen( NULL, "rb", stdin );
#endif
		fileHandles[*fileHandle] = stdin;
		return 0;
	}
	fileHandles[*fileHandle] = fopen( filename, "rb" );
	if ( fileHandles[*fileHandle] == NULL ) {
		*fileHandle = 0;
		return -1;
	}
	bytesRead[*fileHandle] = 0;
	fseek( fileHandles[*fileHandle], 0, SEEK_END );
	long length = ftell( fileHandles[*fileHandle] );
	fseek( fileHandles[*fileHandle], 0, SEEK_SET );
	return length;
}

fileHandle_t QDECL FS_FOpenFileWrite( const char *filename, qboolean safe ) {
	fileHandle_t fileHandle = fileHandleCount++;
	if ( !Q_strncmp( filename, "-", 2 ) ) {
#ifdef WIN32
		setmode( fileno( stdout ), O_BINARY );
#else
		freopen( NULL, "rb", stdout );
#endif
		fileHandles[fileHandle] = stdout;
		return 0;
	}
	fileHandles[fileHandle] = fopen( filename, "wb" );
	if ( fileHandles[fileHandle] == NULL ) {
		fileHandle = 0;
	}
	return fileHandle;
}

int QDECL FS_Read( void *data, int dataSize, fileHandle_t fileHandle ) {
	int totalRead = 0;
	byte *buf = (byte *)data;
	while ( totalRead < dataSize && !feof( fileHandles[fileHandle] ) ) {
		totalRead += fread( &buf[totalRead], 1, dataSize - totalRead, fileHandles[fileHandle] );
	}
	bytesRead[fileHandle] += totalRead;
	return totalRead;
}

int QDECL FS_Write( const void *data, int dataSize, fileHandle_t fileHandle ) {
	int totalWritten = 0;
	byte *buf = (byte *) data;
	while ( totalWritten < dataSize && !feof( fileHandles[fileHandle] ) ) {
		totalWritten += fwrite( &buf[totalWritten], 1, dataSize - totalWritten, fileHandles[fileHandle] );
	}
	return totalWritten;
}

long QDECL FS_ReadCount( fileHandle_t fileHandle ) {
	return bytesRead[fileHandle];
}

void QDECL FS_FCloseFile( fileHandle_t fileHandle ) {
	fclose( fileHandles[fileHandle] );
	fileHandles[fileHandle] = NULL;
}

/*
========================
CopyString

NOTE:	never write over the memory CopyString returns because
memory from a memstatic_t might be returned
========================
*/
char *CopyString( const char *in ) {
	char	*out;

	out = (char *) Z_Malloc( strlen( in ) + 1, TAG_SMALL );
	strcpy( out, in );
	return out;
}

/*
================
Com_Milliseconds

Can be used for profiling, but will be journaled accurately
================
*/
int Com_Milliseconds( void ) {
	using std::chrono::duration_cast;
	using std::chrono::milliseconds;
	using std::chrono::steady_clock;

	static milliseconds start_time( duration_cast<milliseconds>(
		steady_clock::now().time_since_epoch() ) );

	milliseconds ms = duration_cast<milliseconds>(
		steady_clock::now().time_since_epoch());
	return (ms - start_time).count();
}

int Sys_Milliseconds( bool ) {
	return Com_Milliseconds();
}

/*
============
Com_HashKey
============
*/
int Com_HashKey( char *string, int maxlen ) {
	int register hash, i;

	hash = 0;
	for ( i = 0; i < maxlen && string[i] != '\0'; i++ ) {
		hash += string[i] * ( 119 + i );
	}
	hash = ( hash ^ ( hash >> 10 ) ^ ( hash >> 20 ) );
	return hash;
}

/*
=====================
CL_ConfigstringModified
=====================
*/
void CL_ConfigstringModified( void ) {
	char		*old, *s;
	int			i, index;
	char		*dup;
	gameState_t	oldGs;
	int			len;

	index = atoi( Cmd_Argv( 1 ) );
	if ( index < 0 || index >= MAX_CONFIGSTRINGS ) {
		Com_Error( ERR_DROP, "CL_ConfigstringModified: bad index %i", index );
	}
	// get everything after "cs <num>"
	s = Cmd_ArgsFrom( 2 );

	old = cl.gameState.stringData + cl.gameState.stringOffsets[index];
	if ( !strcmp( old, s ) ) {
		return;		// unchanged
	}

	// uber hack to work around base_enhanced forced net settings
	char buf[MAX_INFO_STRING];
	if ( index == CS_SYSTEMINFO ) {
		if ( *Info_ValueForKey( s, "sv_serverid" ) == '\0' ) {
			// just concat them instead of overwriting in this case
			Com_sprintf( buf, sizeof( buf ), "%s%s", old, s );
			s = buf;
		}
	}

	// build the new gameState_t
	oldGs = cl.gameState;

	Com_Memset( &cl.gameState, 0, sizeof( cl.gameState ) );

	// leave the first 0 for uninitialized strings
	cl.gameState.dataCount = 1;

	for ( i = 0; i < MAX_CONFIGSTRINGS; i++ ) {
		if ( i == index ) {
			dup = s;
		}
		else {
			dup = oldGs.stringData + oldGs.stringOffsets[i];
		}
		if ( !dup[0] ) {
			continue;		// leave with the default empty string
		}

		len = strlen( dup );

		if ( len + 1 + cl.gameState.dataCount > MAX_GAMESTATE_CHARS ) {
			Com_Error( ERR_DROP, "MAX_GAMESTATE_CHARS exceeded" );
		}

		// append it to the gameState string buffer
		cl.gameState.stringOffsets[i] = cl.gameState.dataCount;
		Com_Memcpy( cl.gameState.stringData + cl.gameState.dataCount, dup, len + 1 );
		cl.gameState.dataCount += len + 1;
	}

	if ( index == CS_SYSTEMINFO ) {
		// parse serverId and other cvars
		CL_SystemInfoChanged();
	} else if ( index == CS_VOTE_STRING ) {
		Com_Printf( "Vote: %s\n", s );
	}
}

/*
================
CL_Connect_f

================
*/
void CL_Connect_f( void ) {
	char	*server;
	const char	*serverString;

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "usage: connect [server]\n" );
		return;
	}

	server = Cmd_Argv( 1 );

	Q_strncpyz( cls.servername, server, sizeof( cls.servername ) );

	if ( !NET_StringToAdr( cls.servername, &clc.serverAddress ) ) {
		Com_Printf( "Bad server address\n" );
		cls.state = CA_DISCONNECTED;
		return;
	}
	if ( clc.serverAddress.port == 0 ) {
		clc.serverAddress.port = BigShort( PORT_SERVER );
	}

	serverString = NET_AdrToString( clc.serverAddress );

	Com_Printf( "%s resolved to %s\n", cls.servername, serverString );

	// if we aren't playing on a lan, we need to authenticate
	if ( NET_IsLocalAddress( clc.serverAddress ) ) {
		cls.state = CA_CHALLENGING;
	}
	else {
		cls.state = CA_CONNECTING;

		// Set a client challenge number that ideally is mirrored back by the server.
		clc.challenge = ( ( rand() << 16 ) ^ rand() ) ^ Com_Milliseconds();
	}

	clc.connectTime = -99999;	// CL_CheckForResend() will fire immediately
	clc.connectPacketCount = 0;

	// server connection string
	Cvar_Set( "cl_currentServerAddress", server );
	Cvar_Set( "cl_currentServerIP", serverString );
}

#define	RETRANSMIT_TIMEOUT	3000	// time between connection packet retransmits

/*
=================
CL_CheckForResend

Resend a connect message if the last one has timed out
=================
*/
void CL_CheckForResend( void ) {
	int		port;
	char	info[MAX_INFO_STRING];
	char	data[MAX_INFO_STRING + 10];

	// resend if we haven't gotten a reply yet
	if ( cls.state != CA_CONNECTING && cls.state != CA_CHALLENGING ) {
		return;
	}

	if ( cls.realtime - clc.connectTime < RETRANSMIT_TIMEOUT ) {
		return;
	}

	clc.connectTime = cls.realtime;	// for retransmit requests
	clc.connectPacketCount++;


	switch ( cls.state ) {
	case CA_CONNECTING:
		// requesting a challenge

		// The challenge request shall be followed by a client challenge so no malicious server can hijack this connection.
		Com_sprintf( data, sizeof( data ), "getchallenge %d", clc.challenge );

		NET_OutOfBandPrint( NS_CLIENT, clc.serverAddress, data );
		break;

	case CA_CHALLENGING:
		// sending back the challenge
		port = (int) Cvar_VariableValue( "net_qport" );

		Q_strncpyz( info, Cvar_InfoString( CVAR_USERINFO ), sizeof( info ) );
		if ( clc.connectPacketCount % 3 == 0 ) {
			Info_SetValueForKey( info, "protocol", va( "%i", PROTOCOL_VERSION ) );
		} else if ( clc.connectPacketCount % 3 == 1 ) {
			Info_SetValueForKey( info, "protocol", va( "%i", PROTOCOL_VERSION - 1 ) );
		} else {
			Info_SetValueForKey( info, "protocol", va( "%i", 4007 ) );
		}
		Info_SetValueForKey( info, "qport", va( "%i", port ) );
		Info_SetValueForKey( info, "challenge", va( "%i", clc.challenge ) );

		Com_sprintf( data, sizeof( data ), "connect \"%s\"", info );
		//Com_Printf( "connect: %s\n", data );
		NET_OutOfBandData( NS_CLIENT, clc.serverAddress, (byte *) data, strlen( data ) );

		// the most current userinfo has been sent, so watch for any
		// newer changes to userinfo variables
		cvar_modifiedFlags &= ~CVAR_USERINFO;
		break;

	default:
		Com_Error( ERR_FATAL, "CL_CheckForResend: bad cls.state" );
	}
}

/*
===================
CL_DisconnectPacket

Sometimes the server can drop the client and the netchan based
disconnect can be lost.  If the client continues to send packets
to the server, the server will send out of band disconnect packets
to the client so it doesn't have to wait for the full timeout period.
===================
*/
void CL_DisconnectPacket( netadr_t from ) {
	if ( cls.state < CA_AUTHORIZING ) {
		return;
	}

	// if not from our server, ignore it
	if ( !NET_CompareAdr( from, clc.netchan.remoteAddress ) ) {
		return;
	}

	// if we have received packets within three seconds, ignore it
	// (it might be a malicious spoof)
	if ( cls.realtime - clc.lastPacketTime < 3000 ) {
		return;
	}

	// drop the connection (FIXME: connection dropped dialog)
	Com_Printf( "Server disconnected for unknown reason\n" );

	CL_Disconnect_f();
}

/*
===================
CL_InitServerInfo
===================
*/
void CL_InitServerInfo( serverInfo_t *server, netadr_t *address ) {
	server->adr = *address;
	server->clients = 0;
	server->hostName[0] = '\0';
	server->mapName[0] = '\0';
	server->maxClients = 0;
	server->maxPing = 0;
	server->minPing = 0;
	server->netType = 0;
	server->needPassword = qfalse;
	server->trueJedi = 0;
	server->weaponDisable = 0;
	server->forceDisable = 0;
	server->ping = -1;
	server->game[0] = '\0';
	server->gameType = 0;
	server->humans = server->bots = 0;
}

#define MAX_SERVERSPERPACKET	256

/*
===================
CL_ServersResponsePacket
===================
*/
void CL_ServersResponsePacket( const netadr_t *from, msg_t *msg ) {
	int				i, j, count, total;
	netadr_t addresses[MAX_SERVERSPERPACKET];
	int				numservers;
	byte*			buffptr;
	byte*			buffend;

	Com_DPrintf( "CL_ServersResponsePacket\n" );

	if ( cls.numglobalservers == -1 ) {
		// state to detect lack of servers or lack of response
		cls.numglobalservers = 0;
		cls.numGlobalServerAddresses = 0;
	}

	// parse through server response string
	numservers = 0;
	buffptr = msg->data;
	buffend = buffptr + msg->cursize;

	// advance to initial token
	do
	{
		if ( *buffptr == '\\' )
			break;

		buffptr++;
	} while ( buffptr < buffend );

	while ( buffptr + 1 < buffend )
	{
		// IPv4 address
		if ( *buffptr == '\\' )
		{
			buffptr++;

			if ( buffend - buffptr < (int) ( sizeof( addresses[numservers].ip ) + sizeof( addresses[numservers].port ) + 1 ) )
				break;

			for ( size_t i = 0; i < sizeof( addresses[numservers].ip ); i++ )
				addresses[numservers].ip[i] = *buffptr++;

			addresses[numservers].type = NA_IP;
		}
		else
			// syntax error!
			break;

		// parse out port
		addresses[numservers].port = ( *buffptr++ ) << 8;
		addresses[numservers].port += *buffptr++;
		addresses[numservers].port = BigShort( addresses[numservers].port );

		// syntax check
		if ( *buffptr != '\\' )
			break;

		numservers++;
		if ( numservers >= MAX_SERVERSPERPACKET )
			break;
	}

	count = cls.numglobalservers;

	for ( i = 0; i < numservers && count < MAX_GLOBAL_SERVERS; i++ ) {
		// build net address
		serverInfo_t *server = &cls.globalServers[count];

		// Tequila: It's possible to have sent many master server requests. Then
		// we may receive many times the same addresses from the master server.
		// We just avoid to add a server if it is still in the global servers list.
		for ( j = 0; j < count; j++ )
		{
			if ( NET_CompareAdr( cls.globalServers[j].adr, addresses[i] ) )
				break;
		}

		if ( j < count )
			continue;

		CL_InitServerInfo( server, &addresses[i] );
		// advance to next slot
		count++;
	}

	// if getting the global list
	if ( count >= MAX_GLOBAL_SERVERS && cls.numGlobalServerAddresses < MAX_GLOBAL_SERVERS )
	{
		// if we couldn't store the servers in the main list anymore
		for ( ; i < numservers && cls.numGlobalServerAddresses < MAX_GLOBAL_SERVERS; i++ )
		{
			// just store the addresses in an additional list
			cls.globalServerAddresses[cls.numGlobalServerAddresses++] = addresses[i];
		}
	}

	cls.numglobalservers = count;
	total = count + cls.numGlobalServerAddresses;

	Com_Printf( "%d servers parsed (total %d)\n", numservers, total );
}

static void CL_SetServerInfo( serverInfo_t *server, const char *info, int ping ) {
	if ( server ) {
		if ( info ) {
			server->clients = atoi( Info_ValueForKey( info, "clients" ) );
			Q_strncpyz( server->hostName, Info_ValueForKey( info, "hostname" ), sizeof( server->hostName ) );
			Q_strncpyz( server->mapName, Info_ValueForKey( info, "mapname" ), sizeof( server->mapName ) );
			server->maxClients = atoi( Info_ValueForKey( info, "sv_maxclients" ) );
			Q_strncpyz( server->game, Info_ValueForKey( info, "game" ), sizeof( server->game ) );
			server->gameType = atoi( Info_ValueForKey( info, "gametype" ) );
			server->netType = atoi( Info_ValueForKey( info, "nettype" ) );
			server->minPing = atoi( Info_ValueForKey( info, "minping" ) );
			server->maxPing = atoi( Info_ValueForKey( info, "maxping" ) );
			//			server->allowAnonymous = atoi(Info_ValueForKey(info, "sv_allowAnonymous"));
			server->needPassword = (qboolean) atoi( Info_ValueForKey( info, "needpass" ) );
			server->trueJedi = atoi( Info_ValueForKey( info, "truejedi" ) );
			server->weaponDisable = atoi( Info_ValueForKey( info, "wdisable" ) );
			server->forceDisable = atoi( Info_ValueForKey( info, "fdisable" ) );
			server->humans = atoi( Info_ValueForKey( info, "g_humanplayers" ) );
			server->bots = atoi( Info_ValueForKey( info, "bots" ) );
			//			server->pure = (qboolean)atoi(Info_ValueForKey(info, "pure" ));
		}
		server->ping = ping;
	}
}

static void CL_SetServerInfoByAddress( netadr_t from, const char *info, int ping ) {
	int i;

	for ( i = 0; i < MAX_OTHER_SERVERS; i++ ) {
		if ( NET_CompareAdr( from, cls.localServers[i].adr ) ) {
			CL_SetServerInfo( &cls.localServers[i], info, ping );
		}
	}

	for ( i = 0; i < MAX_GLOBAL_SERVERS; i++ ) {
		if ( NET_CompareAdr( from, cls.globalServers[i].adr ) ) {
			CL_SetServerInfo( &cls.globalServers[i], info, ping );
		}
		/*else if ( info && *info && !strcmp( Info_ValueForKey( info, "hostname" ), cls.globalServers[i].hostName ) && !strcmp( Info_ValueForKey( info, "mapname" ), cls.globalServers[i].mapName ) ) {
			Com_Printf( "Duplicate?? " );
			Com_Printf( "%s", NET_AdrToString( from ) );
			Com_Printf( " %s\n", NET_AdrToString( cls.globalServers[i].adr ) );
		}*/
	}

	for ( i = 0; i < MAX_OTHER_SERVERS; i++ ) {
		if ( NET_CompareAdr( from, cls.favoriteServers[i].adr ) ) {
			CL_SetServerInfo( &cls.favoriteServers[i], info, ping );
		}
	}
}

/*
===================
CL_ServerInfoPacket
===================
*/
void CL_ServerInfoPacket( netadr_t from, msg_t *msg ) {
	int		i, type;
	char	info[MAX_INFO_STRING];
	char	*infoString;
	int		prot;

	infoString = MSG_ReadString( msg );

	// if this isn't the correct protocol version, ignore it
	prot = atoi( Info_ValueForKey( infoString, "protocol" ) );
	if ( prot != PROTOCOL_VERSION ) {
		Com_DPrintf( "Different protocol info packet: %s\n", infoString );
		//return;
	}

	// iterate servers waiting for ping response
	for ( i = 0; i<MAX_PINGREQUESTS; i++ )
	{
		if ( cl_pinglist[i].adr.port && !cl_pinglist[i].time && NET_CompareAdr( from, cl_pinglist[i].adr ) )
		{
			// calc ping time
			cl_pinglist[i].time = Sys_Milliseconds() - cl_pinglist[i].start;
			Com_DPrintf( "ping time %dms from %s\n", cl_pinglist[i].time, NET_AdrToString( from ) );

			// save of info
			Q_strncpyz( cl_pinglist[i].info, infoString, sizeof( cl_pinglist[i].info ) );

			// tack on the net type
			// NOTE: make sure these types are in sync with the netnames strings in the UI
			switch ( from.type )
			{
			case NA_BROADCAST:
			case NA_IP:
				type = 1;
				break;

			default:
				type = 0;
				break;
			}
			Info_SetValueForKey( cl_pinglist[i].info, "nettype", va( "%d", type ) );
			CL_SetServerInfoByAddress( from, infoString, cl_pinglist[i].time );

			return;
		}
	}

	// if not just sent a local broadcast or pinging local servers
	if ( cls.pingUpdateSource != AS_LOCAL ) {
		return;
	}

	for ( i = 0; i < MAX_OTHER_SERVERS; i++ ) {
		// empty slot
		if ( cls.localServers[i].adr.port == 0 ) {
			break;
		}

		// avoid duplicate
		if ( NET_CompareAdr( from, cls.localServers[i].adr ) ) {
			return;
		}
	}

	if ( i == MAX_OTHER_SERVERS ) {
		Com_DPrintf( "MAX_OTHER_SERVERS hit, dropping infoResponse\n" );
		return;
	}

	// add this to the list
	cls.numlocalservers = i + 1;
	CL_InitServerInfo( &cls.localServers[i], &from );

	Q_strncpyz( info, MSG_ReadString( msg ), MAX_INFO_STRING );
	if ( strlen( info ) ) {
		if ( info[strlen( info ) - 1] != '\n' ) {
			strncat( info, "\n", sizeof( info ) - 1 );
		}
		Com_Printf( "%s: %s", NET_AdrToString( from ), info );
	}
}

/*
===================
CL_GetServerStatus
===================
*/
serverStatus_t *CL_GetServerStatus( netadr_t from ) {
	int i, oldest, oldestTime;

	for ( i = 0; i < MAX_SERVERSTATUSREQUESTS; i++ ) {
		if ( NET_CompareAdr( from, cl_serverStatusList[i].address ) ) {
			return &cl_serverStatusList[i];
		}
	}
	for ( i = 0; i < MAX_SERVERSTATUSREQUESTS; i++ ) {
		if ( cl_serverStatusList[i].retrieved ) {
			return &cl_serverStatusList[i];
		}
	}
	oldest = -1;
	oldestTime = 0;
	for ( i = 0; i < MAX_SERVERSTATUSREQUESTS; i++ ) {
		if ( oldest == -1 || cl_serverStatusList[i].startTime < oldestTime ) {
			oldest = i;
			oldestTime = cl_serverStatusList[i].startTime;
		}
	}
	if ( oldest != -1 ) {
		return &cl_serverStatusList[oldest];
	}
	serverStatusCount++;
	return &cl_serverStatusList[serverStatusCount & ( MAX_SERVERSTATUSREQUESTS - 1 )];
}

/*
===================
CL_ServerStatus
===================
*/
int CL_ServerStatus( const char *serverAddress, char *serverStatusString, int maxLen ) {
	int i;
	netadr_t	to;
	serverStatus_t *serverStatus;

	// if no server address then reset all server status requests
	if ( !serverAddress ) {
		for ( i = 0; i < MAX_SERVERSTATUSREQUESTS; i++ ) {
			cl_serverStatusList[i].address.port = 0;
			cl_serverStatusList[i].retrieved = qtrue;
		}
		return qfalse;
	}
	// get the address
	if ( !NET_StringToAdr( serverAddress, &to ) ) {
		return qfalse;
	}
	serverStatus = CL_GetServerStatus( to );
	// if no server status string then reset the server status request for this address
	if ( !serverStatusString ) {
		serverStatus->retrieved = qtrue;
		return qfalse;
	}

	// if this server status request has the same address
	if ( NET_CompareAdr( to, serverStatus->address ) ) {
		// if we received a response for this server status request
		if ( !serverStatus->pending ) {
			Q_strncpyz( serverStatusString, serverStatus->string, maxLen );
			//serverStatus->retrieved = qtrue;
			//serverStatus->startTime = 0;
			return qtrue;
		}
		// resend the request regularly
		else if ( serverStatus->startTime < Com_Milliseconds() - 10000 ) {
			serverStatus->print = qfalse;
			serverStatus->pending = qtrue;
			serverStatus->retrieved = qfalse;
			serverStatus->time = 0;
			serverStatus->startTime = Com_Milliseconds();
			NET_OutOfBandPrint( NS_CLIENT, to, "getstatus" );
			return qfalse;
		}
	}
	// if retrieved
	else if ( serverStatus->retrieved ) {
		serverStatus->address = to;
		serverStatus->print = qfalse;
		serverStatus->pending = qtrue;
		serverStatus->retrieved = qfalse;
		serverStatus->startTime = Com_Milliseconds();
		serverStatus->time = 0;
		NET_OutOfBandPrint( NS_CLIENT, to, "getstatus" );
		return qfalse;
	}
	return qfalse;
}

/*
===================
CL_ServerStatusResponse
===================
*/
void CL_ServerStatusResponse( netadr_t from, msg_t *msg ) {
	char	*s;
	char	info[MAX_INFO_STRING];
	int		i, l, score, ping;
	int		len;
	serverStatus_t *serverStatus;

	serverStatus = NULL;
	for ( i = 0; i < MAX_SERVERSTATUSREQUESTS; i++ ) {
		if ( NET_CompareAdr( from, cl_serverStatusList[i].address ) ) {
			serverStatus = &cl_serverStatusList[i];
			break;
		}
	}
	// if we didn't request this server status
	if ( !serverStatus ) {
		return;
	}

	s = MSG_ReadStringLine( msg );

	len = 0;
	Com_sprintf( &serverStatus->string[len], sizeof( serverStatus->string ) - len, "%s", s );

	if ( serverStatus->print ) {
		Com_Printf( "Server (%s)\n",
			NET_AdrToString( serverStatus->address ) );
		Com_Printf( "Server settings:\n" );
		// print cvars
		while ( *s ) {
			for ( i = 0; i < 2 && *s; i++ ) {
				if ( *s == '\\' )
					s++;
				l = 0;
				while ( *s ) {
					info[l++] = *s;
					if ( l >= MAX_INFO_STRING - 1 )
						break;
					s++;
					if ( *s == '\\' ) {
						break;
					}
				}
				info[l] = '\0';
				if ( i ) {
					Com_Printf( "%s\n", info );
				}
				else {
					Com_Printf( "%-24s", info );
				}
			}
		}
	}

	len = strlen( serverStatus->string );
	Com_sprintf( &serverStatus->string[len], sizeof( serverStatus->string ) - len, "\\" );

	if ( serverStatus->print ) {
		Com_Printf( "\nPlayers:\n" );
		Com_Printf( "num: score: ping: name:\n" );
	}
	for ( i = 0, s = MSG_ReadStringLine( msg ); *s; s = MSG_ReadStringLine( msg ), i++ ) {

		len = strlen( serverStatus->string );
		Com_sprintf( &serverStatus->string[len], sizeof( serverStatus->string ) - len, "\\%s", s );

		if ( serverStatus->print ) {
			score = ping = 0;
			sscanf( s, "%d %d", &score, &ping );
			s = strchr( s, ' ' );
			if ( s )
				s = strchr( s + 1, ' ' );
			if ( s )
				s++;
			else
				s = "unknown";
			Com_Printf( "%-2d   %-3d    %-3d   %s\n", i, score, ping, s );
		}
	}
	len = strlen( serverStatus->string );
	Com_sprintf( &serverStatus->string[len], sizeof( serverStatus->string ) - len, "\\" );

	serverStatus->time = Com_Milliseconds();
	serverStatus->address = from;
	serverStatus->pending = qfalse;
	if ( serverStatus->print ) {
		serverStatus->retrieved = qtrue;
	}
}

/*
=================
CL_ConnectionlessPacket

Responses to broadcasts, etc
=================
*/
void CL_ConnectionlessPacket( netadr_t from, msg_t *msg ) {
	char	*s;
	char	*c;
	int challenge = 0;

	MSG_BeginReadingOOB( msg );
	MSG_ReadLong( msg );	// skip the -1

	s = MSG_ReadStringLine( msg );
	Com_DPrintf( "Command: %s\n", s );

	Cmd_TokenizeString( s );

	c = Cmd_Argv( 0 );

	Com_DPrintf( "CL packet %s: %s\n", NET_AdrToString( from ), c );

	// challenge from the server we are connecting to
	if ( !Q_stricmp( c, "challengeResponse" ) )
	{
		if ( cls.state != CA_CONNECTING )
		{
			Com_Printf( "Unwanted challenge response received.  Ignored.\n" );
			return;
		}

		c = Cmd_Argv( 2 );
		if ( *c )
			challenge = atoi( c );

		if ( !NET_CompareAdr( from, clc.serverAddress ) )
		{
			// This challenge response is not coming from the expected address.
			// Check whether we have a matching client challenge to prevent
			// connection hi-jacking.

			if ( !*c || challenge != clc.challenge )
			{
				Com_DPrintf( "Challenge response received from unexpected source. Ignored.\n" );
				return;
			}
		}

		// start sending challenge response instead of challenge request packets
		clc.challenge = atoi( Cmd_Argv( 1 ) );
		cls.state = CA_CHALLENGING;
		clc.connectPacketCount = 0;
		clc.connectTime = -99999;

		// take this address as the new server address.  This allows
		// a server proxy to hand off connections to multiple servers
		clc.serverAddress = from;
		Com_DPrintf( "challengeResponse: %d\n", clc.challenge );
		return;
	}

	// server connection
	if ( !Q_stricmp( c, "connectResponse" ) ) {
		if ( cls.state >= CA_CONNECTED ) {
			Com_Printf( "Dup connect received. Ignored.\n" );
			return;
		}
		if ( cls.state != CA_CHALLENGING ) {
			Com_Printf( "connectResponse packet while not connecting. Ignored.\n" );
			return;
		}
		if ( !NET_CompareAdr( from, clc.serverAddress ) ) {
			Com_Printf( "connectResponse from wrong address. Ignored.\n" );
			return;
		}
		Netchan_Setup( NS_CLIENT, &clc.netchan, from, Cvar_VariableValue( "net_qport" ) );
		cls.state = CA_CONNECTED;
		clc.lastPacketSentTime = -9999;		// send first packet immediately
		return;
	}

	// server responding to an info broadcast
	if ( !Q_stricmp( c, "infoResponse" ) ) {
		CL_ServerInfoPacket( from, msg );
		return;
	}

	// server responding to a get playerlist
	if ( !Q_stricmp( c, "statusResponse" ) ) {
		CL_ServerStatusResponse( from, msg );
		return;
	}

	// a disconnect message from the server, which will happen if the server
	// dropped the connection but it is still getting packets from us
	if ( !Q_stricmp( c, "disconnect" ) ) {
		CL_DisconnectPacket( from );
		return;
	}

	// echo request from server
	if ( !Q_stricmp( c, "echo" ) ) {
		NET_OutOfBandPrint( NS_CLIENT, from, "%s", Cmd_Argv( 1 ) );
		return;
	}

	// cd check
	if ( !Q_stricmp( c, "keyAuthorize" ) ) {
		// we don't use these now, so dump them on the floor
		return;
	}

	// global MOTD from id
	/*if ( !Q_stricmp( c, "motd" ) ) {
	CL_MotdPacket( from );
	return;
	}*/

	// echo request from server
	if ( !Q_stricmp( c, "print" ) )
	{
		// NOTE: we may have to add exceptions for auth and update servers
		if ( NET_CompareAdr( from, clc.serverAddress )/* || NET_CompareAdr( from, rcon_address ) */ )
		{
			/*char sTemp[MAX_STRINGED_SV_STRING];

			s = MSG_ReadString( msg );
			CL_CheckSVStringEdRef( sTemp, s );
			Q_strncpyz( clc.serverMessage, sTemp, sizeof( clc.serverMessage ) );
			Com_Printf( "%s", sTemp );*/
			s = MSG_ReadString( msg );
			Com_Printf( "%s", s );
		}
		return;
	}

	// list of servers sent back by a master server (classic)
	if ( !Q_strncmp( c, "getserversResponse", 18 ) ) {
		CL_ServersResponsePacket( &from, msg );
		return;
	}

	Com_DPrintf( "Unknown connectionless packet command %s.\n", c );
}

/*
=====================
CL_ParseCommandString

Command strings are just saved off until cgame asks for them
when it transitions a snapshot
=====================
*/
void CL_ParseCommandString( msg_t *msg ) {
	char	*s;
	int		seq;
	int		index;

	seq = MSG_ReadLong( msg );
	s = MSG_ReadString( msg );

	// see if we have already executed stored it off
	if ( clc.serverCommandSequence >= seq ) {
		return;
	}
	clc.serverCommandSequence = seq;

	index = seq & ( MAX_RELIABLE_COMMANDS - 1 );
	Q_strncpyz( clc.serverCommands[index], s, sizeof( clc.serverCommands[index] ) );
	Cmd_ExecuteString( s );
	//Com_Printf( "command: %s\n", s );
}

/*
==================
CL_SystemInfoChanged

The systeminfo configstring has been changed, so parse
new information out of it.  This will happen at every
gamestate, and possibly during gameplay.
==================
*/
void CL_SystemInfoChanged( void ) {
	char			*systemInfo;
	const char		*s;
	char			key[BIG_INFO_KEY];
	char			value[BIG_INFO_VALUE];
	qboolean		gameSet;

	systemInfo = cl.gameState.stringData + cl.gameState.stringOffsets[CS_SYSTEMINFO];
	// NOTE TTimo:
	// when the serverId changes, any further messages we send to the server will use this new serverId
	// https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=475
	// in some cases, outdated cp commands might get sent with this new serverId
	cl.serverId = atoi( Info_ValueForKey( systemInfo, "sv_serverid" ) );

	// don't set any vars when playing a demo
	if ( clc.demoplaying ) {
		return;
	}

	gameSet = qfalse;
	// scan through all the variables in the systeminfo and locally set cvars to match
	s = systemInfo;
	while ( s ) {
		Info_NextPair( &s, key, value );
		if ( !key[0] ) {
			break;
		}
		// ehw!
		if ( !Q_stricmp( key, "fs_game" ) ) {
			gameSet = qtrue;
		}
		Cvar_Set( key, value );
	}
	// if game folder should not be set and it is set at the client side
	if ( !gameSet && *Cvar_VariableString( "fs_game" ) ) {
		Cvar_Set( "fs_game", "" );
	}
}

bool CL_WriteDemoPacket( const char *buf, int buflen ) {
	FS_Write( buf, buflen, clc.demofile );
	return true;
}

/*
====================
CL_WriteDemoMessage

Dumps the current net message, prefixed by the length
====================
*/
bool( *demoWrite )( const char *buf, int buflen ) = CL_WriteDemoPacket;
void CL_WriteDemoMessage( msg_t *msg, int headerBytes ) {
	int		len, swlen;

	// write the packet sequence
	len = clc.serverMessageSequence;
	swlen = LittleLong( len );
	demoWrite( (const char *) &swlen, 4 );

	// skip the packet sequencing information
	len = msg->cursize - headerBytes;
	swlen = LittleLong( len );
	demoWrite( (const char *) &swlen, 4 );
	demoWrite( (const char *) msg->data + headerBytes, len );
}

bool CL_StopDemo() {
	FS_FCloseFile( clc.demofile );
	clc.demofile = 0;
	return true;
}

/*
====================
CL_StopRecording_f

stop recording a demo
====================
*/
bool( *demoStop )( void ) = CL_StopDemo;
void CL_StopRecord_f( void ) {
	int		len;

	if ( !clc.demorecording ) {
		Com_Printf( "Not recording a demo.\n" );
		return;
	}

	// finish up
	len = -1;
	demoWrite( (const char *) &len, 4 );
	demoWrite( (const char *) &len, 4 );
	demoStop();
	clc.demorecording = qfalse;
	clc.spDemoRecording = qfalse;
	Com_Printf( "Stopped demo.\n" );
}

/*
==================
CL_DemoFilename
==================
*/
void CL_DemoFilename( char *buf, int bufSize ) {
	time_t rawtime;
	char timeStr[32] = { 0 }; // should really only reach ~19 chars

	time( &rawtime );
	strftime( timeStr, sizeof( timeStr ), "%Y-%m-%d_%H-%M-%S", localtime( &rawtime ) ); // or gmtime

	const char *info = cl.gameState.stringData + cl.gameState.stringOffsets[CS_SERVERINFO];
	const char *mapname = Info_ValueForKey( info, "mapname" );
	if ( mapname == nullptr ) {
		mapname = "unknown";
	}

	Com_sprintf( buf, bufSize, "%s %s", mapname, timeStr );
	Q_strstrip( buf, "\n\r;:.?*<>|\\/\"", NULL );
}

#ifdef _WIN32
#include <Windows.h>

HANDLE hProcess = nullptr;
#endif

const char *indexBinary = "C:/Users/dan/Documents/Visual Studio 2010/Projects/JKDemoMetadata/Debug/JKDemoMetadata.exe";
void( *indexFinished )( void ) = nullptr;
void CL_IndexDemo( void ) {
#ifdef _WIN32
	// Initialize StartupInfo structure
	STARTUPINFO    StartupInfo;
	memset( &StartupInfo, 0, sizeof( StartupInfo ) );
	StartupInfo.cb = sizeof( StartupInfo );

	// This will contain the information about the newly created process
	PROCESS_INFORMATION ProcessInformation;

	char command[MAX_STRING_CHARS];
	Com_sprintf( command, sizeof( command ), "\"%s\" \"%s\" \"%s\"", indexBinary, clc.demoName, "live" );
	BOOL results = CreateProcess( 0, command,
		0, // Process Attributes
		0, // Thread Attributes
		FALSE, // Inherit Handles
		0 /*CREATE_NEW_CONSOLE*/, // CreationFlags,
		0, // Enviornment
		0, // Current Directory
		&StartupInfo, // StartupInfo
		&ProcessInformation // Process Information
		);
	if ( !results ) {
		Com_Printf( "Failed to launch process\n" );
		return;
	}
	Com_Printf( "Spawned process %s\n", command );

	// Cleanup
	if ( hProcess != nullptr ) {
		CloseHandle( hProcess );
	}
	hProcess = ProcessInformation.hProcess;
	CloseHandle( ProcessInformation.hThread );
#endif
}

bool CL_StartDemo( const char *name ) {
	clc.demofile = FS_FOpenFileWrite( name );
	if ( !clc.demofile ) {
		return false;
	}
	return true;
}

/*
====================
CL_Record_f

record <demoname>

Begins recording a demo from the current position
====================
*/
static char		demoName[MAX_QPATH];	// compiler bug workaround
qboolean		indexDemo = qfalse;		// set to true to index demo as it is saved
const char *demoFolder = "U:/demos/demobot/";
bool( *demoStart )( const char *name ) = CL_StartDemo;
void CL_Record_f( void ) {
	char		name[MAX_OSPATH];
	// timestamp the file
	CL_DemoFilename( demoName, sizeof( demoName ) );

	Com_sprintf( name, sizeof( name ), "%s%s.dm_%d", demoFolder, demoName, PROTOCOL_VERSION );

	// open the demo file

	Com_Printf( "recording to %s.\n", name );
	if ( !demoStart( name ) ) {
		Com_Printf( "ERROR: couldn't open.\n" );
		return;
	}
	clc.demorecording = qtrue;

	Q_strncpyz( clc.demoName, name, sizeof( clc.demoName ) );

	// don't start saving messages until a non-delta compressed message is received
	clc.demowaiting = qtrue;

	if ( indexDemo ) {
		CL_IndexDemo();
	}
}

static char *NewClientCommand( void ) {
	clc.reliableSequence++;
	return clc.reliableCommands[clc.reliableSequence & ( MAX_RELIABLE_COMMANDS - 1 )];
}

extern char dlfilename[MAX_STRING_CHARS];

/*
=====================
CL_ParseDownload

A download message has been received from the server
=====================
*/
void CL_ParseDownload( msg_t *msg ) {
	int		size;
	unsigned char data[MAX_MSGLEN];
	int block;

	// read the data
	block = (unsigned short) MSG_ReadShort( msg );

	if ( !block )
	{
		// block zero is special, contains file size
		clc.downloadSize = MSG_ReadLong( msg );

		Com_Printf( "Download size: %d\n", clc.downloadSize );

		if ( clc.downloadSize < 0 )
		{
			Com_Printf( "Error: %s\n", MSG_ReadString( msg ) );
			return;
		}
	}

	size = (unsigned short) MSG_ReadShort( msg );
	if ( size > 0 )
		MSG_ReadData( msg, data, size );

	if ( clc.downloadBlock != block ) {
		Com_DPrintf( "CL_ParseDownload: Expected block %d, got %d\n", clc.downloadBlock, block );
		return;
	}

	// open the file if not opened yet
	if ( !clc.download )
	{
		/*clc.download = FS_SV_FOpenFileWrite( clc.downloadTempName );

		if ( !clc.download ) {
			Com_Printf( "Could not create %s\n", clc.downloadTempName );
			CL_AddReliableCommand( "stopdl" );
			CL_NextDownload();
			return;
		}*/
	}

	static FILE* fp = NULL;
	if ( size ) {
		//FS_Write( data, size, clc.download );
		if ( fp == NULL ) {
			fp = fopen( dlfilename, "wb" );
		}
		fwrite( data, size, 1, fp );
		//data[size] = 0;
		Com_Printf( "Recv: %d of %d bytes (%lld.%02lld%%)\r", clc.downloadCount, clc.downloadSize, ( clc.downloadCount * 10000ll / clc.downloadSize ) / 100ll, ( clc.downloadCount * 10000ll / clc.downloadSize ) % 100ll );
	}

	//CL_AddReliableCommand( va( "nextdl %d", clc.downloadBlock ) );
	Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "nextdl %d", clc.downloadBlock );
	CL_WritePacket();
	clc.downloadBlock++;

	clc.downloadCount += size;

	if ( !size ) { // A zero length block means EOF
		if ( clc.download ) {
			if ( fp != NULL ) { fclose( fp ); }
			//FS_FCloseFile( clc.download );
			clc.download = 0;

			// rename the file
			//FS_SV_Rename( clc.downloadTempName, clc.downloadName );
			Com_Printf( "Recv completed\n" );
		}
		*clc.downloadTempName = *clc.downloadName = 0;
		//Cvar_Set( "cl_downloadName", "" );

		// send intentions now
		// We need this because without it, we would hold the last nextdl and then start
		// loading right away.  If we take a while to load, the server is happily trying
		// to send us that last block over and over.
		// Write it twice to help make sure we acknowledge the download
		CL_WritePacket();
		CL_WritePacket();

		// get another file if needed
		//CL_NextDownload();
	}
}

void CL_DeltaEntity( msg_t *msg, clSnapshot_t *frame, int newnum, entityState_t *old,
	qboolean unchanged );

/*
==================
CL_ParsePacketEntities

==================
*/
void CL_ParsePacketEntities( msg_t *msg, clSnapshot_t *oldframe, clSnapshot_t *newframe ) {
	int			newnum;
	entityState_t	*oldstate;
	int			oldindex, oldnum;

	newframe->parseEntitiesNum = cl.parseEntitiesNum;
	newframe->numEntities = 0;

	// delta from the entities present in oldframe
	oldindex = 0;
	oldstate = NULL;
	if ( !oldframe ) {
		oldnum = 99999;
	}
	else {
		if ( oldindex >= oldframe->numEntities ) {
			oldnum = 99999;
		}
		else {
			oldstate = &cl.parseEntities[
				( oldframe->parseEntitiesNum + oldindex ) & ( MAX_PARSE_ENTITIES - 1 )];
			oldnum = oldstate->number;
		}
	}

	while ( 1 ) {
		// read the entity index number
		newnum = MSG_ReadBits( msg, GENTITYNUM_BITS );

		if ( newnum == ( MAX_GENTITIES - 1 ) ) {
			break;
		}

		if ( msg->readcount > msg->cursize ) {
			Com_Error( ERR_DROP, "CL_ParsePacketEntities: end of message" );
		}

		while ( oldnum < newnum ) {
			// one or more entities from the old packet are unchanged
			if ( cl_shownet->integer == 3 ) {
				Com_Printf( "%3i:  unchanged: %i\n", msg->readcount, oldnum );
			}
			CL_DeltaEntity( msg, newframe, oldnum, oldstate, qtrue );

			oldindex++;

			if ( oldindex >= oldframe->numEntities ) {
				oldnum = 99999;
			}
			else {
				oldstate = &cl.parseEntities[
					( oldframe->parseEntitiesNum + oldindex ) & ( MAX_PARSE_ENTITIES - 1 )];
				oldnum = oldstate->number;
			}
		}
		if ( oldnum == newnum ) {
			// delta from previous state
			if ( cl_shownet->integer == 3 ) {
				Com_Printf( "%3i:  delta: %i\n", msg->readcount, newnum );
			}
			CL_DeltaEntity( msg, newframe, newnum, oldstate, qfalse );

			oldindex++;

			if ( oldindex >= oldframe->numEntities ) {
				oldnum = 99999;
			}
			else {
				oldstate = &cl.parseEntities[
					( oldframe->parseEntitiesNum + oldindex ) & ( MAX_PARSE_ENTITIES - 1 )];
				oldnum = oldstate->number;
			}
			continue;
		}

		if ( oldnum > newnum ) {
			// delta from baseline
			if ( cl_shownet->integer == 3 ) {
				Com_Printf( "%3i:  baseline: %i\n", msg->readcount, newnum );
			}
			CL_DeltaEntity( msg, newframe, newnum, &cl.entityBaselines[newnum], qfalse );
			continue;
		}

	}

	// any remaining entities in the old frame are copied over
	while ( oldnum != 99999 ) {
		// one or more entities from the old packet are unchanged
		if ( cl_shownet->integer == 3 ) {
			Com_Printf( "%3i:  unchanged: %i\n", msg->readcount, oldnum );
		}
		CL_DeltaEntity( msg, newframe, oldnum, oldstate, qtrue );

		oldindex++;

		if ( oldindex >= oldframe->numEntities ) {
			oldnum = 99999;
		}
		else {
			oldstate = &cl.parseEntities[
				( oldframe->parseEntitiesNum + oldindex ) & ( MAX_PARSE_ENTITIES - 1 )];
			oldnum = oldstate->number;
		}
	}
}

/*
================
CL_ParseSnapshot

If the snapshot is parsed properly, it will be copied to
cl.snap and saved in cl.snapshots[].  If the snapshot is invalid
for any reason, no changes to the state will be made at all.
================
*/
void CL_ParseSnapshot( msg_t *msg ) {
	int			len;
	clSnapshot_t	*old;
	clSnapshot_t	newSnap;
	int			oldMessageNum;
	int			i, packetNum;

	cl.serverTime = MSG_ReadLong( msg );
	//Com_Printf( "new server time: %d\n", cl.serverTime );
	int deltaNum = MSG_ReadByte( msg );
	if ( deltaNum > 0 ) {
		deltaNum = clc.serverMessageSequence - deltaNum;
	}
	if ( deltaNum <= 0 ) {
		clc.demowaiting = qfalse;	// we can start recording now
	}
	cl.snap.messageNum = clc.serverMessageSequence;

	// get the reliable sequence acknowledge number
	// NOTE: now sent with all server to client messages
	//clc.reliableAcknowledge = MSG_ReadLong( msg );

	// read in the new snapshot to a temporary buffer
	// we will only copy to cl.snap if it is valid
	Com_Memset( &newSnap, 0, sizeof( newSnap ) );

	newSnap.deltaNum = deltaNum;
	newSnap.snapFlags = MSG_ReadByte( msg );

	// If the frame is delta compressed from data that we
	// no longer have available, we must suck up the rest of
	// the frame, but not use it, then ask for a non-compressed
	// message 
	if ( newSnap.deltaNum <= 0 ) {
		newSnap.valid = qtrue;		// uncompressed frame
		old = NULL;
	}
	else {
		old = &cl.snapshots[newSnap.deltaNum & PACKET_MASK];
	}

	// read areamask
	len = MSG_ReadByte( msg );
	MSG_ReadData( msg, &newSnap.areamask, len );

	// read playerinfo
	if ( old ) {
		MSG_ReadDeltaPlayerstate( msg, &old->ps, &newSnap.ps );
		if ( newSnap.ps.m_iVehicleNum )
		{ //this means we must have written our vehicle's ps too
			MSG_ReadDeltaPlayerstate( msg, &old->vps, &newSnap.vps, qtrue );
		}
	}
	else {
		MSG_ReadDeltaPlayerstate( msg, NULL, &newSnap.ps );
		if ( newSnap.ps.m_iVehicleNum )
		{ //this means we must have written our vehicle's ps too
			MSG_ReadDeltaPlayerstate( msg, NULL, &newSnap.vps, qtrue );
		}
	}

	// read packet entities
	CL_ParsePacketEntities( msg, old, &newSnap );

	// if not valid, dump the entire thing now that it has
	// been properly read
	return;
}

/*
=====================
CL_ParseServerMessage
=====================
*/
void CL_ParseServerMessage( msg_t *msg ) {
	int			cmd;

	if ( cl_shownet->integer == 1 ) {
		Com_Printf( "%i ", msg->cursize );
	}
	else if ( cl_shownet->integer >= 2 ) {
		Com_Printf( "------------------\n" );
	}

	int headerBytes = msg->readcount;

	MSG_Bitstream( msg );

	// get the reliable sequence acknowledge number
	clc.reliableAcknowledge = MSG_ReadLong( msg );
	//
	if ( clc.reliableAcknowledge < clc.reliableSequence - MAX_RELIABLE_COMMANDS ) {
		clc.reliableAcknowledge = clc.reliableSequence;
	}

	//
	// parse the message
	//
	while ( 1 ) {
		if ( msg->readcount > msg->cursize ) {
			Com_Error( ERR_DROP, "CL_ParseServerMessage: read past end of server message" );
			break;
		}

		cmd = MSG_ReadByte( msg );

		if ( cmd == svc_EOF ) {
			break;
		}

		qboolean endOfParsing = qfalse;
		// other commands
		switch ( cmd ) {
		default:
			Com_Error( ERR_DROP, "CL_ParseServerMessage: Illegible server message\n" );
			break;
		case svc_nop:
			break;
		case svc_serverCommand:
			CL_ParseCommandString( msg );
			break;
		case svc_gamestate:
			clc.serverCommandSequence = MSG_ReadLong( msg );
			Com_Memset( &cl, 0, sizeof( cl ) );
			// parse all the configstrings and baselines
			cl.gameState.dataCount = 1;	// leave a 0 at the beginning for uninitialized configstrings
			while ( 1 ) {
				cmd = MSG_ReadByte( msg );

				if ( cmd == svc_EOF ) {
					break;
				}

				if ( cmd == svc_configstring ) {
					int		len, start;

					start = msg->readcount;

					int i = MSG_ReadShort( msg );
					if ( i < 0 || i >= MAX_CONFIGSTRINGS ) {
						Com_Error( ERR_DROP, "configstring > MAX_CONFIGSTRINGS" );
					}
					char *s = MSG_ReadBigString( msg );

					if ( cl_shownet->integer >= 2 )
					{
						Com_Printf( "%3i: %d: %s\n", start, i, s );
					}

					len = strlen( s );

					if ( len + 1 + cl.gameState.dataCount > MAX_GAMESTATE_CHARS ) {
						Com_Error( ERR_DROP, "MAX_GAMESTATE_CHARS exceeded" );
					}

					// append it to the gameState string buffer
					cl.gameState.stringOffsets[i] = cl.gameState.dataCount;
					Com_Memcpy( cl.gameState.stringData + cl.gameState.dataCount, s, len + 1 );
					cl.gameState.dataCount += len + 1;
				}
				else if ( cmd == svc_baseline ) {
					int newnum = MSG_ReadBits( msg, GENTITYNUM_BITS );
					if ( newnum < 0 || newnum >= MAX_GENTITIES ) {
						Com_Error( ERR_DROP, "Baseline number out of range: %i", newnum );
					}
					entityState_t nullstate = {};
					entityState_t *es = &cl.entityBaselines[newnum];
					MSG_ReadDeltaEntity( msg, &nullstate, es, newnum );
				}
				else {
					Com_Error( ERR_DROP, "CL_ParseGamestate: bad command byte" );
				}
			}

			clc.clientNum = MSG_ReadLong( msg );
			// read the checksum feed
			clc.checksumFeed = MSG_ReadLong( msg );

			CL_SystemInfoChanged();
			CL_StopRecord_f();
			CL_Record_f();
			CL_WriteDemoMessage( msg, headerBytes );
			endOfParsing = qtrue;
			//CL_ParseGamestate( msg );
			break;
		case svc_snapshot: {
			CL_ParseSnapshot( msg );
			/*cl.serverTime = MSG_ReadLong( msg );
			//Com_Printf( "new server time: %d\n", cl.serverTime );
			int deltaNum = MSG_ReadByte( msg );
			if ( deltaNum > 0 ) {
				deltaNum = clc.serverMessageSequence - deltaNum;
			}
			if ( deltaNum <= 0 ) {
				clc.demowaiting = qfalse;	// we can start recording now
			}
			cl.snap.messageNum = clc.serverMessageSequence;
			endOfParsing = qtrue;*/
			break;
		}
		case svc_setgame:
			//CL_ParseSetGame( msg );
			endOfParsing = qtrue;
			break;
		case svc_download:
			CL_ParseDownload( msg );
			//endOfParsing = qtrue;
			break;
		case svc_mapchange:
			//if ( cls.cgameStarted )
			//	CGVM_MapChange();
			endOfParsing = qtrue;
			break;
		}
		if ( endOfParsing ) {
			break;
		}
	}
}

/*
=================
CL_PacketEvent

A packet has arrived from the main event loop
=================
*/
void CL_PacketEvent( netadr_t from, msg_t *msg ) {
	int		headerBytes;

	// for lack of a better place to put this check...
#ifdef WIN32
	if ( hProcess != nullptr ) {
		DWORD ret = WaitForSingleObject( hProcess, 0 );
		if ( ret != WAIT_TIMEOUT ) {
			CloseHandle( hProcess );
			hProcess = nullptr;
			if ( indexFinished ) {
				indexFinished();
			}
		}
	}
#endif

	clc.lastPacketTime = cls.realtime;

	if ( msg->cursize >= 4 && *(int *) msg->data == -1 ) {
		CL_ConnectionlessPacket( from, msg );
		return;
	}

	if ( cls.state < CA_CONNECTED ) {
		return;		// can't be a valid sequenced packet
	}

	if ( msg->cursize < 4 ) {
		Com_Printf( "%s: Runt packet\n", NET_AdrToString( from ) );
		return;
	}

	//
	// packet from server
	//
	if ( !NET_CompareAdr( from, clc.netchan.remoteAddress ) ) {
		Com_DPrintf( "%s:sequenced packet without connection\n"
			, NET_AdrToString( from ) );
		// FIXME: send a client disconnect?
		return;
	}

	if ( !CL_Netchan_Process( &clc.netchan, msg ) ) {
		return;		// out of order, duplicated, etc
	}

	// the header is different lengths for reliable and unreliable messages
	headerBytes = msg->readcount;

	// track the last message received so it can be returned in
	// client messages, allowing the server to detect a dropped
	// gamestate
	clc.serverMessageSequence = LittleLong( *(int *) msg->data );

	clc.lastPacketTime = cls.realtime;
	CL_ParseServerMessage( msg );

	//
	// we don't know if it is ok to save a demo message until
	// after we have parsed the frame
	//
	if ( clc.demorecording && !clc.demowaiting ) {
		CL_WriteDemoMessage( msg, headerBytes );
	}
}

/*
===================
CL_WritePacket

Create and send the command packet to the server
Including both the reliable commands and the usercmds

During normal gameplay, a client packet will contain something like:

4	sequence number
2	qport
4	serverid
4	acknowledged sequence number
4	clc.serverCommandSequence
<optional reliable commands>
1	clc_move or clc_moveNoDelta
1	command count
<count * usercmds>

===================
*/
int lastCommandTime = -10 * 1000;
void CL_WritePacket( void ) {
	msg_t		buf;
	byte		data[MAX_MSGLEN];
	int			i, j;
	usercmd_t	*cmd, *oldcmd;
	usercmd_t	nullcmd;
	int			packetNum;
	int			oldPacketNum;
	int			count, key;

	// don't send anything if playing back a demo
	if ( clc.demoplaying || cls.state == CA_CINEMATIC ) {
		return;
	}

	Com_Memset( &nullcmd, 0, sizeof( nullcmd ) );
	oldcmd = &nullcmd;

	MSG_Init( &buf, data, sizeof( data ) );

	MSG_Bitstream( &buf );
	// write the current serverId so the server
	// can tell if this is from the current gameState
	MSG_WriteLong( &buf, cl.serverId );

	// write the last message we received, which can
	// be used for delta compression, and is also used
	// to tell if we dropped a gamestate
	MSG_WriteLong( &buf, clc.serverMessageSequence );

	// write the last reliable message we received
	MSG_WriteLong( &buf, clc.serverCommandSequence );

	// write any unacknowledged clientCommands
	// to avoid flooding the server, send only 1 command at a time.
	// this should be ok since this is only used by bots.
	if ( cls.realtime - lastCommandTime > 1000 ) {
		for ( i = clc.reliableAcknowledge + 1; i <= clc.reliableSequence; i++ ) {
			MSG_WriteByte( &buf, clc_clientCommand );
			MSG_WriteLong( &buf, i );
			MSG_WriteString( &buf, clc.reliableCommands[i & ( MAX_RELIABLE_COMMANDS - 1 )] );
			lastCommandTime = cls.realtime;
			break;
		}
	}

	// we want to send all the usercmds that were generated in the last
	// few packet, so even if a couple packets are dropped in a row,
	// all the cmds will make it to the server
	/*if ( cl_packetdup->integer < 0 ) {
	Cvar_Set( "cl_packetdup", "0" );
	}
	else if ( cl_packetdup->integer > 5 ) {
	Cvar_Set( "cl_packetdup", "5" );
	}*/
	int packetdup = 0;
	oldPacketNum = ( clc.netchan.outgoingSequence - 1 - packetdup ) & PACKET_MASK;
	count = cl.cmdNumber - cl.outPackets[oldPacketNum].p_cmdNumber;
	if ( count > MAX_PACKET_USERCMDS ) {
		count = MAX_PACKET_USERCMDS;
		Com_Printf( "MAX_PACKET_USERCMDS\n" );
	}
	if ( count >= 1 ) {
		if ( /*cl_showSend->integer*/ 0 ) {
			Com_Printf( "(%i)", count );
		}

		// begin a client move command
		if ( /*cl_nodelta->integer ||*/ !cl.snap.valid
			|| clc.demowaiting
			|| clc.serverMessageSequence != cl.snap.messageNum ) {
			MSG_WriteByte( &buf, clc_moveNoDelta );
		}
		else {
			MSG_WriteByte( &buf, clc_move );
		}

		// write the command count
		MSG_WriteByte( &buf, count );

		// use the checksum feed in the key
		key = clc.checksumFeed;
		// also use the message acknowledge
		key ^= clc.serverMessageSequence;
		// also use the last acknowledged server command in the key
		key ^= Com_HashKey( clc.serverCommands[clc.serverCommandSequence & ( MAX_RELIABLE_COMMANDS - 1 )], 32 );

		// write all the commands, including the predicted command
		for ( i = 0; i < count; i++ ) {
			j = ( cl.cmdNumber - count + i + 1 ) & CMD_MASK;
			cmd = &cl.cmds[j];
			MSG_WriteDeltaUsercmdKey( &buf, key, oldcmd, cmd );
			oldcmd = cmd;
		}

		if ( cl.gcmdSentValue )
		{ //hmm, just clear here, I guess.. hoping it will resolve issues with gencmd values sometimes not going through.
			cl.gcmdSendValue = qfalse;
			cl.gcmdSentValue = qfalse;
			cl.gcmdValue = 0;
		}
	}

	//
	// deliver the message
	//
	packetNum = clc.netchan.outgoingSequence & PACKET_MASK;
	cl.outPackets[packetNum].p_realtime = cls.realtime;
	cl.outPackets[packetNum].p_serverTime = oldcmd->serverTime;
	cl.outPackets[packetNum].p_cmdNumber = cl.cmdNumber;
	clc.lastPacketSentTime = cls.realtime;

	if ( /*cl_showSend->integer*/ 0 ) {
		Com_Printf( "%i ", buf.cursize );
	}

	CL_Netchan_Transmit( &clc.netchan, &buf );

	// clients never really should have messages large enough
	// to fragment, but in case they do, fire them all off
	// at once
	while ( clc.netchan.unsentFragments ) {
		CL_Netchan_TransmitNextFragment( &clc.netchan );
	}
}

void StripColor( char *text ) {
	char *src = text, *dst = text;
	while ( src[0] != '\0' ) {
		if ( src[0] == '^' && ( src[1] >= '0' && src[1] <= '9' ) ) {
			src += 2;
			continue;
		}
		*dst++ = *src++;
	}
	*dst = 0;
}

void Cmd_Chat_f( void ) {
	char msg[MAX_STRING_CHARS];
	Q_strncpyz( msg, Cmd_Argv( 1 ), sizeof( msg ) );
	StripColor( msg );
	Com_Printf( "%s\n", msg );
}

const char *getPlayerName( int playerIdx ) {
	if ( playerIdx > MAX_CLIENTS ) {
		return "UNKNOWN";
	}
	const char *result = Info_ValueForKey( cl.gameState.stringData + cl.gameState.stringOffsets[CS_PLAYERS + playerIdx], "n" );
	if ( result ) {
		return result;
	}
	return "UNKNOWN";
}

team_t getPlayerTeam( int playerIdx ) {
	if ( playerIdx > MAX_CLIENTS ) {
		return TEAM_FREE;
	}
	return (team_t) atoi( Info_ValueForKey( cl.gameState.stringData + cl.gameState.stringOffsets[CS_PLAYERS + playerIdx], "t" ) );
}

const char *CG_TeamName( team_t team )
{
	if ( team == TEAM_RED )
		return "RED";
	else if ( team == TEAM_BLUE )
		return "BLUE";
	else if ( team == TEAM_SPECTATOR )
		return "SPECTATOR";
	return "FREE";
}

const char *getPlayerTeamName( int playerIdx ) {
	if ( playerIdx > MAX_CLIENTS ) {
		return "UNKNOWN";
	}
	return CG_TeamName( getPlayerTeam( playerIdx ) );
}

void CL_Disconnect_f( void ) {
	Com_sprintf( NewClientCommand(), MAX_STRING_CHARS, "disconnect" );

	while ( cls.realtime - lastCommandTime <= 1000 ) {
		NET_Sleep( 1000 - ( cls.realtime - lastCommandTime ) );
		cls.realtime = Com_Milliseconds();
	}
	//cls.realtime = Q_max( cls.realtime, lastCommandTime + 1001 );

	CL_WritePacket();
	CL_WritePacket();
	CL_WritePacket();

	if ( clc.demorecording ) {
		CL_StopRecord_f();
	}

	if ( clc.demofile ) {
		FS_FCloseFile( clc.demofile );
		clc.demofile = 0;
	}

	cls.state = CA_DISCONNECTED;
}

/*
==================
CL_GlobalServers_f
==================
*/
void CL_GlobalServers_f( void ) {
	netadr_t	to;
	int			count, i, masterNum;
	char		command[1024], *masteraddress;

	if ( ( count = Cmd_Argc() ) < 3 || ( masterNum = atoi( Cmd_Argv( 1 ) ) ) < 0 || masterNum > MAX_MASTER_SERVERS - 1 )
	{
		Com_Printf( "usage: globalservers <master# 0-%d> <protocol> [keywords]\n", MAX_MASTER_SERVERS - 1 );
		return;
	}

	Com_sprintf( command, sizeof( command ), "sv_master%d", masterNum + 1 );
	masteraddress = Cvar_VariableString( command );

	if ( !*masteraddress )
	{
		Com_Printf( "CL_GlobalServers_f: Error: No master server address given.\n" );
		return;
	}

	// reset the list, waiting for response
	// -1 is used to distinguish a "no response"

	i = NET_StringToAdr( masteraddress, &to );

	if ( !i )
	{
		Com_Printf( "CL_GlobalServers_f: Error: could not resolve address of master %s\n", masteraddress );
		return;
	}
	to.type = NA_IP;
	to.port = BigShort( PORT_MASTER );

	Com_DPrintf( "Requesting servers from the master %s (%s)...\n", masteraddress, NET_AdrToString( to ) );

	cls.numglobalservers = -1;
	cls.pingUpdateSource = AS_GLOBAL;

	Com_sprintf( command, sizeof( command ), "getservers %s", Cmd_Argv( 2 ) );

	// tack on keywords
	for ( i = 3; i < count; i++ )
	{
		Q_strcat( command, sizeof( command ), " " );
		Q_strcat( command, sizeof( command ), Cmd_Argv( i ) );
	}

	NET_OutOfBandPrint( NS_SERVER, to, "%s", command );
}

/*
==================
CL_GetPing
==================
*/
void CL_GetPing( int n, char *buf, int buflen, int *pingtime )
{
	const char	*str;
	int		time;
	int		maxPing;

	if ( n < 0 || n >= MAX_PINGREQUESTS || !cl_pinglist[n].adr.port )
	{
		// empty or invalid slot
		buf[0] = '\0';
		*pingtime = 0;
		return;
	}

	str = NET_AdrToString( cl_pinglist[n].adr );
	Q_strncpyz( buf, str, buflen );

	time = cl_pinglist[n].time;
	if ( !time )
	{
		// check for timeout
		time = Sys_Milliseconds() - cl_pinglist[n].start;
		maxPing = Cvar_VariableIntegerValue( "cl_maxPing" );
		if ( maxPing < 100 ) {
			maxPing = 100;
		}
		if ( time < maxPing )
		{
			// not timed out yet
			time = 0;
		}
	}

	const char *info = NULL;
	if ( time > 0 ) {
		info = cl_pinglist[n].info;
	}
	CL_SetServerInfoByAddress( cl_pinglist[n].adr, info, cl_pinglist[n].time );

	*pingtime = time;
}

/*
==================
CL_GetPingInfo
==================
*/
void CL_GetPingInfo( int n, char *buf, int buflen )
{
	if ( n < 0 || n >= MAX_PINGREQUESTS || !cl_pinglist[n].adr.port )
	{
		// empty or invalid slot
		if ( buflen )
			buf[0] = '\0';
		return;
	}

	Q_strncpyz( buf, cl_pinglist[n].info, buflen );
}

/*
==================
CL_ClearPing
==================
*/
void CL_ClearPing( int n )
{
	if ( n < 0 || n >= MAX_PINGREQUESTS )
		return;

	cl_pinglist[n].adr.port = 0;
}

/*
==================
CL_GetPingQueueCount
==================
*/
int CL_GetPingQueueCount( void )
{
	int		i;
	int		count;
	ping_t*	pingptr;

	count = 0;
	pingptr = cl_pinglist;

	for ( i = 0; i<MAX_PINGREQUESTS; i++, pingptr++ ) {
		if ( pingptr->adr.port ) {
			count++;
		}
	}

	return ( count );
}

/*
==================
CL_GetFreePing
==================
*/
ping_t* CL_GetFreePing( void )
{
	ping_t*	pingptr;
	ping_t*	best;
	int		oldest;
	int		i;
	int		time;

	pingptr = cl_pinglist;
	for ( i = 0; i<MAX_PINGREQUESTS; i++, pingptr++ )
	{
		// find free ping slot
		if ( pingptr->adr.port )
		{
			if ( !pingptr->time )
			{
				if ( Sys_Milliseconds() - pingptr->start < 500 )
				{
					// still waiting for response
					continue;
				}
			}
			else if ( pingptr->time < 500 )
			{
				// results have not been queried
				continue;
			}
		}

		// clear it
		pingptr->adr.port = 0;
		return ( pingptr );
	}

	// use oldest entry
	pingptr = cl_pinglist;
	best = cl_pinglist;
	oldest = INT_MIN;
	for ( i = 0; i<MAX_PINGREQUESTS; i++, pingptr++ )
	{
		// scan for oldest
		time = Sys_Milliseconds() - pingptr->start;
		if ( time > oldest )
		{
			oldest = time;
			best = pingptr;
		}
	}

	return ( best );
}

/*
==================
CL_Ping_f
==================
*/
void CL_Ping_f( void ) {
	netadr_t	to;
	ping_t*		pingptr;
	char*		server;

	if ( Cmd_Argc() != 2 ) {
		Com_Printf( "usage: ping [server]\n" );
		return;
	}

	Com_Memset( &to, 0, sizeof( netadr_t ) );

	server = Cmd_Argv( 1 );

	if ( !NET_StringToAdr( server, &to ) ) {
		return;
	}

	pingptr = CL_GetFreePing();

	memcpy( &pingptr->adr, &to, sizeof( netadr_t ) );
	pingptr->start = Sys_Milliseconds();
	pingptr->time = 0;

	CL_SetServerInfoByAddress( pingptr->adr, NULL, 0 );

	NET_OutOfBandPrint( NS_CLIENT, to, "getinfo xxx" );
}

/*
==================
CL_UpdateVisiblePings_f
==================
*/
qboolean CL_UpdateVisiblePings_f( int source ) {
	int			slots, i;
	char		buff[MAX_STRING_CHARS];
	int			pingTime;
	int			max;
	qboolean status = qfalse;

	if ( source < 0 || source > AS_FAVORITES ) {
		return qfalse;
	}

	cls.pingUpdateSource = source;

	slots = CL_GetPingQueueCount();
	if ( slots < MAX_PINGREQUESTS ) {
		serverInfo_t *server = NULL;

		switch ( source ) {
		case AS_LOCAL:
			server = &cls.localServers[0];
			max = cls.numlocalservers;
			break;
		case AS_GLOBAL:
			server = &cls.globalServers[0];
			max = cls.numglobalservers;
			break;
		case AS_FAVORITES:
			server = &cls.favoriteServers[0];
			max = cls.numfavoriteservers;
			break;
		default:
			return qfalse;
		}
		for ( i = 0; i < max; i++ ) {
			if ( server[i].visible || qtrue ) {
				if ( server[i].ping == -1 ) {
					int j;

					if ( slots >= MAX_PINGREQUESTS ) {
						break;
					}
					for ( j = 0; j < MAX_PINGREQUESTS; j++ ) {
						if ( !cl_pinglist[j].adr.port ) {
							continue;
						}
						if ( NET_CompareAdr( cl_pinglist[j].adr, server[i].adr ) ) {
							// already on the list
							break;
						}
					}
					if ( j >= MAX_PINGREQUESTS ) {
						status = qtrue;
						for ( j = 0; j < MAX_PINGREQUESTS; j++ ) {
							if ( !cl_pinglist[j].adr.port ) {
								break;
							}
						}
						if ( j < MAX_PINGREQUESTS ) {
							memcpy( &cl_pinglist[j].adr, &server[i].adr, sizeof( netadr_t ) );
							cl_pinglist[j].start = Sys_Milliseconds();
							cl_pinglist[j].time = 0;
							NET_OutOfBandPrint( NS_CLIENT, cl_pinglist[j].adr, "getinfo xxx" );
						}
						slots++;
					}
				}
				// if the server has a ping higher than cl_maxPing or
				// the ping packet got lost
				else if ( server[i].ping == 0 ) {
					// if we are updating global servers
					if ( source == AS_GLOBAL ) {
						//
						if ( cls.numGlobalServerAddresses > 0 ) {
							// overwrite this server with one from the additional global servers
							cls.numGlobalServerAddresses--;
							CL_InitServerInfo( &server[i], &cls.globalServerAddresses[cls.numGlobalServerAddresses] );
							// NOTE: the server[i].visible flag stays untouched
						}
					}
				}
			}
		}
	}

	if ( slots ) {
		status = qtrue;
	}
	for ( i = 0; i < MAX_PINGREQUESTS; i++ ) {
		if ( !cl_pinglist[i].adr.port ) {
			continue;
		}
		CL_GetPing( i, buff, MAX_STRING_CHARS, &pingTime );
		if ( pingTime != 0 ) {
			CL_ClearPing( i );
			status = qtrue;
		}
	}

	return status;
}
