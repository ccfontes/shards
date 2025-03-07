/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright © 2019 Fragcolor Pte. Ltd. */

#include <shards/shards.h>
#include <shards/shards.hpp>
#include <shards/core/shared.hpp>
#include <unordered_set>
#include <shards/core/params.hpp>
#include <shards/utility.hpp>

namespace shards {
struct Flatten {
  SHVar outputCache{};
  Type seqType{};
  Types seqTypes{};

  void destroy() {
    if (outputCache.valueType == SHType::Seq) {
      shards::arrayFree(outputCache.payload.seqValue);
    }
  }

  static SHTypesInfo inputTypes() { return CoreInfo::AnyType; }
  static SHTypesInfo outputTypes() { return CoreInfo::AnyType; }

  void addInnerType(SHTypeInfo info, std::unordered_set<SHTypeInfo> &types) {
    switch (info.basicType) {
    case SHType::None:
    case SHType::Any:
    case SHType::EndOfBlittableTypes:
      // Nothing
      break;
    case SHType::Type:
    case SHType::Wire:
    case SHType::ShardRef:
    case SHType::Object:
    case SHType::Enum:
    case SHType::String:
    case SHType::ContextVar:
    case SHType::Path:
    case SHType::Image:
    case SHType::Audio:
    case SHType::Int:
    case SHType::Float:
    case SHType::Bytes:
    case SHType::Array:
    case SHType::Bool: {
      types.insert(info);
      break;
    }
    case SHType::Color:
    case SHType::Int2:
    case SHType::Int3:
    case SHType::Int4:
    case SHType::Int8:
    case SHType::Int16: {
      types.insert(CoreInfo::IntType);
      break;
    }
    case SHType::Float2:
    case SHType::Float3:
    case SHType::Float4: {
      types.insert(CoreInfo::FloatType);
      break;
    }
    case SHType::Seq:
      for (uint32_t i = 0; i < info.seqTypes.len; i++) {
        addInnerType(info.seqTypes.elements[i], types);
      }
      break;
    case SHType::Set:
      for (uint32_t i = 0; i < info.setTypes.len; i++) {
        addInnerType(info.setTypes.elements[i], types);
      }
      break;
    case SHType::Table: {
      for (uint32_t i = 0; i < info.table.types.len; i++) {
        addInnerType(info.table.types.elements[i], types);
      }
      break;
    }
    }
  }

  SHTypeInfo compose(const SHInstanceData &data) {
    std::unordered_set<SHTypeInfo> types;
    addInnerType(data.inputType, types);
    if (types.size() == 0) {
      // add any as single type
      types.insert(CoreInfo::AnyType);
    }
    std::vector<SHTypeInfo> vTypes(types.begin(), types.end());
    seqTypes = Types(vTypes);
    seqType = Type::SeqOf(seqTypes);
    return seqType;
  }

  void add(const SHVar &input) {
    switch (input.valueType) {
    case SHType::None:
    case SHType::Any:
    case SHType::EndOfBlittableTypes:
      // Nothing
      break;
    case SHType::Type:
    case SHType::Wire:
    case SHType::ShardRef:
    case SHType::Object:
    case SHType::Enum:
    case SHType::String:
    case SHType::Path:
    case SHType::ContextVar:
    case SHType::Image:
    case SHType::Audio:
    case SHType::Int:
    case SHType::Float:
    case SHType::Bytes:
    case SHType::Array:
    case SHType::Bool:
      shards::arrayPush(outputCache.payload.seqValue, input);
      break;
    case SHType::Color: {
      shards::arrayPush(outputCache.payload.seqValue, Var(input.payload.colorValue.r));
      shards::arrayPush(outputCache.payload.seqValue, Var(input.payload.colorValue.g));
      shards::arrayPush(outputCache.payload.seqValue, Var(input.payload.colorValue.b));
      shards::arrayPush(outputCache.payload.seqValue, Var(input.payload.colorValue.a));
      break;
    }
    case SHType::Int2:
      for (auto i = 0; i < 2; i++)
        shards::arrayPush(outputCache.payload.seqValue, Var(input.payload.int2Value[i]));
      break;
    case SHType::Int3:
      for (auto i = 0; i < 3; i++)
        shards::arrayPush(outputCache.payload.seqValue, Var(input.payload.int3Value[i]));
      break;
    case SHType::Int4:
      for (auto i = 0; i < 4; i++)
        shards::arrayPush(outputCache.payload.seqValue, Var(input.payload.int4Value[i]));
      break;
    case SHType::Int8:
      for (auto i = 0; i < 8; i++)
        shards::arrayPush(outputCache.payload.seqValue, Var(input.payload.int8Value[i]));
      break;
    case SHType::Int16:
      for (auto i = 0; i < 16; i++)
        shards::arrayPush(outputCache.payload.seqValue, Var(input.payload.int16Value[i]));
      break;
    case SHType::Float2:
      for (auto i = 0; i < 2; i++)
        shards::arrayPush(outputCache.payload.seqValue, Var(input.payload.float2Value[i]));
      break;
    case SHType::Float3:
      for (auto i = 0; i < 3; i++)
        shards::arrayPush(outputCache.payload.seqValue, Var(input.payload.float3Value[i]));
      break;
    case SHType::Float4:
      for (auto i = 0; i < 4; i++)
        shards::arrayPush(outputCache.payload.seqValue, Var(input.payload.float4Value[i]));
      break;
    case SHType::Seq:
      for (uint32_t i = 0; i < input.payload.seqValue.len; i++) {
        add(input.payload.seqValue.elements[i]);
      }
      break;
    case SHType::Table: {
      auto &t = input.payload.tableValue;
      SHTableIterator tit;
      t.api->tableGetIterator(t, &tit);
      SHVar k;
      SHVar v;
      while (t.api->tableNext(t, &tit, &k, &v)) {
        add(k);
        add(v);
      }
    } break;
    case SHType::Set: {
      auto &s = input.payload.setValue;
      SHSetIterator sit;
      s.api->setGetIterator(s, &sit);
      SHVar v;
      while (s.api->setNext(s, &sit, &v)) {
        add(v);
      }
    } break;
    }
  }

  SHVar activate(SHContext *context, const SHVar &input) {
    outputCache.valueType = SHType::Seq;
    // Quick reset no deallocs, slow first run only
    shards::arrayResize(outputCache.payload.seqValue, 0);
    add(input);
    return outputCache;
  }
};

struct IndexOf {
  static inline Types OutputTypes = {{CoreInfo::IntSeqType, CoreInfo::IntType}};

  ParamVar _item{};
  SHSeq _results = {};
  bool _all = false;

  void destroy() {
    if (_results.elements) {
      shards::arrayFree(_results);
    }
  }

  void cleanup() { _item.cleanup(); }
  void warmup(SHContext *context) { _item.warmup(context); }

  static SHTypesInfo inputTypes() { return CoreInfo::AnySeqType; }
  SHTypesInfo outputTypes() { return OutputTypes; }

  static inline ParamsInfo params = ParamsInfo(ParamsInfo::Param("Item",
                                                                 SHCCSTR("The item to find the index of from the input, "
                                                                         "if it's a sequence it will try to match all "
                                                                         "the items in the sequence, in sequence."),
                                                                 CoreInfo::AnyType),
                                               ParamsInfo::Param("All",
                                                                 SHCCSTR("If true will return a sequence with all the indices of "
                                                                         "Item, empty sequence if not found."),
                                                                 CoreInfo::BoolType));

  static SHParametersInfo parameters() { return SHParametersInfo(params); }

  void setParam(int index, const SHVar &value) {
    if (index == 0)
      _item = value;
    else
      _all = value.payload.boolValue;
  }

  SHVar getParam(int index) {
    if (index == 0)
      return _item;
    else
      return Var(_all);
  }

  SHTypeInfo compose(const SHInstanceData &data) {
    if (_all)
      return CoreInfo::IntSeqType;
    else
      return CoreInfo::IntType;
  }

  SHVar activate(SHContext *context, const SHVar &input) {
    auto inputLen = input.payload.seqValue.len;
    auto itemLen = 0;
    auto item = _item.get();
    shards::arrayResize(_results, 0);
    if (item.valueType == SHType::Seq) {
      itemLen = item.payload.seqValue.len;
    }

    for (uint32_t i = 0; i < inputLen; i++) {
      if (item.valueType == SHType::Seq) {
        if ((i + itemLen) > inputLen) {
          if (!_all)
            return Var(-1); // we are likely at the end
          else
            return Var(_results);
        }

        auto ci = i;
        for (auto y = 0; y < itemLen; y++, ci++) {
          if (item.payload.seqValue.elements[y] != input.payload.seqValue.elements[ci]) {
            goto failed;
          }
        }

        if (!_all)
          return Var((int64_t)i);
        else
          shards::arrayPush(_results, Var((int64_t)i));

      failed:
        continue;
      } else if (input.payload.seqValue.elements[i] == item) {
        if (!_all)
          return Var((int64_t)i);
        else
          shards::arrayPush(_results, Var((int64_t)i));
      }
    }

    if (!_all)
      return Var(-1);
    else
      return Var(_results);
  }
};

struct Join {
  std::string _buffer;

  static inline Type InputType = Type::SeqOf(CoreInfo::StringOrBytes);

  static SHTypesInfo inputTypes() { return InputType; }
  static SHTypesInfo outputTypes() { return CoreInfo::BytesType; }

  SHVar activate(SHContext *context, const SHVar &input) {
    _buffer.clear();
    auto &seq = input.payload.seqValue;
    for (uint32_t i = 0; i < seq.len; i++) {
      auto &v = seq.elements[i];
      auto len = SHSTRLEN(v);
      auto &s = v.payload;
      // string and bytes share same layout!
      _buffer.insert(_buffer.end(), s.stringValue, s.stringValue + len);
    }

    return Var((uint8_t *)_buffer.c_str(), _buffer.size());
  }
};

struct Merge {
  PARAM_PARAMVAR(_target, "Target", "The table to merge into.",
                 {
                     CoreInfo::AnyVarTableType,
                 });
  PARAM_IMPL(PARAM_IMPL_FOR(_target));

  static SHTypesInfo inputTypes() { return CoreInfo::AnyTableType; }
  static SHTypesInfo outputTypes() { return CoreInfo::AnyTableType; }

  SHOptionalString help() {
    return SHCCSTR("Combine two tables into one, with the input table taking priority over the operand table, which will be "
                   "written and returned as output. This shard is useful in scenarios where you need to merge data from "
                   "different sources while keeping the priority of certain values.");
  }

  void cleanup() { PARAM_CLEANUP(); }

  void warmup(SHContext *context) { PARAM_WARMUP(context); }

  SHTypeInfo compose(const SHInstanceData &data) {
    if (!_target.isVariable()) {
      throw ComposeError("Target must be a variable");
    }

    for (auto &shared : data.shared) {
      if (strcmp(shared.name, _target.variableName()) == 0) {
        if (!shared.isMutable || shared.isProtected) {
          throw ComposeError("Target must be a mutable variable");
        }
        return shared.exposedType;
      }
    }

    throw ComposeError("Target variable not found");
  }

  SHVar activate(SHContext *context, const SHVar &input) {
    auto target = _target.get();
    auto &targetTable = asTable(target);
    ForEach(input.payload.tableValue, [&](auto &key, auto &val) { targetTable[key] = val; });
    return target;
  }
};

SHARDS_REGISTER_FN(seqs) {
  REGISTER_SHARD("Flatten", Flatten);
  REGISTER_SHARD("IndexOf", IndexOf);
  REGISTER_SHARD("Bytes.Join", Join);
  REGISTER_SHARD("Merge", Merge);
}
}; // namespace shards
