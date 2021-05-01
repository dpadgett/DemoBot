#include "qcommon/qcommon.h"
#include "game/bg_public.h"
#include "client/client.h"

void CL_ConfigstringModified( void );
void CL_Connect_f( void );
void Cmd_Chat_f( void );
void CL_CheckForResend( void );
const char *getPlayerName( int playerIdx );
team_t getPlayerTeam( int playerIdx );
const char* getPlayerTeamName( int playerIdx );
void StripColor( char *text );
void SetColor( char* text, char desiredColor );
void CL_StopRecord_f( void );
void CL_Record_f( void );
void CL_InitServerInfo( serverInfo_t *server, netadr_t *address );
extern qboolean indexDemo; // set to true to index demo as it is saved
extern void( *indexFinished )( void ); // callback called when indexer completes
extern const char *demoFolder; // folder to save demos to

extern bool( *demoStart )( const char *name ); // called to start a new demo
extern bool( *demoWrite )( const char *buf, int buflen ); // called to add bytes to the demo file
extern bool( *demoStop )( void ); // called to flush the current demo and close
