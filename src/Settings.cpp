#include "Settings.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "Utils/Logger.h"

static std::string ConfigPath()
{
	const char* home = getenv( "HOME" );
	return std::string( home ? home : "/tmp" ) + "/.config/dota2helper.cfg";
}

void Settings::Load()
{
	FILE* f = fopen( ConfigPath().c_str(), "r" );
	if( !f )
		return;
	char key[64];
	float value;
	while( fscanf( f, "%63[a-z_]=%f\n", key, &value ) == 2 ){
		if( !strcmp( key, "autoaccept" ) )     autoAccept = value != 0.0f;
		else if( !strcmp( key, "camera_distance" ) ) camDistance = value;
		else if( !strcmp( key, "lastseen" ) )  lastSeen = value != 0.0f;
		else if( !strcmp( key, "lastseen_secs" ) ) lastSeenSecs = (int)value;
	}
	fclose( f );
	Logger::Log( "settings loaded: accept=%d dist=%g lastseen=%d secs=%d", autoAccept, camDistance, lastSeen, lastSeenSecs );
}

void Settings::Save()
{
	FILE* f = fopen( ConfigPath().c_str(), "w" );
	if( !f ){
		Logger::Log( "can't write %s", ConfigPath().c_str() );
		return;
	}
	fprintf( f, "autoaccept=%d\ncamera_distance=%g\nlastseen=%d\nlastseen_secs=%d\n",
	         autoAccept, camDistance, lastSeen, lastSeenSecs );
	fclose( f );
}
