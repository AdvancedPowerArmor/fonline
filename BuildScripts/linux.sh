#!/bin/bash -ex

[ "$FO_ROOT" ] || { echo "FO_ROOT variable is not set"; exit 1; }
[ "$FO_BUILD_DEST" ] || { echo "FO_BUILD_DEST variable is not set"; exit 1; }

export ROOT_FULL_PATH=$(cd $FO_ROOT; pwd)

export EMSCRIPTEN_VERSION="sdk-1.38.15-64bit"

if [[ -z "$FO_INSTALL_PACKAGES" ]]; then
	sudo apt-get -y update || true
	sudo apt-get -y install build-essential
	sudo apt-get -y install cmake
	sudo apt-get -y install wput
	sudo apt-get -y install libx11-dev
	sudo apt-get -y install freeglut3-dev
	sudo apt-get -y install libssl-dev
	sudo apt-get -y install libevent-dev
	sudo apt-get -y install libxi-dev
fi

mkdir -p $FO_BUILD_DEST
cd $FO_BUILD_DEST
mkdir -p linux
cd linux
rm -rf Client/*
rm -rf Server/*
rm -rf Mapper/*
rm -rf ASCompiler/*

cd Server
cp -r "$ROOT_FULL_PATH/BuildScripts/emsdk" "EmscriptenLinux"
cd EmscriptenLinux
chmod +x ./emsdk
./emsdk update
./emsdk install $EMSCRIPTEN_VERSION
./emsdk activate $EMSCRIPTEN_VERSION
cd ../
cd ../

mkdir -p x64
cd x64
cmake -G "Unix Makefiles" -C "$ROOT_FULL_PATH/BuildScripts/linux64.cache.cmake" "$ROOT_FULL_PATH/Source"
make -j4
cd ../

# x86 (Temporarily disabled)
#if [[ -z "$FO_INSTALL_PACKAGES" ]]; then
#	sudo apt-get -y install gcc-multilib
#	sudo apt-get -y install g++-multilib
#	sudo apt-get -y install libx11-dev:i386
#	sudo apt-get -y install freeglut3-dev:i386
#	sudo apt-get -y install libssl-dev:i386
#	sudo apt-get -y install libevent-dev:i386
#	sudo apt-get -y install libxi-dev:i386
#fi

#mkdir -p x86
#cd x86
#cmake -G "Unix Makefiles" -C "$ROOT_FULL_PATH/BuildScripts/linux32.cache.cmake" "$ROOT_FULL_PATH/Source"
#make -j4
#cd ../
