#pragma once
#include <deque>
#include <mutex>
#include <optional>
#include <functional>

template <typename T>
class NonBlockingQueue {
public:
    NonBlockingQueue() = default;
    ~NonBlockingQueue() = default;

    // Push methods
    void push(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(item);
    }

    void pushFront(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_front(item);
    }

    void pushBack(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back(item);
    }

    // Pop methods
    std::optional<T> pop() {
        return popFront();
    }

    std::optional<T> popFront() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        T item = queue_.front();
        queue_.pop_front();
        return item;
    }

    std::optional<T> popBack() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        T item = queue_.back();
        queue_.pop_back();
        return item;
    }

    // Peek methods
    std::optional<T> front() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        return queue_.front();
    }

    std::optional<T> back() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        return queue_.back();
    }

    // Remove items matching a predicate
    void removeIf(std::function<bool(const T&)> predicate) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = queue_.begin(); it != queue_.end(); ) {
            if (predicate(*it)) it = queue_.erase(it);
            else ++it;
        }
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
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
    std::deque<T> queue_;
};