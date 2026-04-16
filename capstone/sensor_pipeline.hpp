#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <random>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

// ── Constants (Module 02: constexpr, zero runtime cost) ──
namespace config {
    constexpr std::size_t QUEUE_CAPACITY = 256;
    constexpr int         IMU_HZ         = 100;
    constexpr auto        IMU_PERIOD     = std::chrono::milliseconds(1000 / IMU_HZ);
}

// ── Data Types (Module 06: enum class + POD struct) ──
enum class PipelineState { IDLE, RUNNING, SHUTTING_DOWN };

struct IMUReading {
    double accel_x{0.0};
    double accel_y{0.0};
    double accel_z{-9.81};
    std::chrono::steady_clock::time_point timestamp{};
};

// ── Thread-Safe Bounded Queue (Modules 11, 12: Templates + Concurrency) ──
template<typename T, std::size_t N>
class BoundedQueue {
public:
    // Producer: push with bounded blocking
    bool push(const T& item) {
        std::unique_lock<std::mutex> lock(mux_);
        cv_space_.wait(lock, [this] { return count_ < N || !active_; });
        if (!active_) return false;

        buffer_[(head_ + count_) % N] = item;
        ++count_;
        lock.unlock();
        cv_data_.notify_one();
        return true;
    }

    // Consumer: pop with blocking wait (Module 08: std::optional)
    [[nodiscard]] std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mux_);
        cv_data_.wait(lock, [this] { return count_ > 0 || !active_; });
        if (count_ == 0) return std::nullopt; // Shutdown signal

        T item = buffer_[head_];
        head_ = (head_ + 1) % N;
        --count_;
        lock.unlock();
        cv_space_.notify_one();
        return item;
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mux_);
            active_ = false;
        }
        cv_data_.notify_all();
        cv_space_.notify_all();
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard<std::mutex> lock(mux_);
        return count_;
    }

private:
    std::array<T, N> buffer_{};
    std::size_t head_{0};
    std::size_t count_{0};
    bool active_{true};
    mutable std::mutex mux_;
    std::condition_variable cv_data_;
    std::condition_variable cv_space_;
};

// ── Sensor Pipeline (Modules 07, 10, 12: RAII + Classes + Threads) ──
class SensorPipeline {
public:
    explicit SensorPipeline(std::string_view name)
        : name_(name) {}

    // RAII: destructor guarantees clean shutdown (Module 07)
    ~SensorPipeline() { stop(); }

    // Non-copyable, non-movable (owns threads — Module 10)
    SensorPipeline(const SensorPipeline&) = delete;
    SensorPipeline& operator=(const SensorPipeline&) = delete;

    void start() {
        running_ = true;
        producer_ = std::thread(&SensorPipeline::producerLoop, this);
        consumer_ = std::thread(&SensorPipeline::consumerLoop, this);
    }

    void stop() {
        if (!running_.exchange(false)) return; // Already stopped
        queue_.shutdown();
        if (producer_.joinable()) producer_.join();
        if (consumer_.joinable()) consumer_.join();
    }

    [[nodiscard]] std::size_t processedCount() const noexcept {
        return processed_count_.load(std::memory_order_relaxed);
    }

private:
    void producerLoop() {
        // Thread-local RNG — each thread gets its own engine (no data race)
        std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<double> noise(0.0, 0.1);

        while (running_) {
            IMUReading reading{};
            reading.timestamp = std::chrono::steady_clock::now();
            reading.accel_z = -9.81 + noise(rng);

            if (!queue_.push(reading)) break;
            std::this_thread::sleep_for(config::IMU_PERIOD);
        }
    }

    void consumerLoop() {
        // Rely solely on pop()'s internal lock + shutdown signal,
        // NOT on an unguarded queue_.size() check which would be a data race.
        while (true) {
            auto maybe_reading = queue_.pop();
            if (!maybe_reading) break; // shutdown() was called and queue is drained

            // Simple magnitude filter (Module 03: arithmetic)
            const auto& r = *maybe_reading;
            [[maybe_unused]] double magnitude = std::sqrt(
                r.accel_x * r.accel_x +
                r.accel_y * r.accel_y +
                r.accel_z * r.accel_z);

            // Latency tracking (Module 15: std::chrono profiling)
            auto latency = std::chrono::steady_clock::now() - r.timestamp;
            auto latency_us = std::chrono::duration_cast<
                std::chrono::microseconds>(latency).count();

            if (latency_us > 5000) {
                std::cerr << "[WARN] " << name_ << " pipeline latency: "
                          << latency_us << " µs\n";
            }

            processed_count_.fetch_add(1, std::memory_order_relaxed);
        }
    }

    std::string name_;
    BoundedQueue<IMUReading, config::QUEUE_CAPACITY> queue_;
    std::atomic<bool> running_{false};
    std::atomic<std::size_t> processed_count_{0};
    std::thread producer_;
    std::thread consumer_;
};
