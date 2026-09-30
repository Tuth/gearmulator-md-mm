#pragma once

#include <sstream>
#include <string>
#include <iomanip>

// Release purity (STATE v42, Thor's rule 2026-09-30): a release plugin must
// contain no debug logging, no tracing and no probes. GEARMULATOR_DIAGNOSTIC_LOGGING
// is defined by the root CMakeLists from the gearmulator_DIAGNOSTICS option
// (default OFF). The disabled LOG deliberately does not evaluate its operands,
// so stream expressions cost nothing and no std::stringstream is constructed.
#ifndef GEARMULATOR_DIAGNOSTIC_LOGGING
#define GEARMULATOR_DIAGNOSTIC_LOGGING 0
#endif

namespace baseLib::logging
{
	typedef void (*LogFunc)(const std::string&);

	void logToConsole( const std::string& _s );
	void logToFile( const std::string& _s );
	void setLogFunc(LogFunc _func);
}

#define LOGTOCONSOLE(ss)	{ baseLib::logging::logToConsole( (ss).str() ); }
#define LOGTOFILE(ss)		{ baseLib::logging::logToFile( (ss).str() ); }

#if GEARMULATOR_DIAGNOSTIC_LOGGING

#define LOG(S)																						\
do																										\
{																										\
	std::stringstream __ss__logging_h;	__ss__logging_h << __func__ << "@" << __LINE__ << ": " << S;					\
																										\
	LOGTOCONSOLE(__ss__logging_h)																					\
}																										\
while(false)

#else

#define LOG(S)	do {} while(false)

#endif

#define LOGF(S)																						\
do																										\
{																										\
	std::stringstream __ss__logging_h;	__ss__logging_h << S;													\
																										\
	LOGTOFILE(__ss__logging_h)																							\
}																										\
while (false)

#define HEX(S)		std::hex << std::setfill('0') << std::setw(8) << S
#define HEXN(S, n)	std::hex << std::setfill('0') << std::setw(n) << (uint32_t)S
