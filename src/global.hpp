#pragma once
#include <stdint.h>

/* healthcheck */
#define DEFAULT_HC_WINDOW 2.5

/* drogon */
#define THREAD_N 1
#define DROGON_PORT 8080
#define DROGON_IP "0.0.0.0"
#define POOL_SIZE 16

/* payment */
#define PAYMENT_DRAIN_RATE 0.01
#define MAX_IN_FLIGHT 64
#define PAYMENT_SUM_TIMEOUT 1.0

/* misc */
enum class Processor : uint8_t { Default = 0, Fallback = 1 };
