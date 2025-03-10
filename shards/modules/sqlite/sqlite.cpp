#include <cstdint>
#include <shards/core/module.hpp>
#include <shards/core/foundation.hpp>
#include <shards/core/shared.hpp>
#include <shards/core/params.hpp>
#include <shards/utility.hpp>
#include <functional>
#include <optional>

#pragma clang attribute push(__attribute__((no_sanitize("undefined"))), apply_to = function)
#include "sqlite3.h"
#pragma clang attribute pop

namespace shards {
namespace DB {
struct Connection {
  sqlite3 *db;

  Connection(const char *path) {
    if (sqlite3_open(path, &db) != SQLITE_OK) {
      throw ActivationError(sqlite3_errmsg(db));
    }

    uint32_t res;
    if (sqlite3_db_config(db, SQLITE_DBCONFIG_ENABLE_LOAD_EXTENSION, 2, &res) != SQLITE_OK) {
      throw ActivationError(sqlite3_errmsg(db));
    }
  }

  Connection(const Connection &) = delete;

  Connection(Connection &&other) {
    db = other.db;
    other.db = nullptr;
  }

  ~Connection() { sqlite3_close(db); }

  sqlite3 *get() { return db; }

  void loadExtension(const std::string &path) {
    if (sqlite3_load_extension(db, path.c_str(), nullptr, nullptr) != SQLITE_OK) {
      throw ActivationError(sqlite3_errmsg(db));
    }
  }
};

struct Statement {
  sqlite3_stmt *stmt;

  Statement(sqlite3 *db, const char *query) {
    if (sqlite3_prepare_v2(db, query, -1, &stmt, nullptr) != SQLITE_OK) {
      throw ActivationError(sqlite3_errmsg(db));
    }
  }

  Statement(const Statement &) = delete;

  Statement(Statement &&other) {
    stmt = other.stmt;
    other.stmt = nullptr;
  }

  ~Statement() { sqlite3_finalize(stmt); }

  sqlite3_stmt *get() { return stmt; }
};

struct Base {
  AnyStorage<Connection> _connection;
  std::string_view _dbName{"shards.db"};

  void warmup(SHContext *context) {
    auto storageKey = fmt::format("DB.Connection_{}", _dbName);
    auto mesh = context->main->mesh.lock();
    _connection = getOrCreateAnyStorage(mesh.get(), storageKey, [&]() { return Connection(_dbName.data()); });
  }

  void cleanup() {}
};

struct Query : public Base {
  static SHTypesInfo inputTypes() { return CoreInfo::AnySeqType; }
  static SHTypesInfo outputTypes() { return CoreInfo::AnyTableType; }

  PARAM_VAR(_query, "Query", "The database query to execute every activation.", {CoreInfo::StringType});
  PARAM_VAR(_dbName, "Database", "The optional sqlite database filename.", {CoreInfo::NoneType, CoreInfo::StringType});
  PARAM_IMPL(PARAM_IMPL_FOR(_query), PARAM_IMPL_FOR(_dbName));

  void cleanup() {
    PARAM_CLEANUP();

    prepared.reset();

    Base::cleanup();
  }

  std::unique_ptr<Statement> prepared;

  void warmup(SHContext *context) {
    if (_dbName.valueType != SHType::None) {
      Base::_dbName = SHSTRVIEW(_dbName);
    } else {
      Base::_dbName = "shards.db";
    }

    Base::warmup(context);

    PARAM_WARMUP(context);
  }

  TableVar output;
  std::vector<SeqVar *> colSeqs;

  SHVar activate(SHContext *context, const SHVar &input) {
    if (!prepared) {
      prepared.reset(new Statement(_connection->get(), _query.payload.stringValue)); // _query is full terminated cos cloned
    }

    sqlite3_reset(prepared->get());
    sqlite3_clear_bindings(prepared->get());

    int rc;

    int idx = 0;
    for (auto value : input) {
      idx++; // starting from 1 with sqlite
      switch (value.valueType) {
      case SHType::Bool:
        rc = sqlite3_bind_int(prepared->get(), idx, int(value.payload.boolValue));
        break;
      case SHType::Int:
        rc = sqlite3_bind_int64(prepared->get(), idx, value.payload.intValue);
        break;
      case SHType::Float:
        rc = sqlite3_bind_double(prepared->get(), idx, value.payload.floatValue);
        break;
      case SHType::String: {
        auto sv = SHSTRVIEW(value);
        rc = sqlite3_bind_text(prepared->get(), idx, sv.data(), sv.size(), SQLITE_STATIC);
      } break;
      case SHType::Bytes:
        rc = sqlite3_bind_blob(prepared->get(), idx, value.payload.bytesValue, value.payload.bytesSize, SQLITE_STATIC);
        break;
      case SHType::None:
        rc = sqlite3_bind_null(prepared->get(), idx);
        break;
      default:
        throw ActivationError("Unsupported Var type for sqlite");
      }
      if (rc != SQLITE_OK) {
        throw ActivationError(sqlite3_errmsg(_connection->get()));
      }
    }

    colSeqs.clear();
    while ((rc = sqlite3_step(prepared->get())) == SQLITE_ROW) {
      auto count = sqlite3_column_count(prepared->get());
      // fill column cache on first row
      if (colSeqs.empty()) {
        for (int i = 0; i < count; i++) {
          auto colName = sqlite3_column_name(prepared->get(), i);
          auto colNameVar = Var(colName);
          auto &col = output[colNameVar];
          if (col.valueType == SHType::None) {
            SeqVar seq;
            output.insert(colNameVar, std::move(seq));
            colSeqs.push_back(&asSeq(output[colNameVar]));
          } else {
            auto &seq = asSeq(col);
            seq.clear();
            colSeqs.push_back(&seq);
          }
        }
      }

      for (auto i = 0; i < count; i++) {
        auto &col = *colSeqs[i];
        auto type = sqlite3_column_type(prepared->get(), i);
        switch (type) {
        case SQLITE_INTEGER:
          col.push_back(Var((int64_t)sqlite3_column_int64(prepared->get(), i)));
          break;
        case SQLITE_FLOAT:
          col.push_back(Var(sqlite3_column_double(prepared->get(), i)));
          break;
        case SQLITE_TEXT:
          col.push_back(Var((const char *)sqlite3_column_text(prepared->get(), i)));
          break;
        case SQLITE_BLOB:
          col.push_back(
              Var((const uint8_t *)sqlite3_column_blob(prepared->get(), i), (uint32_t)sqlite3_column_bytes(prepared->get(), i)));
          break;
        case SQLITE_NULL:
          col.push_back(Var::Empty);
          break;
        default:
          throw ActivationError("Unsupported Var type for sqlite");
        }
      }
    }

    if (rc != SQLITE_DONE) {
      throw ActivationError(sqlite3_errmsg(_connection->get()));
    }

    return output;
  }
};

struct Transaction : public Base {
  static SHTypesInfo inputTypes() { return CoreInfo::AnyType; }
  static SHTypesInfo outputTypes() { return CoreInfo::AnyType; }

  PARAM(ShardsVar, _queries, "Queries", "The Shards logic executing various DB queries.", {CoreInfo::ShardsOrNone});
  PARAM_VAR(_dbName, "Database", "The optional sqlite database filename.", {CoreInfo::NoneType, CoreInfo::StringType});
  PARAM_IMPL(PARAM_IMPL_FOR(_queries), PARAM_IMPL_FOR(_dbName));

  SHTypeInfo compose(SHInstanceData &data) {
    _queries.compose(data);
    return data.inputType;
  }

  void cleanup() {
    PARAM_CLEANUP();

    Base::cleanup();
  }

  void warmup(SHContext *context) {
    if (_dbName.valueType != SHType::None) {
      Base::_dbName = SHSTRVIEW(_dbName);
    } else {
      Base::_dbName = "shards.db";
    }

    Base::warmup(context);

    PARAM_WARMUP(context);
  }

  SHVar activate(SHContext *context, const SHVar &input) {
    auto rc = sqlite3_exec(_connection->get(), "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
      throw ActivationError(sqlite3_errmsg(_connection->get()));
    }
    SHVar output{};
    auto state = _queries.activate(context, input, output);
    if (state != SHWireState::Continue) {
      // likely something went wrong! lets rollback.
      auto rc = sqlite3_exec(_connection->get(), "ROLLBACK;", nullptr, nullptr, nullptr);
      if (rc != SQLITE_OK) {
        throw ActivationError(sqlite3_errmsg(_connection->get()));
      }
    } else {
      // commit
      auto rc = sqlite3_exec(_connection->get(), "COMMIT;", nullptr, nullptr, nullptr);
      if (rc != SQLITE_OK) {
        throw ActivationError(sqlite3_errmsg(_connection->get()));
      }
    }
    return input;
  }
};

struct LoadExtension : public Base {
  static SHTypesInfo inputTypes() { return CoreInfo::AnyType; }
  static SHTypesInfo outputTypes() { return CoreInfo::AnyType; }

  PARAM_VAR(_extPath, "Path", "The path to the extension to load.", {CoreInfo::StringType});
  PARAM_VAR(_dbName, "Database", "The optional sqlite database filename.", {CoreInfo::NoneType, CoreInfo::StringType});
  PARAM_IMPL(PARAM_IMPL_FOR(_extPath), PARAM_IMPL_FOR(_dbName));

  void cleanup() {
    PARAM_CLEANUP();

    Base::cleanup();
  }

  void warmup(SHContext *context) {
    if (_dbName.valueType != SHType::None) {
      Base::_dbName = SHSTRVIEW(_dbName);
    } else {
      Base::_dbName = "shards.db";
    }

    Base::warmup(context);

    std::string extPath(_extPath.payload.stringValue, _extPath.payload.stringLen);
    _connection->loadExtension(extPath);

    PARAM_WARMUP(context);
  }

  SHVar activate(SHContext *context, const SHVar &input) { return input; }
};

struct RawQuery : public Base {
  static SHTypesInfo inputTypes() { return CoreInfo::StringType; }
  static SHTypesInfo outputTypes() { return CoreInfo::StringType; }

  PARAM_VAR(_dbName, "Database", "The optional sqlite database filename.", {CoreInfo::NoneType, CoreInfo::StringType});
  PARAM_IMPL(PARAM_IMPL_FOR(_dbName));

  void cleanup() {
    PARAM_CLEANUP();

    prepared.reset();

    Base::cleanup();
  }

  std::unique_ptr<Statement> prepared;

  void warmup(SHContext *context) {
    if (_dbName.valueType != SHType::None) {
      Base::_dbName = SHSTRVIEW(_dbName);
    } else {
      Base::_dbName = "shards.db";
    }

    Base::warmup(context);

    PARAM_WARMUP(context);
  }

  TableVar output;
  std::vector<SeqVar *> colSeqs;

  SHVar activate(SHContext *context, const SHVar &input) {
    char *errMsg = nullptr;
    std::string query(input.payload.stringValue, input.payload.stringLen); // we need to make sure we are 0 terminated
    int rc = sqlite3_exec(_connection->get(), query.c_str(), nullptr, nullptr, &errMsg);

    if (rc != SQLITE_OK) {
      throw ActivationError(errMsg);
      sqlite3_free(errMsg);
    }

    return input;
  }
};

} // namespace DB
} // namespace shards
SHARDS_REGISTER_FN(sqlite) {
  using namespace shards::DB;
  REGISTER_SHARD("DB.Query", Query);
  REGISTER_SHARD("DB.Transaction", Transaction);
  REGISTER_SHARD("DB.LoadExtension", LoadExtension);
  REGISTER_SHARD("DB.RawQuery", RawQuery);
}
