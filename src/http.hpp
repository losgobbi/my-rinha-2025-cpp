#pragma once
#include <drogon/drogon.h>

#include "healthcheck.hpp"
#include "payment.hpp"
#include "utils.hpp"

#define THREAD_N    4
#define DROGON_PORT 8080
#define DROGON_IP   "0.0.0.0"

int https_start() {
    std::cout << "registering http server at " << DROGON_PORT << "..." << std::endl;
    drogon::app()
        .setLogLevel(trantor::Logger::kError)
        .addListener(DROGON_IP, DROGON_PORT)
        .setThreadNum(0/*THREAD_N*/);

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
                auto agg2 = resp->getJsonObject();
                std::cout << " get partial result " << result << " status " << status << std::endl;
                Json::Value paySummary = {};
                if (result == drogon::ReqResult::Ok) {
                    Summary::Totals m = aggi;
                    m.defaultCount += (*agg2)["default"]["totalRequests"].asInt64();
                    m.defaultCents += (*agg2)["default"]["totalAmount"].asInt64();
                    m.fallbackCount += (*agg2)["fallback"]["totalRequests"].asInt64();
                    m.fallbackCents += (*agg2)["fallback"]["totalAmount"].asInt64();

                    paySummary["default"]["totalRequests"] = m.defaultCount;
                    paySummary["default"]["totalAmount"] = m.defaultCents/100.0;
                    paySummary["fallback"]["totalRequests"] = m.fallbackCount;
                    paySummary["fallback"]["totalAmount"] = m.fallbackCents/100.0;
                    json_dump("aggregate", paySummary);
                } else {
                }
                
                auto t1 = std::chrono::system_clock::now();
                auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                std::cout << " get summary done with " << ms << std::endl;
                auto res = drogon::HttpResponse::newHttpJsonResponse(paySummary);
                cb(res);
            });
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
            if (!body) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

            try {
                auto [client, proc] = hc.getAvailableState();
                Payment order((*body)["correlationId"].asString(),
                              (*body)["amount"].asDouble(),
                              client, static_cast<uint8_t>(proc), &summary);
                order.process(callback);
            } catch (const std::invalid_argument& e) {
                std::cout << "....not available endpoints..." << std::endl;
            }
        },
        {drogon::Post});

    std::cout << "http server started" << std::endl;
    drogon::app().run();

    return 0;
}
