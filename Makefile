
CXX = g++
CXXFLAGS = -std=c++17 -O2
LIBS = -lncurses

all: sysmon

sysmon: sysmon.cpp
	$(CXX) $(CXXFLAGS) sysmon.cpp -o sysmon $(LIBS)

clean:
	rm -f sysmon
