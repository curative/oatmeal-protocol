"""
`oatmeal` is a Python implementation of the Oatmeal Protocol. Oatmeal is a
simple way to connect and talk to embedded electronics over Serial UART
communications. It supports multiple python data types (strings, ints, floats,
bools, None, dictionaries and lists). All data types can be nested inside lists
and dictionaries.
"""

from .protocol import \
    OatmealError, OatmealTimeout, OatmealParseError, \
    OatmealMsg, OatmealStats, OatmealProtocol, \
    OatmealBgMsgHandlerBase, OatmealBgMsgHandler, \
    OatmealDeviceDetails, OatmealPort, OATMEAL_BAUD_RATE
from .device import OatmealDevice, DeviceError, \
                    find_devices, detect_all_devices, \
                    open_device, close_devices

name = "oatmeal"

__all__ = [
    "OatmealError",
    "OatmealTimeout",
    "OatmealParseError",
    "OatmealMsg",
    "OatmealStats",
    "OatmealProtocol",
    "OatmealBgMsgHandlerBase",
    "OatmealBgMsgHandler",
    "OatmealDeviceDetails",
    "OatmealPort",
    "OATMEAL_BAUD_RATE",
    "DeviceError",
    "OatmealDevice",
    "find_devices",
    "detect_all_devices",
    "open_device",
    "close_devices",
]
