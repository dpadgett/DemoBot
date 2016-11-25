#include "qcommon/qcommon.h"
#include "game/bg_public.h"

void CL_ConfigstringModified( void );
void CL_Connect_f( void );
void Cmd_Chat_f( void );
void CL_CheckForResend( void );
const char *getPlayerName( int playerIdx );
team_t getPlayerTeam( int playerIdx );
const char* getPlayerTeamName( int playerIdx );
void StripColor( char *text );
void CL_StopRecord_f( void );
void CL_Record_f( void );
extern qboolean indexDemo; // set to true to index demo as it is saved
extern void( *indexFinished )( void ); // callback called when indexer completes