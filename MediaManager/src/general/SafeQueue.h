#pragma once
#ifndef SAFE_QUEUE
#define SAFE_QUEUE

#include <deque>
#include <mutex>
//#include <condition_variable>
#include <optional>

// A threadsafe-queue.
template <class T>
class SafeQueue
{
public:
    SafeQueue(void)
        : q()
        , m()
        //, c()
    {}

    ~SafeQueue(void)
    {}

    // Add an element to the queue.
    void enqueue(T t)
    {
        std::lock_guard<std::mutex> lock(m);
        q.push_back(t);
        //c.notify_one();
    }

    void enqueue_front(T t)
    {
        std::lock_guard<std::mutex> lock(m);
        q.push_front(t);
        //c.notify_one();
    }

    // Get the "front"-element.
    // If the queue is empty, wait till a element is avaiable.
    std::optional<T> dequeue(void)
    {
        //std::unique_lock<std::mutex> lock(m);
        //while (q.empty())
        //{
        //    // release lock as long as the wait and reaquire it afterwards.
        //    c.wait(lock);
        //}
        std::lock_guard<std::mutex> lock(m);

        if(q.empty())
            return std::nullopt;

        T val = q.front();
        q.pop_front();
        return std::optional<T>{val};
    }
    std::optional<T> front(void)
    {
        std::lock_guard<std::mutex> lock(m);

        if (q.empty())
            return std::nullopt;

        T val = q.front();
        return std::optional<T>{val};
    }
    std::optional<T> back(void)
    {
        std::lock_guard<std::mutex> lock(m);

        if (q.empty())
            return std::nullopt;

        T val = q.back();
        return std::optional<T>{val};
    }
    inline bool isEmpty() {
        return this->q.empty();
    }
    inline int size() {
        return this->q.size();
    }
    void clear() {
        std::lock_guard<std::mutex> lock(m);
        while (!this->isEmpty())
            this->q.pop_front();
    }
    std::deque<T> q;
    mutable std::mutex m;
    //std::condition_variable c;
};
#endif
