#include <atomic>
#include <thread>
#include <cassert>
#include <iostream>
#include <array>
#include <chrono>

using namespace std::chrono_literals;

//////////// START ring_buffer //////////////////
struct ring_buffer { // Would be a class template in real code
    static constexpr std::size_t capacity = 1<<20;
    static constexpr std::size_t cache_line_size = std::hardware_destructive_interference_size; 
//     static constexpr std::size_t cache_line_size = 8; 

    ring_buffer() : next_producer_record(0)
                    , next_consumer_record(0)
                    , cached_producer_record{0}
                    , cached_consumer_record{0} {}
    inline void push(int64_t record) noexcept;
    inline void push2(int64_t record) noexcept;
    inline int64_t pop() noexcept;
    inline int64_t pop2() noexcept;

    inline bool has_space(std::size_t record) noexcept;
    inline bool has_data(std::size_t record) noexcept;

    inline std::size_t size() const noexcept;
    inline void sleep(std::size_t& spin_counter) const noexcept;
    inline std::size_t map_index(std::size_t index) const noexcept;

    alignas(cache_line_size) std::atomic<std::size_t> next_producer_record;
    alignas(cache_line_size) std::atomic<std::size_t> next_consumer_record;

    // cache of producer for consumer
    alignas(cache_line_size) size_t cached_consumer_record;
    alignas(cache_line_size) size_t cached_producer_record;

    alignas(64) std::array<int64_t, capacity> data;
};

inline bool ring_buffer::has_data(std::size_t consumer) noexcept {
    if(cached_producer_record - consumer != 0) return true;

    std::size_t producer = next_producer_record.load(std::memory_order_acquire);
    cached_producer_record = producer;    
    if (producer - consumer != 0) return true;
    else return false;
}

inline bool ring_buffer::has_space(std::size_t producer) noexcept {
    if(producer - cached_consumer_record < capacity) return true;

    std::size_t consumer = next_consumer_record.load(std::memory_order_acquire);
    cached_consumer_record = consumer;
    if (producer - consumer < capacity) return true;
    else return false;
}

inline void ring_buffer::push(int64_t record) noexcept {
    // Wait until there's space
    std::size_t spin_counter = 0;
    while (size() == capacity) sleep(spin_counter);

    // Produce the record
    data[map_index(next_producer_record)] = record;
    ++next_producer_record;
}

inline void ring_buffer::push2(int64_t record) noexcept {
    // Wait until there's space
    std::size_t spin_counter = 0;
    std::size_t producer = next_producer_record.load(std::memory_order_acquire);
    while (!has_space(producer)) sleep(spin_counter);

    // Produce the record
    data[map_index(producer)] = record;
    next_producer_record.store(producer + 1, std::memory_order_release);
}

inline int64_t ring_buffer::pop2() noexcept {
    // Wait until there's data
    std::size_t spin_counter = 0;
    std::size_t consumer = next_consumer_record.load(std::memory_order_acquire);
    while (!has_data(consumer)) sleep(spin_counter);

    // Consume the record
    auto record = data[map_index(consumer)];
    next_consumer_record.store(consumer + 1, std::memory_order_release);
    return record;
}

inline std::size_t ring_buffer::size() const noexcept {
    return next_producer_record - next_consumer_record;
}

inline std::size_t ring_buffer::map_index(std::size_t index) const noexcept {
    return index % capacity; // Could be a bitmask
}

inline int64_t ring_buffer::pop() noexcept {
    // Wait until there's data
    std::size_t spin_counter = 0;
    while (!size()) sleep(spin_counter);

    // Consume the record
    auto record = data[map_index(next_consumer_record)];
    ++next_consumer_record;
    return record;
}

inline void ring_buffer::sleep(std::size_t& spin_counter) const noexcept {
    using namespace std::chrono_literals;
    if (spin_counter++ < 16) return;
    else std::this_thread::sleep_for(50us);
}

//////////// END ring_buffer //////////////////

bool runflag = true;

void print_stats(ring_buffer const* queue) {
    std::size_t prev_idx = 0;
    while (runflag) {
        std::size_t new_idx = queue->next_consumer_record;
        std::cout << "Queue is moving at " << (new_idx - prev_idx)/1'000'000 << " Mrecords/second." << "\n";
        std::this_thread::sleep_for(1s);
        prev_idx = new_idx;
    }
}

int64_t const iterations = (1LL<<34);
void producer(ring_buffer* queue) {
    int64_t counter = 0;
    while (counter < iterations) {
        queue->push2(counter);
        ++counter;
    }
}

void consumer(ring_buffer* queue) {
    int64_t counter = 0;
    while (counter < iterations) {
        auto val = queue->pop2();
        assert(val == counter);
        ++counter;
    }
}

int main() {
    std::cout << "Cache line size = " << ring_buffer::cache_line_size << "\n";
    auto queue = std::make_unique<ring_buffer>();
    std::thread stats {print_stats, queue.get()}, prod {producer, queue.get()}, cons {consumer, queue.get()};
    prod.join(); cons.join();
    runflag = false; stats.join();
}
