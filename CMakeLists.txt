CMAKE_MINIMUM_REQUIRED(VERSION 2.4.7 FATAL_ERROR)

PROJECT(MySQL-transaction_time_audit)

# Path for MySQL include directory
INCLUDE_DIRECTORIES("C:/Program Files/MySQL/MySQL Server 5.1/include/")

ADD_DEFINITIONS("-DHAVE_DLOPEN -DDBUG_OFF")

ADD_LIBRARY(MySQL-transaction_time_audit MODULE transaction_time_audit.cpp)

TARGET_LINK_LIBRARIES(MySQL-transaction_time_audit wsock32)
