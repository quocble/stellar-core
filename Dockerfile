FROM ubuntu:trusty

RUN sudo apt-get update
RUN sudo apt-get install -y software-properties-common python-software-properties
RUN sudo add-apt-repository ppa:ubuntu-toolchain-r/test
RUN sudo apt-get update
RUN sudo apt-get -y install git build-essential pkg-config autoconf automake libtool bison flex libpq-dev clang++-3.5 gcc-4.9 g++-4.9 cpp-4.9
