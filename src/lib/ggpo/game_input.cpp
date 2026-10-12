/* -----------------------------------------------------------------------
 * GGPO.net (http://ggpo.net)  -  Copyright 2009 GroundStorm Studios, LLC.
 *
 * Use of this software is governed by the MIT license that can be found
 * in the LICENSE file.
 */

#include "types.h"
#include "game_input.h"
#include "log.h"

void
GameInput::init(int iframe, char *ibits, int isize, int offset)
{
   ASSERT(isize);
   ASSERT(isize <= GAMEINPUT_MAX_BYTES);
   frame = iframe;
   size = isize;
   memset(bits, 0, sizeof(bits));
   if (ibits) {
      memcpy(bits + (offset * isize), ibits, isize);
   }
}

void
GameInput::init(int iframe, char *ibits, int isize)
{
   ASSERT(isize);
   ASSERT(isize <= GAMEINPUT_MAX_BYTES * GAMEINPUT_MAX_PLAYERS);
   frame = iframe;
   size = isize;
   memset(bits, 0, sizeof(bits));
   if (ibits) {
      memcpy(bits, ibits, isize);
   }
}

void
GameInput::desc(char *buf, size_t buf_size, bool show_frame) const
{
   ASSERT(size);
   if (!buf_size) return;
   int written = show_frame ? snprintf(buf, buf_size, "(frame:%d size:%d ", frame, size)
                            : snprintf(buf, buf_size, "(size:%d ", size);
   if (written < 0) { buf[0] = 0; return; }
   size_t used = MIN(static_cast<size_t>(written), buf_size - 1);
   for (int i = 0; i < size * 8 && used < buf_size - 1; i++) {
      if (value(i)) {
         written = snprintf(buf + used, buf_size - used, "%2d ", i);
         if (written < 0) break;
         used += MIN(static_cast<size_t>(written), buf_size - used - 1);
      }
   }
   if (used < buf_size - 1) snprintf(buf + used, buf_size - used, ")");
}

void
GameInput::log(char *prefix, bool show_frame) const
{
   char buf[1024];
   desc(buf, sizeof(buf), show_frame);
   Log("%s%s\n", prefix, buf);
}


bool
GameInput::equal(GameInput &other, bool bitsonly)
{
   if (!bitsonly && frame != other.frame) {
      Log("frames don't match: %d, %d\n", frame, other.frame);
   }
   if (size != other.size) {
      Log("sizes don't match: %d, %d\n", size, other.size);
   }
   if (memcmp(bits, other.bits, size)) {
      Log("bits don't match\n");
   }
   ASSERT(size && other.size);
   return (bitsonly || frame == other.frame) &&
          size == other.size &&
          memcmp(bits, other.bits, size) == 0;
}

