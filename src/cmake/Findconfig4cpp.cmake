find_library(CONFIG4CPP_LIBRARY config4cpp
  PATH ${ROOT_SOURCE_DIR}/config4cpp/lib
)

find_path(CONFIG4CPP_INCLUDE_DIR "config4cpp/Configuration.h"
  PATH ${ROOT_SOURCE_DIR}/config4cpp/include
)
