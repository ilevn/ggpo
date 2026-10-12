#include "ggponet.h"
#include <stdint.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#include <vector>
#include <string>

struct State { int frame; uint32_t hash; };
static State state = {0, 2166136261u};
static GGPOSession *session;
static bool running, disconnected, interrupted, resumed;
static int loads, replays, stalls;
static std::vector<uint32_t> history(1, state.hash);
static const int input_size = 56;
static int target = 600;

static void require(bool value, const char *message) {
    if (!value) { std::fprintf(stderr, "%s (frame %d)\n", message, state.frame); std::exit(2); }
}
static void input(int player, int frame, unsigned char *bytes) {
    std::memset(bytes, 0, input_size);
    // Sparse changes exercise all 56 bytes; packet_capacity covers dense backlogs.
    bytes[(frame / 4 + player * 17) % input_size] = static_cast<unsigned char>((frame + 3 * player) % 255 + 1);
}
static uint32_t step_hash(uint32_t hash, int frame, const unsigned char *bytes) {
    hash = (hash ^ static_cast<uint32_t>(frame)) * 16777619u;
    for (int i = 0; i < input_size * 2; ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}
static bool GGPO_CALL begin(const char *) { return true; }
static bool GGPO_CALL save(unsigned char **data, int *len, int *checksum, int frame) {
    require(frame == state.frame, "save frame mismatch");
    *len = sizeof(state);
    *data = static_cast<unsigned char*>(std::malloc(*len));
    require(*data != NULL, "allocation failed");
    std::memcpy(*data, &state, *len);
    *checksum = static_cast<int>(state.hash);
    return true;
}
static bool GGPO_CALL load(unsigned char *data, int len) {
    require(len == sizeof(state), "snapshot size mismatch");
    std::memcpy(&state, data, len);
    history.resize(state.frame + 1);
    ++loads;
    return true;
}
static bool GGPO_CALL log_state(char *name, unsigned char *data, int len) {
    FILE *out = std::fopen(name, "wb");
    if (!out) return false;
    std::fwrite(data, 1, len, out); std::fclose(out); return true;
}
static void GGPO_CALL release(void *data) { std::free(data); }
static bool GGPO_CALL event(GGPOEvent *e) {
    if (e->code == GGPO_EVENTCODE_RUNNING) running = true;
    if (e->code == GGPO_EVENTCODE_CONNECTION_INTERRUPTED) interrupted = true;
    if (e->code == GGPO_EVENTCODE_CONNECTION_RESUMED) resumed = true;
    if (e->code == GGPO_EVENTCODE_DISCONNECTED_FROM_PEER) disconnected = true;
    return true;
}
static bool simulate() {
    unsigned char inputs[input_size * 2]; int flags = 0;
    if (ggpo_synchronize_input(session, inputs, sizeof(inputs), &flags) != GGPO_OK) return false;
    state.hash = step_hash(state.hash, state.frame, inputs);
    ++state.frame;
    history.resize(state.frame + 1);
    history[state.frame] = state.hash;
    require(ggpo_advance_frame(session) == GGPO_OK, "advance failed");
    return true;
}
static bool GGPO_CALL advance(int) { ++replays; require(simulate(), "replay failed"); return true; }

int main(int argc, char **argv) {
    require(argc == 6, "usage: ggpo_peer MODE ID LOCAL_PORT REMOTE_PORT SPECTATOR_PORT");
    const std::string mode = argv[1];
    int id = std::atoi(argv[2]), port = std::atoi(argv[3]), remote = std::atoi(argv[4]);
    int spectator_port = std::atoi(argv[5]);
    bool spectator = mode == "spectator", synctest = mode == "synctest";
    bool disconnect_test = mode == "disconnect";
    GGPOSessionCallbacks cb = {begin, save, load, log_state, release, advance, event};
    char game[] = "ggpo-linux-test", host[] = "127.0.0.1";
    GGPOErrorCode err;
    if (synctest) err = ggpo_start_synctest(&session, &cb, game, 2, input_size, 3);
    else if (spectator) err = ggpo_start_spectating(&session, &cb, game, 2, input_size, port, host, remote);
    else err = ggpo_start_session(&session, &cb, game, 2, input_size, port);
    require(err == GGPO_OK && session, "session creation failed");
    GGPOPlayerHandle local = GGPO_INVALID_HANDLE, second = GGPO_INVALID_HANDLE;
    if (!spectator) {
        for (int player = 0; player < 2; ++player) {
            GGPOPlayer desc = {}; desc.size = sizeof(desc); desc.player_num = player + 1;
            desc.type = (synctest || player == id) ? GGPO_PLAYERTYPE_LOCAL : GGPO_PLAYERTYPE_REMOTE;
            if (desc.type == GGPO_PLAYERTYPE_REMOTE) {
                std::strcpy(desc.u.remote.ip_address, host); desc.u.remote.port = remote;
            }
            GGPOPlayerHandle handle;
            require(ggpo_add_player(session, &desc, &handle) == GGPO_OK, "add player failed");
            if (player == id) local = handle; else second = handle;
        }
        if (spectator_port && id == 0) {
            GGPOPlayer desc = {}; desc.size = sizeof(desc); desc.type = GGPO_PLAYERTYPE_SPECTATOR;
            std::strcpy(desc.u.remote.ip_address, host); desc.u.remote.port = spectator_port;
            GGPOPlayerHandle handle;
            require(ggpo_add_player(session, &desc, &handle) == GGPO_OK, "add spectator failed");
        }
        if (!synctest) {
            ggpo_set_disconnect_notify_start(session, 300);
            ggpo_set_disconnect_timeout(session, disconnect_test ? 900 : 10000);
        }
    }
    typedef std::chrono::steady_clock Clock;
    // Spectators have no prediction backpressure: model the real 60 Hz game
    // while an independently started spectator completes its retry handshake.
    // Packet-capacity/backlog stress is deterministic in packet_capacity.
    const auto frame_interval = std::chrono::milliseconds((spectator || spectator_port) ? 17 : 2);
    const auto start = Clock::now();
    auto next = start;
    // Close synctest between verification batches to exercise pending snapshot cleanup.
    const int finish = synctest ? target + 1 : (spectator ? target : target + 30);
    while (state.frame < finish && !disconnected) {
        require(Clock::now() - start < std::chrono::seconds(35), "peer timed out");
        require(ggpo_idle(session, 0) == GGPO_OK, "idle failed");
        if (running && Clock::now() >= next) {
            if (spectator) { if (simulate()) next = Clock::now() + frame_interval; }
            else {
                unsigned char bytes[input_size]; input(id, state.frame, bytes);
                err = ggpo_add_local_input(session, local, bytes, sizeof(bytes));
                if (err == GGPO_ERRORCODE_PREDICTION_THRESHOLD) ++stalls;
                else {
                    require(err == GGPO_OK, "add input failed");
                    if (synctest) {
                        input(1 - id, state.frame, bytes);
                        require(ggpo_add_local_input(session, second, bytes, sizeof(bytes)) == GGPO_OK, "second input failed");
                    }
                    require(simulate(), "simulate failed");
                    next = Clock::now() + frame_interval;
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (disconnect_test) {
        require(interrupted && disconnected, "disconnect events missing");
    } else {
        require(!disconnected, "unexpected disconnect");
        // Continue servicing peers and the spectator while final acknowledgements arrive.
        const auto end = Clock::now() + std::chrono::milliseconds(800);
        while (!synctest && Clock::now() < end) {
            ggpo_idle(session, 0); std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        uint32_t expected = 2166136261u;
        for (int frame = 0; frame < target; ++frame) {
            unsigned char inputs[input_size * 2]; input(0, frame, inputs); input(1, frame, inputs + input_size);
            expected = step_hash(expected, frame, inputs);
            require(history.at(frame + 1) == expected, "state differs from reference simulation");
        }
        if (synctest) require(loads && replays, "synctest did not replay");
    }
    require(ggpo_close_session(session) == GGPO_OK, "close failed");
    std::printf("RESULT frame=%d hash=%u loads=%d replays=%d stalls=%d interrupted=%d resumed=%d disconnected=%d\n",
                state.frame, history.at(state.frame > target ? target : state.frame), loads, replays, stalls, interrupted, resumed, disconnected);
    return 0;
}
