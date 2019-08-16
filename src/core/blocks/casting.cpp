#include "shared.hpp"

namespace chainblocks {
#define TO_SOMETHING(_varName_, _width_, _type_, _payload_, _strOp_, _info_)   \
  struct To##_varName_##_width_ {                                              \
    CBTypesInfo inputTypes() { return CBTypesInfo(SharedTypes::anyInfo); }     \
    CBTypesInfo outputTypes() { return CBTypesInfo((SharedTypes::_info_)); }   \
                                                                               \
    bool convert(CBVar &dst, int &index, const CBVar &src) {                   \
      switch (src.valueType) {                                                 \
      case String: {                                                           \
        dst.payload._payload_[index] =                                         \
            static_cast<_type_>(std::_strOp_(src.payload.stringValue));        \
        index++;                                                               \
        if (index == _width_)                                                  \
          return true;                                                         \
        break;                                                                 \
      }                                                                        \
      case Float:                                                              \
        dst.payload._payload_[index] =                                         \
            static_cast<_type_>(src.payload.floatValue);                       \
        index++;                                                               \
        if (index == _width_)                                                  \
          return true;                                                         \
        break;                                                                 \
      case Float2:                                                             \
        dst.payload._payload_[index] =                                         \
            static_cast<_type_>(src.payload.float2Value[0]);                   \
        index++;                                                               \
        if (index == _width_)                                                  \
          return true;                                                         \
        dst.payload._payload_[index] =                                         \
            static_cast<_type_>(src.payload.float2Value[1]);                   \
        index++;                                                               \
        if (index == _width_)                                                  \
          return true;                                                         \
        break;                                                                 \
      case Float3:                                                             \
        dst.payload._payload_[index] =                                         \
            static_cast<_type_>(src.payload.float3Value[0]);                   \
        index++;                                                               \
        if (index == _width_)                                                  \
          return true;                                                         \
        dst.payload._payload_[index] =                                         \
            static_cast<_type_>(src.payload.float3Value[1]);                   \
        index++;                                                               \
        if (index == _width_)                                                  \
          return true;                                                         \
        dst.payload._payload_[index] =                                         \
            static_cast<_type_>(src.payload.float3Value[2]);                   \
        index++;                                                               \
        if (index == _width_)                                                  \
          return true;                                                         \
        break;                                                                 \
      case Float4:                                                             \
        dst.payload._payload_[index] =                                         \
            static_cast<_type_>(src.payload.float4Value[0]);                   \
        index++;                                                               \
        if (index == _width_)                                                  \
          return true;                                                         \
        dst.payload._payload_[index] =                                         \
            static_cast<_type_>(src.payload.float4Value[1]);                   \
        index++;                                                               \
        if (index == _width_)                                                  \
          return true;                                                         \
        dst.payload._payload_[index] =                                         \
            static_cast<_type_>(src.payload.float4Value[2]);                   \
        index++;                                                               \
        if (index == _width_)                                                  \
          return true;                                                         \
        dst.payload._payload_[index] =                                         \
            static_cast<_type_>(src.payload.float4Value[3]);                   \
        index++;                                                               \
        if (index == _width_)                                                  \
          return true;                                                         \
        break;                                                                 \
      case Int:                                                                \
        dst.payload._payload_[index] =                                         \
            static_cast<_type_>(src.payload.intValue);                         \
        index++;                                                               \
        if (index == _width_)                                                  \
          return true;                                                         \
        break;                                                                 \
      case Int2:                                                               \
        dst.payload._payload_[index] =                                         \
            static_cast<_type_>(src.payload.int2Value[0]);                     \
        index++;                                                               \
        if (index == _width_)                                                  \
          return true;                                                         \
        dst.payload._payload_[index] =                                         \
            static_cast<_type_>(src.payload.int2Value[1]);                     \
        index++;                                                               \
        if (index == _width_)                                                  \
          return true;                                                         \
        break;                                                                 \
      case Int3:                                                               \
        dst.payload._payload_[index] =                                         \
            static_cast<_type_>(src.payload.int3Value[0]);                     \
        index++;                                                               \
        if (index == _width_)                                                  \
          return true;                                                         \
        dst.payload._payload_[index] =                                         \
            static_cast<_type_>(src.payload.int3Value[1]);                     \
        index++;                                                               \
        if (index == _width_)                                                  \
          return true;                                                         \
        dst.payload._payload_[index] =                                         \
            static_cast<_type_>(src.payload.int3Value[2]);                     \
        index++;                                                               \
        if (index == _width_)                                                  \
          return true;                                                         \
        break;                                                                 \
      case Int4:                                                               \
        dst.payload._payload_[index] =                                         \
            static_cast<_type_>(src.payload._payload_[0]);                     \
        index++;                                                               \
        if (index == _width_)                                                  \
          return true;                                                         \
        dst.payload._payload_[index] =                                         \
            static_cast<_type_>(src.payload._payload_[1]);                     \
        index++;                                                               \
        if (index == 4)                                                        \
          return true;                                                         \
        dst.payload._payload_[index] =                                         \
            static_cast<_type_>(src.payload._payload_[2]);                     \
        index++;                                                               \
        if (index == _width_)                                                  \
          return true;                                                         \
        dst.payload._payload_[index] =                                         \
            static_cast<_type_>(src.payload._payload_[3]);                     \
        index++;                                                               \
        if (index == _width_)                                                  \
          return true;                                                         \
        break;                                                                 \
      default:                                                                 \
        throw CBException("Cannot cast given input type.");                    \
      }                                                                        \
      return false;                                                            \
    }                                                                          \
                                                                               \
    CBVar activate(CBContext *context, const CBVar &input) {                   \
      int index = 0;                                                           \
      CBVar output{};                                                          \
      output.valueType = _varName_##_width_;                                   \
      switch (input.valueType) {                                               \
      case Seq: {                                                              \
        for (auto i = 0;                                                       \
             i < _width_, i < stbds_arrlen(input.payload.seqValue); i++) {     \
          if (convert(output, index, input.payload.seqValue[i]))               \
            return output;                                                     \
        }                                                                      \
        break;                                                                 \
      }                                                                        \
      case Int:                                                                \
      case Int2:                                                               \
      case Int3:                                                               \
      case Int4:                                                               \
      case Float:                                                              \
      case Float2:                                                             \
      case Float3:                                                             \
      case Float4:                                                             \
      case String:                                                             \
        if (convert(output, index, input))                                     \
          return output;                                                       \
        break;                                                                 \
      default:                                                                 \
        throw CBException("Cannot cast given input type.");                    \
      }                                                                        \
      return output;                                                           \
    }                                                                          \
  };                                                                           \
  RUNTIME_CORE_BLOCK(To##_varName_##_width_);                                  \
  RUNTIME_BLOCK_inputTypes(To##_varName_##_width_);                            \
  RUNTIME_BLOCK_outputTypes(To##_varName_##_width_);                           \
  RUNTIME_BLOCK_activate(To##_varName_##_width_);                              \
  RUNTIME_BLOCK_END(To##_varName_##_width_);

TO_SOMETHING(Int, 2, int64_t, int2Value, stoi, int2Info);
TO_SOMETHING(Int, 3, int32_t, int3Value, stoi, int3Info);
TO_SOMETHING(Int, 4, int32_t, int4Value, stoi, int4Info);
TO_SOMETHING(Float, 2, double, float2Value, stod, float2Info);
TO_SOMETHING(Float, 3, float, float3Value, stof, float3Info);
TO_SOMETHING(Float, 4, float, float4Value, stof, float4Info);

void registerCastingBlocks() {
  REGISTER_CORE_BLOCK(ToInt2);
  REGISTER_CORE_BLOCK(ToInt3);
  REGISTER_CORE_BLOCK(ToInt4);
  REGISTER_CORE_BLOCK(ToFloat2);
  REGISTER_CORE_BLOCK(ToFloat3);
  REGISTER_CORE_BLOCK(ToFloat4);
}
}; // namespace chainblocks
