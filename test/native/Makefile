# Native (host) unit tests for the DataNet Arduino SDK.
#
#   make deps    # fetch the single-header ArduinoJson amalgamation
#   make test    # build and run
#   make clean
#
# The SDK is compiled for its generic-Ethernet transport, which is the one
# branch that needs no vendor board core. ESP and WiFiNINA branches are covered
# by the arduino-cli compile matrix in CI instead.

ARDUINOJSON_VERSION ?= 7.4.3
THIRD_PARTY         := third_party
ARDUINOJSON_HEADER  := $(THIRD_PARTY)/ArduinoJson.h
ARDUINOJSON_URL     := https://github.com/bblanchon/ArduinoJson/releases/download/v$(ARDUINOJSON_VERSION)/ArduinoJson-v$(ARDUINOJSON_VERSION).h

SRC_DIR := ../../src
BUILD   := build
BIN     := $(BUILD)/datanet-tests

CXX      ?= c++
CXXFLAGS += -std=c++17 -g -O1 -Wall -Wextra -Wno-unused-parameter
CPPFLAGS += -I. -Ishims -I$(THIRD_PARTY) -I$(SRC_DIR) -DDATANET_ENABLE_TEST_ACCESS

# Point ArduinoJson at the shim's String/Print/Stream so the test build
# exercises the same integration path a real board compiles. PROGMEM is off:
# the shim collapses F() to plain RAM literals.
CPPFLAGS += -DARDUINOJSON_ENABLE_ARDUINO_STRING=1 \
            -DARDUINOJSON_ENABLE_ARDUINO_STREAM=1 \
            -DARDUINOJSON_ENABLE_ARDUINO_PRINT=1 \
            -DARDUINOJSON_ENABLE_PROGMEM=0

SOURCES := \
	main.cpp \
	shims/Arduino.cpp \
	$(SRC_DIR)/DataNet.cpp \
	$(wildcard test_*.cpp)

OBJECTS := $(addprefix $(BUILD)/,$(notdir $(SOURCES:.cpp=.o)))

.PHONY: test build deps clean

test: build
	@./$(BIN)

build: $(ARDUINOJSON_HEADER) $(BIN)

deps: $(ARDUINOJSON_HEADER)

$(ARDUINOJSON_HEADER):
	@mkdir -p $(THIRD_PARTY)
	@echo "Fetching ArduinoJson v$(ARDUINOJSON_VERSION)..."
	@curl -fsSL -o $@ $(ARDUINOJSON_URL)

$(BIN): $(OBJECTS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD)/%.o: %.cpp $(ARDUINOJSON_HEADER)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/%.o: shims/%.cpp $(ARDUINOJSON_HEADER)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/%.o: $(SRC_DIR)/%.cpp $(ARDUINOJSON_HEADER)
	@mkdir -p $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -rf $(BUILD)
