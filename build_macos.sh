#!/bin/bash
#cmake --build . --target clean &&
cmake .&& 
make -j4 && 
cd package && 
./osx.sh && 
cd ..
