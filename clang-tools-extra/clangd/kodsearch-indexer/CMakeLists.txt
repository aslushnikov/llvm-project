include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../)

set(LLVM_LINK_COMPONENTS
    Support
    )

add_clang_executable(kodsearch-indexer
  IndexerMain.cpp
  )

clang_target_link_libraries(kodsearch-indexer
  PRIVATE
  clangAST
  clangBasic
  clangFrontend
  clangIndex
  clangLex
  clangTooling
)
target_link_libraries(kodsearch-indexer
  PRIVATE
  clangDaemon
  clangdSupport
)
