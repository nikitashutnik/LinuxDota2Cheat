#pragma once

#include <cstdio>
#include <cstdarg>

#include "../SDK/ConMsg.h"

namespace Logger
{
	inline const char* path = "/tmp/d2h_debug.log";

	inline void Log( const char* fmt, ... )
	{
		static FILE* f = fopen( path, "a" );
		if( !f )
			return;
		va_list ap;
		va_start( ap, fmt );
		vfprintf( f, fmt, ap );
		va_end( ap );
		fputc( '\n', f );
		fflush( f );
	}
}

// Dota console + logfile
#define MC_PRINTF(f_, ...)       do { ConColorMsg( Color( 255, 140, 5 ), "[D2H] " f_, ##__VA_ARGS__ ); Logger::Log( "[D2H] " f_, ##__VA_ARGS__ ); } while( 0 )
#define MC_PRINTF_ERROR(f_, ...) do { ConColorMsg( Color( 255, 20, 140 ), "[D2H:%s] " f_, __FUNCTION__, ##__VA_ARGS__ ); Logger::Log( "[D2H:%s] " f_, __FUNCTION__, ##__VA_ARGS__ ); } while( 0 )
#define MC_PRINTF_WARN(f_, ...)  do { ConColorMsg( Color( 255, 223, 0 ), "[D2H:%s] " f_, __FUNCTION__, ##__VA_ARGS__ ); Logger::Log( "[D2H:%s] " f_, __FUNCTION__, ##__VA_ARGS__ ); } while( 0 )
