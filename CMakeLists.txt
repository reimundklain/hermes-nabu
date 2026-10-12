cmake_minimum_required(VERSION 3.16)

# Reported to Hermes in `hello`. Override with -DPROJECT_VER=...
if(NOT DEFINED PROJECT_VER)
    set(PROJECT_VER "0.1.0")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hermes-nabu)
