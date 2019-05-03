# Running the mydevice.ino example
In order to run the oatmeal `mydevice.ino` example, your system needs to know where to find `oatmeal_arduino.h` and `oatmeal_protocol.h`. This can be done easily one of two ways:
    1) Copy both libraries into your arduino 'libraries' directory
    2) Use symbolic links (symlinks) to point the the git directory from your arduino 'libraries' directory.

## Symlinks on Mac or Linux

Symlinking C++ Arduino libraries:

    gln -rs oatmeal_protocol/src/oatmeal_protocol <YOUR_ARDUINO_LIBS_DIR>/oatmeal_protocol

Symlinking python libraries:

    gln -rs oatmeal_protocol/python/oatmeal <YOUR_PYTHON_LIBS_DIR>/oatmeal

## Symlinks on Windows

- Follow this great how-to from howtogeek.com: https://www.howtogeek.com/howto/16226/complete-guide-to-symbolic-links-symlinks-on-windows-or-linux/ to understand how to use symlinks.

- Navigate to your Arduino 'libraries' folder. This is usually C:\Program Files (x86)\Arduino\libraries.

- Create a symlinks there:
    - oatmeal-protocol/src

- Create another symlink on your python libraries folder:
    - oatmeal-protocol/python/oatmeal

## Flashing the .ino example
Simply open the `mydevice.ino` sketch in the Arduino IDE, select your board and programmer, and click the 'Upload' button ('->'' arrow).

Then run the example python script:

    python3 ide_example.py
