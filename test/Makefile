CXX      = g++
CXXFLAGS = -std=c++14 -Wall -Wextra -Wno-unused-parameter

all: release_check run_tests test_queue
	./run_tests
	./test_queue

release_check:
	$(MAKE) -C ../TempBot verify_release

run_tests: run_tests.cpp
	$(CXX) $(CXXFLAGS) run_tests.cpp -o run_tests

queue: test_queue
	./test_queue

test_queue: test_queue.cpp
	$(CXX) $(CXXFLAGS) test_queue.cpp -o test_queue

clean:
	rm -f run_tests test_queue

.PHONY: all queue clean release_check
