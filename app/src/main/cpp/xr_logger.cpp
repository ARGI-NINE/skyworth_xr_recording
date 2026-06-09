// 
// Auth:    zhgyee
// Date:    2019/12/19
// Mail:    zhgyee@gmail.com
// Disc:    This file a part of xreality

#include <android/log.h>
#include <cstddef>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <inttypes.h>
#include "xr_logger.h"

const int XR_LOG_DEBUG = ANDROID_LOG_DEBUG;
const int XR_LOG_ERROR = ANDROID_LOG_ERROR;
const int XR_LOG_WARN = ANDROID_LOG_WARN;
const int XR_LOG_FATAL = ANDROID_LOG_FATAL;
const int XR_LOG_INFO = ANDROID_LOG_INFO;

bool Gopenlog = false;

xr_logger::xr_logger(const char* name) : handle(0),  api_name(name)
{
	XR_DEBUG("==== enter %s, no inst/session ====", api_name);
}

xr_logger::xr_logger(int64_t h, const char* name) : handle(h), api_name(name)
{
    XR_DEBUG("==== enter %s, handle:%" PRIi64 " ====", api_name, handle);
}

xr_logger::~xr_logger() {
	XR_DEBUG("==== leave %s, handle:%" PRIi64 " ====", api_name, handle);
}

void xr_logger::log(const int prio, const char* fmt, ...)
{
	va_list ap;
	va_start( ap, fmt );
	__android_log_vprint( prio, this->api_name, fmt, ap );
	va_end( ap );
}
// Log with an explicit tag
void LogWithTag( const int prio, const char * tag, const char * fmt, ... )
{
	if(prio == XR_LOG_DEBUG && !Gopenlog) return;
    va_list ap;
	va_start( ap, fmt );
	__android_log_vprint( prio, tag, fmt, ap );
	va_end( ap );
}

void FilePathToTag( const char * filePath, char * strippedTag, size_t const strippedTagSize )
{
    // scan backwards from the end to the first slash
    const int len = static_cast< int >( strlen( filePath ) );
    int	slash;
    for ( slash = len - 1; slash > 0 && filePath[slash] != '/' && filePath[slash] != '\\'; slash-- )
    {
    }
    if ( filePath[slash] == '/' || filePath[slash] == '\\' )
    {
        slash++;
    }
    // copy forward until a dot or 0
    size_t i;
    for ( i = 0; i < strippedTagSize - 1; i++ )
    {
        const char c = filePath[slash+i];
        if ( c == '.' || c == 0 )
        {
            break;
        }
        strippedTag[i] = c;
    }
    strippedTag[i] = 0;
}

void LogWithFileTag( const int prio, const char * fileTag, const char * fmt, ... )
{
    va_list ap, ap2;

	// fileTag will be something like "jni/App.cpp", which we
	// want to strip down to just "App"
	char strippedTag[128];

	FilePathToTag( fileTag, strippedTag, sizeof( strippedTag ) );

	va_start( ap, fmt );

	// Calculate the length of the log message... if its too long __android_log_vprint() will clip it!
	va_copy( ap2, ap );
	const int loglen = vsnprintf( NULL, 0, fmt, ap2 );
	va_end( ap2 );

	if ( prio == ANDROID_LOG_FATAL )
	{
		// For FAIL messages which are longer than 512, truncate at 512.
		// We do not know the max size of abort message that will be taken by SIGABRT. 512 has been verified to work
		char *formattedMsg = ( char * )malloc( 512 );
		vsnprintf( formattedMsg, 512U, fmt, ap2 );
		__android_log_assert( "FAIL", strippedTag, "%s", formattedMsg );
		free( formattedMsg );
	}
	if ( loglen < 512 )
	{
		// For short messages just use android's default formatting path (which has a fixed size buffer on the stack).
		__android_log_vprint( prio, strippedTag, fmt, ap );
	}
	else
	{
		// For long messages allocate off the heap to avoid blowing the stack...
		char *formattedMsg = ( char * )malloc( loglen + 1 );
		vsnprintf( formattedMsg, ( size_t ) ( loglen + 1 ), fmt, ap2 );
		__android_log_write( prio, strippedTag, formattedMsg );
		free( formattedMsg );
	}

	va_end( ap );
}