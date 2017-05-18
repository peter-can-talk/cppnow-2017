#!/bin/bash

DYLD_LIBRARY_PATH=~/Documents/Projects/llvm/build/lib \
PYTHONPATH=~/Documents/Projects/llvm/tools/clang/bindings/python \
python cppgrep.py $@
