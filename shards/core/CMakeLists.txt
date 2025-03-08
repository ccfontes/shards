set(core_SOURCES
  async.cpp
  ops_internal.cpp
  number_types.cpp
)

add_library(shards-core STATIC ${core_SOURCES})

target_include_directories(shards-core PUBLIC ${SHARDS_DIR}/include)
target_include_directories(shards-core PUBLIC ${SHARDS_DIR})

if(TRACY_ENABLE AND TRACY_CALLSTACK_SIZE AND (TRACY_CALLSTACK_SIZE GREATER_EQUAL 0))
  target_compile_definitions(shards-core PUBLIC
    # The default call stack size
    TRACY_CALLSTACK=${TRACY_CALLSTACK_SIZE}
  )
endif()

if(CMAKE_BUILD_TYPE MATCHES Debug)
  set(SHARDS_INLINE_EVERYTHING_DEFAULT OFF)
else()
  set(SHARDS_INLINE_EVERYTHING_DEFAULT ON)
endif()
if(NOT DEFINED SHARDS_INLINE_EVERYTHING)
  set(SHARDS_INLINE_EVERYTHING ${SHARDS_INLINE_EVERYTHING_DEFAULT} CACHE BOOL "When enabled, runtime and core shards are inlined in a single compilation unit")
endif()

if(SHARDS_INLINE_EVERYTHING)
  # Add define to check
  target_compile_definitions(shards-core PUBLIC SHARDS_INLINE_EVERYTHING=1)
endif()

if(WIN32)
  target_link_libraries(shards-core Boost::stacktrace_windbg_cached)

  # Target windows SDK (https://docs.microsoft.com/en-us/cpp/porting/modifying-winver-and-win32-winnt)
  # 0x0A00 = Windows 10
  target_compile_definitions(shards-core PUBLIC WINVER=0x0A00 _WIN32_WINNT=0x0A00)
elseif(DESKTOP_LINUX)
  target_link_libraries(shards-core Boost::stacktrace_addr2line)
else()
  target_link_libraries(shards-core Boost::stacktrace_basic)
endif()

target_precompile_headers(shards-core PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/pch.cpp>")

if(APPLE)
  target_link_libraries(shards-core
    "-framework Foundation"
    "-framework CoreAudio"
    "-framework AudioToolbox"
    "-framework CoreVideo"
    "-framework IOKit"
    "-framework QuartzCore"
    "-framework Metal"
    "-framework Security"
    "-framework CoreHaptics"
    "-framework GameController"
    -liconv
  )

  if(IOS)
    target_link_libraries(shards-core
      "-framework AVFoundation"
      "-framework CoreMotion"
      "-framework CoreGraphics"
      "-framework UIKit"
    )
  else()
    target_link_libraries(shards-core
      "-framework Cocoa"
      "-framework Carbon"
      "-framework ForceFeedback"
    )
  endif()
endif()

target_link_libraries(shards-core Boost::filesystem Boost::lockfree Boost::foreach Boost::multiprecision Boost::atomic)

if(NOT EMSCRIPTEN)
  target_link_libraries(shards-core Boost::beast Boost::asio Boost::context)
else()
  target_include_directories(shards-core PUBLIC $<TARGET_PROPERTY:Boost::asio,INTERFACE_INCLUDE_DIRECTORIES>)
  # For usage of boost/beast/core/detail/base64.hpp
  target_include_directories(shards-core PUBLIC $<TARGET_PROPERTY:Boost::beast,INTERFACE_INCLUDE_DIRECTORIES>)
endif()

target_link_libraries(shards-core
  spdlog magic_enum nameof linalg xxHash
  pdqsort entt utf8.h Taskflow stb nlohmann_json
  shards-logging
)

if(DESKTOP)
  target_compile_definitions(shards-core PUBLIC SHARDS_DESKTOP=1)
endif()

if(EMSCRIPTEN)
  target_sources(shards-core PRIVATE coro.cpp) # emscripten coroutines
endif()

if(WIN32)
  target_link_libraries(shards-core ws2_32 mswsock Secur32 Crypt32 NCrypt)
elseif(DESKTOP_LINUX)
  target_link_libraries(shards-core pthread dl rt)
endif()

target_compile_options(shards-core
  PRIVATE -ffast-math -fno-finite-math-only -funroll-loops
)

if(EMSCRIPTEN)
  target_compile_options(shards-core PUBLIC
    "SHELL:-s DISABLE_EXCEPTION_CATCHING=0"
  )

  target_link_options(shards-core PUBLIC
    "SHELL:-s DISABLE_EXCEPTION_CATCHING=0"
    "SHELL:-s INITIAL_MEMORY=209715200"
    "SHELL:-s STACK_SIZE=1000000"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s LLD_REPORT_UNDEFINED"
    "SHELL:-s \"EXPORTED_FUNCTIONS=['_main', '_shardsInterface', '_malloc', '_free', '_emscripten_get_now']\""
    "SHELL:-s \"ASYNCIFY_IMPORTS=['emEval']\""
    "SHELL:-s \"EXPORTED_RUNTIME_METHODS=['FS', 'callMain', 'ENV', 'IDBFS', 'PThread', 'setValue', 'getValue', 'lengthBytesUTF8', 'stringToUTF8']\""
  )

  target_link_options(shards-core PUBLIC "SHELL:-s ASYNCIFY=1")

  # Require optimizations when building with asyncify too keep size/build time reasonable
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(shards-core PUBLIC "-O3")
    target_link_options(shards-core PUBLIC "-O3")
  endif()
endif()

duplicate_library_target(shards-core SHARED shards-core-shared)
target_compile_definitions(shards-core-shared
  PUBLIC SHARDS_CORE_DLL=1 
  PRIVATE shards_core_EXPORTS=1)
