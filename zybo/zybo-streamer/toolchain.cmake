set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(SDK_SYSROOT $ENV{SDKTARGETSYSROOT})
set(SDK_BIN $ENV{OECORE_NATIVE_SYSROOT}/usr/bin/arm-amd-linux-gnueabi)

set(CMAKE_C_COMPILER   ${SDK_BIN}/arm-amd-linux-gnueabi-gcc)
set(CMAKE_CXX_COMPILER ${SDK_BIN}/arm-amd-linux-gnueabi-g++)

set(CMAKE_C_FLAGS   "-mthumb -mfpu=neon -mfloat-abi=hard -mcpu=cortex-a9" CACHE STRING "")
set(CMAKE_CXX_FLAGS "-mthumb -mfpu=neon -mfloat-abi=hard -mcpu=cortex-a9" CACHE STRING "")

set(CMAKE_SYSROOT ${SDK_SYSROOT})
set(CMAKE_FIND_ROOT_PATH ${SDK_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
