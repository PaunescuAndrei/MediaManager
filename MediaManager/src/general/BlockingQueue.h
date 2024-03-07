#pragma once
#include <stdlib.h>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

using namespace std;
template <class T> class BlockingQueue : public queue<T> {
public:
    BlockingQueue(int size) {
        maxSize = size;
    }

    void push(T item) {
        unique_lock<std::mutex> wlck(writerMutex);
        while (Full())
            isFull.wait(wlck);
        queue<T>::push(item);
        isEmpty.notify_all();
    }

    bool notEmpty() {
        return !queue<T>::empty();
    }

    bool Full() {
        return queue<T>::size() >= maxSize;
    }

    T pop() {
        unique_lock<std::mutex> lck(readerMutex);
        while (queue<T>::empty()) {
            isEmpty.wait(lck);
        }
        T value = queue<T>::front();
        queue<T>::pop();
        if (!Full())
            isFull.notify_all();
        return value;
    }

private:
    int maxSize;
    std::mutex readerMutex;
    std::mutex writerMutex;
    condition_variable isFull;
    condition_variable isEmpty;
};