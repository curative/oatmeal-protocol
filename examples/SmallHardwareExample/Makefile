#
# Set this paths / board details
#

ARDMK_DIR = ../../Arduino-Makefile
USER_LIB_PATH = .
OBJDIR = build
BOARD_TAG = uno
ARDUINO_LIBS = oatmeal_protocol

#
# End of params
#

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    ARDUINO_DIR = /Applications/Arduino.app/Contents/Java
else
    ARDUINO_DIR = $(shell ls -d -1 /opt/arduino-* | sort -t. -r -k1,1nr -k2,2nr -k3,3nr -k4,4nr | head -1)
endif

# Add extra compile warnings
CFLAGS=-Wall -Wextra -Wno-old-style-declaration
CPPFLAGS=-Wall -Wextra
CXXFLAGS=-Wall -Wextra -Wno-c++14-compat

COMMIT_SHORT:=$(shell git rev-parse --short HEAD)
FLASH_ID:=$(shell cat /dev/urandom | env LC_CTYPE=C tr -dc 'a-z0-9' | fold -w 8 | head -n 1)

CPPFLAGS += -DOATMEAL_VERSION_STR="$(COMMIT_SHORT)" -DOATMEAL_HARDWARE_ID_STR="$(FLASH_ID)"

ifeq ($(BOARD_TAG),teensy36)
	# Special case handling for Teensy boards: set a preprocessor variable that
	# we can use in our code to detect that we're on Teensy, and use the
	# special .mk file for Teensy boards instead of the generic Arduino.mk
	CPPFLAGS += -DTEENSY36
	include $(ARDMK_DIR)/Teensy.mk
else
	include $(ARDMK_DIR)/Arduino.mk
endif
