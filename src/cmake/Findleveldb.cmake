find_library(LEVELDB_LIBRARY leveldb
  PATH ${ROOT_SOURCE_DIR}/leveldb
)

find_path(LEVELDB_INCLUDE_DIR "leveldb/db.h"
  PATH ${ROOT_SOURCE_DIR}/leveldb/include
)
