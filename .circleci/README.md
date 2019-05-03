# CircleCI Setup

Compiling embedded code on CircleCI requires Arduino and Teensyduino. To create the Arduino + Teensy asset, you need to:

1. Install Arduino on a linux computer (see https://www.arduino.cc/en/guide/linux)
2. Install Teensyduino on the computer (see https://www.pjrc.com/teensy/teensyduino.html)
3. Create a tar of the `/opt/arduino-X.Y.Z` directory e.g. `tar -zcvf arduino-X.Y.Z.tar.gz /opt/arduino-X.Y.Z`
4. Upload to s3: `aws s3 cp arduino-X.Y.Z.tar.gz s3://oatmeal-protocol-testing/`
