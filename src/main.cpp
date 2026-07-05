#include <chrono>
#include <iostream>
#include <format>

#include "http.hpp"

int main(int argc, char *argv[])
{
    std::cout << std::unitbuf;

    https_start();
    while (true);
    return 0;
}