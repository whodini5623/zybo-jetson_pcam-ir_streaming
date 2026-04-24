cmake_minimum_required(VERSION 3.16)
project(zybo-streamer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDK sysroot 경로
set(SYSROOT $ENV{SDKTARGETSYSROOT})

# Boost (헤더 온리 + system 라이브러리)
set(BOOST_ROOT "${SYSROOT}/usr")
set(Boost_NO_SYSTEM_PATHS ON)
find_package(Boost REQUIRED COMPONENTS system)

# 실행 파일
add_executable(zybo-streamer
    main.cpp
    v4l2_capture.cpp
    udp_sender.cpp
)

target_include_directories(zybo-streamer PRIVATE
    ${Boost_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SYSROOT}/usr/include          # jpeglib.h
)

target_link_libraries(zybo-streamer PRIVATE
    ${Boost_LIBRARIES}
    pthread
    ${SYSROOT}/usr/lib/libjpeg.so   # libjpeg-turbo
)

# 빌드 타입 기본값
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
