#include "sensor_pipeline.hpp"

int main() {
    SensorPipeline pipeline("IMU_Pipeline");
    pipeline.start();

    std::cout << "[INFO] Pipeline running at " << config::IMU_HZ << " Hz\n";
    std::this_thread::sleep_for(std::chrono::seconds(3));

    pipeline.stop();
    std::cout << "[INFO] Processed " << pipeline.processedCount() << " readings\n";
    return 0;
}
