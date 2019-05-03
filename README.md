# Oatmeal Protocol - Control and communicate with Arduino devices from Python

**Oatmeal Protocol** provides a simple mechanism to autoconnect and control any Arduino-compatible microcontroller from Python over a UART serial port.

The protocol supports multiple data types including integers, strings, floats, booleans, lists, dictionaries and missing values (NULL/None/nil). It even supports nested and mixed type lists.

By using the Oatmeal libraries, developers don't need to develop yet-another bespoke protocol and all the code around parsing those messages -- and can instead focus on building cool devices faster.

Oatmeal was developed by the R&D Team @ Shield Dx and released under the Apache License 2.0. It is not an official Shield Dx product -- the code is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND. For more details please see the `LICENSE` file.

### Python Example

    from oatmeal import OatmealDevice

    class MyDevice(OatmealDevice):
        ROLE_STR = "MyDevice"

    board = MyDevice.find()
    print("Temperature: ", board.send_and_ack("TMPR").args[0])

### Arduino Example (C++)

    #define HARDWARE_ID_STR "MyDevice"
    #include "oatmeal_protocol.h"

    OatmealMsgReadonly msg;
    while (port.check_for_msgs(&msg)) {
      if (msg.is_opcode("TMPR")) {
        port.start("TMP", 'A');
        port.append(TEMP_SENSOR.read());
        port.append(OTHER_SENSOR.read());
        port.finish();
      }
    }

### Message structure

    >>> import oatmeal
    >>> msg = oatmeal.OatmealMsg("RUNR", 1.23, True, "Hi!", [1, 2], token='aa')
    >>> msg
    OatmealMsg('RUNR', (1.23, True, 'Hi!', [1, 2]), token='aa')
    >>> msg.encode()
    bytearray(b'<RUNRaa1.23,T,Hi!,[1,2]>}V')
    >>>

## Installation

On linux you need to run `sudo usermod -a -G dialout $USER` and then restart before using UART over USB. You may also have to install USB-to-UART drivers depending on the chip used.

### Oatmeal Python (3.5+) Library

Simply install our Python library using using pip:

    sudo pip3 install oatmeal

### Oatmeal Arduino (1.8+) Library

To install via the Arduino IDE, open the IDE and navigate to 'Sketch' -> 'Include Library' -> 'Manage Libraries...'. From there, search for 'oatmeal' and install the latest version.

To install without the Arduino IDE, download the library from the git repository and copy it into your Arduino libraries folder. For more info, see the [Arduino website](https://www.arduino.cc/en/guide/libraries "Arduino libraries").

## Minimal hardware set up

If you are running Python on a device that has a serial port, simply connect the serial port to the arduino device:

| Python device | Arduino device |
|---------------|----------------|
| Rx            | Tx             |
| Tx            | Rx             |
| GND           | GND            |

If you are running Python on a device that has a USB port, get a USB-to-UART bridge like Adafruit's [FTDI Friend](https://www.adafruit.com/product/284 "FTDI Friend") or Sparkfun's [FTDI Basic Breakout](https://www.sparkfun.com/products/9873 "FTDI Basic Breakout - 3.3V") and make the same 3 connections as shown above. Hardware flow control pins (DTR/CTS) are not used.

## Debugging

Oatmeal proxies messages over UDP to localhost ports 5551 (incoming messages) and 5552 (outgoing messages).

Listen to UDP messages with `socat`:

    # Listen to incoming UART messages
    socat -u udp-recv:5551 -
    # Listen to outgoing UART messages
    socat -u udp-recv:5552 -

## Issues, support and contributing

License: Apache v2.0 - see `license.txt`.

See CONTRIBUTING.md for guidelines on submitting issues and PRs.
