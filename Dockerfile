FROM ubuntu
MAINTAINER <peter@goldsborough.me>

# Install packages.
RUN apt-get update  -y \
 && apt-get install -y git cmake vim make wget gnupg libz-dev

# Get LLVM apt repositories.
RUN wget -O - 'http://apt.llvm.org/llvm-snapshot.gpg.key' | apt-key add - \
 && echo 'deb http://apt.llvm.org/xenial/ llvm-toolchain-xenial-4.0 main' \
    >> /etc/apt/sources.list \
 && echo 'deb-src http://apt.llvm.org/xenial/ llvm-toolchain-xenial-4.0 main' \
    >> /etc/apt/sources.list

# Install clang.
RUN apt-get update -y && apt-get install -y \
  llvm-4.0 llvm-4.0-dev clang-4.0 clang-4.0-dev python-clang-4.0

ENV C clang-4.0
ENV CXX clang++-4.0
ENV PATH /usr/lib/llvm-4.0/bin/:${PATH}

# Symbolically link for convenience
RUN ln -s /usr/lib/llvm-4.0/ /llvm

# These volumes should be mounted on the host.
VOLUME /home/
WORKDIR /home
