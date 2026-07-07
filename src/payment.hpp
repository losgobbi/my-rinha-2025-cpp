#pragma once
#include <chrono>
#include <drogon/drogon.h>
#include <format>
#include <iostream>
#include <math.h>
#include <string>
#include <tuple>
#include <queue>
#include <vector>

#include "global.hpp"

class Summary {
public:
    struct Record {
        int64_t requestedAtMs;
        int64_t amountCents;
        uint8_t processor;
    };     

    struct Totals {
        int64_t defaultCount = 0,  defaultCents = 0;
        int64_t fallbackCount = 0, fallbackCents = 0;
    }; 

    Totals aggregate(int64_t lo, int64_t hi) {
        std::lock_guard<std::mutex> lk(mtx);
        Totals t{};
        for (const auto& r : records) {
            if (r.requestedAtMs >= lo && r.requestedAtMs <= hi) {
                if (r.processor == static_cast<uint8_t>(Processor::Default)) {
                  t.defaultCount++;
                  t.defaultCents += r.amountCents;
            } else {
                t.fallbackCount++;
                t.fallbackCents += r.amountCents;
            }
        }
      }
      return t;
    }

    Summary() {
        records.reserve(100'000);
    }

    ~Summary() {}

    void add(const Record& r) {
        std::lock_guard<std::mutex> lk(mtx);
        records.push_back(r);
    }
private:
    std::mutex mtx;
    std::vector<Record> records;
};

class Payment {
public:
    Payment(std::string _correlationId, double _amount, Summary *summary, HealthCheck *hc)
        :correlationId(_correlationId), amount(_amount), summary(summary), hc(hc) {
        requestedAt = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    }

    static std::tuple<int,int> getStats() {
        std::unique_lock<std::mutex> lk(pq_mtx);
        size_t q_len = p_queue.size();
        auto stats = std::make_tuple(q_len, inFlight.load());
        return stats;
    }

    static void enqueue(Payment order) {
        std::unique_lock<std::mutex> lk(pq_mtx);
        p_queue.push(std::move(order));
        lk.unlock();
        Payment::drainQueue();
    }

    static void drainQueue() {
        for (;;) {
            std::unique_lock<std::mutex> lk(pq_mtx);
            if (p_queue.empty() || inFlight.load() >= MAX_IN_FLIGHT) return;
            if (!p_queue.front().hc->hasAvailable()) return;
            Payment payorder = std::move(p_queue.front());
            p_queue.pop();
            lk.unlock();
            inFlight++;
            payorder.process();
        }
    }

    void process() {
        Json::Value json;
        json["correlationId"] = correlationId;
        json["amount"] = amount;
        json["requestedAt"] = std::format("{:%FT%T}Z", requestedAt);
        
        auto t0 = std::chrono::system_clock::now();
        auto req = drogon::HttpRequest::newHttpJsonRequest(json);
        req->setMethod(drogon::Post);
        req->setPath("/payments");
        
        try {
            auto [client, processor] = hc->getAvailableState();
            int64_t requestedAtMs = requestedAt.time_since_epoch().count();
            int64_t amountCents = std::llround(amount * 100.0);
            uint8_t procByte = static_cast<uint8_t>(processor);
            Summary *sum = summary;
#ifdef DEBUG_PAYMENT
            std::cout << "processing correlationId=" << correlationId << std::endl;
#endif            
            client->sendRequest(req, [t0, sum, requestedAtMs, amountCents, procByte, self = *this]
                (drogon::ReqResult result, const drogon::HttpResponsePtr &resp) {
                int status = resp ? static_cast<int>(resp->statusCode()) : -1;
                auto t1 = std::chrono::system_clock::now();
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                if (result == drogon::ReqResult::Ok && 
                    status == 200) {
                    sum->add({requestedAtMs, amountCents, static_cast<uint8_t>(procByte)});
#ifdef DEBUG_PAYMENT
                    std::cout << " processing done endpoint=" << static_cast<int>(procByte)
                                << " correlationId " << self.correlationId
                                << " with " << ms
                                << std::endl;
#endif
                    inFlight--;
                    Payment::drainQueue();
                } else {
#ifdef DEBUG_PAYMENT
                    std::cout << "processor " << static_cast<int>(procByte) << " rejected corr=" << self.correlationId
                                << " result=" << static_cast<int>(result)
                                << " status=" << status
                                << " body="  << (resp ? std::string(resp->getBody()) : "<null>")
                                << " with=" << ms
                                << std::endl;
#endif
                    inFlight--;
                    Payment::enqueue(std::move(self));
                }
            });
        } catch(const std::invalid_argument& e) {
#ifdef DEBUG_PAYMENT
            std::cout << "unavailable endpoint during correlationId=" << correlationId << " " << *hc << std::endl;
#endif
            inFlight--;
            std::lock_guard<std::mutex> lk(pq_mtx);
            p_queue.push(std::move(*this));
        }
    }

private:
    std::string correlationId;
    double amount;
    std::chrono::sys_time<std::chrono::milliseconds> requestedAt;
    Summary *summary;
    HealthCheck *hc;
    inline static std::queue<Payment> p_queue;
    inline static std::mutex pq_mtx;
    inline static std::atomic<int> inFlight{0};
};