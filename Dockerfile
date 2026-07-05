FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    g++-14 cmake git make pkg-config \
    libjsoncpp-dev \
    libssl-dev \
    zlib1g-dev \
    libbrotli-dev \
    libpq-dev \
    uuid-dev \
    libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

RUN git clone --depth=1 --recurse-submodules https://github.com/drogonframework/drogon /tmp/drogon \
    && cmake -S /tmp/drogon -B /tmp/drogon/build -DCMAKE_CXX_COMPILER=g++-14 \
    && cmake --build /tmp/drogon/build -j$(nproc) \
    && cmake --install /tmp/drogon/build \
    && rm -rf /tmp/drogon

WORKDIR /app
COPY . .

RUN make clean && make CXX=g++-14

CMD ["./src/app"]
