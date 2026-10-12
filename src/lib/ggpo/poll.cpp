/* -----------------------------------------------------------------------
 * GGPO.net (http://ggpo.net)  -  Copyright 2009 GroundStorm Studios, LLC.
 *
 * Use of this software is governed by the MIT license that can be found
 * in the LICENSE file.
 */

#include "types.h"
#include "poll.h"
#ifndef _WIN32
#include <sys/poll.h>
#include <errno.h>
#endif

Poll::Poll(void) : _start_time(Platform::GetCurrentTimeMS())
{
#ifdef _WIN32
   _handle_count = 0;
   _handles[_handle_count++] = CreateEvent(NULL, true, false, NULL);
#endif
}

Poll::~Poll()
{
#ifdef _WIN32
   CloseHandle(_handles[0]); // Only the dummy event belongs to Poll.
#endif
}

#ifdef _WIN32
void Poll::RegisterHandle(IPollSink *sink, HANDLE h, void *cookie)
{
   ASSERT(_handle_count < MAX_POLLABLE_HANDLES - 1);
   _handles[_handle_count] = h;
   _handle_sinks[_handle_count] = PollSinkCb(sink, cookie);
   _handle_count++;
}
#endif

void
Poll::RegisterMsgLoop(IPollSink *sink, void *cookie)
{
   _msg_sinks.push_back(PollSinkCb(sink, cookie));
}

void
Poll::RegisterLoop(IPollSink *sink, void *cookie)
{
   _loop_sinks.push_back(PollSinkCb(sink, cookie));
}
void
Poll::RegisterPeriodic(IPollSink *sink, int interval, void *cookie)
{
   _periodic_sinks.push_back(PollPeriodicSinkCb(sink, cookie, interval));
}

void
Poll::Run()
{
   while (Pump(100)) {
      continue;
   }
}

bool
Poll::Pump(int timeout)
{
   int i;
#ifdef _WIN32
   DWORD res;
#endif
   bool finished = false;

   int elapsed = static_cast<int>(Platform::GetCurrentTimeMS() - _start_time);
   int maxwait = ComputeWaitTime(elapsed);
   if (maxwait >= 0 && (timeout < 0 || maxwait < timeout)) {
      timeout = maxwait;
   }
#ifdef _WIN32
   res = WaitForMultipleObjects(_handle_count, _handles, false, timeout < 0 ? INFINITE : timeout);
   if (res >= WAIT_OBJECT_0 && res < WAIT_OBJECT_0 + _handle_count) {
      i = res - WAIT_OBJECT_0;
      finished = !_handle_sinks[i].sink->OnHandlePoll(_handle_sinks[i].cookie) || finished;
   }
#else
   // Networking registers loop callbacks and uses Pump(0). No Windows-style
   // handles are registered by the SDK; nonzero waits only pace callbacks.
   const unsigned int started = Platform::GetCurrentTimeMS();
   int remaining = timeout;
   while (poll(NULL, 0, remaining) < 0 && errno == EINTR) {
      if (timeout >= 0) {
         unsigned int waited = Platform::GetCurrentTimeMS() - started;
         if (waited >= static_cast<unsigned int>(timeout)) break;
         remaining = timeout - static_cast<int>(waited);
      }
   }
#endif
   elapsed = static_cast<int>(Platform::GetCurrentTimeMS() - _start_time);
   for (i = 0; i < _msg_sinks.size(); i++) {
      PollSinkCb &cb = _msg_sinks[i];
      finished = !cb.sink->OnMsgPoll(cb.cookie) || finished;
   }

   for (i = 0; i < _periodic_sinks.size(); i++) {
      PollPeriodicSinkCb &cb = _periodic_sinks[i];
      if (cb.interval + cb.last_fired <= elapsed) {
         cb.last_fired = (elapsed / cb.interval) * cb.interval;
         finished = !cb.sink->OnPeriodicPoll(cb.cookie, cb.last_fired) || finished;
      }
   }

   for (i = 0; i < _loop_sinks.size(); i++) {
      PollSinkCb &cb = _loop_sinks[i];
      finished = !cb.sink->OnLoopPoll(cb.cookie) || finished;
   }
   return finished;
}

int
Poll::ComputeWaitTime(int elapsed)
{
   int waitTime = -1;
   size_t count = _periodic_sinks.size();

   if (count > 0) {
      for (int i = 0; i < count; i++) {
         PollPeriodicSinkCb &cb = _periodic_sinks[i];
         int timeout = (cb.interval + cb.last_fired) - elapsed;
         if (waitTime < 0 || (timeout < waitTime)) {
            waitTime = MAX(timeout, 0);
         }         
      }
   }
   return waitTime;
}
