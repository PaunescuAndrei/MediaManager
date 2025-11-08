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

    // Push methods
    void push(const T& item) {
        pushBack(item);
    }

    void pushFront(const T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        condFull_.wait(lock, [this] { return queue_.size() < maxSize_; });
        queue_.push_front(item);
        condEmpty_.notify_one();
    }

    void pushBack(const T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        condFull_.wait(lock, [this] { return queue_.size() < maxSize_; });
        queue_.push_back(item);
        condEmpty_.notify_one();
    }

    // Pop methods
    T pop() {
        return popFront();
    }

    T popFront() {
        std::unique_lock<std::mutex> lock(mutex_);
        condEmpty_.wait(lock, [this] { return !queue_.empty(); });
        T item = queue_.front();
        queue_.pop_front();
        condFull_.notify_one();
        return item;
    }

    T popBack() {
        std::unique_lock<std::mutex> lock(mutex_);
        condEmpty_.wait(lock, [this] { return !queue_.empty(); });
        T item = queue_.back();
        queue_.pop_back();
        condFull_.notify_one();
        return item;
    }

    // Peek methods (blocking)
    T front() {
        std::unique_lock<std::mutex> lock(mutex_);
        condEmpty_.wait(lock, [this] { return !queue_.empty(); });
        return queue_.front();
    }

    T back() {
        std::unique_lock<std::mutex> lock(mutex_);
        condEmpty_.wait(lock, [this] { return !queue_.empty(); });
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
};
