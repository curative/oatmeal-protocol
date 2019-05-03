#!/bin/bash

set -eou pipefail  # bash strict mode

# Base of the Machines repo
BASEDIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && cd .. && pwd )"

apt-get update
apt-get install -y apt-transport-https software-properties-common
pip3 install -U pip

apt-get install -y doxygen
# Requirements for developing C++ and python Oatmeal code and docs
pip3 install -r $BASEDIR/requirements.txt

apt-get install awscli

# Install Arduino build tools
apt-get install -y gcc-avr gdb-avr binutils-avr avr-libc avrdude socat build-essential
if ! [ -e /opt/arduino-1.8.2 ]
then
  if ! [ -e arduino-1.8.2.tar.gz ]
  then
    echo "Trying to fetch Arduino build tools"
    if ! aws s3 cp s3://oatmeal-protocol-testing/arduino-1.8.2.tar.gz .
    then
      echo "Please run (on your host machine):"
      echo "  cd $BASEDIR/.circleci"
      echo "  aws s3 cp s3://oatmeal-protocol-testing/arduino-1.8.2.tar.gz ."
      exit -1
    fi
  fi
  tar xfvz arduino-1.8.2.tar.gz
  sudo mv arduino-1.8.2 /opt/
fi
