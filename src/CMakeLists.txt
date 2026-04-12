target_sources(
  minic
  PRIVATE ${CMAKE_CURRENT_LIST_DIR}/main.cc
          ${CMAKE_CURRENT_LIST_DIR}/Collector.cc
          ${CMAKE_CURRENT_LIST_DIR}/Renamer.cc
          ${CMAKE_CURRENT_LIST_DIR}/postProcessLexer.cc
          ${CMAKE_CURRENT_LIST_DIR}/Utils.cc)
