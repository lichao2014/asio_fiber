cmake_minimum_required(VERSION 3.24)

project(asio_fiber_test CXX)

set(CMAKE_CXX_STANDARD 14)

set(Boost_VERBOSE ON)
set(BOOST_INSTALL_DIR E:/code/3rd/boost_1_79_0/vs2022)
find_package(Boost 1.79 REQUIRED COMPONENTS ALL HINTS ${BOOST_INSTALL_DIR} NO_DEFAULT_PATH)

file(GLOB ASIO_FIBER_INC asio_fiber/*.h)
add_library(asio_fiber INTERFACE ${ASIO_FIBER_INC})
target_include_directories(asio_fiber INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

function(add_samples TARGET DIR)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "INC_DIR" "BOOST_LIB")

    message(DEBUG "Add samples ${TARGET} in dir ${DIR}")

    add_executable(${TARGET})

    if (DEFINED ARG_INC_DIR)
        file(GLOB EXT_SRC ${ARG_INC_DIR}/*.h ${ARG_INC_DIR}/*.cpp)
        source_group(${ARG_INC_DIR} FILES ${EXT_SRC})
    endif()

    file(GLOB SRCS ${DIR}/*.cpp ${DIR}/*.hpp)
    target_sources(${TARGET} PRIVATE ${SRCS} ${EXT_SRC})
    target_include_directories(${TARGET} PRIVATE ${Boost_INCLUDE_DIRS})
    target_link_libraries(${TARGET} PRIVATE asio_fiber Boost::fiber)

    set(i 0)
    foreach(LIB IN LISTS ARG_BOOST_LIB)
        message(STATUS "Add boost library ${i}=${LIB}")
        target_link_libraries(${TARGET} PRIVATE Boost::${LIB})
        math(EXPR i "${i} + 1")
    endforeach()
endfunction()

add_samples(sample1 samples/sample1 INC_DIR asio_fiber)
add_samples(http_server samples/http_server BOOST_LIB program_options)
