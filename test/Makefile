CXX      = g++
CXXFLAGS = -std=c++14 -Wall -Wextra -Wno-unused-parameter

all: run_tests test_queue
	./run_tests
	./test_queue

run_tests: run_tests.cpp
	$(CXX) $(CXXFLAGS) run_tests.cpp -o run_tests

queue: test_queue
	./test_queue

test_queue: test_queue.cpp
	$(CXX) $(CXXFLAGS) test_queue.cpp -o test_queue

clean:
	rm -f run_tests test_queue

.PHONY: all queue clean
