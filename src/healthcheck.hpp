#pragma once
#include <drogon/drogon.h>
#include <ostream>
#include <iostream>
#include <unordered_map>

#include "utils.hpp"
#include "global.hpp"

class HealthCheck {
public:
    struct State {
        bool failing = false;
        int minResponseTime = 0;
        drogon::ReqResult res;

        friend std::ostream& operator<<(std::ostream& os, const State& st) {
            os << "failing=" << st.failing
                << " minResponseTime=" << std::to_string(st.minResponseTime) 
                << " res=" + std::to_string(static_cast<int>(st.res));
            return os;
        }
    };

    HealthCheck() {
        defaultClient = drogon::HttpClient::newHttpClient(
            std::getenv("PROCESSOR_DEFAULT_URL") ?: "http://payment-processor-default:8080");
        fallbackClient = drogon::HttpClient::newHttpClient(
            std::getenv("PROCESSOR_FALLBACK_URL") ?: "http://payment-processor-fallback:8080");
        buildPools();
    }

    ~HealthCheck() {}

    void check(const drogon::HttpClientPtr &client, const std::string &name, State &state) {
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setPath("/payments/service-health");
        client->sendRequest(req, [name, &state, this](drogon::ReqResult result, const drogon::HttpResponsePtr &resp) {
            int status = resp ? static_cast<int>(resp->statusCode()) : -1;
#ifdef DEBUG_HC
            std::cout << "hc status=" << status
                    << " result=" << static_cast<int>(result)
                    << " body=" << (resp ? std::string(resp->getBody()) : "<null>")
                    << std::endl;
#endif
            state.res = result;
            if (result == drogon::ReqResult::Ok) {
                if (auto body = resp->getJsonObject()) {
                   state.failing = (*body)["failing"].asBool();
                   state.minResponseTime = (*body)["minResponseTime"].asInt();
                }
            } 
#ifdef DEBUG_HC
            std::cout << *this << std::endl;
#endif
        });
    }

    void buildPools() { 
        size_t n = drogon::app().getThreadNum(); 
        for (int i = 0; i < POOL_SIZE; ++i) {
            trantor::EventLoop* lp = drogon::app().getIOLoop(i % n);
            defaultClients.push_back(drogon::HttpClient::newHttpClient(std::getenv("PROCESSOR_DEFAULT_URL"), lp));
            fallbackClients.push_back(drogon::HttpClient::newHttpClient(std::getenv("PROCESSOR_FALLBACK_URL"), lp));
        }
    }

    void startTimer() {
        drogon::app().getLoop()->runEvery(DEFAULT_HC_WINDOW, [this]() {
            check(defaultClient, "default", defaultState);
            check(fallbackClient, "fallback", fallbackState);
        });
    }

    bool hasAvailable() const {
        return !defaultState.failing || !fallbackState.failing;
    }

    std::pair<drogon::HttpClientPtr, Processor> getAvailableState() {
        static std::atomic<size_t> rr{0};
        size_t i = rr++;

        if (defaultState.failing && fallbackState.failing)
            throw std::invalid_argument("no available endpoint");

        if (!defaultState.failing && fallbackState.failing) 
            return { defaultClients[i % defaultClients.size()], Processor::Default };

        if (!fallbackState.failing && defaultState.failing)
            return { fallbackClients[i % fallbackClients.size()], Processor::Fallback };

        if (fallbackState.minResponseTime < defaultState.minResponseTime)
            return { fallbackClients[i % fallbackClients.size()], Processor::Fallback };

        return { defaultClients[i % defaultClients.size()], Processor::Default };

#ifdef OLD
        if (!defaultState.failing)
            return { defaultClients[i % defaultClients.size()], Processor::Default };
        if (!fallbackState.failing)
            return { fallbackClients[i % fallbackClients.size()], Processor::Fallback };

        throw std::invalid_argument("no available endpoint");
#endif
    }

    friend std::ostream& operator<<(std::ostream& os, const HealthCheck& hc) {
        os << "default " << hc.defaultState << " fallback " << hc.fallbackState;
        return os;
    }

private:
    State defaultState;
    State fallbackState;
    drogon::HttpClientPtr defaultClient;
    drogon::HttpClientPtr fallbackClient;
    std::vector<drogon::HttpClientPtr> defaultClients;
    std::vector<drogon::HttpClientPtr> fallbackClients;
};