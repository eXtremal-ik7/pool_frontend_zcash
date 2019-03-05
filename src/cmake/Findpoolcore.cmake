set(BUILD_DIR "${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_SYSTEM_NAME}")

find_library(POOLCORE_LIBRARY poolcore
  PATH ${ROOT_SOURCE_DIR}/poolcore/${BUILD_DIR}/poolcore
)

find_library(POOLCOMMON_LIBRARY poolcommon
  PATH ${ROOT_SOURCE_DIR}/poolcore/${BUILD_DIR}/poolcommon
)

find_library(POOLCORE_LOGURU_LIBRARY loguru
  PATH ${ROOT_SOURCE_DIR}/poolcore/${BUILD_DIR}
)

find_path(POOLCORE_INCLUDE_DIR "poolcore/backend.h"
  PATH ${ROOT_SOURCE_DIR}/poolcore/src/include
)

set(POOLCORE_INCLUDE_DIR
  ${POOLCORE_INCLUDE_DIR} ${POOLCORE_INCLUDE_DIR}/../../${BUILD_DIR}
)
