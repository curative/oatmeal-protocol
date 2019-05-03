#!/usr/bin/env python3

# device.py
# Copyright 2019, Shield Diagnostics and the Oatmeal Protocol contributors
# License: Apache 2.0

from typing import Union, Tuple, Dict, List, Type, TypeVar, Optional
import serial.tools.list_ports
from serial import SerialException
import logging
import time
import functools
import serial
import os

from .protocol import OatmealMsg, OatmealPort, OatmealBgMsgHandler, \
                      OatmealDeviceDetails, OATMEAL_BAUD_RATE, \
                      OatmealError, OatmealTimeout, OatmealDataMirror


OatmealDevice_T = TypeVar('OatmealDevice_T', bound='OatmealDevice')


class DeviceError(OatmealError):
    """ Error raised by an Oatmeal Device. """
    pass


class OatmealDevice(OatmealBgMsgHandler):
    """ Create the device, start the port listening / sending packets. """

    ROLE_STR = None  # type: Optional[str]
    """ The 'role' string that device of this kind are expected to respond to a
    discovery request with. """

    BAUD_RATE = OATMEAL_BAUD_RATE
    """ Baud rate to use to communicate with this device. """

    MAX_FRAME_LEN = OatmealMsg.DEFAULT_MAX_FRAME_LEN
    """ Max frame length for this device """

    MAX_HEARTBEAT_GAP_SEC = None  # type: Optional[float]
    """ Warn if the time between heartbeats is greater than this time (seconds).
    If set to None, don't warn if no heartbeats seen. """

    def __init__(self, *,
                 details: OatmealDeviceDetails,
                 port: OatmealPort = None,
                 serial_fh: serial.Serial = None,
                 mirror_data: Union[OatmealDataMirror, bool] = True,
                 max_frame_len: int = MAX_FRAME_LEN) -> None:
        assert (serial_fh is None) != (port is None)
        self.details = details
        super().__init__(board_name=details.role)
        if port is not None:
            self.port = port
        else:
            self.port = OatmealPort(serial_fh,
                                    mirror_data=mirror_data,
                                    bg_msg_handler=self,
                                    max_frame_len=max_frame_len)

    @classmethod
    def create(cls: Type[OatmealDevice_T], uart_path: str,
               details: OatmealDeviceDetails, *,
               baud_rate: int = OATMEAL_BAUD_RATE) -> OatmealDevice_T:
        """
        Connect to peripheral represented by a class that extends
        :class:`OatmealDevice`.

        Args:
            details: Details about the board we are connecting to - the value
                     returned by :meth:`OatmealPort.ask_who()`

        Returns:
            A new instance of the class.
        """
        serial_fh = serial.Serial(uart_path, baud_rate,
                                  timeout=0, exclusive=True)
        return cls(serial_fh=serial_fh, details=details)

    def get_path(self) -> str:
        """ Get path used to connect to this device """
        return self.port.get_path()

    def get_name(self) -> str:
        """ Get a string that represents this device.

        See also:
            :attr:`OatmealDeviceDetails.name`
        """
        return self.details.name

    def send(self, msg: OatmealMsg) -> None:
        """ Wrapper for :meth:`OatmealPort.send()` """
        self.port.send(msg)

    def send_and_ack(self, msg: OatmealMsg,
                     ackcode: str = None,
                     timeout: float = OatmealPort.DEFAULT_ACK_TIMEOUT_SEC,
                     n_retries: int = OatmealPort.DEFAULT_N_RETRIES) \
            -> OatmealMsg:
        """ Wrapper for :meth:`OatmealPort.send_and_ack()` """
        return self.port.send_and_ack(msg, ackcode,
                                      timeout=timeout,
                                      n_retries=n_retries)

    def send_and_done(self, msg: OatmealMsg,
                      ackcode: str = None,
                      donecode: str = None,
                      ack_timeout: float = OatmealPort.DEFAULT_ACK_TIMEOUT_SEC,
                      done_timeout: float = OatmealPort.DEFAULT_DONE_TIMEOUT_SEC,
                      n_ack_retries: int = OatmealPort.DEFAULT_N_RETRIES) \
            -> Tuple[OatmealMsg, OatmealMsg]:
        """ Wrapper for :meth:`OatmealPort.send_and_done()` """
        return self.port.send_and_done(msg, ackcode, donecode, ack_timeout,
                                       done_timeout, n_ack_retries)

    def read(self, timeout: float = None) -> OatmealMsg:
        """ Wrapper for :meth:`OatmealPort.read()` """
        return self.port.read(timeout=timeout)

    def try_read(self, timeout: float = None) -> Optional[OatmealMsg]:
        """ Wrapper for :meth:`OatmealPort.try_read()` """
        return self.port.try_read(timeout=timeout)

    def flush(self) -> None:
        """ Clear Oatmeal input buffer of messages """
        self.port.flush()

    def stop(self) -> None:
        """ Stop the threads reading messages from this device. """
        self.port.stop()
        logging.info("Stopped %s on %s", self.get_name(), self.get_path())

    def ask_who(self, timeout: float = 1,
                n_retries: int = 3) -> OatmealDeviceDetails:
        """
        Send a discovery request to the board for details about it.

        Args:
            timeout: timeout on discovery requests to the device
            n_retries: number of retries when asking querying this device
        """
        return self.port.ask_who(timeout=timeout, n_retries=n_retries)

    def halt(self) -> None:
        """
        Halt whatever the device is doing
        """
        command = OatmealMsg("HALR")
        self.port.send_and_done(command, "HALA", "HALD")

    def toggle_heartbeats(self, send_heartbeats: bool) -> None:
        """
        Toggle whether the device should send heartbeats
        """
        command = OatmealMsg("HRTR", send_heartbeats)
        self.port.send_and_ack(command, "HRTA")
        # Toggle whether or not we expect heartbeats and therefore if
        # missing_heartbeat() should be called when we don't see one for a while
        if send_heartbeats:
            self.port.expect_heartbeats_token.set()
        else:
            self.port.expect_heartbeats_token.clear()

    @staticmethod
    def haltable(func):
        """ Function decorator to halt a function on Control-C """
        @functools.wraps(func)
        def haltable_func(self, *args, **kwargs):
            try:
                return func(self, *args, **kwargs)
            except (KeyboardInterrupt) as ki:
                print("Halting...")
                self.halt()
                time.sleep(1)
                self.flush()
                raise ki
        return haltable_func

    @classmethod
    def find(cls: Type[OatmealDevice_T], **kwargs) -> Optional['OatmealDevice_T']:
        """ Find and connect to an instance of this class

        Args:
            kwargs: see :func:`detect_all_devices()`

        Returns:
            An instance of this class or None
        """
        if 'fast_search' not in kwargs:
            kwargs['fast_search'] = True
        if 'baud_rate' not in kwargs:
            kwargs['baud_rate'] = cls.BAUD_RATE

        d = find_devices([cls], **kwargs)
        if cls.ROLE_STR not in d:
            return None
        assert isinstance(cls.ROLE_STR, str)
        role_device = d[cls.ROLE_STR]
        assert isinstance(role_device, OatmealDevice)
        assert isinstance(role_device, cls)
        role_device.toggle_heartbeats(True)
        return role_device

    @classmethod
    def find_all(cls: Type[OatmealDevice_T], **kwargs) -> List['OatmealDevice_T']:
        """ Find and connect to instances of this class

        Args:
            kwargs: see `detect_all_devices()`

        Returns:
            A list of instances of this class or None
        """
        if 'baud_rate' not in kwargs:
            kwargs['baud_rate'] = cls.BAUD_RATE

        d = detect_all_devices([cls], **kwargs)
        if cls.ROLE_STR not in d:
            return []
        assert isinstance(cls.ROLE_STR, str)
        role_devices = d[cls.ROLE_STR]
        for device in role_devices:
            role_device = d[cls.ROLE_STR]
            assert isinstance(role_device, OatmealDevice)
            assert isinstance(role_device, cls)
            role_device.toggle_heartbeats(True)
        return role_devices


def open_device(uart_path: str,
                device_map: Dict[str, Type[OatmealDevice_T]],
                *,
                baud_rate: int = None,
                raise_on_unknown_role: bool = False,
                extras: dict = None) -> Optional[OatmealDevice_T]:
    """
    Opens UART port, detects device, creates+returns appropriate class instance

    Args:
        uart_path: device path to open a serial connection at
        device_map: map of role_str values to python classes to instantiate
            for each given string.
        extras: key-value pairs to pass to the device constructor
        baud_rate: serial baud rate to use
        raise_on_unknown_role: if `true` and device `role` is not in the
            device_map passed, raise an :exc:`OatmealError`.

    Raises:
        OatmealError: on failure or OatmealTimeout on timeout

    Returns:
        Instantiated class representing the new device or None
    """
    baud_rate = OATMEAL_BAUD_RATE if baud_rate is None else baud_rate

    # read timeout of zero means non-blocking
    serial_fh = serial.Serial(uart_path, baud_rate,
                              timeout=0, write_timeout=0.1,
                              exclusive=True)

    if not serial_fh.readable() or not serial_fh.writable():
        serial_fh.close()
        return None

    port = OatmealPort(serial_fh)

    # Query the device
    logging.debug("Querying device...")
    try:
        device_details = port.ask_who(timeout=0.1, n_retries=1)
    except OatmealTimeout:
        port.stop()
        return None

    port.stop()

    cls = device_map.get(device_details.role, None)
    if cls is None:
        if raise_on_unknown_role:
            raise OatmealError("Unknown device %r not in %r" % (
                               device_details.role, sorted(device_map)))
        return None

    serial_fh = serial.Serial(uart_path, baud_rate, timeout=0, exclusive=True)
    extras = extras if extras else {}
    return cls(details=device_details, serial_fh=serial_fh, **extras)


def _prioritise_serial_path(path: str) -> int:
    """ Approximate ordering over paths based on how likely they are to point
    to a USB -> UART adapter. This speeds up device discovery when looking for
    a single device (see `fast_search` parameter in `find_devices()`). """
    basename = os.path.basename(path).lower()
    if "usb" in basename:
        return 0
    elif basename.startswith("tty"):
        return 1
    else:
        return 2


def _device_list_to_map(device_list: List[Type[OatmealDevice_T]]) \
        -> Dict[str, Type[OatmealDevice_T]]:
    """ Given a list of OatmealDevice classes, generate a dict that maps from
    role string to class. Role string is the ROLE_STR class variable of each
    class. """
    for device_cls in device_list:
        if 'ROLE_STR' not in device_cls.__dict__ or device_cls.ROLE_STR is None:
            raise Exception("Class %s doesn't define 'ROLE_STR'" % (
                            device_cls.__name__))

    return {device_cls.ROLE_STR: device_cls  # type: ignore
            for device_cls in device_list}


def detect_all_devices(device_list: List[Type[OatmealDevice_T]],
                       *,
                       baud_rate: int = None,
                       fast_search: bool = False,
                       extras: dict = None) \
        -> Dict[str, List[OatmealDevice_T]]:
    """
    Autodetect and open Oatmeal devices.

    Open all Serial devices that report having a role that is a key in the dict
    passed.

    Args:
        device_list: list of device classes to find and initialise
        baud_rate: baud rate to use
        fast_search: return as soon as we find at least one of each device
        extras: key-value pairs to pass to the device constructor

    Returns:
        dict: Mapping role to a list of devices opened::

                {'MotorBoard': [board1, board2, ...],
                 'SensorBoard': [sensor],
                 ...}

    """
    # Can alternatively fall back to globbing (requires `import glob`) with:
    # ports = glob.glob("/dev/ttyUSB[0-9]*")
    ports = [port.device for port in serial.tools.list_ports.comports()]

    if fast_search:
        # Speed up detection by trying most likely ports first
        ports = sorted(ports, key=_prioritise_serial_path)

    device_map = _device_list_to_map(device_list)
    devices_by_role = {}  # type: Dict[str, List[OatmealDevice_T]]

    for port in ports:
        logging.debug("Trying to connect to %s..." % (port))
        d = None
        try:
            d = open_device(port,
                            device_map=device_map,
                            baud_rate=baud_rate,
                            raise_on_unknown_role=False,
                            extras=extras)
        except (IOError, OSError, EOFError, BlockingIOError, SerialException) as err:
            # Silently catch connection errors thrown by pyserial
            #  e.g. when the port is already held exclusively
            # Some of these may be platform dependent and change based on
            # pyserial version. These are exceptions we have seen in the past.
            # e.g. OSError is raised when doing fnctl/ioctl system calls on linux
            logging.debug("Failed to connect to '%s': %r", port, err)

        if d is not None:
            role = d.details.role
            devices_by_role.setdefault(role, []).append(d)

            if fast_search and len(devices_by_role) == len(device_map):
                # stop search as soon as we have at least one of each board
                break

    return devices_by_role


def find_devices(device_list: List[Type[OatmealDevice_T]], **kwargs) \
        -> Dict[str, OatmealDevice_T]:
    """
    Find a single instance of each device type. Filters down to roles given.
    Returns {'role1': board, ...}. Skips over devices not found.

    Args:
        device_list: list of device classes to find and initialise
        **kwargs: see :func:`detect_all_devices()`

    Raises:
        DeviceError: if multiple devices found with same role.

    Returns:
        dict: Mapping role to a device::

                {'MotorBoard': board,
                 'SensorBoard': sensor,
                 ...}

    """
    # Find and connect to all devices
    all_devices = detect_all_devices(device_list, **kwargs)

    single_devices = {}  # type: Dict[str, OatmealDevice_T]

    # Check we don't have more than one instance of each board
    for role, boards in all_devices.items():
        if len(boards) > 1:
            raise DeviceError("Too many boards with role '%s' (%i)" % (
                              role, len(boards)))

        single_devices[role] = boards[0]

    return single_devices


def close_devices(*args: OatmealDevice_T) -> None:
    """ Release devices.

    Call stop on all devices passed that are not `None`. """
    for board in args:
        if board is not None:
            board.stop()
