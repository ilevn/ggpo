#include "ggponet.h"
#include "types.h"
#include "poll.h"
#include "game_input.h"
#include "bitvector.h"
#include "network/udp.h"
#include <stdint.h>
#include <signal.h>
#include <sys/time.h>
#include <dirent.h>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static void packet(UdpMsg &msg, const char *hex) {
    std::string actual;
    const unsigned char *bytes = reinterpret_cast<const unsigned char*>(&msg);
    for (int i = 0; i < msg.PacketSize(); ++i) { char b[3]; snprintf(b, sizeof(b), "%02x", bytes[i]); actual += b; }
    if (actual != hex) fprintf(stderr, "packet %d: %s != %s\n", msg.hdr.type, actual.c_str(), hex);
    CHECK(actual == hex);
    printf("packet %d %s\n", msg.hdr.type, actual.c_str());
}
static void init(UdpMsg &msg, UdpMsg::MsgType kind) {
    memset(&msg, 0, sizeof(msg)); msg.hdr.magic = 0x1234; msg.hdr.sequence_number = 0x5678; msg.hdr.type = kind;
}
static void packets() {
    CHECK(sizeof(unsigned int) == 4); CHECK(sizeof(UdpMsg::connect_status) == 4);
    UdpMsg m(UdpMsg::Invalid);
    CHECK(sizeof(m.hdr) == 5);
    CHECK(reinterpret_cast<char*>(&m.u) - reinterpret_cast<char*>(&m) == 5);
    CHECK(reinterpret_cast<char*>(m.u.input.bits) - reinterpret_cast<char*>(&m) == 32);
    init(m, UdpMsg::SyncRequest); m.u.sync_request.random_request = 0x11223344; m.u.sync_request.remote_magic = 0x99aa; m.u.sync_request.remote_endpoint = 2;
    packet(m, "341278560144332211aa9902");
    init(m, UdpMsg::SyncReply); m.u.sync_reply.random_reply = 0x11223344;
    packet(m, "341278560244332211");
    init(m, UdpMsg::Input);
    m.u.input.peer_connect_status[0].disconnected = 1; m.u.input.peer_connect_status[0].last_frame = -1;
    m.u.input.peer_connect_status[1].last_frame = 7;
    m.u.input.start_frame = 0x11223344; m.u.input.disconnect_requested = -1; m.u.input.ack_frame = -1;
    m.u.input.num_bits = 3; m.u.input.input_size = 56; m.u.input.bits[0] = 5;
    packet(m, "3412785603ffffffff0e000000000000000000000044332211ffffffff03003805");
    init(m, UdpMsg::QualityReport); m.u.quality_report.frame_advantage = -2; m.u.quality_report.ping = 0x11223344;
    packet(m, "3412785604fe44332211");
    init(m, UdpMsg::QualityReply); m.u.quality_reply.pong = 0x11223344;
    packet(m, "341278560544332211");
    init(m, UdpMsg::KeepAlive); packet(m, "3412785606");
    init(m, UdpMsg::InputAck); m.u.input_ack.ack_frame = -1; packet(m, "3412785607ffffff7f");
    // Parsing a fixture back into the packed fields covers signed sentinels too.
    const unsigned char status[] = {0xff, 0xff, 0xff, 0xff};
    UdpMsg::connect_status parsed; memcpy(&parsed, status, sizeof(parsed));
    CHECK(parsed.disconnected == 1 && parsed.last_frame == -1);
    uint8 bits[8] = {}; int write = 0, read = 0;
    BitVector_WriteNibblet(bits, 895, &write);
    CHECK(write == 10 && BitVector_ReadNibblet(bits, &read) == 895 && read == 10);
}
struct Counter : IPollSink {
    int count; Counter() : count(0) {}
    bool OnLoopPoll(void*) { ++count; return true; }
    bool OnPeriodicPoll(void*, int) { ++count; return true; }
};
static volatile sig_atomic_t signals;
static void alarm_handler(int) { ++signals; }
static void platform() {
    unsetenv("ggpo.test"); CHECK(Platform::GetConfigInt("ggpo.test") == 0); CHECK(!Platform::GetConfigBool("ggpo.test"));
    setenv("ggpo.test", "TrUe", 1); CHECK(Platform::GetConfigBool("ggpo.test"));
    setenv("ggpo.test", "-7", 1); CHECK(Platform::GetConfigInt("ggpo.test") == -7); CHECK(Platform::GetConfigBool("ggpo.test"));
    setenv("ggpo.test", "0", 1); CHECK(!Platform::GetConfigBool("ggpo.test")); unsetenv("ggpo.test");
    Counter c; Poll p; p.RegisterLoop(&c);
    unsigned int start = Platform::GetCurrentTimeMS();
    for (int i = 0; i < 100; ++i) p.Pump(0);
    CHECK(c.count == 100 && Platform::GetCurrentTimeMS() - start < 500);
    struct sigaction action = {}; action.sa_handler = alarm_handler; sigemptyset(&action.sa_mask); sigaction(SIGALRM, &action, NULL);
    struct itimerval timer = {}; timer.it_value.tv_usec = timer.it_interval.tv_usec = 1000;
    setitimer(ITIMER_REAL, &timer, NULL);
    start = Platform::GetCurrentTimeMS(); p.Pump(30);
    CHECK(Platform::GetCurrentTimeMS() - start >= 25 && Platform::GetCurrentTimeMS() - start < 500);
    start = Platform::GetCurrentTimeMS(); Platform::SleepMS(30);
    CHECK(Platform::GetCurrentTimeMS() - start >= 25 && signals > 0);
    memset(&timer, 0, sizeof(timer)); setitimer(ITIMER_REAL, &timer, NULL);
    Counter periodic; Poll timed; timed.RegisterPeriodic(&periodic, 10); timed.Pump(100);
    CHECK(periodic.count == 1);
    // Maximum-sized descriptions must truncate safely even with one byte left.
    char raw[112]; memset(raw, 0xff, sizeof(raw)); GameInput in; in.init(0, raw, sizeof(raw));
    char small[4] = {'x','x','x','!'}; in.desc(small, 3); CHECK(small[2] == 0 && small[3] == '!');
    in.desc(small, 0); in.desc(small, 1); CHECK(small[0] == 0);
    setenv("ggpo.log", "true", 1); setenv("ggpo.log.timestamps", "true", 1);
    Log("format %s %d\n", "test", 1234); LogFlush(); unsetenv("ggpo.log"); unsetenv("ggpo.log.timestamps");
}
static bool GGPO_CALL begin(const char*) { return true; }
static void GGPO_CALL release(void *p) { free(p); }
static int fd_count() {
    DIR *dir = opendir("/proc/self/fd"); CHECK(dir); int n = 0;
    while (readdir(dir)) ++n; closedir(dir); return n;
}
static void sockets() {
    int occupied = socket(AF_INET, SOCK_DGRAM, 0); CHECK(occupied >= 0);
    sockaddr_in addr = {}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(occupied, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    socklen_t len = sizeof(addr); CHECK(getsockname(occupied, reinterpret_cast<sockaddr*>(&addr), &len) == 0);
    const unsigned short port = ntohs(addr.sin_port);
    GGPOSessionCallbacks cb = {}; cb.begin_game = begin; cb.free_buffer = release;
    int before = fd_count();
    for (int i = 0; i < 30; ++i) {
        GGPOSession *s = NULL;
        CHECK(ggpo_start_session(&s, &cb, "test", 2, 56, port) == GGPO_ERRORCODE_GENERAL_FAILURE && s == NULL);
        char host[] = "127.0.0.1";
        CHECK(ggpo_start_spectating(&s, &cb, "test", 2, 56, port, host, port) == GGPO_ERRORCODE_GENERAL_FAILURE && s == NULL);
    }
    CHECK(fd_count() == before); close(occupied);
    GGPOSession *s = NULL;
    CHECK(ggpo_start_session(&s, &cb, "test", 2, 56, port) == GGPO_OK);
    unsigned int start = Platform::GetCurrentTimeMS();
    for (int i = 0; i < 100; ++i) CHECK(ggpo_idle(s, 0) == GGPO_OK);
    CHECK(Platform::GetCurrentTimeMS() - start < 500);
    CHECK(ggpo_close_session(s) == GGPO_OK);
    CHECK(fd_count() == before - 1);
}
int main() { packets(); platform(); sockets(); puts("platform and packet checks passed"); }
