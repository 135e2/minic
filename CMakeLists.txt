cmake_minimum_required(VERSION 3.14)
project(minic LANGUAGES C CXX)

add_executable(minic "")
set(DEFAULT_CMAKE_BUILD_TYPE Release)
set_property(TARGET minic PROPERTY CXX_STANDARD 17)
set_property(TARGET minic PROPERTY CXX_STANDARD_REQUIRED ON)
set_property(TARGET minic PROPERTY CXX_EXTENSIONS OFF)

find_package(Clang REQUIRED)

if(CLANG_LINK_CLANG_DYLIB)
  target_link_libraries(minic PRIVATE clang-cpp)
else()
  target_link_libraries(
    minic
    PRIVATE clangIndex
            clangFormat
            clangTooling
            clangToolingInclusions
            clangToolingCore
            clangFrontend
            clangParse
            clangSerialization
            clangSema
            clangAST
            clangLex
            clangDriver
            clangBasic)
endif()

if(LLVM_LINK_LLVM_DYLIB)
  target_link_libraries(minic PRIVATE LLVM)
else()
  target_link_libraries(minic PRIVATE LLVMOption LLVMSupport)
endif()

if(NOT LLVM_ENABLE_RTTI)
  # releases.llvm.org libraries are compiled with -fno-rtti The mismatch between
  # lib{clang,LLVM}* and minic can make libstdc++ std::make_shared return
  # nullptr _Sp_counted_ptr_inplace::_M_get_deleter
  if(MSVC)
    target_compile_options(minic PRIVATE /GR-)
  else()
    target_compile_options(minic PRIVATE -fno-rtti)
  endif()
endif()

add_subdirectory(src)

foreach(include_dir ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
  get_filename_component(include_dir_realpath ${include_dir} REALPATH)
  # Don't add as SYSTEM if they are in CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES.
  # It would reorder the system search paths and cause issues with libstdc++'s
  # use of #include_next. See https://github.com/MaskRay/ccls/pull/417
  if(NOT "${include_dir_realpath}" IN_LIST
     CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
    target_include_directories(minic SYSTEM PRIVATE ${include_dir})
  endif()
endforeach()

install(TARGETS minic RUNTIME DESTINATION bin)