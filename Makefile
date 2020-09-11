RELEASE_FLAGS=-static -s
LINUX_HEADER_x86_64=-I/Volumes/transcend/programs/linux-header/x86_64/include
LINUX_HEADER_arm=-I/Volumes/transcend/programs/linux-header/arm/include
LINUX_HEADER_i386=-I/Volumes/transcend/programs/linux-header/i386/include
VERBOSE=-DCMAKE_VERBOSE_MAKEFILE=ON

CC ?= cc
CXX ?= c++

.PHONY: release release-mac release-linux-x86_64 release-linux-arm release-win

release: release-mac release-linux-x86_64 release-linux-arm release-win
	echo "build all"

release-native:
	mkdir -p build-native && \
    cd build-native && \
    conan install .. --profile ../profiles/release-native --build missing && \
    cmake .. -GNinja \
             -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_C_COMPILER=${CC}   \
             -DCMAKE_CXX_COMPILER=${CXX} && \
    cmake --build .

release-mac:
	mkdir -p build-mac && \
    cd build-mac       && \
    conan install .. --profile ../profiles/release-mac --build missing && \
    cmake .. -GNinja \
             -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_C_COMPILER=clang   \
             -DCMAKE_CXX_COMPILER=clang++ && \
    cmake --build .

release-linux-x86_64:
	mkdir -p build-linux-x86_64 && \
    cd build-linux-x86_64       && \
    conan install .. --profile ../profiles/release-linux-x86_64 --build missing && \
    cmake .. -GNinja \
             -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_C_COMPILER=x86_64-linux-musl-gcc \
             -DCMAKE_C_FLAGS="${RELEASE_FLAGS} ${LINUX_HEADER_x86_64}" \
             -DCMAKE_CXX_COMPILER=x86_64-linux-musl-g++ \
             -DCMAKE_CXX_FLAGS="${RELEASE_FLAGS} ${LINUX_HEADER_x86_64}" && \
    cmake --build .

release-linux-arm:
	mkdir -p build-linux-arm && \
    cd build-linux-arm       && \
    conan install .. --profile ../profiles/release-linux-arm --build missing && \
    cmake .. -GNinja \
             -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_C_COMPILER=arm-linux-musleabihf-gcc \
             -DCMAKE_C_FLAGS="${RELEASE_FLAGS} ${LINUX_HEADER_arm}" \
             -DCMAKE_CXX_COMPILER=arm-linux-musleabihf-g++ \
             -DCMAKE_CXX_FLAGS="${RELEASE_FLAGS} ${LINUX_HEADER_arm}" && \
    cmake --build .

release-win-x86_64:
	mkdir -p build-win-x86_64 && \
    cd build-win-x86_64 && \
    conan install .. --profile ../profiles/release-win-x86_64 --build missing && \
    cmake .. -GNinja \
             -DCMAKE_SYSTEM_NAME=Windows \
             -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
             -DCMAKE_C_FLAGS="${RELEASE_FLAGS}" \
             -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++ \
             -DCMAKE_CXX_FLAGS="${RELEASE_FLAGS}" && \
    cmake --build .

multiplex-tcp: multiplex-tcp.cpp basic.hpp
	${CXX} -std=c++17 -g -o multiplex-tcp multiplex-tcp.cpp -DBOOST_LOG_DYN_LINK \
            -lboost_system -lboost_program_options -lboost_filesystem -lboost_log-mt -lboost_thread-mt

remote: remote.cpp basic.hpp
	${CXX} -std=c++17 -g -o remote remote.cpp -DBOOST_LOG_DYN_LINK \
            -lboost_system -lboost_program_options -lboost_filesystem -lboost_log-mt -lboost_thread-mt

clean:
	rm -rf build-* multiplex-tcp remote *.dSYM
