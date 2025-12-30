project(vhost-user-lib)

cmake_minimum_required(VERSION 3.10)

set (VHOST_USER_LIB_INC_PATH ${CMAKE_CURRENT_LIST_DIR}/include/)

#enable test related macros
#add_definitions(-D__TEST__)

#enable ARM64 architecture specific code
add_definitions(-DARCH_ARM64)

set(VHOST_USER_LIB_SOURCE_FILES
      src/vhost_user.c
      src/socket.c
      src/vq.c
      )

SET(CMAKE_INSTALL_PREFIX "/usr/")

add_library (vhostuserlib SHARED ${VHOST_USER_LIB_SOURCE_FILES})

include_directories (${SYSROOTINC_PATH})
include_directories (${SYSROOT_INCLUDEDIR})
include_directories (${VHOST_USER_LIB_INC_PATH})

link_directories(${SYSROOT_LIBDIR})
link_directories(${CMAKE_INSTALL_LIBDIR})

target_link_libraries (vhostuserlib c)
target_link_libraries (vhostuserlib pthread)
target_link_libraries (vhostuserlib systemd)


install(DIRECTORY include/  DESTINATION include)
install(TARGETS vhostuserlib  ARCHIVE DESTINATION lib   LIBRARY DESTINATION lib    COMPONENT library)

