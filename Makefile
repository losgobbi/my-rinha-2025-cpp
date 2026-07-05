CXX      ?= g++
CXXFLAGS = -std=c++20 -g
TARGET   = src/app
SRC      = src/main.cpp

DROGON_CFLAGS  = $(shell pkg-config --cflags drogon 2>/dev/null || echo "-I/usr/local/include -I/usr/include/jsoncpp")
DROGON_LIBS    = $(shell pkg-config --libs drogon 2>/dev/null || echo "-L/usr/local/lib -ldrogon -ltrantor -ljsoncpp -lssl -lcrypto -lpthread -lz -luuid -lbrotlidec -lbrotlienc -lsqlite3 -lpq")

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(DROGON_CFLAGS) $(SRC) -o $(TARGET) $(DROGON_LIBS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
