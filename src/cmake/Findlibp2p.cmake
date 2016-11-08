set(BUILD_DIR "${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_SYSTEM_NAME}")

find_library(AIO_LIBRARY asyncio-0.3
  PATH ${ROOT_SOURCE_DIR}/libp2p/${BUILD_DIR}/asyncio
)

find_library(P2P_LIBRARY p2p
  PATH ${ROOT_SOURCE_DIR}/libp2p/${BUILD_DIR}/p2p
)

find_library(P2PUTILS_LIBRARY p2putils
  PATH ${ROOT_SOURCE_DIR}/libp2p/${BUILD_DIR}/p2putils
)

find_path(AIO_INCLUDE_DIR "asyncio/asyncio.h"
  PATH ${ROOT_SOURCE_DIR}/libp2p/src/include
)

set(AIO_INCLUDE_DIR
  ${AIO_INCLUDE_DIR} ${AIO_INCLUDE_DIR}/../../${BUILD_DIR}/include
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(AIO_LIBRARY ${AIO_LIBRARY} rt)
endif()
