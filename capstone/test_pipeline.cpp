#include <gtest/gtest.h>
#include "sensor_pipeline.hpp"

// Test 1: BoundedQueue basic push/pop
TEST(BoundedQueueTest, PushPopRoundTrip) {
    BoundedQueue<int, 4> q;
    q.push(42);
    auto val = q.pop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 42);
}

// Test 2: Shutdown unblocks waiting consumer
TEST(BoundedQueueTest, ShutdownUnblocksConsumer) {
    BoundedQueue<int, 4> q;
    std::thread t([&q] { q.pop(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    q.shutdown();
    t.join(); // Should return promptly — not hang
}

// Test 3: Pipeline processes readings
TEST(SensorPipelineTest, ProcessesReadings) {
    SensorPipeline pipeline("TestIMU");
    pipeline.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    pipeline.stop();
    EXPECT_GT(pipeline.processedCount(), 0u);
}

// Test 4: RAII safety — rapid start/stop must not crash
TEST(SensorPipelineTest, RapidStartStop) {
    for (int i = 0; i < 10; ++i) {
        SensorPipeline pipeline("StressTest");
        pipeline.start();
        // Immediate destruction — destructor must cleanly join threads
    }
}
