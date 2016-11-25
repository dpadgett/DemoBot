#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>
#include <inttypes.h>

const wchar_t *utf8BytesToString( const char *utf8 );
const char *cp437toUTF8( const char *cp437 );
const char *cp1252toUTF8( const char *cp1252 );
const char *UTF8toCP1252( const char *utf8 );

#endif
