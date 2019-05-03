# Oatmeal Protocol Spec v1.0


## Section 1 - Universal Data Frame

The messages are designed to pass many data types (or a combination of data types) between the host and the device. Any baud rate that is accepted by both platforms can be used (we use 115,200 by default). The message structure is as follows:

### Section 1.1 - Example message

An example message, followed by a newline:

    <RUNRaa1.23,T,Hi!,[1,2]>}V\n

Example message breakdown:

| Length (bytes) | 1          | 3       | 1    | 2     | n                  | 1        | 1        | 1        |
|----------------|------------|---------|------|-------|--------------------|----------|----------|----------|
| Label          | Start byte | Command | Flag | Token | Arguments          | End byte | checklen | checksum |
| Example value  | `<`        | `RUN`   | `R`  | `aa`  | `1.23,T,Hi!,[1,2]` | `>`      | `}`      | `V`      |


### Section 1.3 - Components of a message

Frames are made up exclusively of bytes (see Section 1.8): `{1..255}` (inclusive). Messages consist of the following components:

#### Start Byte (Byte 0)

An unchanging byte which announces the beginning of a message `<` (60 in ASCII).

#### Command (Bytes 1-3)

Three valid ASCII characters (see Section 1.8) that determine what the message is about (Example: Motor could me `MOT`). Reserved commands are listed in Section 1.4.

#### Flag (Byte 4)

A single valid ASCII character (see Section 1.8) paired with the command (together the 'opcode'), gives more information about the request/response. The flag determines whether the message is a request to perform the command, an acknowledgement that the command was understood, or a notification that the command timed out, failed for some other reason, or was successfully completed. Reserved flags are listed in Section 1.5.

#### Token (Byte 5-6)

Incremented by the host, the token keeps track of which command a message is concerned with in the event that multiples of the same command have been sent. It is also used to indicate which message is being responded to (e.g. ACK'd). The token may use any valid ASCII characters as described in Section 1.8.

#### Arguments (Bytes 7-n)

Comma-separated list of arguments. Arguments can be a mix of the following types:

* integer e.g. `123`
* float e.g. `1.2` or `1.23e+08`
* boolean: `T` or `F`
* missing value (None/nil/NULL): `N`
* strings `"asdf"` (supports unicode, see Section 1.8)
* raw bytes `0"asqfa"` (see Section 1.8)
* lists (comma-separated) e.g. `[42,T,"hi",[1.2,101]]` - can contain any mix of types
* dictionaries e.g. `{order_price=12.3,prefs={John="spicy",Sally="mild"}}`. Dictionary keys are not quoted, are case-sensitive and can only contain the characters `a-z`, `A-Z`, `0-9`, `_`. Dictionary values can be any Oatmeal type including dictionaries and lists.


#### End Byte

An unchanging byte which announces the end of a message `>` (62 in ASCII).

#### Length checksum

A check byte which verifies the integrity of the message. See Section 1.6 for more info.

#### Checksum

A check byte which verifies the integrity of the message. See Section 1.6 for more info.

## Section 1.4 - Reserved commands

| Sender   | Message               | Command | Flag | Arguments                                                       | Args example            |
|----------|-----------------------|---------|------|-----------------------------------------------------------------|-------------------------|
| Request  | Discovery request     | `DIS`   | `R`  | None                                                            |                         |
| Response | Discovery ack         | `DIS`   | `A`  | `<role:str>,<instance_idx:int>,<hardware_id:str>,<version:str>` | `MyBoard,12,abc,0a9ef2` |
| Request  | Toggle Heartbeats     | `HRT`   | `R`  | `<heartbeats_on:bool>`                                          | `T`                     |
| Response | Heartbeat toggle ack. | `HRT`   | `A`  | None                                                            |                         |
| Any      | Heartbeat message     | `HRT`   | `B`  | `<key1=val1:str>,<key2=val2:str>`                               | `T=21.2,pos=1021`       |
| Request  | Toggle logging        | `LOG`   | `R`  | `<logging_on:bool>`                                             | `T`                     |
| Response | Logging toggle ack.   | `LOG`   | `A`  | None                                                            |                         |
| Any      | Logging message       | `LOG`   | `B`  | `<level:str>,<message:str>`                                     | `ERROR,No sensor found` |
| Request  | Halt / Reset          | `HAL`   | `R`  | None                                                            |                         |
| Response | Halt acknowledgment   | `HAL`   | `A`  | None                                                            |                         |

Normally `Request` will come from Python on the PC and `Respone` from the Arduino.

## Section 1.5 - Reserved flags

| Flag Type          | Char | Notes                                                 |
|--------------------|------|-------------------------------------------------------|
| Request            | R    | The host requests that the device do something.       |
| Acknowledgement    | A    | Request acknowledged as successful received.          |
| Failed             | F    | The device has failed to complete the host's request. |
| Done               | D    | The device successfully completed the host's request. |
| Background message | B    | Unsolicited message - see Section 1.7               |

## Section 1.6 - Checksum bytes

The checksum bytes are computed using the algorithm shown below, which returns a two ASCII characters. This code generates the checksums of a complete frame (bytes stored in an array `buf` of length `len`):

    uint8_t checklen;
    checklen = (((uint16_t)len * 7) % (127-33-2)) + 33;
    checklen += (checklen >= '<');
    checklen += (checklen >= '>');

    uint8_t checksum = 0;
    for(int i = 0; i < len-1; i++) { checksum = (checksum + buf[i]) * 31; }
    checksum = (checksum % (127-33-2)) + 33;
    checksum += (checksum >= '<');
    checksum += (checksum >= '>');

The last two bytes of the frame should be `checklen` and then `checksum`.

Frame examples with the checksums as the last two characters:

- `<DISRXY>i_`
- `<RUNRaa1.23,T,"Hi!",[1,2]>-b`
- `<XYZAzZ101,[0,42]>SH`
- `<LOLROh123,T,99.9>SS`


## Section 1.7 - Background messages: heartbeats, logging and updates

Unsolicited messages from devices such as heartbeats, logging and updates are a special class of messages called 'background messages'. Background messages are not acknowledged -- they are one way communications.

### Heartbeats

Devices can send heartbeat messages regularly with opcode `HRTB`, including any status data (e.g. sensor readings). The arguments of heartbeat messages are an arbitrary number of strings formatted as "key=value" pairs. For instance: `x=742.7,zl=0,zr=0,zc=30.6`.

### Log messages

Log messages use opcode `LOGB` and take two arguments:

- `level` (str): Log level as described in Python (https://docs.python.org/3/howto/logging.html): `DEBUG`, `INFO`, `WARNING`, `ERROR` and `CRITICAL`.
- `message` (str): The log message.

### Miscellaneous background messages: updates

Any other opcode with flag `B` (opcodes that end with a `B` e.g. `xxxB`).

One use case for this is as a mechanism for the devices to raise "events". For example, an Arduino hooked up to a motion sensor could immediately send a `MOTB` message every time it detects motion.


## Section 1.8 - Encodings

When encoding an argument as raw bytes, the data is surrounded by quotes and prefixed with a zero i.e. `0"..."`. The following characters are escaped (as ascii):

* `\` -> `"\\"`
* `"` -> `"\""`
* `<` -> `"\("`
* `>` -> `"\)"`
* `\n` -> `"\n"`
* `\r` -> `"\r"`
* `0x0` -> `"\0"`

Strings are encoded as utf-8 then encoded with the above scheme and surrounded by quotes i.e. `"hello"`:

    0"asdf" -> bytearray([97, 115, 100, 102])
    "asdf"  -> str("asdf")


Frames are made up of bytes 1..255 (inclusive). Certain 'special characters' may only be used in certains places:

- Characters '<' (60) and '>' (62) are only permitted when marking the frame start and end.
- Spaces ' ' (32) may only be used within strings/bytes in arguments

Commands, flags and tokens may use any of the following characters:

    >>> print(''.join(chr(c) for c in range(32, 127) if chr(c) not in ' <>'))
    !"#$%&'()*+,-./0123456789:;=?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_`abcdefghijklmnopqrstuvwxyz{|}~
