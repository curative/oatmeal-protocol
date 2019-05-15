CXXFLAGS=-Wall -Wextra -std=c++11

OATMEAL_CPP_PATH=..

ARDUINO_FILES=$(wildcard $(OATMEAL_CPP_PATH)/*.cpp) $(wildcard $(OATMEAL_CPP_PATH)/*.h)

all: test_oatmeal_message

clean:
	rm -rf test_oatmeal_message

test: test_oatmeal_message
	./test_oatmeal_message

test_oatmeal_message: test_oatmeal_message.cpp $(OATMEAL_CPP_PATH)/oatmeal_message.h
	$(CXX) $(CXXFLAGS) -I$(OATMEAL_CPP_PATH) -o $@ $<

.PHONY: all clean test
