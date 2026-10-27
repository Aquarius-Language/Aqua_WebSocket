cmake_minimum_required(VERSION 3.20)
project(aqua_websocket C)
include(AquariusLibrary)
aqua_add_library(aquaws SOURCES src/aq_json.c
    NATIVE_SOURCES src/aquaws.c src/aq_plat.c
    WEB_SOURCES src/web.c src/web_support.c
    DEFINITIONS AQ_HOSTED_LIBC)
if(WIN32)
    target_link_libraries(aqua_aquaws PRIVATE ws2_32 bcrypt)
endif()
