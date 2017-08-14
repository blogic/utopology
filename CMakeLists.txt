cmake_minimum_required(VERSION 2.6)

PROJECT(utopology C)
INCLUDE(GNUInstallDirs)
ADD_DEFINITIONS(-Os -ggdb -Wall -Werror --std=gnu99 -Wmissing-declarations)

SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")

ADD_EXECUTABLE(utopology utopology.c)
TARGET_LINK_LIBRARIES(utopology ubox ubus json-c blobmsg_json)
INSTALL(TARGETS utopology
	RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR}
)

ADD_EXECUTABLE(utopologyd utopologyd.c ubus.c nl80211.c rtnl.c blob.c netlink.c config.c bridge.c)
TARGET_LINK_LIBRARIES(utopologyd ubox ubus json-c blobmsg_json nl-tiny uci)
INSTALL(TARGETS utopologyd
	RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR}
)
