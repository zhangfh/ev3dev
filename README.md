# ev3dev development
1.ev3dev-buildscripts 
  # https://github.com/zhangfh/ev3dev-buildscripts/tree/ev3dev-jessie
  
  # If you haven't already added the ev3dev.org repository...
  sudo apt-add-repository "deb http://archive.ev3dev.org/ubuntu trusty main"
  sudo apt-key adv --keyserver pgp.mit.edu --recv-keys 2B210565
  sudo apt-get update
  
  # then install required packages
  sudo apt-get install git build-essential ncurses-dev fakeroot bc
  
  sudo apt-get install u-boot-tools lzop gcc-linaro-arm-linux-gnueabihf-5.2
  
  git clone git://github.com/ev3dev/ev3dev-buildscripts --branch ev3dev-jessie
2. kernel
  # https://github.com/zhangfh/ev3-kernel/tree/ev3dev-jessie
  git clone --recursive git://github.com/ev3dev/ev3-kernel --branch ev3dev-jessie --depth 50
  

