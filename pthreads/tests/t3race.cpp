// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Dalsin Limited.
// t3_threads: std::thread, mutex, condition_variable, thread_local, call_once, async.
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
static thread_local int tl = 0;
static std::once_flag once; static int once_count;
int main()
{
    int fails = 0;
    std::mutex m; std::condition_variable cv; std::queue<int> q; bool done = false; long sum = 0;
    std::thread consumer([&] { std::unique_lock<std::mutex> lk(m); for (;;) { cv.wait(lk, [&] { return !q.empty() || done; }); while (!q.empty()) { sum += q.front(); q.pop(); } if (done) break; } });
    std::vector<std::thread> producers;
    for (int t = 0; t < 4; ++t) producers.emplace_back([&, t] { for (int i = 1; i <= 250; ++i) { { std::lock_guard<std::mutex> g(m); q.push(i + t * 1000); } cv.notify_one(); } tl += t + 1; std::call_once(once, [] { ++once_count; }); });
    for (auto &p : producers) p.join();
    { std::lock_guard<std::mutex> g(m); done = true; } cv.notify_one(); consumer.join();
    long want = 4 * (250 * 251 / 2) + 250 * (0 + 1000 + 2000 + 3000);
    if (sum != want) { std::printf("FAIL producer/consumer sum %ld want %ld\n", sum, want); ++fails; } else std::puts("PASS producer/consumer");
    if (tl != 0) { std::puts("FAIL thread_local leaked into main"); ++fails; } else std::puts("PASS thread_local");
    if (once_count != 1) { std::puts("FAIL call_once"); ++fails; } else std::puts("PASS call_once");
    auto f = std::async(std::launch::async, [] { return 6 * 7; });
    if (f.get() != 42) { std::puts("FAIL async"); ++fails; } else std::puts("PASS async");
    std::puts(fails ? "t3 FAILED" : "t3 OK");
    return fails ? 20 : 0;
}
