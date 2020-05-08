cmake_minimum_required(VERSION 2.6)

project (SQLite C)

add_definitions(-DSQLITE_SHM_DIRECTORY="/dev/shm")

# page size 4096bytes by default

# auto checkpoint 200 pages
add_definitions(-DSQLITE_DEFAULT_WAL_AUTOCHECKPOINT=200)

# journal size 0,truncate WAL files to their minimum size
add_definitions(-DSQLITE_DEFAULT_JOURNAL_SIZE_LIMIT=0)

SET(SQLite_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}")
SET(SQLite_LIBRARIES sqlite)

SET(LIBRARY_OUTPUT_PATH "" CACHE INTERNAL 
  "Where to put the libraries for SQLite"
  )

SET (LIB sqlite3.c)

IF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	SET(OperatingSystem "Linux")
ENDIF(${CMAKE_SYSTEM_NAME} MATCHES "Linux")

ADD_LIBRARY(sqlite STATIC ${LIB})
