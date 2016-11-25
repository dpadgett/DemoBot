#include <cwchar>   // wchar_t wide characters
#include "client/client.h"
#if (defined _MSC_VER)
#include <Windows.h>

// Convert a wide Unicode string to an UTF8 string
static const char *utf8_encode( const wchar_t *wstr, int wstrSize )
{
	int size_needed = WideCharToMultiByte( CP_UTF8, 0, wstr, wstrSize, NULL, 0, NULL, NULL );
	char *strTo = (char *) malloc( size_needed + 1 );
	WideCharToMultiByte( CP_UTF8, 0, &wstr[0], wstrSize, strTo, size_needed, NULL, NULL );
	strTo[size_needed] = '\0';
	return strTo;
}

const wchar_t *utf8BytesToString( const char *utf8 ) {
	int size_needed = MultiByteToWideChar( CP_UTF8, 0, utf8, -1, 0, 0 );
	wchar_t *strTo = (wchar_t *) malloc( size_needed * sizeof( wchar_t ) );
	MultiByteToWideChar( CP_UTF8, 0, utf8, -1, strTo, size_needed );
	return strTo;
}

const char *cp1252toUTF8( const char *cp1252 )
{
	int size_needed = MultiByteToWideChar( CP_ACP, 0, cp1252, -1, 0, 0 );
	wchar_t *strTo = (wchar_t *) malloc( size_needed * sizeof( wchar_t ) );
	MultiByteToWideChar( CP_ACP, 0, cp1252, -1, strTo, size_needed );
	const char *result = utf8_encode( strTo, size_needed );
	free( strTo );
	return result;
}

const char *UTF8toCP1252( const char *utf8 )
{
	const wchar_t *from = utf8BytesToString( utf8 );
	int size_needed = WideCharToMultiByte( CP_ACP, 0, from, -1, NULL, 0, NULL, NULL );
	char *strTo = (char *) malloc( size_needed * sizeof( char ) );
	WideCharToMultiByte( CP_ACP, 0, &from[0], -1, strTo, size_needed, NULL, NULL );
	free( (void *)from );
	return strTo;
}
#else
#include <locale.h>

// Convert a wide Unicode string to an UTF8 string
static const char *utf8_encode( const wchar_t *wstr, int wstrSize )
{
	setlocale( LC_CTYPE, "en_US.UTF-8" );
	mbstate_t ps;
	memset( &ps, 0, sizeof( ps ) );
	size_t size_needed = wcsrtombs( NULL, &wstr, 0, &ps );
	if ( size_needed == (size_t) -1 ) {
		printf( "failure at: %S\n", wstr );
		perror( "utf8 conversion failed" );
		return NULL;
	}
	char *strTo = (char *) malloc( size_needed + 1 );
	memset( &ps, 0, sizeof( ps ) );
	wcsrtombs( strTo, &wstr, size_needed, &ps );
	strTo[size_needed] = '\0';
	return strTo;
}

const wchar_t *utf8BytesToString( const char *utf8 ) {
	setlocale( LC_CTYPE, "en_US.UTF-8" );
	mbstate_t ps;
	memset( &ps, 0, sizeof( ps ) );
	size_t size_needed = mbsrtowcs( NULL, &utf8, 0, &ps );
	if ( size_needed == (size_t) -1 ) {
		perror( "utf8 conversion failed" );
		return NULL;
	}
	wchar_t *strTo = (wchar_t *) malloc( size_needed * sizeof( wchar_t ) );
	mbsrtowcs( strTo, &utf8, size_needed, &ps );
	return strTo;
}

const char *cp1252toUTF8( const char *cp1252 )
{
	setlocale( LC_CTYPE, "en_US.CP1252" );
	mbstate_t ps;
	memset( &ps, 0, sizeof( ps ) );
	size_t size_needed = mbsrtowcs( NULL, &cp1252, 0, &ps );
	if ( size_needed == (size_t) -1 ) {
		printf( "failure at: %s\n", cp1252 );
		perror( "cp1252 conversion failed" );
		return NULL;
	}
	wchar_t *strTo = (wchar_t *) malloc( ( size_needed + 1 ) * sizeof( wchar_t ) );
	memset( &ps, 0, sizeof( ps ) );
	mbsrtowcs( strTo, &cp1252, size_needed, &ps );
	strTo[size_needed] = '\0';
	const char *result = utf8_encode( strTo, size_needed );
	free( strTo );
	return result;
}
#endif
