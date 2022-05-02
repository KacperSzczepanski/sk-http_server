CXX = g++ -std=c++17
CXXSOURCES = serwer.cpp
CXXFLAGS = -Wall -lstdc++fs

all: serwer

serwer: 
	$(CXX) $(CXXSOURCES) $(CXXFLAGS) -o serwer

.PHONY: clean
clean:
	rm -rf *.o serwer