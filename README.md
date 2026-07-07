# Rinha de Backend 2025 — C++ / Drogon

A payment-intermediary backend for the [Rinha de Backend 2025](https://github.com/zanfranceschi/rinha-de-backend-2025) challenge.

## Stack

- **C++20**
- **[Drogon](https://github.com/drogonframework/drogon)** — asynchronous, event-loop based HTTP framework (built from source)
- **nginx** — load balancer
- **Docker / Docker Compose**
- **jsoncpp** — JSON parsing

## Architecture

```
                 ┌────────────┐
   client  ───▶  │ nginx (lb) │  :9999   round-robin, keep-alive
                 └─────┬──────┘
             ┌─────────┴─────────┐
             ▼                   ▼
        ┌─────────┐         ┌─────────┐
        │  api01  │◀───────▶│  api02  │   two identical C++/Drogon instances (:8080)
        └────┬────┘ summary └────┬────┘   (each queries the peer to merge the summary)
             │      merge        │
             └─────────┬─────────┘
                       ▼
        ┌──────────────────────────────┐
        │  payment-processor-default    │  (cheaper)
        │  payment-processor-fallback   │  (expensive, plan B)
        └──────────────────────────────┘
```

Resource limits (per the challenge rules): `api01`/`api02` — 0.6 CPU & 150 MB each,
`lb` — 0.2 CPU & 25 MB.