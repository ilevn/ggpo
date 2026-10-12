#include "types.h"
#include "network/udp_proto.h"
#include "bitvector.h"
#include "backends/p2p.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

// Exercise the real encoder, decoder and acknowledgement handling with a
// deliberately backlogged queue. Holding the built-in send queue lets this
// test simulate loss/duplicates without timing or socket scheduling assumptions.
class Endpoint : public UdpProtocol {
    Udp transport;
public:
    Endpoint() { _send_latency = 60000; _oop_percent = 0; }
    void enqueue(const GameInput &input) { _pending_output.push(input); }
    UdpMsg packet() {
        CHECK(_send_queue.empty());
        SendPendingOutput();
        return take_packet();
    }
    UdpMsg take_packet() {
        CHECK(_send_queue.size() == 1);
        UdpMsg result = *_send_queue.front().msg;
        delete _send_queue.front().msg;
        _send_queue.pop();
        return result;
    }
    void receive(UdpMsg &msg) { CHECK(OnInput(&msg, msg.PacketSize())); }
    void acknowledge(int frame, bool standalone) {
        UdpMsg ack(standalone ? UdpMsg::InputAck : UdpMsg::Input);
        memset(&ack.u, 0, sizeof(ack.u));
        if (standalone) {
            ack.u.input_ack.ack_frame = frame;
            CHECK(OnInputAck(&ack, ack.PacketSize()));
        } else {
            ack.u.input.ack_frame = frame;
            CHECK(OnInput(&ack, ack.PacketSize()));
        }
    }
    void running() { _udp = &transport; _current_state = Running; }
    void expire() { _shutdown_timeout = 0; OnLoopPoll(NULL); }
    int pending() { return _pending_output.size(); }
    int sent_frame() { return _last_sent_input.frame; }
};

static void drain(Endpoint &receiver, const std::vector<GameInput> &inputs, int &next) {
    UdpProtocol::Event event;
    while (receiver.GetEvent(event)) {
        CHECK(event.type == UdpProtocol::Event::Input);
        CHECK(next < static_cast<int>(inputs.size()));
        CHECK(event.u.input.input.frame == next);
        CHECK(event.u.input.input.size == inputs[next].size);
        CHECK(memcmp(event.u.input.input.bits, inputs[next].bits, inputs[next].size) == 0);
        ++next;
    }
}

static void backlog(int size, bool exact_boundary, bool standalone_ack, bool unchanged = false) {
    Endpoint sender, receiver;
    std::vector<GameInput> inputs;
    char data[GAMEINPUT_MAX_BYTES * GAMEINPUT_MAX_PLAYERS] = {};
    const int frames = unchanged ? 63 : 40;
    for (int frame = 0; frame < frames; ++frame) {
        // Three maximally dense spectator deltas followed by 21 changes use
        // exactly 32512 bits, filling the existing 4096-byte receive buffer.
        int changes = exact_boundary && frame == 3 ? 21 : size * 8;
        if (unchanged) changes = 0;
        for (int i = 0; i < changes; ++i) data[i / 8] ^= 1 << (i % 8);
        GameInput input; input.init(frame, data, size);
        inputs.push_back(input); sender.enqueue(input);
    }
    int next = 0, packets = 0;
    while (next < frames) {
        UdpMsg dropped = sender.packet();
        UdpMsg retry = sender.packet();
        CHECK(dropped.PacketSize() == retry.PacketSize());
        // Unused padding bits in the final byte are not part of the encoding.
        const int final_bit_count = ((retry.u.input.num_bits - 1) % 8) + 1;
        const int final_byte = (retry.u.input.num_bits - 1) / 8;
        dropped.u.input.bits[final_byte] &= (1 << final_bit_count) - 1;
        retry.u.input.bits[final_byte] &= (1 << final_bit_count) - 1;
        CHECK(memcmp(&dropped.u, &retry.u, dropped.PayloadSize()) == 0);
        CHECK(retry.PacketSize() <= MAX_UDP_PACKET_SIZE);
        CHECK(retry.u.input.num_bits > 0);
        CHECK(retry.u.input.input_size == size);
        if (!packets && exact_boundary) CHECK(retry.PacketSize() == MAX_UDP_PACKET_SIZE);
        int previous = next;
        receiver.receive(retry); drain(receiver, inputs, next);
        CHECK(next > previous); // A retained acknowledged frame must not stall a batch.
        CHECK(sender.sent_frame() == next - 1);
        receiver.receive(retry); drain(receiver, inputs, next); // Lost ACK / duplicate packet.
        CHECK(sender.sent_frame() == next - 1);
        sender.acknowledge(next - 1, standalone_ack);
        CHECK(sender.pending() == frames - next + 1); // Preserve existing ACK semantics.
        ++packets;
        CHECK(packets <= frames);
    }
    CHECK(unchanged ? packets == 1 : packets > 1);
    printf("capacity size=%d boundary=%d standalone_ack=%d unchanged=%d frames=%d packets=%d\n",
           size, exact_boundary, standalone_ack, unchanged, next, packets);
}

static int disconnect_callbacks;
static bool GGPO_CALL begin_game(const char *) { return true; }
static void GGPO_CALL release_state(void *data) { free(data); }
static bool GGPO_CALL on_event(GGPOEvent *event) {
    CHECK(event->code == GGPO_EVENTCODE_DISCONNECTED_FROM_PEER);
    CHECK(event->u.disconnected.player == 1000);
    ++disconnect_callbacks;
    return true;
}
class Host : public Peer2PeerBackend {
public:
    Host(GGPOSessionCallbacks *cb) : Peer2PeerBackend(cb, "capacity", 0, 2, 56) { }
    void disconnected(UdpProtocol::Event &event) { OnUdpProtocolSpectatorEvent(event, 0); }
};
static void exhausted_queue() {
    Endpoint endpoint;
    endpoint.running();
    for (int frame = 0; frame < 63; ++frame) {
        GameInput input; input.init(frame, NULL, 112); endpoint.enqueue(input);
    }
    GameInput overflow; overflow.init(63, NULL, 112);
    endpoint.SendInput(overflow);
    CHECK(!endpoint.IsRunning() && endpoint.pending() == 63);
    UdpMsg first = endpoint.take_packet();
    CHECK(first.u.input.disconnect_requested);
    // A receiver that lost every prior packet must have room for the
    // disconnect event; including all 63 inputs would overflow its event queue.
    Endpoint receiver;
    receiver.receive(first);
    UdpProtocol::Event received;
    CHECK(receiver.GetEvent(received) && received.type == UdpProtocol::Event::Disconnected);
    CHECK(!receiver.GetEvent(received));
    receiver.receive(first);
    CHECK(!receiver.GetEvent(received));
    CHECK(first.u.input.num_bits == 0 && first.u.input.input_size == 0);
    CHECK(first.PacketSize() == 32);
    UdpProtocol::Event event;
    CHECK(endpoint.GetEvent(event) && event.type == UdpProtocol::Event::Disconnected);
    GGPOSessionCallbacks cb = {};
    cb.begin_game = begin_game; cb.free_buffer = release_state; cb.on_event = on_event;
    Host host(&cb); CHECK(host.IsValid()); host.disconnected(event);
    CHECK(disconnect_callbacks == 1);
    endpoint.SendInput(overflow);
    UdpMsg repeat = endpoint.take_packet();
    CHECK(repeat.u.input.disconnect_requested && endpoint.pending() == 63);
    CHECK(repeat.u.input.num_bits == 0 && repeat.u.input.input_size == 0);
    CHECK(repeat.PacketSize() == 32);
    receiver.receive(repeat); CHECK(!receiver.GetEvent(received));
    CHECK(!endpoint.GetEvent(event));
    endpoint.expire(); CHECK(!endpoint.IsInitialized());
    endpoint.SendInput(overflow); CHECK(!endpoint.GetEvent(event));
    puts("exhausted backlog disconnects once, notifies spectator host, and shuts down");
}

int main() {
    // A single 112-byte all-bits delta already exceeds the old 4096-bit
    // assertion. A 40-frame backlog would overrun the allocated byte array.
    backlog(112, false, true);
    backlog(112, true, true);
    backlog(56, false, false);
    backlog(112, false, true, true);
    exhausted_queue();
    Endpoint empty;
    UdpMsg packet = empty.packet();
    CHECK(packet.u.input.num_bits == 0 && packet.u.input.input_size == 0);
    CHECK(packet.PacketSize() == 32);
    return 0;
}
