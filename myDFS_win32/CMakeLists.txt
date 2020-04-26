cmake_minimum_required(VERSION 3.2)
project(myDFSServer)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c99")

include_directories(myDFSCommons)
include_directories(myDFSServer)
include_directories(myDFSClient)
include_directories(test)

file(GLOB CLIENT_CONFIG "test/mydfs_client.config")
file(GLOB SERVER_CONFIG "myDFSServer/mydfs_server.config")
file(GLOB SERVER_SOURCES "myDFSServer/*")

file(GLOB COMMONS_SOURCES "myDFSCommons/*")

add_library(myDFSCommons ${COMMONS_SOURCES})
add_library(myDFSClient myDFSClient/client.h myDFSClient/client.c)
add_executable(myDFSServer ${SERVER_SOURCES})
add_executable(clientTest test/main.c)
target_link_libraries(myDFSCommons ws2_32)
target_link_libraries(myDFSServer myDFSCommons)
target_link_libraries(myDFSClient myDFSCommons)
target_link_libraries(clientTest myDFSClient)
file(COPY ${CLIENT_CONFIG} DESTINATION "/")
file(COPY ${SERVER_CONFIG} DESTINATION "/")