// Reconnect backoff scheduling.
//
// This runs on installations that stay powered for months, so both the
// backoff curve and its behaviour across the millis() rollover matter.

#include <set>
#include <vector>

#include "test_access.h"
#include "tiny_test.h"

namespace {

struct Sdk {
    DataNet dn{"ak_test", "http://gateway.local", "gateway.local", 80};

    Sdk() {
        datanetTestWire().reset();
        datanetTestSetMillis(100000);
        randomSeed(12345);
    }

    // Schedules one reconnect and returns the delay it chose.
    uint32_t scheduleAndMeasure() {
        DataNetTestAccess::clearReconnectPending(dn);
        uint32_t now = millis();
        DataNetTestAccess::scheduleReconnect(dn);
        return DataNetTestAccess::reconnectAtMs(dn) - now;
    }
};

}  // namespace

TEST(reconnect_backoff_grows_exponentially_from_the_base) {
    Sdk sdk;

    uint32_t first  = sdk.scheduleAndMeasure();
    uint32_t second = sdk.scheduleAndMeasure();
    uint32_t third  = sdk.scheduleAndMeasure();
    uint32_t fourth = sdk.scheduleAndMeasure();

    // Jitter is additive and capped at +20%, so each delay sits in
    // [base, base * 1.2].
    CHECK(first >= DATANET_RECONNECT_BASE_MS && first <= DATANET_RECONNECT_BASE_MS * 12 / 10);
    CHECK(second >= DATANET_RECONNECT_BASE_MS * 2 && second <= DATANET_RECONNECT_BASE_MS * 24 / 10);
    CHECK(third >= DATANET_RECONNECT_BASE_MS * 4 && third <= DATANET_RECONNECT_BASE_MS * 48 / 10);
    CHECK(fourth >= DATANET_RECONNECT_BASE_MS * 8 && fourth <= DATANET_RECONNECT_BASE_MS * 96 / 10);
}

TEST(reconnect_backoff_is_capped) {
    Sdk sdk;
    uint32_t delay = 0;
    for (int i = 0; i < 20; i++) delay = sdk.scheduleAndMeasure();

    CHECK(delay >= DATANET_RECONNECT_MAX_MS);
    CHECK(delay <= DATANET_RECONNECT_MAX_MS * 12 / 10);
}

TEST(reconnect_backoff_applies_real_jitter) {
    // Without jitter, every device on a site reconnects on the same tick after
    // a router reboot and hammers the service in lockstep. The spread must
    // actually vary, not just be documented.
    Sdk sdk;

    std::set<uint32_t> observed;
    for (int i = 0; i < 40; i++) {
        DataNetTestAccess::clearReconnectPending(sdk.dn);
        // Pin the attempt count so the exponential base is identical on every
        // iteration. Any spread that remains comes from jitter alone.
        DataNetTestAccess::setReconnectCount(sdk.dn, 3);
        uint32_t now = millis();
        DataNetTestAccess::scheduleReconnect(sdk.dn);
        observed.insert(DataNetTestAccess::reconnectAtMs(sdk.dn) - now);
    }

    CHECK(observed.size() > 1);
}

TEST(reconnect_is_not_rescheduled_while_one_is_pending) {
    Sdk sdk;
    DataNetTestAccess::scheduleReconnect(sdk.dn);
    uint32_t firstDeadline = DataNetTestAccess::reconnectAtMs(sdk.dn);
    uint8_t  firstCount    = DataNetTestAccess::reconnectCount(sdk.dn);

    DataNetTestAccess::scheduleReconnect(sdk.dn);

    CHECK_EQ(DataNetTestAccess::reconnectAtMs(sdk.dn), firstDeadline);
    CHECK_EQ(DataNetTestAccess::reconnectCount(sdk.dn), firstCount);
}

TEST(reconnect_deadline_survives_the_millis_rollover) {
    // millis() wraps every ~49.7 days. If the deadline is compared with a bare
    // >= against a wrapped counter, the backoff collapses to zero for one tick
    // and the device retries immediately — exactly when a long-running
    // installation can least afford a reconnect storm.
    Sdk sdk;
    DataNetTestAccess::setJwt(sdk.dn, "test.jwt.token");
    datanetTestSetMillis(0xFFFFFF00u);   // 256 ms before the wrap

    DataNetTestAccess::scheduleReconnect(sdk.dn);
    uint32_t deadline = DataNetTestAccess::reconnectAtMs(sdk.dn);

    // The delay is ~1000 ms, so the deadline lands past the wrap point.
    CHECK(deadline < 0xFFFFFF00u);

    datanetTestWire().reset();
    datanetTestWire().connectShouldFail = true;

    // 16 ms later: still 700+ ms of backoff to serve, so loop() must not
    // attempt a connection.
    datanetTestSetMillis(0xFFFFFF10u);
    sdk.dn.loop();
    CHECK_EQ(datanetTestWire().connectCount, 0);
    CHECK(DataNetTestAccess::reconnectPending(sdk.dn));

    // Once the (wrapped) deadline actually passes, it fires.
    datanetTestSetMillis(deadline + 5);
    sdk.dn.loop();
    CHECK_EQ(datanetTestWire().connectCount, 1);
}

TEST(reconnect_loop_honours_the_backoff_before_retrying) {
    // loop() must not attempt a reconnect before the deadline. The generic
    // transport reconnects by opening a TCP socket, so connectCount is the
    // observable signal.
    Sdk sdk;
    DataNetTestAccess::setJwt(sdk.dn, "test.jwt.token");
    DataNetTestAccess::scheduleReconnect(sdk.dn);
    uint32_t deadline = DataNetTestAccess::reconnectAtMs(sdk.dn);

    datanetTestWire().reset();
    datanetTestWire().connectShouldFail = true;

    // Well before the deadline: no connection attempts.
    datanetTestSetMillis(deadline - 500);
    sdk.dn.loop();
    CHECK_EQ(datanetTestWire().connectCount, 0);

    // At the deadline: exactly one attempt.
    datanetTestSetMillis(deadline);
    sdk.dn.loop();
    CHECK_EQ(datanetTestWire().connectCount, 1);
}
