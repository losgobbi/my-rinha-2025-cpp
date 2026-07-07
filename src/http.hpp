#pragma once
#include <drogon/drogon.h>

#include "healthcheck.hpp"
#include "payment.hpp"
#include "utils.hpp"

#include "global.hpp"

int https_start() {
    std::cout << "registering http server at " << DROGON_PORT << "..." << std::endl;
    drogon::app()
        .setLogLevel(trantor::Logger::kError)
        .addListener(DROGON_IP, DROGON_PORT)
        .setThreadNum(THREAD_N);

    HealthCheck hc;
    hc.startTimer();

    Summary summary;

    drogon::HttpClientPtr auxClient = drogon::HttpClient::newHttpClient(
        std::getenv("GET_PARTIAL_URL"));

    // routes
    drogon::app().registerHandler(
        "/payments-summary",
        [&summary, &auxClient](const drogon::HttpRequestPtr &req,
            std::function<void(const drogon::HttpResponsePtr &)> &&callback) {

            auto t0 = std::chrono::system_clock::now();

            Json::Value paySummary;
            paySummary["from"] = req->getParameter("from");
            paySummary["to"] = req->getParameter("to");
            json_dump("summary", paySummary);
            
            int64_t lo = parseIsoMs(req->getParameter("from"));
            int64_t hi = parseIsoMs(req->getParameter("to"));
            auto aggi = summary.aggregate(lo, hi);

            auto extraReq = drogon::HttpRequest::newHttpRequest();
            extraReq->setMethod(drogon::Get);
            extraReq->setPath("/get-partial-summary");
            extraReq->setParameter("from", req->getParameter("from"));
            extraReq->setParameter("to", req->getParameter("to"));
            auxClient->sendRequest(extraReq, [extraReq, aggi, cb = std::move(callback), t0]
                (drogon::ReqResult result, const drogon::HttpResponsePtr &resp) {
                int status = resp ? static_cast<int>(resp->statusCode()) : -1;
                std::cout << " get partial result=" << result << " status=" << status
                    << " resp=" << (resp ? "valid" : "null") << std::endl;
                Json::Value paySummary = {};
                Summary::Totals m = aggi;
                if (result == drogon::ReqResult::Ok && resp) {
                    if (auto agg2 = resp->getJsonObject()) {
                        m.defaultCount += (*agg2)["default"]["totalRequests"].asInt64();
                        m.defaultCents += (*agg2)["default"]["totalAmount"].asInt64();
                        m.fallbackCount += (*agg2)["fallback"]["totalRequests"].asInt64();
                        m.fallbackCents += (*agg2)["fallback"]["totalAmount"].asInt64();
                    }
                } 
                paySummary["default"]["totalAmount"] = m.defaultCents/100.0;
                paySummary["default"]["totalRequests"] = m.defaultCount;
                paySummary["fallback"]["totalRequests"] = m.fallbackCount;
                paySummary["fallback"]["totalAmount"] = m.fallbackCents/100.0;
                json_dump("aggregate", paySummary);
                auto t1 = std::chrono::system_clock::now();
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                std::cout << " get summary done with " << ms << std::endl;
                auto res = drogon::HttpResponse::newHttpJsonResponse(paySummary);
                cb(res);
            }, PAYMENT_SUM_TIMEOUT);
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/get-partial-summary",
        [&summary](const drogon::HttpRequestPtr &req,
            std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            Json::Value result;
            result["from"] = req->getParameter("from");
            result["to"] = req->getParameter("to");
            json_dump("summary", result);
            
            int64_t lo = parseIsoMs(req->getParameter("from"));
            int64_t hi = parseIsoMs(req->getParameter("to"));
            auto t = summary.aggregate(lo, hi);

            result = {};
            result["default"]["totalRequests"] = static_cast<Json::Int64>(t.defaultCount);
            result["default"]["totalAmount"] = static_cast<Json::Int64>(t.defaultCents);
            result["fallback"]["totalRequests"] = static_cast<Json::Int64>(t.fallbackCount);
            result["fallback"]["totalAmount"] = static_cast<Json::Int64>(t.fallbackCents);
            json_dump("aggregate", result);

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get});

    drogon::app().registerHandler(
        "/payments",
        [&](const drogon::HttpRequestPtr &req,
            std::function<void(const drogon::HttpResponsePtr &)> &&callback) {

            auto body = req->getJsonObject();
            auto resp = drogon::HttpResponse::newHttpResponse();
            if (!body) {
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

            Payment order((*body)["correlationId"].asString(),
                    (*body)["amount"].asDouble(), &summary, &hc);
            Payment::enqueue(std::move(order));

            resp->setStatusCode(drogon::k202Accepted);
            callback(resp);
        },
        {drogon::Post});

    drogon::app().registerHandler(
        "/get-internal-stats",
        [](const drogon::HttpRequestPtr &req,
            std::function<void(const drogon::HttpResponsePtr &)> &&callback) {
            Json::Value result;
            auto [len, flight] = Payment::getStats();
            result["queue_size"] = len;
            result["load"] = flight;
            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get});

    drogon::app().getLoop()->runEvery(PAYMENT_DRAIN_RATE, []() {
        Payment::drainQueue();
    });

    std::cout << "http server started" << std::endl;
    drogon::app().run();

    return 0;
}
