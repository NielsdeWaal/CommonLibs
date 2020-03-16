#ifndef UTIL_H
#define UTIL_H

#if COMMONLIBS_TRACE

#define TRACY_ENABLE 1
#include "Tracy.hpp"
#include "TracyClient.cpp"

#define PROFILING_ZONE() ZoneScoped
#define PROFILING_ZONE_NAMED(staticName) ZoneScopedN(staticName)
#define PROFILING_FRAME() FrameMark

#else

#define PROFILING_ZONE()
#define PROFILING_FRAME()
#define PROFILING_ZONE_NAMED(staticName)

#endif

#endif // UTIL_H
