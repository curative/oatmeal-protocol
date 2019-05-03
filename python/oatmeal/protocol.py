#!/usr/bin/env python3

# protocol.py
# Copyright 2019, Shield Diagnostics and the Oatmeal Protocol contributors
# License: Apache 2.0

from typing import Tuple, Dict, List, Sequence, Iterator, Iterable, Optional, \
                   Union, TypeVar, Type, ItemsView, Any
from abc import ABC, abstractmethod
import re
import os
import logging
import socket
import random
import string
import time
import platform
import warnings
import hashlib
import binascii
from enum import Enum

import serial

# from multiprocessing import Pipe, Event, Process
# Interactive version
from multiprocessing import Pipe
from multiprocessing.connection import Connection
from threading import Thread, Event, Lock


# Forward UART data over UDP to the following ports on localhost.
# Listen with:
#
#   socat -u udp4-recv:5551 -
#   socat -u udp4-recv:5552 -
#
# Set to None to turn off forwarding
#
MIRROR_IN_UDP_ADDR = ('127.0.0.1', 5551)
MIRROR_OUT_UDP_ADDR = ('127.0.0.1', 5552)

T = TypeVar('T')
S = TypeVar('S', bound='OatmealMsg')
ByteLike = Union[bytes, bytearray]
OatmealItem = Union[str, int, float, bool, None, ByteLike, list, tuple, dict]

OatmealTypes = (int, float, bool, str, type(None), bytes, bytearray,
                list, tuple, dict)

ESCAPING_BYTES = {ord('\\'): b'\\\\',
                  ord('"'): b'\\"',
                  ord('<'): b'\\(',
                  ord('>'): b'\\)',
                  ord('\n'): b'\\n',
                  ord('\r'): b'\\r',
                  ord('\0'): b'\\0'}

ESCAPED_BYTES = {ord('\\'): ord('\\'),
                 ord('"'): ord('"'),
                 ord('('): ord('<'),
                 ord(')'): ord('>'),
                 ord('n'): ord('\n'),
                 ord('r'): ord('\r'),
                 ord('0'): ord('\0')}


def _rand_choices(arr: Sequence, k: int) -> List:
    """ Equivalent to `random.choices(arr, k=k)` """
    return [arr[random.randint(0, len(arr)-1)] for _ in range(k)]


def _max_frame_hard_limit(max_frame_len: int) -> int:
    """ Get the hard limit of the longest frame we'll try to read.

    We need to enforce a maximum frame length so we don't read forever if we
    see a frame start byte followed by noise. We don't need to set this
    very low, but should be warned if the device is sending frames much
    bigger than we are expecting. Therefore we use 'soft' and 'hard' limits
    so that messages don't just disappear if the boards accidently sends one
    too long and we don't crash if we see noise.
    """
    return max_frame_len * 2


OATMEAL_BAUD_RATE = 115200
""" Baud rate used over UART by default by Oatmeal Protocol """


class OatmealError(Exception):
    """ Top level Oatmeal exception - all custom :mod:`oatmeal` Exceptions
    extend this class. """
    pass


class OatmealTimeout(OatmealError):
    """ Exception raised when timing out trying to read an Oatmeal message. """
    pass


class OatmealParseError(OatmealError):
    """ Error raised when a message cannot be parsed because it is invalid. """
    pass


class OatmealMsg:
    """
    Class representing an Oatmeal message and the underlying byte array that
    represents it (the 'frame').

    OatmealMsgs without a token will be given one by OatmealPort before trying
    to send them.

    Do not change any of the class constants.

    Args:
        opcode: four letter valid string (see OatmealMsg.is_valid_opcode())
        args: list of arguments (can be of mixed type)
        token: two letter valid string (see OatmealMsg.is_valid_token())

    Attributes:
        opcode (str): command+flag (4 char string)
        token (str): 2 char token string
        args (tuple): arguments of this command
        heartbeat (Optional[dict]): key=val string args into a dict if and only
           if this is a valid heartbeat message. Otherwise None.
    """
    MIN_FRAME_LEN = 10
    """ Minimum frame length. """

    DEFAULT_MAX_FRAME_LEN = 512
    """ Max frame length that the board is expecting to send/receive. """

    FRAME_START_BYTE = ord('<')
    FRAME_END_BYTE = ord('>')
    LIST_START_BYTE = ord('[')
    LIST_END_BYTE = ord(']')
    DICT_START_BYTE = ord('{')
    DICT_END_BYTE = ord('}')

    SEP_BYTE = ord(',')

    real_sig_figs = 6
    """ Number of significant figures to use when sending float/double values """

    def __init__(self, opcode: str, *args, token: str = None) -> None:
        self.opcode = opcode
        self.args = list(args)
        self.token = token
        self.heartbeat = OatmealMsg._parse_heartbeat(self)

    @staticmethod
    def _traverse_args(args: Iterable) -> Iterator[OatmealItem]:
        for arg in args:
            yield arg
            if isinstance(arg, dict):
                yield from OatmealMsg._traverse_args(arg.values())
            elif isinstance(arg, (list, tuple)):
                yield from OatmealMsg._traverse_args(arg)

    @property
    def traverse_args(self) -> Iterator[OatmealItem]:
        """ Iterator that returns elements from `self.args` one at a time,
        as a flat list. """
        yield from OatmealMsg._traverse_args(self.args)

    def validate(self) -> None:
        """ Run sanity checks to validate this instance """
        assert self.token is not None
        if not OatmealMsg.is_valid_opcode(self.opcode):
            raise ValueError("Bad opcode: %r" % (self.opcode))
        if self.token is None or not OatmealMsg.is_valid_token(self.token):
            raise ValueError("Bad token: %r" % (self.token))
        # Check the types of the args
        assert isinstance(self.args, list)
        for arg in self.traverse_args:
            assert isinstance(arg, OatmealTypes)
            # Validate dictionary keys
            if isinstance(arg, dict):
                assert all(OatmealMsg.is_valid_dict_key(k) for k in arg.keys())

    @property
    def command(self) -> str:
        """ The three character command string of this :class:`OatmealMsg`. """
        return self.opcode[0:3]

    @property
    def flag(self) -> str:
        """ The one character flag string of this :class:`OatmealMsg`. """
        return self.opcode[-1]

    @property
    def is_background_msg(self):
        """ Whether this is a background message (has flag 'B'). """
        return self.flag == OatmealProtocol.BACKGROUND_MSG_FLAG

    @staticmethod
    def _parse_heartbeat(msg: 'OatmealMsg') -> Optional[Dict[str, Any]]:
        """
        If this message doesn't have opcode `HRTB` immediately returns None.
        If this message has opcode `HRTB`, then check that it has a single arg
        that is a valid dictionary, and if it is return the dictionary of
        key-value pairs. Logs a warning if not a valid heartbeat and returns
        None.
        """
        if msg.opcode != 'HRTB':
            return None
        elif (len(msg.args) == 1 and isinstance(msg.args[0], dict) and
              all(OatmealMsg.is_valid_dict_key(k) for k in msg.args[0])):
            return msg.args[0]
        else:
            logging.warning("Invalid heartbeat message: %r", msg)
            return None

    @staticmethod
    def checkbyte_uint16_to_ascii(checksum: int) -> int:
        """
        Convert an integer into a printable ASCII value (one-way function).
        """
        checksum &= 0xffff
        checksum = (checksum % (127-33-2)) + 33
        checksum += (checksum >= OatmealMsg.FRAME_START_BYTE)
        checksum += (checksum >= OatmealMsg.FRAME_END_BYTE)
        return checksum

    @staticmethod
    def length_checksum(frame_len: int) -> int:
        """
        Calculate a length checkbyte for a given frame length.

        Note:
            Remember to call `checkbyte_uint16_to_ascii()` before using in a
            message.

        See also:
            * :func:`OatmealMsg.checkbyte_uint16_to_ascii()`
            * :func:`OatmealMsg.calc_checksum()`
        """
        return OatmealMsg.checkbyte_uint16_to_ascii(frame_len * 7)

    @staticmethod
    def calc_checksum(frame: Sequence[int]) -> int:
        """
        Calculate the checksum for a given list of bytes. Frame should not
        include a checksum byte. First and last byte of frame are not used
        in the checksum and should be tested separately.

        See also:
            * :func:`OatmealMsg.checkbyte_uint16_to_ascii()`
            * :func:`OatmealMsg.length_checksum()`
        """
        checksum = 0
        for c in frame:
            checksum = ((checksum + c) * 31) & 0xff
        return OatmealMsg.checkbyte_uint16_to_ascii(checksum)

    @staticmethod
    def is_valid_token(token: str) -> bool:
        """
        Valid tokens are two characters long. Characters may be any printable
        ASCII characters (no whitespace) except '<' and '>'.

        Returns:
            bool: True if the string is a valid Oatmeal token
        """
        # 33..126 inclusive are printable (non-whitespace) ASCII chars
        return (isinstance(token, str) and len(token) == 2 and
                all(33 <= ord(c) <= 126 and c not in ' <>' for c in token))

    @staticmethod
    def is_valid_opcode(opcode: str) -> bool:
        """
        Valid opcodes are four characters long. Characters may be any printable
        ASCII characters (no whitespace) except '<' and '>'.

        Returns:
            bool: True if the string is a valid Oatmeal opcode
        """
        # 33..126 inclusive are printable (non-whitespace) ASCII chars
        return (isinstance(opcode, str) and len(opcode) == 4 and
                all(33 <= ord(c) <= 126 and c not in ' <>' for c in opcode))

    @staticmethod
    def is_valid_dict_key(key: str) -> bool:
        """ Return True if the string passed is a valid dictionary key
        Dictionary keys must only use alphanumeric (a-z A-Z 0-9) and
        underscore _ characters (regex `[a-zA-Z0-9_]+`).

        Returns:
            bool: True if the string is a valid Oatmeal dictionary key
        """
        return (isinstance(key, str) and
                re.match("[a-zA-Z0-9_]+", key) is not None)

    @staticmethod
    def _encode_bytes(buf: ByteLike) -> bytes:
        """ Encode a bytes using Oatmeal string encoding """
        outbuf = bytearray(b'"')
        for b in buf:
            outbuf += ESCAPING_BYTES.get(b, bytes([b]))
        outbuf += b'"'
        return bytes(outbuf)

    @staticmethod
    def _decode_bytes(buf: ByteLike) -> Tuple[bytearray, int]:
        """ Decode a string/list of bytes from the start of `buf`.

        Returns:
            decoded bytes and the number of bytes consumed from `buf`.

        Raises:
            OatmealParseError: for invalid encodings
        """
        assert buf[0] == ord('"')
        decoded = bytearray()
        backslash_escaped = False
        # Start iterating after the open double quote
        for i, b in enumerate(buf[1:], start=1):
            if backslash_escaped:
                if b not in ESCAPED_BYTES:
                    raise OatmealParseError("Invalid escaped character "
                                            "'\\%s'" % (b))
                decoded.append(ESCAPED_BYTES[b])
                backslash_escaped = False
            elif b == ord('\\'):
                backslash_escaped = True
            elif b == ord('"'):
                return decoded, i+1
            else:
                decoded.append(int(b))
        raise OatmealParseError("String didn't end")

    @staticmethod
    def _decode_str(s: str) -> OatmealItem:
        """
        Try to convert a string into an int, float, or None, or fall back to
        string
        """
        # Try int (fails if there is a decimal point)
        try:
            return int(s)
        except ValueError:
            pass

        # Try float
        try:
            return float(s)
        except ValueError:
            pass

        # Try boolean
        if s == "T":
            return True
        if s == "F":
            return False

        if s == "N":
            return None

        # Received a string in future will want to raise instead of returning
        # raise OatmealParseError("Cannot parse item %r" % (s))

        return s

    @staticmethod
    def _parse_single_item(buf: bytes) -> Tuple[OatmealItem, int]:
        """
        Parse a single argument item from the start of bytes `buf`.

        Parse an item e.g. an integer or string from the start of `s` and
        return a Python object and the number of characters that were consumed
        to parse the object.

        Returns:
            The item and length consumed in characters
        """
        if len(buf) >= 2 and buf.startswith(b'"'):
            str_bytes, n_bytes = OatmealMsg._decode_bytes(buf)
            try:
                return str_bytes.decode('utf-8'), n_bytes
            except UnicodeDecodeError:
                raise OatmealParseError("Frame wasn't valid UTF-8")
        elif len(buf) >= 3 and buf.startswith(b'0"'):
            data_bytes, n_bytes = OatmealMsg._decode_bytes(buf[1:])
            return data_bytes, n_bytes+1  # +1 for '0' we skipped over
        else:
            i = 0
            while i < len(buf) and buf[i] not in b',]}':
                i += 1

            if i == 0:
                return None, 0
            else:
                return OatmealMsg._decode_str(buf[:i].decode('ascii')), i

    @staticmethod
    def _parse_dict(b: bytes) -> Tuple[Dict[str, OatmealItem], int]:
        """
        Parse a dict from the start of bytes `b`.

        Dict being parsed may be shorter than bytes `b`.

        Raises:
            OatmealParseError: if error whilst parsing

        Returns:
            The dict and length consumed in bytes
        """
        assert b[0] == OatmealMsg.DICT_START_BYTE
        d = {}  # type: Dict[str, OatmealItem]
        offset = 1
        while offset < len(b):
            # Check if dict ended
            if b[offset] == OatmealMsg.DICT_END_BYTE:
                return d, offset+1
            # Step over separator
            if len(d) > 0:
                if b[offset] != OatmealMsg.SEP_BYTE:
                    raise OatmealParseError("Missing separator")
                offset += 1
                if offset == len(b):
                    raise OatmealParseError("Dict never ended: %r" % (b))
            # Find end of key ('=')
            key_start = offset
            try:
                equal_sign_pos = b.index(ord('='), offset)
            except ValueError:
                raise OatmealParseError("Dict value is not a key: %r" % (b))
            try:
                key_name = b[key_start:equal_sign_pos].decode('ascii')
            except UnicodeDecodeError:
                raise OatmealParseError("Invalid dict key name: %r" % (b))
            if not OatmealMsg.is_valid_dict_key(key_name):
                raise OatmealParseError("Invalid dict key name: %r" % (b))
            val, n_val_bytes = OatmealMsg._parse_arg(b[equal_sign_pos+1:])
            offset = equal_sign_pos + 1 + n_val_bytes
            d[key_name] = val
        raise OatmealParseError("Dict never ended: %r" % (b))

    @staticmethod
    def _parse_list(b: bytes) -> Tuple[List[OatmealItem], int]:
        """
        Parse a list of arguments from the start of bytes `b`.

        List being parsed may be shorter than bytes `b`.

        Raises:
            OatmealParseError: if error whilst parsing

        Returns:
            The list and length consumed in bytes
        """
        assert b[0] == OatmealMsg.LIST_START_BYTE
        offset = 1
        obj_list = []  # type: List[OatmealItem]
        while offset < len(b):
            if b[offset] == OatmealMsg.LIST_END_BYTE:
                # end of this list
                return obj_list, offset+1
            # Step over separator
            if len(obj_list) > 0:
                if b[offset] != OatmealMsg.SEP_BYTE:
                    raise OatmealParseError("Missing separator")
                offset += 1
                # Note: we don't need to check that offset < len(s), since
                # we just saw ',' and the last char is ']' (see assert at top)
            # Look at next character
            obj, n_bytes = OatmealMsg._parse_arg(b[offset:])
            offset += n_bytes
            obj_list.append(obj)
        raise OatmealParseError("List never finished: %r" % (b))

    @staticmethod
    def _parse_arg(buf: bytes) -> Tuple[OatmealItem, int]:
        """ Parse a single argument (could be a int, str, list, dict etc.)
        from the start of the bytes parsed. May not consume all of the bytes.

        Raises:
            OatmealParseError: If cannot parse an argument

        Returns:
            The arg and the number of bytes consumed
        """
        obj = None  # type: OatmealItem
        if buf[0] == OatmealMsg.LIST_START_BYTE:
            obj, n_bytes = OatmealMsg._parse_list(buf)
        elif buf[0] == OatmealMsg.DICT_START_BYTE:
            obj, n_bytes = OatmealMsg._parse_dict(buf)
        else:
            obj, n_bytes = OatmealMsg._parse_single_item(buf)
        if n_bytes == 0:
            raise OatmealParseError("Missing/invalid item: %r" % (buf))
        return obj, n_bytes

    @staticmethod
    def _parse_args(buf: bytes) -> List[OatmealItem]:
        """ Parse the args of a message.

        Raises:
            OatmealParseError: If cannot parse args

        Returns:
            tuple: tuple containing the args
        """
        b = bytes([OatmealMsg.LIST_START_BYTE]) + buf + \
            bytes([OatmealMsg.LIST_END_BYTE])
        args, n_bytes = OatmealMsg._parse_list(b)
        if n_bytes < len(b):
            # Parsing ended early indicating that the list finished before the
            # end therefore there were surplus close brackets in the input
            raise OatmealParseError("Extra close bracket: %r" % (b))
        assert n_bytes == len(b), "%i != %i" % (n_bytes, len(b))
        return args

    @classmethod
    def decode(cls: Type[S], frame: bytearray, has_checksums=True) -> S:
        """
        Decode a valid frame (list of bytes) into opcode, token and arguments

        Raises:
            OatmealParseError: If cannot parse args

        Returns:
            OatmealMsg: OatmealMsg represented by the frame given
        """
        assert OatmealMsg.MIN_FRAME_LEN <= len(frame)

        try:
            opcode = frame[1:5].decode('ascii')
            token = frame[5:7].decode('ascii')
        except UnicodeDecodeError:
            raise OatmealParseError('Frame contained non-ASCII characters: %r'
                                    % frame)

        args_bytes = frame[7:-3] if has_checksums else frame[7:-1]
        args = OatmealMsg._parse_args(args_bytes)
        msg = cls(opcode, *args, token=token)
        msg.validate()
        return msg

    def _encode_val(self, x) -> bytes:
        """
        Convert a python object to a string representation. Nested lists
        are permitted. Call self.validate() before calling to ensure valid
        result.
        """
        if isinstance(x, (list, tuple)):
            args = b','.join(self._encode_val(i) for i in x)
            return b'[' + args + b']'
        elif isinstance(x, dict):
            args = b','.join(b'%s=%b' % (k.encode('ascii'), self._encode_val(v))
                             for k, v in sorted(x.items()))
            return b'{' + args + b'}'
        elif isinstance(x, bool):
            return b'T' if x else b'F'
        elif x is None:
            return b'N'
        elif isinstance(x, int):
            return str(x).encode('ascii')
        elif isinstance(x, float):
            return b'%.*g' % (self.real_sig_figs, x)
        elif isinstance(x, (bytearray, bytes)):
            return b'0' + OatmealMsg._encode_bytes(x)
        elif isinstance(x, str):
            return OatmealMsg._encode_bytes(x.encode('utf-8'))
        else:
            raise Exception("Programmer error?")

    def encode(self) -> bytearray:
        """
        Construct a frame (bytearray) representing this OatmealMsg
        """
        self.validate()
        assert self.token is not None
        args = b','.join(self._encode_val(x) for x in self.args)
        frame = bytearray()
        frame.append(OatmealMsg.FRAME_START_BYTE)
        frame += self.opcode.encode('ascii')
        frame += self.token.encode('ascii')
        frame += args
        frame_len = len(frame) + 3
        frame.append(OatmealMsg.FRAME_END_BYTE)
        frame.append(OatmealMsg.length_checksum(frame_len))
        frame.append(OatmealMsg.calc_checksum(frame))
        return frame

    @property
    def checksums(self) -> str:
        """
        Get the checksum chars of this packet by encoding it and returning the
        last two bytes.

        Returns:
            The two checksum bytes encoded as a string.
        """
        assert self.token is not None
        return self.encode()[-2:].decode('ascii')

    def __repr__(self) -> str:
        arg_strs = [
            repr(self.opcode),
            *(repr(arg) for arg in self.args),
            'token=%r' % self.token
        ]
        return "OatmealMsg(%s)" % ", ".join(arg for arg in arg_strs)

    def __eq__(self, other: Any) -> bool:
        """ Compare based on: opcode, args and token """
        if not isinstance(other, OatmealMsg):
            return NotImplemented
        return (self.opcode == other.opcode and
                self.args == other.args and
                self.token == other.token)

    def __lt__(self, other: Any) -> bool:
        """
        OatmealMsg objects are ordered by token, opcode then args
        """
        if not isinstance(other, OatmealMsg):
            return NotImplemented
        if self.token != other.token:
            # tokens can be None. None < x
            if self.token is not None and other.token is not None:
                return self.token < other.token
            else:
                return self.token is None
        if self.opcode != other.opcode:
            return self.opcode < other.opcode
        return self.args < other.args


class OatmealStats:
    """ Aggregate statistics about an :class:`OatmealPort` """

    def __init__(self) -> None:
        self.n_frame_too_short = 0
        self.n_frame_too_long = 0
        self.n_missing_start_byte = 0
        self.n_missing_end_byte = 0
        self.n_invalid_bytes = 0
        self.n_bad_checksums = 0
        self.n_misc_bad_frames = 0
        self.n_good_frames = 0

    def log_stats(self, level: int = logging.INFO) -> None:
        """ Log these statistics using the Python logging module.

        Args:
            level: Python :mod:`logging` level to log at.
        """
        logging.log(level, "Approximate UART received stats...")
        logging.log(level, "  # frame too long: %i", self.n_frame_too_short)
        logging.log(level, "  # frame too short: %i", self.n_frame_too_long)
        logging.log(level, "  # missing start byte: %i", self.n_missing_start_byte)
        logging.log(level, "  # missing end byte: %i", self.n_missing_end_byte)
        logging.log(level, "  # non-ascii bytes: %i", self.n_invalid_bytes)
        logging.log(level, "  # bad checksum: %i", self.n_bad_checksums)
        logging.log(level, "  # misc bad frames: %i", self.n_misc_bad_frames)
        logging.log(level, "  # good frames: %i", self.n_good_frames)


class OatmealDataMirror(ABC):
    """ Abstract class that describes a handler that mirrors data sent between
    this computer and the UART device it is communicating with. """

    @abstractmethod
    def incoming_data(self, data: bytes) -> None:
        """ Report data received from the device. """
        raise NotImplementedError

    @abstractmethod
    def outgoing_data(self, data: bytes) -> None:
        """ Report data sent to the device. """
        raise NotImplementedError

    @abstractmethod
    def close(self) -> None:
        """ Shutdown this object, release resources. """
        raise NotImplementedError


class OatmealUDPMirror(OatmealDataMirror):
    """ Mirror incoming and outgoing UART data over UDP for debugging.

    Args:
        udp_addr_in: (host address string, port number)-tuple to mirror
            incoming bytes to
        udp_addr_out: (host address string, port number)-tuple to mirror
            outgoing bytes to
    """

    def __init__(self,
                 udp_addr_in: Optional[Tuple[str, int]],
                 udp_addr_out: Optional[Tuple[str, int]]) -> None:
        self.udp_addr_in = udp_addr_in
        self.udp_addr_out = udp_addr_out
        self.udp_sock_incoming = None
        self.udp_sock_outgoing = None
        if udp_addr_in is not None:
            self.udp_sock_incoming = socket.socket(socket.AF_INET,
                                                   socket.SOCK_DGRAM)
        if udp_addr_out is not None:
            self.udp_sock_outgoing = socket.socket(socket.AF_INET,
                                                   socket.SOCK_DGRAM)

    def incoming_data(self, data: bytes) -> None:
        if self.udp_sock_incoming is not None:
            assert self.udp_addr_in is not None
            self.udp_sock_incoming.sendto(data, self.udp_addr_in)

    def outgoing_data(self, data: bytes) -> None:
        if self.udp_sock_outgoing is not None:
            assert self.udp_addr_out is not None
            self.udp_sock_outgoing.sendto(data, self.udp_addr_out)

    def close(self) -> None:
        if self.udp_sock_incoming is not None:
            self.udp_sock_incoming.close()
        if self.udp_sock_outgoing is not None:
            self.udp_sock_outgoing.close()


class _PortState(Enum):
    WAIT_ON_START = 0
    WAIT_ON_END = 1
    WAIT_ON_LENGTH = 2
    WAIT_ON_CHECKSUM = 3


class OatmealProtocol:
    """
    Class that handles reading and writing messages to/from a serial port.
    """

    BACKGROUND_MSG_FLAG = 'B'
    """ Messages with this flag will be sent to the background pipe (rather than
    the messages pipe) if OatmealPort.bg_msg_handling is set to
    `BgMsgRedirect.SEPARATE`, or discarded of set to
    `BgMsgRedirect.DISCARD`. They'll be sent to the messages pipe if
    bg_msg_handling is set to KEEP.

    By sending to the background pipe, they'll be handled by the background
    thread rather than the main thread. See :class:`OatmealBgMsgHandler` and
    :class:`BgMsgRedirect` for details.
    """

    @staticmethod
    def convert_frame(frame: bytearray, stats: OatmealStats,
                      max_frame_len: int) -> Optional[OatmealMsg]:
        """
        Check frame is valid, convert to OatmealMsg

        Args:
            frame: bytes that represent this Oatmeal Protocol frame
            stats: statistics to update while parsing this frame

        Returns:
            OatmealMsg or None on error
        """
        if len(frame) < OatmealMsg.MIN_FRAME_LEN:
            logging.warning("Frame too short: (%i < %i) %r",
                            len(frame), OatmealMsg.MIN_FRAME_LEN, frame)
            stats.n_frame_too_short += 1
            return None  # BAD frame: too short

        if len(frame) > max_frame_len:
            stats.n_frame_too_long += 1
            logging.warning("Frame too long: (%i > %i) %r",
                            len(frame), max_frame_len, frame)
            if len(frame) > _max_frame_hard_limit(max_frame_len):
                logging.warning("Discarding frame.")
                return None  # BAD frame: too long

        if frame[0] != OatmealMsg.FRAME_START_BYTE:
            logging.warning("Bad start byte: %r", frame)
            stats.n_missing_start_byte += 1
            return None  # BAD frame: missing start byte

        if frame[-3] != OatmealMsg.FRAME_END_BYTE:
            logging.warning("Bad end byte: %r", frame)
            stats.n_missing_end_byte += 1
            return None  # BAD frame: missing end byte

        checklen = OatmealMsg.length_checksum(len(frame))
        if frame[-2] != checklen:
            logging.warning("Bad checklen: %r", frame)
            stats.n_bad_checksums += 1
            return None  # BAD frame: checklen

        checksum = OatmealMsg.calc_checksum(frame[:-1])
        if frame[-1] != checksum:
            logging.warning("Bad checksum: %r", frame)
            stats.n_bad_checksums += 1
            return None  # BAD frame: checksum

        try:
            msg = OatmealMsg.decode(frame)
            stats.n_good_frames += 1
            return msg
        except OatmealParseError:
            logging.warning("Cannot parse frame: %r" % (frame), exc_info=True)
            stats.n_misc_bad_frames += 1

        return None

    @staticmethod
    def read_frame_loop(serial_port,
                        exit_token: Event, outgoing_msg_pipe: Connection = None,
                        data_mirror: OatmealDataMirror = None,
                        stats: OatmealStats = None,
                        max_frame_len: int = OatmealMsg.DEFAULT_MAX_FRAME_LEN) \
            -> Iterator[OatmealMsg]:
        """
        Looping UART read/write method. Called by a background process.
        Frames are read from the `serial_port`, parsed and yielded. Blocks
        until a message is read and yielded or `exit_token` is set.
        Outgoing frames (bytearrays) are read from `outgoing_msg_pipe` and
        written to the serial port.
        """
        # stats
        if stats is None:
            stats = OatmealStats()

        # Incoming frame is constructed in a buffer
        frame_in = bytearray()
        state = _PortState.WAIT_ON_START

        max_frame_len_hard = _max_frame_hard_limit(max_frame_len)

        while not exit_token.is_set():
            n = serial_port.in_waiting
            if n > 0:
                buf = serial_port.read(n)
                assert len(buf) >= n, "Serial error: %i < %i" % (len(buf), n)

                # Send input to data comms mirror
                if data_mirror is not None:
                    data_mirror.incoming_data(buf)

                # In case we're left reading rubbish forever, let's not fill up
                # the memory and crash
                if len(frame_in) > max_frame_len_hard:
                    logging.warning("Clearing UART input buffer (overflow): "
                                    "%i > %i", len(frame_in),
                                    max_frame_len_hard)
                    frame_in.clear()
                    state = _PortState.WAIT_ON_START
                    stats.n_frame_too_long += 1

                for b in buf:
                    if b == 0:
                        # Invalid byte -- clear input frame
                        frame_in.clear()
                        stats.n_invalid_bytes += 1
                        state = _PortState.WAIT_ON_START
                    elif b == OatmealMsg.FRAME_START_BYTE:
                        stats.n_missing_end_byte += \
                            (state != _PortState.WAIT_ON_START)
                        frame_in.clear()
                        frame_in.append(b)
                        state = _PortState.WAIT_ON_END
                    elif state == _PortState.WAIT_ON_START:
                        # Frame starting is handled in the condition above
                        # which checks for start bytes, just increment error
                        # counts instead
                        stats.n_missing_start_byte += (b == OatmealMsg.FRAME_END_BYTE)
                    elif state == _PortState.WAIT_ON_END:
                        # Reading bytes until we see the frame end byte
                        frame_in.append(b)
                        if b == OatmealMsg.FRAME_END_BYTE:
                            state = _PortState.WAIT_ON_LENGTH
                    elif state == _PortState.WAIT_ON_LENGTH:
                        # We've just read a byte to use as the length checksum
                        frame_in.append(b)
                        state = _PortState.WAIT_ON_CHECKSUM
                    elif state == _PortState.WAIT_ON_CHECKSUM:
                        # We've just read the checksum which completes a frame
                        frame_in.append(b)
                        msg = OatmealProtocol.convert_frame(frame_in, stats,
                                                            max_frame_len)
                        if msg is not None:
                            yield msg
                        frame_in.clear()
                        state = _PortState.WAIT_ON_START

            if outgoing_msg_pipe is not None and outgoing_msg_pipe.poll(0):
                # Append newline to frame before sending out
                frame_out = outgoing_msg_pipe.recv() + b'\n'
                try:
                    serial_port.write(frame_out)
                    if data_mirror is not None:
                        data_mirror.outgoing_data(frame_out)
                except serial.SerialTimeoutException:
                    warnings.warn("UART write timeout: %s" %
                                  (serial_port.name))

        # If we've opened an port that isnt one of our boards, flushing
        # may block for a while and is a bad idea.
        # Clearing buffers may improve closing speed but it's unclear.
        # serial_port.flush()
        # serial_port.reset_output_buffer()
        # serial_port.reset_input_buffer()

        if data_mirror is not None:
            data_mirror.close()


class BgMsgRedirect(Enum):
    """ How to handle background messages including heartbeats and logging
    messages """
    KEEP = 0  # Keep alongside other messages
    SEPARATE = 1  # Place into a separate pipe
    DISCARD = 2  # Discard


class _OatmealPortThread:
    """
    Thread reads from a port and places messages in a pipe(s) for consumption.
    """
    def __init__(self, serial_port,
                 *,
                 data_mirror: OatmealDataMirror = None,
                 bg_msg_handling: BgMsgRedirect = BgMsgRedirect.SEPARATE,
                 max_frame_len: int = OatmealMsg.DEFAULT_MAX_FRAME_LEN) \
            -> None:
        # incoming_packets queue used as threadsafe message passing queue
        msg_pipe_fg, msg_pipe_bg = Pipe(duplex=True)
        # background messages are referred to as 'other' messages in pipe
        # naming to avoid confusion about foreground/background connections
        # making up the pipe
        if bg_msg_handling != BgMsgRedirect.SEPARATE:
            other_pipe_fg, other_pipe_bg = None, None
        else:
            other_pipe_fg, other_pipe_bg = Pipe(duplex=False)
        discard_bg_msgs = bg_msg_handling == BgMsgRedirect.DISCARD
        self.bg_msg_handling = bg_msg_handling
        self.msg_pipe = msg_pipe_fg
        self.other_pipe = other_pipe_fg
        self.exit_token = Event()
        # Encapsulate a thread rather than extend to ensure we only pass
        # instance variables to the background thread that we intend to
        self.thread = Thread(target=_OatmealPortThread._read_msgs_loop,
                             args=(serial_port, self.exit_token, msg_pipe_bg),
                             kwargs=dict(other_pipe=other_pipe_bg,
                                         discard_bg_msgs=discard_bg_msgs,
                                         data_mirror=data_mirror,
                                         max_frame_len=max_frame_len),
                             daemon=True)  # die on program exit

    @staticmethod
    def _read_msgs_loop(serial_port, exit_token: Event,
                        msg_pipe: Connection,
                        *,
                        other_pipe: Connection = None,
                        discard_bg_msgs: bool = False,
                        data_mirror: OatmealDataMirror = None,
                        max_frame_len: int = OatmealMsg.DEFAULT_MAX_FRAME_LEN):
        """
        Looping UART read/write method. Called by a background process.
        Outgoing frames (bytearrays) are read from `msg_pipe` and written to
        the serial port. Frames are read from the `serial_port`, parsed,
        and placed in `msg_pipe` or `other_pipe` as :class:`OatmealMsg`.
        """
        # Don't forward signals to this process from the parent
        # (doesn't work on windows, so test)
        if platform.system() != 'Windows':
            try:
                os.setpgrp()
            except PermissionError:
                warnings.warn("Cannot call setpgrp()")
                pass

        stats = OatmealStats()
        msg_iter = OatmealProtocol.read_frame_loop(serial_port,
                                                   exit_token, msg_pipe,
                                                   data_mirror,
                                                   stats,
                                                   max_frame_len)

        for msg in msg_iter:
            if msg.flag == OatmealProtocol.BACKGROUND_MSG_FLAG:
                if other_pipe is not None:
                    other_pipe.send(msg)
                elif not discard_bg_msgs:
                    msg_pipe.send(msg)
            else:
                msg_pipe.send(msg)

        logging.info("Stopped reading/writing UART.")
        stats.log_stats()
        serial_port.close()

    def start(self) -> None:
        """
        Start the background thread reading and writing to the UART port
        """
        logging.info("Starting reading/writing UART.")
        self.thread.start()

    def stop(self) -> None:
        """
        Stop the background thread which closes the serial port
        """
        self.exit_token.set()
        self.thread.join()
        self.msg_pipe.close()
        if self.other_pipe is not None:
            self.other_pipe.close()

    def read_background_msg(self, timeout: float = None) -> Optional[OatmealMsg]:
        """
        Read a background message such as a heartbeat or logging message from
        the UART port, with some timeout.

        Args:
            timeout: time in seconds to wait for a message.

        Returns:
            `None` on timeout or if `bg_msg_handling` was not set to
            `BgMsgRedirect.SEPARATE` in the constructor otherwise returns
            :class:`OatmealMsg`.
        """
        # If we're not set up to separate out background messages, simply
        # return None.
        # In future we may want to raise a warning or Exception here
        if self.bg_msg_handling != BgMsgRedirect.SEPARATE:
            return None

        assert self.other_pipe is not None
        try:
            if self.other_pipe.poll(timeout):
                return self.other_pipe.recv()
        except EOFError:
            pass
        return None

    def read_msg(self, timeout: float = None) -> Optional[OatmealMsg]:
        """
        Read a message from the UART port, with some timeout.

        Returns:
            None on timeout otherwise returns OatmealMsg
        """
        if self.msg_pipe.poll(timeout):
            msg = self.msg_pipe.recv()
            return msg
        return None

    def send_msg(self, msg: OatmealMsg) -> None:
        """
        Send an OatmealMsg
        """
        self.msg_pipe.send(msg.encode())


class OatmealBgMsgHandlerBase(ABC):
    """
    Interface for handlers to which an OatmealPort can delegate the handling
    of HRTB and LOGB messages.
    """

    @property
    @abstractmethod
    def MAX_HEARTBEAT_GAP_SEC(self) -> Optional[float]:
        """
        Defines the maximum time that is permitted to pass without a heartbeat
        (or log message) before a heartbeat is considered to have been missed
        and :meth:`OatmealBgMsgHandler.missing_heartbeat()` is called.
        If set to None, no background messages are expected.
        """
        raise NotImplementedError

    @abstractmethod
    def handle_heartbeat(self, msg: OatmealMsg) -> None:
        """
        Handler called with every HRTB message received.
        """
        raise NotImplementedError

    @abstractmethod
    def handle_log_msg(self, msg: OatmealMsg) -> None:
        """
        Handler called with every LOGB message received.
        """
        raise NotImplementedError

    @abstractmethod
    def handle_misc_update(self, msg: OatmealMsg) -> None:
        """
        Handler called with every other xxxB message (those with flag='B' that
        are not 'HRTB' and 'LOGB') received.
        """
        raise NotImplementedError

    @abstractmethod
    def missing_heartbeat(self, time_passed_sec: float) -> None:
        """
        Handler called when a heartbeat is "missed" (i.e. none is received for
        max_gap_sec seconds).
        """
        raise NotImplementedError


class OatmealBgMsgHandler(OatmealBgMsgHandlerBase):
    """
    Implementation of the OatmealBgMsgHandlerBase interface that:

    - Logs log messages using the built-in :mod:`logging` module.
    - Exposes a `last_heartbeat` attribute by which the most recently received
      heartbeat message can be accessed.
    - Considers a heartbeat to have been lost after 5 seconds, and logs this.
    """

    MAX_HEARTBEAT_GAP_SEC = 5.0  # type: Optional[float]
    """ Call missing_heartbeat() if we don't see a heartbeat for longer than
    this time (in seconds). If set to None, do not raise call
    missing_heartbeat() if we don't see heartbeats. """

    def __init__(self, *,
                 board_name: str,
                 max_gap_sec: Optional[float] = MAX_HEARTBEAT_GAP_SEC) -> None:
        self.board_name = board_name
        self.last_heartbeat = None  # type: Optional[OatmealMsg]
        self.MAX_HEARTBEAT_GAP_SEC = max_gap_sec

    def handle_heartbeat(self, msg: OatmealMsg) -> None:
        """ Store a new heartbeat as `self.last_heartbeat` """
        self.last_heartbeat = msg  # atomic update

    def missing_heartbeat(self, time_passed_sec: float) -> None:
        """ Warn that we've not seen a heartbeat in a while. """
        logging.warning("[%s] heartbeat lost from device after %.1f seconds",
                        self.board_name, time_passed_sec)

    def handle_log_msg(self, msg: OatmealMsg) -> None:
        """
        For messages with opcode `LOGB`, log with the :mod:`logging` module.
        """
        if len(msg.args) != 2:
            logging.error('Unexpected # LOGB args: %i', len(msg.args))
        elif (not isinstance(msg.args[0], str)
              or not isinstance(msg.args[1], str)):
            logging.error('Unexpected LOGB arg types: (%r, %r)',
                          type(msg.args[0]), type([msg.args[1]]))
        else:
            log_level = logging.getLevelName(msg.args[0])  # type: ignore
            logging.log(log_level, '[%s] %s',  # type: ignore
                        self.board_name, msg.args[1])  # type: ignore

    def handle_misc_update(self, msg: OatmealMsg) -> None:
        """ Log message with :func:`logging.debug()` """
        logging.debug("[%s] misc update: %r" % (self.board_name, msg))


class OatmealDeviceDetails:
    """ Class representing the information that a device reports in response
    to a discovery request. """

    def __init__(self, role: str, instance_idx: int,
                 hardware_id: str, version: str) -> None:
        self.role = role
        self.instance_idx = instance_idx
        self.hardware_id = hardware_id
        self.version = version

    def items(self) -> ItemsView[str, object]:
        return dict(role=self.role,
                    instance_idx=self.instance_idx,
                    hardware_id=self.hardware_id,
                    version=self.version).items()

    @staticmethod
    def shorten_id(s: str) -> str:
        """ Convert a long string into short 6-hex-char string. """
        s_hash = hashlib.sha1(s.encode('ascii'))
        return binascii.hexlify(s_hash.digest())[-6:].decode('ascii')

    @property
    def short_hardware_id(self) -> str:
        """ Return a 6 character hex string representing a hash of
        `self.hardware_id`. """
        return OatmealDeviceDetails.shorten_id(self.hardware_id)

    @property
    def name(self) -> str:
        return "%s_%s_%s" % (self.role, self.instance_idx, self.hardware_id)

    def __repr__(self) -> str:
        return "%s(%s, %i, %s, %s)" % (self.__class__.__name__,
                                       self.role, self.instance_idx,
                                       self.hardware_id, self.version)


class OatmealPort:
    """ Connection to your device for sending and receiving messages.

    Create a new OatmealPort to listen for and send Oatmeal messages.

    Args:
        serial_port: port to send and receive Oatmeal messages over
        mirror_data: Whether the default data mirror should be used or
            a custom handler, or no data mirroring at all. If `True`, all
            incoming and outgoing UART bytes are also sent to localhost on
            UDP ports 5000 (incoming bytes) and 5001 (outgoing bytes).
            If `False`, no data mirroring occurs. If an custom
            :class:`OatmealDataMirror` is passed, that is used instead.
            :class:`OatmealDataMirror` methods are called by a background
            thread, so should not share resources with the main thread.
        bg_msg_handler: Handler for background messages (flag = 'B').
    """

    DEFAULT_ACK_TIMEOUT_SEC = 0.5
    DEFAULT_DONE_TIMEOUT_SEC = 1
    DEFAULT_N_RETRIES = 3

    def __init__(self, serial_port: serial.Serial, *,
                 mirror_data: Union[OatmealDataMirror, bool] = True,
                 max_frame_len: int = OatmealMsg.DEFAULT_MAX_FRAME_LEN,
                 bg_msg_handler: OatmealBgMsgHandler = None,
                 queue_bg_msgs: bool = False) -> None:
        """
        Create a new OatmealPort to listen for and send Oatmeal messages.

        Args:
            serial_port: port to send and receive Oatmeal messages over
            mirror_data: Whether the default data mirror should be used or
                a custom handler, or no data mirroring at all. If `True`, all
                incoming and outgoing UART bytes are also sent to localhost on
                UDP ports 5000 (incoming bytes) and 5001 (outgoing bytes).
                If `False`, no data mirroring occurs. If an custom
                `OatmealDataMirror` is passed, that is used instead.
                `OatmealDataMirror` methods are called by a background thread,
                so should not share resources with the main thread.
            bg_msg_handler: Handler for background messages (flag = 'B').
            queue_bg_msgs: set to `True` if you don't want to pass a handler but
                do want to call `read_bg_msg()`. It will queue up all background
                messages. If set to `True`, the caller _must_ call
                `read_bg_msg()` to clear the queue, otherwise memory will
                eventually be exhausted and python will crash.
        """
        assert max_frame_len > OatmealMsg.MIN_FRAME_LEN

        # issue sequential tokens, starting from a random value
        self.token_lock = Lock()
        self.tokenid = random.randrange(256)

        # This flag is toggled on and off as we enable and disable heartbeats
        # It allows us to keep track of when we should have seen a heartbeat but
        # haven't and therefore need to call missing_heartbeat()
        self.expect_heartbeats_token = Event()
        self.expect_heartbeats_token.set()

        # Set up where to mirror data to.
        # Default to UDP to localhost on default ports.
        data_mirror = None  # type: Optional[OatmealDataMirror]
        if mirror_data is True:
            data_mirror = OatmealUDPMirror(MIRROR_IN_UDP_ADDR,
                                           MIRROR_OUT_UDP_ADDR)
        elif mirror_data is not False:
            assert isinstance(mirror_data, OatmealDataMirror)
            data_mirror = mirror_data

        # Figure out if we are keeping or discarding heartbeats
        assert not (queue_bg_msgs and (bg_msg_handler is not None))
        self.queue_bg_msgs = queue_bg_msgs

        if bg_msg_handler is not None or queue_bg_msgs:
            bg_msg_handling = BgMsgRedirect.SEPARATE
        else:
            bg_msg_handling = BgMsgRedirect.DISCARD

        # Port
        # (during testing serial_port may be None)
        self.serial_path = (serial_port.name if serial_port is not None
                            else None)
        self.uart_port = _OatmealPortThread(
            serial_port,
            data_mirror=data_mirror,
            bg_msg_handling=bg_msg_handling,
            max_frame_len=max_frame_len
        )

        # Set up beackground messsages (e.g. heartbeats) handling thread
        self.bg_stop = None  # type: Optional[Event]
        self.bg_thread = None   # type: Optional[Thread]

        if bg_msg_handler is not None:
            self.bg_stop = Event()
            self.bg_thread = Thread(
                target=OatmealPort._read_background_msgs,
                args=(self.bg_stop,
                      self.uart_port,
                      self.expect_heartbeats_token,
                      bg_msg_handler),
                daemon=True)  # die on program exit

        # stats
        self.n_missed_acks = 0
        self._start()

    def get_path(self) -> str:
        """ Get path for this serial port device """
        return self.serial_path

    def _start(self) -> None:
        """
        Start listening/sending packets and handling background messages
        with `bg_msg_handler`
        """
        self.uart_port.start()
        if self.bg_thread is not None:
            self.bg_thread.start()

    def stop(self) -> None:
        """
        Shutdown port
        """
        if self.bg_thread is not None:
            assert self.bg_stop is not None
            # Stop reading bg messages
            self.bg_stop.set()
            self.bg_thread.join()
        # Stop reading from the UART port
        self.uart_port.stop()

    @staticmethod
    def _read_background_msgs(stop_token: Event, uart_port: _OatmealPortThread,
                              expect_heartbeats_token: Event,
                              bg_msg_handler: OatmealBgMsgHandler) -> None:
        """
        Loop reading background messages and invoking the handler.

        Background messages ('B' flag, including heartbeats and log messages)
        are sent from devices without requesting them and such messages are
        not acknowledged. This function loops forever reading these messages.

        Args:
            bg_msg_handler: is an :class:`OatmealBgMsgHandler` object to which
                we delegate handling the messages we read.
        """
        last_hb_time = time.time()
        triggered_warning = False

        while not stop_token.is_set():
            # How long should we block for waiting for a background message?
            # No longer than the max gap expected between heartbeats so that
            # we can call missing_heartbeat(). Also don't want to block for
            # too long incase the stop_token has been set.
            max_gap_sec = bg_msg_handler.MAX_HEARTBEAT_GAP_SEC or 0.1
            max_gap_sec = min(max_gap_sec, 0.1)

            msg = uart_port.read_background_msg(timeout=max_gap_sec)
            if msg is not None:
                if msg.opcode == 'HRTB':
                    bg_msg_handler.handle_heartbeat(msg)
                    last_hb_time = time.time()
                elif msg.opcode == 'LOGB':
                    bg_msg_handler.handle_log_msg(msg)
                else:
                    bg_msg_handler.handle_misc_update(msg)
                triggered_warning = False

            time_passed = time.time() - last_hb_time
            if (bg_msg_handler.MAX_HEARTBEAT_GAP_SEC is not None and
                    time_passed > bg_msg_handler.MAX_HEARTBEAT_GAP_SEC and
                    not triggered_warning and
                    expect_heartbeats_token.is_set()):
                bg_msg_handler.missing_heartbeat(time_passed)
                triggered_warning = True

        logging.info("Stopped reading heartbeats")

    def send(self, msg: OatmealMsg) -> None:
        """
        Blocking method sends an OatmealMsg

        Args:
            msg: OatmealMsg to send. If `msg.token` is not set, this port will
                 set it to the next token to be used by this port.
        """
        if msg.token is None:
            msg.token = self.next_token()
        self.uart_port.send_msg(msg)

    def send_and_ack(self, msg: OatmealMsg,
                     ackcode: str = None,
                     timeout: float = DEFAULT_ACK_TIMEOUT_SEC,
                     n_retries: int = DEFAULT_N_RETRIES) \
            -> OatmealMsg:
        """
        Send a message and block until we get the ack.

        Args:
            ackcode: The expected opcode (command+flag) of the ack message
                If omitted we use the command+'A' (ack flag)
            timeout: How many seconds to wait for an ACK
            n_retries: How many times to retry sending

        Returns:
            The ACK packet
        """
        if ackcode is None:
            assert msg.flag != 'A'
            ackcode = msg.command + 'A'

        for _ in range(n_retries+1):
            # Send the message out
            self.send(msg)

            # Read the ACK (with timeout)
            try:
                res = self.read(timeout=timeout)
                if res.opcode != ackcode or res.token != msg.token:
                    raise OatmealError(
                        "Expected ACK with opcode %s and token %s but got "
                        "response %r which has opcode %s and token %s"
                        % (ackcode, msg.token, res, res.opcode, res.token))
                else:
                    return res  # Return the ACK
            except OatmealTimeout:
                pass
            logging.debug("Missed ack: %r", msg)
            self.n_missed_acks += 1
            # Set new token
            msg.token = self.next_token()

        raise OatmealTimeout("No ACK! (%s retries, %s timeout)" % (
                          str(n_retries), str(timeout)))

    def send_and_done(self, msg: OatmealMsg,
                      ackcode: str = None,
                      donecode: str = None,
                      ack_timeout: float = DEFAULT_ACK_TIMEOUT_SEC,
                      done_timeout: float = DEFAULT_DONE_TIMEOUT_SEC,
                      n_ack_retries: int = DEFAULT_N_RETRIES) \
            -> Tuple[OatmealMsg, OatmealMsg]:
        """
        Send a message and block until we get the ack AND the done.

        Args:
            ackcode: The expected opcode (command+flag) of the ack message
                If omitted we use the command+'A' (ack flag)
            donecode: The expected opcode (command+flag) of the done message
                If omitted we use the command+'D' (done flag)
            ack_timeout: How many seconds to wait for an ACK
            done_timeout: How many seconds to wait for a DONE
            n_ack_retries: How many times to retry sending

        Returns:
            Tuple of the ACK and DONE packets
        """
        ack_msg = self.send_and_ack(msg, ackcode, ack_timeout, n_ack_retries)

        if donecode is None:
            donecode = msg.command + 'D'

        try:
            done_msg = self.read(timeout=done_timeout)
            if done_msg.token == ack_msg.token and done_msg.opcode == donecode:
                return ack_msg, done_msg
            else:
                raise OatmealError(
                    "Expected DONE with opcode %s and token %s but got "
                    "response %r" % (donecode, ack_msg.token, done_msg))
        except OatmealTimeout:
            logging.error("Timeout waiting for done: %r, %r", msg, ack_msg,
                          exc_info=True)
            raise OatmealTimeout("No done message (timeout: %fs): %r" % (
                done_timeout, ack_msg))

    def read(self, timeout: float = None) -> OatmealMsg:
        """
        Blocking method reads one message

        Raises:
            An OatmealTimeout exception if timeout reached

        Returns:
            An OatmealMsg
        """
        msg = self.uart_port.read_msg(timeout)
        if msg is None:
            assert timeout is not None  # this must be true if we timed out
            raise OatmealTimeout("Timeout after %f seconds" % (timeout))
        return msg

    def try_read(self, timeout: float = None) -> Optional[OatmealMsg]:
        """
        Blocking method reads one message

        Returns:
            An OatmealMsg or None if timedout
        """
        return self.uart_port.read_msg(timeout)

    def read_bg_msg(self, timeout: float = None) -> Optional[OatmealMsg]:
        """ Read a background message.

        Blocks until a message is read, unless timeout is defined, in which
        case case returns None on timeout.

        Only permitted when the user has not specified a background message
        handler.
        """
        assert self.bg_thread is None and self.queue_bg_msgs
        return self.uart_port.read_background_msg(timeout=timeout)

    def flush(self) -> None:
        """ Clear input buffer """
        # Read until there are no bytes left to read
        while True:
            try:
                self.read(timeout=0)
            except OatmealTimeout:
                break

    def expect(self, opcode: str,
               error_dict: Dict[str, Exception] = None,
               timeout: float = None) -> OatmealMsg:
        """
        Read a single message, expecting a given opcode.
        If not received, raise the error from error_dict[msg.opcode] if defined
        otherwise raise an OatmealError

        Raises:
            OatmealError: If unexpected error is received.
        """
        msg = self.read(timeout=timeout)
        if msg.opcode == opcode:
            return msg
        elif error_dict is not None and msg.opcode in error_dict:
            raise error_dict[msg.opcode]
        else:
            raise OatmealError("Unexpected message: "+str(msg))

    def next_token(self, n: int = 2) -> str:
        """
        Generate a token to use in a packet
        """
        letters = string.ascii_letters
        # Randomly generated
        # return ''.join(_rand_choices(letters, n))
        # Sequential tokens
        with self.token_lock:
            t = self.tokenid
            self.tokenid = (self.tokenid + 1) % (len(letters)**n)
        return letters[t // len(letters)] + letters[t % len(letters)]

    def ask_who(self, timeout: float = 1, n_retries: int = 2) \
            -> OatmealDeviceDetails:
        """
        Query device details.

        Returns:
            Details about the device

        Raises:
            OatmealError: If gets an unexpected response from the board
        """
        command = OatmealMsg("DISR")
        ack = self.send_and_ack(command, timeout=timeout, n_retries=n_retries)
        if len(ack.args) != 4:
            raise OatmealError("Bad response: %r" % (ack))
        role, instance_idx, hardware_id, version = ack.args
        return OatmealDeviceDetails(role, instance_idx, hardware_id, version)
