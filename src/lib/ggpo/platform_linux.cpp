/* -----------------------------------------------------------------------
 * GGPO.net (http://ggpo.net)  -  Copyright 2009 GroundStorm Studios, LLC.
 *
 * Use of this software is governed by the MIT license that can be found
 * in the LICENSE file.
 */

#include "platform_linux.h"
#include <time.h>
#include <errno.h>
#include <strings.h>
#include <stdint.h>

unsigned int Platform::GetCurrentTimeMS()
{
   struct timespec now;
   if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
      perror("clock_gettime");
      abort();
   }
   return static_cast<unsigned int>(static_cast<uint64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000);
}

int Platform::GetConfigInt(const char *name)
{
   const char *value = getenv(name);
   return value ? atoi(value) : 0;
}

bool Platform::GetConfigBool(const char *name)
{
   const char *value = getenv(name);
   return value && (atoi(value) != 0 || strcasecmp(value, "true") == 0);
}

void Platform::SleepMS(unsigned int milliseconds)
{
   struct timespec remaining = { milliseconds / 1000, (milliseconds % 1000) * 1000000L };
   while (nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {}
}
