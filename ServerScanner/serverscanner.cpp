#include <chrono>
#include <stdio.h>

#include "deps.h"
#include "client/client.h"

#include "cJSON.h"

#include <string>
#include <fstream>
#include <streambuf>

#ifdef _WIN32
#include <windows.h>
#endif

static byte		sys_packetReceived[MAX_MSGLEN] = {};

extern qboolean Sys_GetPacket( netadr_t *net_from, msg_t *net_message );  // in net_ip.cpp

#define MAX_SERVERSTATUS_LINES	128
#define MAX_SERVERSTATUS_TEXT	4096 //1024

typedef struct playerInfo_s {
	char name[MAX_NAME_LENGTH];
	int ping;
	int score;
} playerInfo_t;

typedef struct serverStatusInfo_s {
	char address[MAX_ADDRESSLENGTH];
	char info[MAX_INFO_STRING];
	playerInfo_t players[MAX_CLIENTS];
	int numPlayers;
} serverStatusInfo_t;

/*
==================
GetServerStatusInfo
==================
*/
static qboolean GetServerStatusInfo( const char *serverAddress, serverStatusInfo_t *info ) {
	char *p, *score, *ping, *name, infoStr[MAX_SERVERSTATUS_TEXT];

	memset( info, 0, sizeof( *info ) );
	if ( CL_ServerStatus( serverAddress, infoStr, sizeof( infoStr ) ) ) {
		Q_strncpyz( info->address, serverAddress, sizeof( info->address ) );
		p = infoStr;
		// get the cvars
		while ( p && *p ) {
			p = strchr( p, '\\' );
			if ( !p ) break;
			p++;
			if ( *p == '\\' )
				break;
			p = strchr( p, '\\' );
			if ( !p ) break;
			p++;
		}
		// get the player list
		// parse players
		while ( p && *p ) {
			if ( *p == '\\' )
				*p++ = '\0';
			score = p;
			p = strchr( p, ' ' );
			if ( !p )
				break;
			*p++ = '\0';
			ping = p;
			p = strchr( p, ' ' );
			if ( !p )
				break;
			*p++ = '\0';
			name = p;
			p = strchr( p, '\\' );
			if ( p )
				*p++ = '\0';
			playerInfo_t *player = &info->players[info->numPlayers];
			Q_strncpyz( player->name, name, sizeof( player->name ) );
			player->ping = atoi( ping );
			player->score = atoi( score );
			info->numPlayers++;
			if ( info->numPlayers >= MAX_CLIENTS ) {
				break;
			}
			if ( !p )
				break;
		}
		Q_strncpyz( info->info, infoStr, sizeof( info->info ) );
		return qtrue;
	}
	return qfalse;
}

serverInfo_t launchedServers[1024];
int numLaunchedServers = 0;

qboolean RemoveServer( serverInfo_t *server ) {
	for ( int idx = 0; idx < numLaunchedServers; idx++ ) {
		if ( NET_CompareAdr( server->adr, launchedServers[idx].adr ) ) {
			memmove( &launchedServers[idx], &launchedServers[idx + 1], sizeof( serverInfo_t ) * ( numLaunchedServers - idx - 1 ) );
			numLaunchedServers--;
			return qtrue;
		}
	}
	return qfalse;
}

static struct connectData_s {
	const char *address;
	const char *password;
	const char *rconpassword;
} serverPasswords[256] = {
	{ "pug.jactf.com:29071", "ctfpug", "" },
	{ "whoracle.jactf.com", "ctfpug", "" },
	{ "whoracle2.jactf.com", "ctfpug", "" },
	{ "sjc.jactf.com:29072", "ctfpug", "" },
	{ "akl.jactf.com", "ctfpug", "" },
	{ "23.95.82.67:29070", "wildcard", "" },
	{ "185.16.85.137:29070", "pureping", "" },
	{ "50.63.117.75:29071", "pugtime", "" },
	{ "23.108.73.202:2220", "fadinglight", "" },
	{ "27.50.71.17:29070", "d4rkCrus4d3", "" },
	{ "37.48.122.129", "borkingmad", "" },
	{ "104.153.105.191", "happycake", "" },
	{ "94.23.88.66:26010", "esl", "" },
	{ "46.228.195.123:29000", "esl", "" },
	{ "91.121.65.219:12345", "esl", "" },
	{ "198.23.145.11:29070", "pug", "" },
	{ "70.90.221.49:29070", "esl", "" },
	{ nullptr, nullptr, nullptr },
};

static const char *serverWhitelist[256] = {
	"pug.jactf.com:29071",
	"whoracle.jactf.com",
	"whoracle2.jactf.com",
	"sjc.jactf.com:29072",
	"akl.jactf.com",
	"bra.jactf.com",
	nullptr,
};

void SpawnClient( const char* address ) {
	const char *password = nullptr;
	const char *rconpassword = nullptr;
	netadr_t adr;
	if ( !NET_StringToAdr( address, &adr ) ) {
		Com_Printf( "Failed to resolve %s\n", address );
		return;
	}
	for ( const auto& serverPassword : serverPasswords ) {
		if ( serverPassword.address == nullptr ) {
			break;
		}
		netadr_t serverAdr;
		if ( !NET_StringToAdr( serverPassword.address, &serverAdr ) ) {
			Com_Printf( "Failed to resolve %s\n", serverPassword.address );
			continue;
		}
		if ( NET_CompareAdr( adr, serverAdr ) ) {
			password = serverPassword.password;
			Com_Printf( "Using password: %s\n", password );
			if ( serverPassword.rconpassword[0] != '\0' ) {
				rconpassword = serverPassword.rconpassword;
				Com_Printf( "Using rconpassword: %s\n", rconpassword );
			}
		}
	}

	char command[MAX_STRING_CHARS];
#ifdef WIN32
#define DEMOBOT "DemoBot.exe"
#else
#define DEMOBOT "./demobot"
#endif
	Com_sprintf( command, sizeof( command ), "%s %s", DEMOBOT, address );
	if ( password != nullptr ) {
		Q_strcat( command, sizeof( command ), " " );
		Q_strcat( command, sizeof( command ), password );
		if ( rconpassword != nullptr ) {
			Q_strcat( command, sizeof( command ), " " );
			Q_strcat( command, sizeof( command ), rconpassword );
		}
	}

#ifdef _WIN32
	// Initialize StartupInfo structure
	STARTUPINFO    StartupInfo;
	memset( &StartupInfo, 0, sizeof( StartupInfo ) );
	StartupInfo.cb = sizeof( StartupInfo );

	// This will contain the information about the newly created process
	PROCESS_INFORMATION ProcessInformation;

	BOOL results = CreateProcess( 0, command,
	                              0, // Process Attributes
	                              0, // Thread Attributes
	                              FALSE, // Inherit Handles
								  0 /*CREATE_NEW_CONSOLE*/, // CreationFlags,
	                              0, // Enviornment
	                              0, // Current Directory
	                              &StartupInfo, // StartupInfo
	                              & ProcessInformation // Process Information
	                              );
	if ( !results ) {
		Com_Printf( "Failed to launch process" );
		return;
	}

	// Cleanup
	CloseHandle( ProcessInformation.hProcess );
	CloseHandle( ProcessInformation.hThread );
#else
	Q_strcat( command, sizeof( command ), " &" );
	system(command);
#endif
}

qboolean AddServer( serverInfo_t *server ) {
	for ( int idx = 0; idx < numLaunchedServers; idx++ ) {
		if ( NET_CompareAdr( server->adr, launchedServers[idx].adr ) ) {
			return qfalse;
		}
		/*else if ( !strcmp( server->hostName, launchedServers[idx].hostName ) ) {
			Com_Printf( "Duplicate?? " );
			Com_Printf( "%s", NET_AdrToString( server->adr ) );
			Com_Printf( " %s\n", NET_AdrToString( launchedServers[idx].adr ) );
		}*/
	}
	if ( numLaunchedServers >= 1024 ) {
		Com_Printf( "Connected to max %d servers\n", numLaunchedServers );
		return qfalse;
	}
	launchedServers[numLaunchedServers] = *server;
	numLaunchedServers++;
	SpawnClient( NET_AdrToString( server->adr ) );
	return qtrue;
}

void FindPopulatedServers( void ) {
	for ( int idx = 0; idx < cls.numglobalservers; idx++ ) {
		serverInfo_t *server = &cls.globalServers[idx];
		if ( server->clients >= MAX_CLIENTS ) {
			// kiddies running fake servers
			continue;
		}
		if ( server->ping > 0 ) {
			int humans = -1;
			if ( server->bots + server->humans == server->clients ) {
				humans = server->humans;
			} else if ( server->clients > 0 ) {
				serverStatusInfo_t status;
				qboolean statusFound = GetServerStatusInfo( NET_AdrToString( server->adr ), &status );
				if ( statusFound ) {
					humans = 0;
					for ( int idx = 0; idx < status.numPlayers; idx++ ) {
						// bots generally have ping 0, players generally have ping > 0
						playerInfo_t *player = &status.players[idx];
						if ( player->ping > 0 ) {
							//Com_Printf( "Ping: %3d Score: %d Name: %s\n", player->ping, player->score, player->name );
							humans++;
						}
					}
				}
			}
			if ( humans > 0 ) {
				bool whitelisted = false;
				for ( const auto& serverAddress : serverWhitelist ) {
					if ( serverAddress == nullptr ) {
						break;
					}
					netadr_t serverAdr;
					if ( !NET_StringToAdr( serverAddress, &serverAdr ) ) {
						Com_Printf( "Failed to resolve %s\n", serverAddress );
						continue;
					}
					if ( NET_CompareAdr( server->adr, serverAdr ) ) {
						whitelisted = true;
					}
				}
				if ( whitelisted && AddServer( server ) ) {
					char hostname[MAX_STRING_CHARS];
					Q_strncpyz( hostname, server->hostName, sizeof( hostname ) );
					StripColor( hostname );
					Com_Printf( "Added server %s (%s) with %d humans\n", hostname, NET_AdrToString( server->adr ), humans );
				}
			} else if ( /*humans <= 1 && humans >= 0*/ humans == 0 ) {
				if ( RemoveServer( server ) ) {
					char hostname[MAX_STRING_CHARS];
					Q_strncpyz( hostname, server->hostName, sizeof( hostname ) );
					StripColor( hostname );
					Com_Printf( "Removed server %s (%s) with %d humans\n", hostname, NET_AdrToString( server->adr ), humans );
				}
			}
		}
	}
}

int main( int argc, char **argv ) {
	if ( argc > 1 ) {
		const char *configFile = argv[1];
		std::ifstream configStream( configFile );
		std::string config( ( std::istreambuf_iterator<char>( configStream ) ),
			std::istreambuf_iterator<char>() );
		cJSON *root = cJSON_Parse( config.c_str() );
		if ( cJSON_HasObjectItem( root, "serverPasswords" ) ) {
			cJSON *passwords = cJSON_GetObjectItem( root, "serverPasswords" );
			for ( int i = 0; i < cJSON_GetArraySize( passwords ); i++ ) {
				cJSON *obj = cJSON_GetArrayItem( passwords, i );
				cJSON *address = cJSON_GetObjectItem( obj, "address" );
				cJSON *password = cJSON_GetObjectItem( obj, "password" );
				cJSON *rconPassword = cJSON_GetObjectItem( obj, "rconPassword" );
				int j = 0;
				for ( ; j < sizeof( serverPasswords ) / sizeof( *serverPasswords ) && serverPasswords[j].address != nullptr; j++ ) {
					if ( !strcmp( serverPasswords[j].address, address->valuestring ) ) {
						break;
					}
				}
				if ( serverPasswords[j].address == nullptr ) {
					serverPasswords[j + 1].address = nullptr;
				}
				serverPasswords[j].address = strdup( address->valuestring );
				serverPasswords[j].password = strdup( password->valuestring );
				serverPasswords[j].rconpassword = strdup( rconPassword->valuestring );
        // add this server to the whitelist as well
        netadr_t serverAdr;
        if ( !NET_StringToAdr( address->valuestring, &serverAdr ) ) {
          Com_Printf( "Failed to resolve %s\n", address->valuestring );
          continue;
        }
        int w = 0;
        for ( ; w < sizeof( serverWhitelist ) / sizeof( *serverWhitelist ) && serverWhitelist[w] != nullptr; w++ ) {
          netadr_t curServerAdr;
          if ( !NET_StringToAdr( serverWhitelist[w], &curServerAdr ) ) {
            Com_Printf( "Failed to resolve %s\n", serverWhitelist[w] );
            continue;
          }
          if ( NET_CompareAdr( serverAdr, curServerAdr ) ) {
            break;
          }
        }
        if ( serverWhitelist[w] == nullptr ) {
          serverWhitelist[w] = strdup( address->valuestring );
					serverWhitelist[w + 1] = nullptr;
          Com_Printf( "Whitelisted server %s\n", serverWhitelist[w] );
				}
      }
		}
		cJSON_Delete( root );
	}

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
	CL_ServerStatus( nullptr, nullptr, 0 );

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
	Cvar_Get( "sex", "male", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get( "password", "", CVAR_USERINFO );
	Cvar_Get( "cg_predictItems", "1", CVAR_USERINFO | CVAR_ARCHIVE );

	//default sabers
	Cvar_Get( "saber1", DEFAULT_SABER, CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get( "saber2", "none", CVAR_USERINFO | CVAR_ARCHIVE );

	//skin color
	Cvar_Get( "char_color_red", "255", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get( "char_color_green", "255", CVAR_USERINFO | CVAR_ARCHIVE );
	Cvar_Get( "char_color_blue", "255", CVAR_USERINFO | CVAR_ARCHIVE );

	Cvar_Set( "name", "spec" );

	Cvar_Set( "sv_master1", "masterjk3.ravensoft.com" );
	Cvar_Set( "cl_maxPing", "9999" );

	Cmd_AddCommand( "connect", CL_Connect_f );
	Cmd_AddCommand( "chat", Cmd_Chat_f );
	Cmd_AddCommand( "cp", Cmd_Chat_f );
	Cmd_AddCommand( "cs", CL_ConfigstringModified );
	Cmd_AddCommand( "disconnect", CL_Disconnect_f );
	Cmd_AddCommand( "globalservers", CL_GlobalServers_f );

  // Manually prime server list with whitelisted servers, so we still scan
  // them if master is down
  for ( const auto& serverAddress : serverWhitelist ) {
    if ( serverAddress == nullptr ) {
      break;
    }
    netadr_t serverAdr;
    if ( !NET_StringToAdr( serverAddress, &serverAdr ) ) {
      Com_Printf( "Failed to resolve %s\n", serverAddress );
      continue;
    }
 	  serverInfo_t *server = &cls.globalServers[cls.numglobalservers];
    CL_InitServerInfo( server, &serverAdr );
    cls.numglobalservers++;
  }
  int whitelistedServerCount = cls.numglobalservers;

	int lastMasterScanTime = -99999;
	int lastServerPacketTime = cls.realtime;
	while ( qtrue ) {
		cls.realtime = Com_Milliseconds();
		CL_CheckForResend();

		netadr_t	adr;
		msg_t		netmsg;
		// check for network packets
		MSG_Init( &netmsg, sys_packetReceived, sizeof( sys_packetReceived ) );
		if ( Sys_GetPacket( &adr, &netmsg ) ) {
			lastServerPacketTime = cls.realtime;
			CL_PacketEvent( adr, &netmsg );
		}

		if ( cls.realtime - lastMasterScanTime > 60 * 1000 ) {
			Cmd_ExecuteString( va( "globalservers %d %d full empty", 0, PROTOCOL_VERSION ) );
			Cmd_ExecuteString( va( "globalservers %d %d full empty", 0, PROTOCOL_VERSION - 1 ) );  // 1.0 servers, some are still there
			lastMasterScanTime = cls.realtime;
      // Don't delete our manually added servers
      cls.numglobalservers = whitelistedServerCount;
		}
    for ( int i = 0; i < whitelistedServerCount; i++ ) {
      cls.globalServers[i].ping = -1;
    }
		CL_UpdateVisiblePings_f( AS_GLOBAL );
		FindPopulatedServers();

		NET_Sleep( 10000 );
	}
	system( "PAUSE" );
	return 0;
}
