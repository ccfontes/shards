set(shards_SOURCES
  stepA_mal.cpp
  Core.cpp
  SHCore.cpp
  Environment.cpp
  Reader.cpp
  ReadLine.cpp
  String.cpp
  Types.cpp
  Validation.cpp
  SHCore.cpp
)

function(setup_shards_target TARGET)
  if(NOT EMSCRIPTEN)
    target_link_libraries(${TARGET} Boost::process)
  endif()
  target_link_libraries(${TARGET} replxx shards-lang-rust shards-core)
endfunction()

function(setup_shards_library_target TARGET)
  target_sources(${TARGET} PRIVATE ${shards_SOURCES})
  target_compile_definitions(${TARGET} PUBLIC NO_MAL_MAIN=1)

  setup_shards_target(${TARGET})
endfunction()

# Executable
add_executable(shards ${shards_SOURCES})
setup_shards_target(shards)
target_link_libraries(shards shards-union)

# library
add_library(shards-lib)
setup_shards_library_target(shards-lib)
set_target_properties(shards-lib PROPERTIES OUTPUT_NAME "${LIB_PREFIX}shards-static")
target_compile_definitions(shards-lib PRIVATE "shards_EXPORTS=1")

if(EMSCRIPTEN)
  if(EMSCRIPTEN_PTHREADS)
    set_target_properties(shards PROPERTIES SUFFIX "-mt.js")
  else()
    set_target_properties(shards PROPERTIES SUFFIX "-st.js")
  endif()

  target_link_options(shards PUBLIC
    "SHELL:-s MODULARIZE=1"
    "SHELL:-s EXPORT_NAME=shards"
    "SHELL:-s NO_EXIT_RUNTIME=1"
  )
endif()
