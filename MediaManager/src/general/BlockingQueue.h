#pragma once
#include <deque>
#include <mutex>
#include <condition_variable>
#include <functional>

template <typename T>
class BlockingQueue {
public:
    explicit BlockingQueue(size_t maxSize)
        : maxSize_(maxSize) {
    }

    ~BlockingQueue() = default;

    // Release every waiter and refuse further pushes. A producer parked in push* on a
    // full queue can only be woken by a consumer, so without this a shutdown that
    // joins the producer deadlocks: the producer never returns from push*, never
    // re-reads its stop flag, and the joining thread waits forever. Call this before
    // waiting on the producer. The queue stays closed; pop*/front/back return a
    // default-constructed T once it is drained.
    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        condFull_.notify_all();
        condEmpty_.notify_all();
    }

    bool isClosed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

    // Push methods - return false when the queue is closed (item dropped).
    bool push(const T& item) {
        return pushBack(item);
    }

    bool pushFront(const T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        condFull_.wait(lock, [this] { return closed_ || queue_.size() < maxSize_; });
        if (closed_)
            return false;
        queue_.push_front(item);
        condEmpty_.notify_one();
        return true;
    }

    bool pushBack(const T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        condFull_.wait(lock, [this] { return closed_ || queue_.size() < maxSize_; });
        if (closed_)
            return false;
        queue_.push_back(item);
        condEmpty_.notify_one();
        return true;
    }

    // Pop methods
    T pop() {
        return popFront();
    }

    T popFront() {
        std::unique_lock<std::mutex> lock(mutex_);
        condEmpty_.wait(lock, [this] { return closed_ || !queue_.empty(); });
        if (queue_.empty())
            return T();
        T item = queue_.front();
        queue_.pop_front();
        condFull_.notify_one();
        return item;
    }

    T popBack() {
        std::unique_lock<std::mutex> lock(mutex_);
        condEmpty_.wait(lock, [this] { return closed_ || !queue_.empty(); });
        if (queue_.empty())
            return T();
        T item = queue_.back();
        queue_.pop_back();
        condFull_.notify_one();
        return item;
    }

    // Peek methods (blocking)
    T front() {
        std::unique_lock<std::mutex> lock(mutex_);
        condEmpty_.wait(lock, [this] { return closed_ || !queue_.empty(); });
        if (queue_.empty())
            return T();
        return queue_.front();
    }

    T back() {
        std::unique_lock<std::mutex> lock(mutex_);
        condEmpty_.wait(lock, [this] { return closed_ || !queue_.empty(); });
        if (queue_.empty())
            return T();
        return queue_.back();
    }

    // Remove items matching a predicate
    void removeIf(std::function<bool(const T&)> predicate) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = queue_.begin(); it != queue_.end(); ) {
            if (predicate(*it)) it = queue_.erase(it);
            else ++it;
        }
        condFull_.notify_all(); // wake pushers if space freed
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        condFull_.notify_all();
    }

    bool isEmpty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condEmpty_;
    std::condition_variable condFull_;
    std::deque<T> queue_;
    size_t maxSize_;
    bool closed_ = false;
};
