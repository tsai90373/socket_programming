CXX = g++
CXXFLAGS = -std=c++17 -Wall -g

all: server client

server: tcp_server.cpp
	$(CXX) $(CXXFLAGS) -o server tcp_server.cpp

client: tcp_client.cpp
	$(CXX) $(CXXFLAGS) -o client tcp_client.cpp

clean:
	rm -f server client