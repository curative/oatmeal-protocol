#!/usr/bin/env python3

# example_hardware.py
# Copyright 2019, Shield Diagnostics and the Oatmeal Protocol contributors
# License: Apache 2.0

"""
Simple example for controlling an LED and reading ADC values via an
Oatmeal device.
"""

import os
import sys
import time  # Used for time.sleep() to generate delay

# Append relative path to oatmeal
file_dir = os.path.abspath(os.path.dirname(__file__))  # noqa: E402
sys.path.append(os.path.join(file_dir, "..", "..", "python"))  # noqa: E402

from oatmeal import OatmealDevice, OatmealMsg


class MyDevice(OatmealDevice):
    """
    This class represents our device. We use it to send and receive messages
    from the device.
    """
    # ROLE_STR is the string used by the board to identify itself, it must
    # match the role given to OatmealPort in mydevice.cpp
    ROLE_STR = "MyDevice"

    def set_led_state(self, led_state):
        self.send_and_ack(OatmealMsg("LEDR", bool(led_state)))

    def get_adc_values(self):
        """
        Returns a list of two intgers representing the values of ADC0 and ADC1
        """
        resp = self.send_and_ack(OatmealMsg("ADCR"))
        return resp.args


def main():
    print("Looking for devices...")
    device = MyDevice.find()

    if device is None:
        raise SystemExit("Couldn't connect to device")

    print("Connected to: %r" % (device.details))

    print("Blinking LED 5 times...")
    for i in range(5):
        device.set_led_state(True)
        time.sleep(0.5)
        device.set_led_state(False)
        time.sleep(0.5)

    # Read value of both ADC pins
    ADC0_value, ADC1_value = device.get_adc_values()  # Returns a list of 2 values

    # Print ADC values (assumes 10-bit resolution like ATmega328p)
    print("ADC0 value: %i/1023" % ADC0_value)
    print("ADC0 value: %i/1023" % ADC1_value)

    # Shut down the device here
    device.stop()


if __name__ == "__main__":
    main()
