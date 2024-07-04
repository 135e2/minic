target_sources(
  minic
  PRIVATE ${CMAKE_CURRENT_LIST_DIR}/main.cc
          ${CMAKE_CURRENT_LIST_DIR}/postProcess.cc
          ${CMAKE_CURRENT_LIST_DIR}/Collector.cc
          ${CMAKE_CURRENT_LIST_DIR}/Renamer.cc)
