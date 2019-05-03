#!/usr/bin/env python3

# getset_example.py
# Copyright 2019, Shield Diagnostics and the Oatmeal Protocol contributors
# License: Apache 2.0

"""
Very simple example of autodetecting an Oatmeal peripheral and talking to it.
The peripheral stores the values of three variables (`x`, `y`, `z`) which store
integer values that can be set/fetched from python over UART.
"""

import logging
import os
import sys

# Append relative path to oatmeal
file_dir = os.path.abspath(os.path.dirname(__file__))  # noqa: E402
sys.path.append(os.path.join(file_dir, "..", "..", "python"))  # noqa: E402

from oatmeal import OatmealDevice, OatmealMsg


# Enable logging to STDOUT
logging.basicConfig(format='%(asctime)s %(levelname)s: %(message)s',
                    level=logging.DEBUG)


class MyDevice(OatmealDevice):
    """
    This class represents our device. We use it to send and receive messages
    from the device.
    """
    # ROLE_STR is the string used by the board to identify itself, it must
    # match the role given to OatmealPort in mydevice.cpp
    ROLE_STR = "MyDevice"

    def set_val(self, name, val):
        self.send_and_ack(OatmealMsg("SETR", name, val))

    def get_val(self, name):
        resp = self.send_and_ack(OatmealMsg("GETR", name))
        return resp.args[0]

    def get_all_vals(self):
        resp = self.send_and_ack(OatmealMsg("FETR"))
        return resp.args

    def handle_heartbeat(self, msg: OatmealMsg) -> None:
        # This method is called each time we receive a heartbeat from the device
        print("Got heartbeat: %r" % (msg))


def main():
    print("Looking for devices...")
    device = MyDevice.find()

    if device is None:
        raise SystemExit("Couldn't connect to device")

    print("Connected to: %r" % (device.details))

    # Construct and send a message to the board
    # Set values x, y, z
    device.set_val("x", 101)
    device.set_val("y", -123)
    device.set_val("z", 42)

    # Fetch values x, y, z
    x_val = device.get_val("x")
    y_val = device.get_val("y")
    z_val = device.get_val("z")

    print("x = %r" % (x_val))
    print("y = %r" % (y_val))
    print("z = %r" % (z_val))

    # Get all values at once
    all_vals = device.get_all_vals()
    print("x = %r, y = %r, z = %r" % (all_vals[0], all_vals[1], all_vals[2]))

    # Shut down the device here
    device.stop()


if __name__ == "__main__":
    main()
