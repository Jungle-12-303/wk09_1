cmake_minimum_required(VERSION 3.20)
project(pintos C ASM)

set(CMAKE_C_STANDARD 17)

# Pintos 헤더 경로 (CLion 코드 분석용)
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/lib
    ${CMAKE_SOURCE_DIR}/include/lib/kernel
    ${CMAKE_SOURCE_DIR}/include/threads
    ${CMAKE_SOURCE_DIR}/include/devices
    ${CMAKE_SOURCE_DIR}/include/userprog
    ${CMAKE_SOURCE_DIR}/include/filesys
    ${CMAKE_SOURCE_DIR}/include/vm
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/lib/kernel
    ${CMAKE_SOURCE_DIR}/tests
)

# 전처리 매크로
add_definitions(-DUSERPROG)

# CLion이 소스 파일을 인식하도록 등록 (빌드는 make로 한다)
file(GLOB THREAD_SOURCES threads/*.c)
file(GLOB DEVICE_SOURCES devices/*.c)
file(GLOB LIB_SOURCES lib/*.c lib/kernel/*.c)
file(GLOB USERPROG_SOURCES userprog/*.c)
file(GLOB FILESYS_SOURCES filesys/*.c)
file(GLOB VM_SOURCES vm/*.c)
file(GLOB TEST_SOURCES tests/threads/*.c)
file(GLOB HEADERS include/**/*.h)

add_executable(pintos_dummy
    ${THREAD_SOURCES}
    ${DEVICE_SOURCES}
    ${LIB_SOURCES}
    ${USERPROG_SOURCES}
    ${FILESYS_SOURCES}
    ${VM_SOURCES}
    ${TEST_SOURCES}
    ${HEADERS}
)

# 이 CMakeLists.txt는 CLion의 코드 분석(자동완성, 에러 표시)만을 위한 것이다.
# 실제 빌드는 pintos/threads 디렉터리에서 make 명령어로 한다.
