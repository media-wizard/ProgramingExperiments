cmake_minimum_required(VERSION 3.10)

project(ChatApp)

include_directories(base)
file(GLOB BASE_SOURCES "base/*.cpp")

find_package (Threads)
add_executable(chatserver ${BASE_SOURCES} "chatapp/chat_server.cpp" "chatapp/main.cpp" "chatapp/io_manager.cpp")
target_link_libraries (chatserver ${CMAKE_THREAD_LIBS_INIT})

add_executable(chatclient "chatapp/chat_client.cpp")

set_target_properties(chatserver PROPERTIES
    CXX_STANDARD 17
    CXX_EXTENSIONS TRUE
)

set_target_properties(chatclient PROPERTIES
    CXX_STANDARD 17
    CXX_EXTENSIONS TRUE
)

