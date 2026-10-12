/* -----------------------------------------------------------------------
 * GGPO.net (http://ggpo.net)  -  Copyright 2009 GroundStorm Studios, LLC.
 *
 * Use of this software is governed by the MIT license that can be found
 * in the LICENSE file.
 */

#ifndef _GGPO_LINUX_H_
#define _GGPO_LINUX_H_

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

class Platform {
public:
   typedef pid_t ProcessID;
   static ProcessID GetProcessID() { return getpid(); }
   static void AssertFailed(const char *msg) { fprintf(stderr, "%s\n", msg); }
   static unsigned int GetCurrentTimeMS();
   static int GetConfigInt(const char *name);
   static bool GetConfigBool(const char *name);
   static void SleepMS(unsigned int milliseconds);
};
#endif
