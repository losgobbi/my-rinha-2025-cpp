#pragma once
#include <chrono>
#include <drogon/drogon.h>
#include <format>
#include <iostream>
#include <math.h>
#include <string>

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
    Payment(std::string _correlationId, double _amount, const drogon::HttpClientPtr client, 
        uint8_t processor, Summary *summary)
        :correlationId(_correlationId), amount(_amount), client(client), 
        summary(summary), processor(processor) {
        requestedAt = std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    }
    ~Payment() {}

    void process(std::function<void(const drogon::HttpResponsePtr &)> callback) {
        Json::Value json;
        json["correlationId"] = correlationId;
        json["amount"] = amount;
        json["requestedAt"] = std::format("{:%FT%T}Z", requestedAt);
        
        auto t0 = std::chrono::system_clock::now();
        auto req = drogon::HttpRequest::newHttpJsonRequest(json);
        
        int64_t requestedAtMs = requestedAt.time_since_epoch().count();
        int64_t amountCents = std::llround(amount * 100.0);
        uint8_t proc = processor;
        Summary *sum = summary; 

        req->setMethod(drogon::Post);
        req->setPath("/payments");
        client->sendRequest(req, [requestedAtMs, amountCents, proc, sum, 
            cb = std::move(callback), corr = correlationId, t0,
            processor = processor, this]
            (drogon::ReqResult result, const drogon::HttpResponsePtr &resp) {
            auto out = drogon::HttpResponse::newHttpResponse();
            int status = resp ? static_cast<int>(resp->statusCode()) : -1;
            auto t1 = std::chrono::system_clock::now();
            if (result == drogon::ReqResult::Ok && status == 200) {
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                sum->add({requestedAtMs, amountCents, proc});
                out->setStatusCode(drogon::k201Created);
                std::cout << " processing done at " << static_cast<int>(processor) << " for payment correlationId " << corr << " with " << ms << std::endl;
            } else {
                std::cout << "processor " << static_cast<int>(processor) << " rejected corr=" << corr
                            << " result=" << static_cast<int>(result)
                            << " status=" << status
                            << " body="  << (resp ? std::string(resp->getBody()) : "<null>")
                            << std::endl;
                handleResponse(status);
                out->setStatusCode(drogon::k502BadGateway);
            }
            cb(out);
        });
    }

    void handleResponse(int httpStatus) {
        if (httpStatus != 500)
            return;

        // TODO, need to wait until its back
    }

private:
    std::string correlationId;
    double amount;
    uint8_t processor;
    std::chrono::sys_time<std::chrono::milliseconds> requestedAt;
    const drogon::HttpClientPtr client;
    Summary *summary;
};
