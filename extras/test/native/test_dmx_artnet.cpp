// DMX frame and Art-Net ArtDMX packet construction.
//
// These packets go straight onto a lighting network, where a malformed header
// is either silently ignored by the node or, worse, interpreted as a different
// universe. Byte offsets are asserted against the Art-Net 4 spec explicitly.

#include "test_access.h"
#include "tiny_test.h"

// --- buildDmxFrame ---------------------------------------------------------

TEST(dmx_frame_zero_fills_unset_channels) {
    const uint8_t values[] = {10, 20, 30};
    uint8_t       frame[512];
    std::memset(frame, 0xAA, sizeof(frame));

    size_t n = DataNet::buildDmxFrame(frame, sizeof(frame), values, 3, 512);

    CHECK_EQ(n, static_cast<size_t>(512));
    CHECK_EQ(static_cast<int>(frame[0]), 10);
    CHECK_EQ(static_cast<int>(frame[2]), 30);
    for (size_t i = 3; i < 512; i++) {
        if (frame[i] != 0) {
            CHECK_EQ(static_cast<int>(frame[i]), 0);
            break;
        }
    }
}

TEST(dmx_frame_truncates_when_more_values_than_channels) {
    const uint8_t values[] = {1, 2, 3, 4, 5};
    uint8_t       frame[8];

    size_t n = DataNet::buildDmxFrame(frame, sizeof(frame), values, 5, 3);

    CHECK_EQ(n, static_cast<size_t>(3));
    CHECK_EQ(static_cast<int>(frame[0]), 1);
    CHECK_EQ(static_cast<int>(frame[2]), 3);
}

TEST(dmx_frame_clamps_frame_length_to_dmx_limits) {
    uint8_t frame[600];
    CHECK_EQ(DataNet::buildDmxFrame(frame, sizeof(frame), nullptr, 0, 0), static_cast<size_t>(1));
    CHECK_EQ(DataNet::buildDmxFrame(frame, sizeof(frame), nullptr, 0, 9999), static_cast<size_t>(512));
}

TEST(dmx_frame_refuses_to_overflow_the_output_buffer) {
    uint8_t small[16];
    CHECK_EQ(DataNet::buildDmxFrame(small, sizeof(small), nullptr, 0, 512), static_cast<size_t>(0));
    CHECK_EQ(DataNet::buildDmxFrame(nullptr, 512, nullptr, 0, 512), static_cast<size_t>(0));
}

// --- buildArtDmxPacket -----------------------------------------------------

TEST(artdmx_header_matches_the_artnet_spec) {
    uint8_t dmx[4] = {1, 2, 3, 4};
    uint8_t packet[18 + 512];

    size_t n = DataNet::buildArtDmxPacket(packet, sizeof(packet), dmx, sizeof(dmx),
                                          /*universe*/ 3, /*subnet*/ 2, /*net*/ 1,
                                          /*sequence*/ 7, /*physical*/ 5);

    CHECK_EQ(n, static_cast<size_t>(18 + 4));
    CHECK_EQ(std::memcmp(packet, "Art-Net\0", 8), 0);   // ID, null terminated
    CHECK_EQ(static_cast<int>(packet[8]), 0x00);        // OpCode lo (0x5000 LE)
    CHECK_EQ(static_cast<int>(packet[9]), 0x50);        // OpCode hi
    CHECK_EQ(static_cast<int>(packet[10]), 0);          // ProtVerHi
    CHECK_EQ(static_cast<int>(packet[11]), 14);         // ProtVerLo
    CHECK_EQ(static_cast<int>(packet[12]), 7);          // Sequence
    CHECK_EQ(static_cast<int>(packet[13]), 5);          // Physical
    CHECK_EQ(static_cast<int>(packet[14]), 0x23);       // SubUni: subnet<<4 | universe
    CHECK_EQ(static_cast<int>(packet[15]), 1);          // Net
    CHECK_EQ(static_cast<int>(packet[16]), 0);          // LengthHi (big endian)
    CHECK_EQ(static_cast<int>(packet[17]), 4);          // LengthLo
    CHECK_EQ(std::memcmp(packet + 18, dmx, 4), 0);
}

TEST(artdmx_length_is_even_per_spec) {
    // Art-Net 4, table "ArtDmx packet definition": Length "should be an even
    // number in the range 2 - 512". Odd lengths are rejected outright by some
    // commercial nodes, so an odd request must be rounded up, not passed on.
    uint8_t dmx[7] = {1, 2, 3, 4, 5, 6, 7};
    uint8_t packet[18 + 512];

    size_t n = DataNet::buildArtDmxPacket(packet, sizeof(packet), dmx, 7);

    size_t declared = (static_cast<size_t>(packet[16]) << 8) | packet[17];
    CHECK_EQ(declared % 2, static_cast<size_t>(0));
    CHECK_EQ(n, 18 + declared);
}

TEST(artdmx_refuses_undersized_output_buffers) {
    uint8_t dmx[512] = {0};
    uint8_t tiny[19];
    CHECK_EQ(DataNet::buildArtDmxPacket(tiny, sizeof(tiny), dmx, 512), static_cast<size_t>(0));
    CHECK_EQ(DataNet::buildArtDmxPacket(nullptr, 530, dmx, 512), static_cast<size_t>(0));
}

// --- extractArtDmx ---------------------------------------------------------

TEST(artdmx_roundtrips_through_extract) {
    uint8_t dmx[512];
    for (size_t i = 0; i < sizeof(dmx); i++) dmx[i] = static_cast<uint8_t>(i & 0xFF);

    uint8_t packet[18 + 512];
    size_t  n = DataNet::buildArtDmxPacket(packet, sizeof(packet), dmx, sizeof(dmx),
                                           /*universe*/ 5, /*subnet*/ 6, /*net*/ 7);

    const uint8_t* outDmx = nullptr;
    size_t         outLen = 0;
    uint8_t        universe = 0, subnet = 0, net = 0;

    CHECK(DataNet::extractArtDmx(packet, n, &outDmx, &outLen, &universe, &subnet, &net));
    CHECK_EQ(outLen, sizeof(dmx));
    CHECK_EQ(std::memcmp(outDmx, dmx, sizeof(dmx)), 0);
    CHECK_EQ(static_cast<int>(universe), 5);
    CHECK_EQ(static_cast<int>(subnet), 6);
    CHECK_EQ(static_cast<int>(net), 7);
}

TEST(artdmx_extract_rejects_foreign_packets) {
    uint8_t dmx[4] = {1, 2, 3, 4};
    uint8_t packet[18 + 512];
    size_t  n = DataNet::buildArtDmxPacket(packet, sizeof(packet), dmx, sizeof(dmx));

    const uint8_t* outDmx = nullptr;
    size_t         outLen = 0;

    // Wrong protocol ID
    uint8_t wrongId[18 + 512];
    std::memcpy(wrongId, packet, n);
    wrongId[0] = 'X';
    CHECK(!DataNet::extractArtDmx(wrongId, n, &outDmx, &outLen));

    // Wrong opcode (ArtPoll 0x2000 rather than ArtDmx 0x5000)
    uint8_t wrongOp[18 + 512];
    std::memcpy(wrongOp, packet, n);
    wrongOp[9] = 0x20;
    CHECK(!DataNet::extractArtDmx(wrongOp, n, &outDmx, &outLen));

    // Truncated: declared length runs past the buffer
    uint8_t truncated[18 + 512];
    std::memcpy(truncated, packet, n);
    truncated[16] = 0x01;
    truncated[17] = 0xF4;  // declares 500 bytes in a 22-byte packet
    CHECK(!DataNet::extractArtDmx(truncated, n, &outDmx, &outLen));

    CHECK(!DataNet::extractArtDmx(packet, 19, &outDmx, &outLen));
    CHECK(!DataNet::extractArtDmx(nullptr, n, &outDmx, &outLen));
}
