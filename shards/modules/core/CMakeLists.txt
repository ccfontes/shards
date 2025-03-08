set(SOURCES
  flow.cpp
  linalg.cpp
  seqs.cpp
  strings.cpp
  wires.cpp
  casting.cpp
  logging.cpp
  serialization.cpp
  time.cpp
)

add_rust_library(NAME shards-core 
  PROJECT_PATH ${CMAKE_CURRENT_LIST_DIR})

add_shards_module(core SOURCES ${SOURCES}
  REGISTER_SHARDS 
    core casting flow linalg math seqs
    strings wires logging serialization time
    rust
  RUST_TARGETS shards-core-rust
  INLINE_SOURCES core.cpp math.cpp inlined.cpp
  INLINE_SHARDS
    NotInline
    NoopShard
    CoreConst
    CoreSleep
    CoreInput
    CoreForRange
    CoreRepeat
    CoreOnce
    CoreGet
    CoreRefRegular
    CoreRefTable
    CoreSetUpdateRegular
    CoreSetUpdateTable
    CoreSwap
    CorePush
    CoreIs
    CoreIsNot
    CoreAnd
    CoreOr
    CoreNot
    CoreIsMore
    CoreIsLess
    CoreIsMoreEqual
    CoreIsLessEqual
    MathAdd
    MathSubtract
    MathMultiply
    MathDivide
    MathXor
    MathAnd
    MathOr
    MathMod
    MathLShift
    MathRShift
    MathAbs
    MathExp
    MathExp2
    MathExpm1
    MathLog
    MathLog10
    MathLog2
    MathLog1p
    MathSqrt
    MathFastSqrt
    MathFastInvSqrt
    MathCbrt
    MathSin
    MathCos
    MathTan
    MathAsin
    MathAcos
    MathAtan
    MathSinh
    MathCosh
    MathTanh
    MathAsinh
    MathAcosh
    MathAtanh
    MathErf
    MathErfc
    MathTGamma
    MathLGamma
    MathCeil
    MathFloor
    MathTrunc
    MathRound
)

if(EMSCRIPTEN)
  em_link_js_library(shards-module-core core.js)
endif()
