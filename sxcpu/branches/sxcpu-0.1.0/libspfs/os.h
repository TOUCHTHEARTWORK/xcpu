/* a very stupid switch to include os-specific stuff */
#ifdef SunOS
#include "SunOS.h"
#endif

#ifdef Linux
#include "Linux.h"
#endif

#ifdef FreeBSD
#include "FreeBSD.h"
#endif

#ifdef NetBSD
#include "NetBSD.h"
#endif
