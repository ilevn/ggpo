/* -----------------------------------------------------------------------
 * GGPO.net (http://ggpo.net)  -  Copyright 2009 GroundStorm Studios, LLC.
 *
 * Use of this software is governed by the MIT license that can be found
 * in the LICENSE file.
 */

/* POSIX/Windows UDP transport. Packet encoding remains in udp_proto. */
#include "types.h"
#include "udp.h"
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#endif

namespace {
#ifdef _WIN32
const ggpo_socket invalid_socket = INVALID_SOCKET;
int socket_error() { return WSAGetLastError(); }
bool would_block(int e) { return e == WSAEWOULDBLOCK; }
bool interrupted(int e) { return e == WSAEINTR; }
void close_socket(ggpo_socket s) { closesocket(s); }
#else
const ggpo_socket invalid_socket = -1;
int socket_error() { return errno; }
bool would_block(int e) { return e == EAGAIN || e == EWOULDBLOCK; }
bool interrupted(int e) { return e == EINTR; }
void close_socket(ggpo_socket s) { close(s); }
#endif

ggpo_socket CreateSocket(uint16 port)
{
   ggpo_socket s = socket(AF_INET, SOCK_DGRAM, 0);
   if (s == invalid_socket) return s;
#ifdef _WIN32
   int optval = 1;
   // Preserve the original Windows socket options.
   setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));
   setsockopt(s, SOL_SOCKET, SO_DONTLINGER, (const char*)&optval, sizeof(optval));
   u_long mode = 1;
   bool ready = ioctlsocket(s, FIONBIO, &mode) == 0;
#else
   // Do not allow a second Linux session to silently steal the same UDP port.
   int flags = fcntl(s, F_GETFL, 0);
   bool ready = flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
   sockaddr_in address;
   memset(&address, 0, sizeof(address));
   address.sin_family = AF_INET;
   address.sin_addr.s_addr = htonl(INADDR_ANY);
   address.sin_port = htons(port);
   if (!ready || bind(s, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
      int error = socket_error();
      close_socket(s);
      Log("UDP setup failed on port %d (error %d).\n", port, error);
      return invalid_socket;
   }
   return s;
}
}

Udp::Udp() : _socket(invalid_socket), _callbacks(NULL), _poll(NULL) {}
Udp::~Udp() { if (IsValid()) close_socket(_socket); }
bool Udp::IsValid() const { return _socket != invalid_socket; }

void Udp::Init(uint16 port, Poll *poll, Callbacks *callbacks)
{
   _callbacks = callbacks;
   _poll = poll;
   _socket = CreateSocket(port);
   if (IsValid()) _poll->RegisterLoop(this);
}

void Udp::SendTo(char *buffer, int len, int flags, struct sockaddr *dst, int destlen)
{
   if (!IsValid()) return;
   int result;
   do {
      result = static_cast<int>(sendto(_socket, buffer, len, flags, dst, destlen));
   } while (result < 0 && interrupted(socket_error()));
   if (result < 0) {
      int error = socket_error();
      // Transient congestion is equivalent to UDP loss: the protocol retries.
      if (!would_block(error)) Log("sendto failed (error %d).\n", error);
   }
}

bool Udp::OnLoopPoll(void *)
{
   if (!IsValid()) return true;
   uint8 buffer[MAX_UDP_PACKET_SIZE];
   sockaddr_in from;
   for (;;) {
#ifdef _WIN32
      int from_len = sizeof(from);
#else
      socklen_t from_len = sizeof(from);
#endif
      int len = static_cast<int>(recvfrom(_socket, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                                         reinterpret_cast<sockaddr*>(&from), &from_len));
      if (len < 0) {
         int error = socket_error();
         if (interrupted(error)) continue;
         if (!would_block(error)) Log("recvfrom failed (error %d).\n", error);
         break;
      }
      if (len > 0) _callbacks->OnMsg(from, reinterpret_cast<UdpMsg*>(buffer), len);
   }
   return true;
}

void Udp::Log(const char *fmt, ...)
{
   char buffer[1024];
   va_list args;
   va_start(args, fmt);
   vsnprintf(buffer, sizeof(buffer), fmt, args);
   va_end(args);
   ::Log("udp | %s", buffer);
}
