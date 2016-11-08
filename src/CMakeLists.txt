cmake_minimum_required(VERSION 3.0)

set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH}
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake
)

find_package(Boost COMPONENTS filesystem thread system)
find_package(Protobuf REQUIRED)
find_package(OpenSSL)

find_package(libp2p)
find_package(poolcore)
find_package(config4cpp)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

PROTOBUF_GENERATE_CPP(ProtoSources ProtoHeaders ${CMAKE_SOURCE_DIR}/protocol.proto)

include_directories(
  ${AIO_INCLUDE_DIR}
  ${FLATBUFFERS_INCLUDE_DIR}
  ${POOLCORE_INCLUDE_DIR}
  ${CONFIG4CPP_INCLUDE_DIR}
  ${CMAKE_BINARY_DIR}
)
 
add_executable(pool_frontend_zcash
  main.cpp
  zcashpool.cpp
  zmtp.cpp
  equihash_original.cpp
  stratum.cpp
  ${ProtoSources}
)

target_link_libraries(pool_frontend_zcash
  ${POOLCOMMON_LIBRARY}
  ${POOLCORE_LIBRARY}
  ${AIO_LIBRARY}
  ${P2P_LIBRARY}
  ${P2PUTILS_LIBRARY}
  gmp
  gmpxx
  ${OPENSSL_SSL_LIBRARY}
  ${OPENSSL_CRYPTO_LIBRARY}
  pthread
  ${Boost_SYSTEM_LIBRARY}
  ${Boost_FILESYSTEM_LIBRARY}
  ${Boost_THREAD_LIBRARY}
  leveldb
  ${PROTOBUF_LIBRARY}
  ${CONFIG4CPP_LIBRARY}
  jansson
  sodium
)

if (WIN32)
  target_link_libraries(pool_frontend_zcash ws2_32 mswsock)
endif()
