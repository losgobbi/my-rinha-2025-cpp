#pragma once
#include <drogon/drogon.h>
#include <iostream>
#include <unordered_map>

#include "utils.hpp"

#define DEFAULT_HC_WINDOW   5.0

class HealthCheck {
public:
    struct State {
        bool failing = false;
        int minResponseTime = 0;
        drogon::ReqResult res;

        std::string toString() const {
          return "failing=" + std::to_string(failing) + " minResponseTime=" + std::to_string(minResponseTime) + " res=" + std::to_string(static_cast<int>(res));
        }
    };

    HealthCheck() {
        defaultClient = drogon::HttpClient::newHttpClient(
            std::getenv("PROCESSOR_DEFAULT_URL") ?: "http://payment-processor-default:8080");
        fallbackClient = drogon::HttpClient::newHttpClient(
            std::getenv("PROCESSOR_FALLBACK_URL") ?: "http://payment-processor-fallback:8080");
    }

    ~HealthCheck() {}

    void check(const drogon::HttpClientPtr &client, const std::string &name, State &state) {
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setPath("/payments/service-health");
        client->sendRequest(req, [&](drogon::ReqResult result, const drogon::HttpResponsePtr &resp) {
            if (result == drogon::ReqResult::Ok) {
                auto body = resp->getJsonObject();
                if (body) {
                    json_dump("service-health", *body);
                    state.failing = (*body)["failing"].asBool();
                    state.minResponseTime = (*body)["minResponseTime"].asInt();
                    state.res = result;
                }
            } else {
                std::cout << name << " health check failed with " << result << std::endl;
                state.res = result;
            }
        });
        if (state.failing)
            std::cout << state.toString() << std::endl;
    }

    void startTimer() {
        drogon::app().getLoop()->runEvery(DEFAULT_HC_WINDOW, [this]() {
            check(defaultClient, "default", defaultState);
            check(fallbackClient, "fallback", fallbackState);
        });
    }

    // TODO, might check the lowest minResponseTime
    std::pair<drogon::HttpClientPtr, Processor> getAvailableState() {
        if (!defaultState.failing)
            return { defaultClient, Processor::Default };
        
        if (!fallbackState.failing)
            return { fallbackClient, Processor::Fallback };


        throw std::invalid_argument("no available endpoint");
    }

    void dumpHc() {
        std::cout << "default: " << defaultState.toString() << std::endl;
        std::cout << "fallback: " << fallbackState.toString() << std::endl;
    }

private:
    State defaultState;
    State fallbackState;
    drogon::HttpClientPtr defaultClient;
    drogon::HttpClientPtr fallbackClient;
};