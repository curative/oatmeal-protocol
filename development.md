# Development

Python set up and develop notes are in `python/README.md`.

Quickstart steps to compiling and running tests and generating all docs etc:

    brew install doxygen
    pip3 install -r requirements.txt
    make all
    make test

## Symlinking

You can copy the libraries onto your system or use relative soft symlinks to add them to the correct paths. On a Mac you need to use `gln` (GNU ln) instead of `ln` (bundled on Mac).

Symlinking C++ Arduino library:

    gln -rs src <YOUR_ARDUINO_LIBS_DIR>/oatmeal_protocol

## API documentation

To generate the docs, you need doxygen. To install doxygen on Mac:

    brew install doxygen

Generate the docs with:

    make docs

Docs will appear in `docs/html/index.html`.

## Arduino keywords

The Arduino IDE needs a list of C++ class and function names to do syntax highlighting, to generate we use python module `arduinokeywords`:

    pip3 install -r requirements.txt
    make keywords.txt

`keywords.txt` is tracked by git.

TODO(Isaac): Put this in the Doxygen docs:
### Compile time configuration

The following macros change the behaviour of the library. If you want to set any of these, then they must be set before importing `oatmeal_protocol.h` for the first time.

### `OATMEAL_HARDWARE_ID_STR`:

A string used to uniquely identify the hardware used. Only used if we cannot extract an identifer from the microchip itself. If this value is needed we suggest setting it to a random string each time compilation is run, so when you re-compile and flash many devices they each identify themselves differently. You can do this in a Makefile with:

    RAND_ID:=$(shell cat /dev/urandom | env LC_CTYPE=C tr -dc 'a-z0-9' | fold -w 8 | head -n 1)
    CPPFLAGS += -DOATMEAL_HARDWARE_ID_STR=$(RAND_ID)


### `OATMEAL_INSTANCE_IDX`:

The 'instance index' is a 32-bit unsigned integer used to differentiate between multiple boards, even those that have the same `role` string. For instance you might have flashed multiple chips with code to control motors, but you refer to your motor control chips as 0, 1 and 2 for controlling the x-axis, y-axis and z-axis motors respectively.

`OATMEAL_INSTANCE_IDX` is used by the calling code to specify the board role index when it can't use hardware (e.g. a dial) to determine the instance index. If it is not set it defaults to 0.


### `OATMEAL_VERSION_STR`:

A string used to identify the version of the code running on a chip. You can automatically set this to e.g. the full commit hash in a Makefile with:


    COMMIT:=$(shell git rev-parse HEAD)
    CPPFLAGS += -DOATMEAL_VERSION_STR=$(COMMIT)

If you prefer to use short commit hashes, you can use the following in your Makefile:

    COMMIT_SHORT:=$(shell git rev-parse --short HEAD)
    CPPFLAGS += -DOATMEAL_VERSION_STR=$(COMMIT_SHORT)
