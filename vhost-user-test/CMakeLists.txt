project(vhost-user-test)

cmake_minimum_required(VERSION 3.10)

set(VHOST_USER_TEST_SOURCE_FILES
  vhost_user_server.c
  vhost_user_client.c
  vhost_user_test.c
)

set (VHOST_USER_LIB_INC_PATH ${CMAKE_CURRENT_LIST_DIR}/../vhost-user/include)

SET(CMAKE_INSTALL_PREFIX "/usr/")

add_executable (vhost-user-test ${VHOST_USER_TEST_SOURCE_FILES})

include_directories (${VHOST_USER_LIB_INC_PATH})
include_directories (${SYSROOTINC_PATH})
include_directories (${SYSROOT_INCLUDEDIR})

link_directories(${SYSROOT_LIBDIR})
link_directories(${CMAKE_INSTALL_LIBDIR})

target_link_libraries (vhost-user-test c)
target_link_libraries (vhost-user-test vhostuserlib)

install(TARGETS "vhost-user-test" DESTINATION bin)

