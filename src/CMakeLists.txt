cmake_minimum_required(VERSION 3.0)
project(pool_frontend_zcash)

set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake
)

find_package(Boost COMPONENTS filesystem thread system REQUIRED)
find_package(Protobuf REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(libp2p REQUIRED)
find_package(poolcore REQUIRED)
find_package(config4cpp REQUIRED)

find_path(GMP_INCLUDE_DIRECTORY gmp.h gmpxx.h)
find_library(GMP_LIBRARY gmp)
find_library(GMPXX_LIBRARY gmpxx)

find_library(LEVELDB_LIBRARY leveldb
  PATH ${ROOT_SOURCE_DIR}/leveldb-1.18
)

find_path(LEVELDB_INCLUDE_DIR "leveldb/db.h"
  PATH ${ROOT_SOURCE_DIR}/leveldb-1.18/include
)

find_path(JANSSON_INCLUDE_DIRECTORY jansson.h)
find_library(JANSSON_LIBRARY jansson)

find_path(SODIUM_INCLUDE_DIRECTORY sodium.h)
find_library(SODIUM_LIBRARY sodium)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

PROTOBUF_GENERATE_CPP(ProtoSources ProtoHeaders ${CMAKE_SOURCE_DIR}/protocol.proto)

include_directories(
  ${AIO_INCLUDE_DIR}
  ${FLATBUFFERS_INCLUDE_DIR}
  ${GMP_INCLUDE_DIRECTORY}
  ${LEVELDB_INCLUDE_DIR}
  ${JANSSON_INCLUDE_DIRECTORY}
  ${POOLCORE_INCLUDE_DIR}
  ${PROTOBUF_INCLUDE_DIR}
  ${SODIUM_INCLUDE_DIRECTORY}
  ${CONFIG4CPP_INCLUDE_DIR}
  ${CMAKE_BINARY_DIR}
)
 
add_executable(pool_frontend_zcash
  main.cpp
  zcashpool.cpp
  equihash_original.cpp
  stratum.cpp
  ${ProtoSources}
)

target_link_libraries(pool_frontend_zcash
  ${POOLCOMMON_LIBRARY}
  ${POOLCORE_LIBRARY}
  ${AIO_LIBRARY}
  ${AIO_EXTRAS_LIBRARY}
  ${P2P_LIBRARY}
  ${P2PUTILS_LIBRARY}
  ${GMP_LIBRARY}
  ${GMPXX_LIBRARY}
  ${OPENSSL_SSL_LIBRARY}
  ${OPENSSL_CRYPTO_LIBRARY}
  ${LEVELDB_LIBRARY}
  pthread
  ${Boost_SYSTEM_LIBRARY}
  ${Boost_FILESYSTEM_LIBRARY}
  ${Boost_THREAD_LIBRARY}
  ${PROTOBUF_LIBRARY}
  ${CONFIG4CPP_LIBRARY}
  ${JANSSON_LIBRARY}
  ${SODIUM_LIBRARY}
)

if (WIN32)
  target_link_libraries(pool_frontend_zcash ws2_32 mswsock)
endif()
