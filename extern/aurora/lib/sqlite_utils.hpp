#pragma once

#include "internal.hpp"
#include "io.hpp"

#include <filesystem>
#include <string>
#include <system_error>

#include <sqlite3.h>

namespace aurora::sqlite {

inline int exec(sqlite3* db, const char* sql) { return sqlite3_exec(db, sql, nullptr, nullptr, nullptr); }

template <typename T>
int exec(sqlite3* db, const char* sql, T callback, char** errmsg = nullptr) {
  return sqlite3_exec(
      db, sql,
      [](void* cb, int argc, char** argv, char** columns) -> int {
        auto& fp = *static_cast<T*>(cb);
        if constexpr (std::is_same_v<decltype(fp(0, 0, 0)), void>) {
          fp(argc, argv, columns);
          return 0;
        } else {
          return fp(argc, argv, columns);
        }
      },
      &callback, errmsg);
}

// True when the last error on db means the file itself is damaged (or not a database).
inline bool is_corrupt(sqlite3* db) {
  const int code = sqlite3_extended_errcode(db) & 0xff;
  return code == SQLITE_CORRUPT || code == SQLITE_NOTADB;
}

// Deletes a cache database with its WAL and shared-memory files. A .db copied
// over an install next to an older -wal/-shm reads as malformed, so they go together.
inline void delete_db_files(const std::string& file, Module& log) {
  for (const char* suffix : {"", "-wal", "-shm", "-journal"}) {
    std::error_code ec;
    std::filesystem::remove(io::fs_path_from_string(file + suffix), ec);
    if (ec) {
      log.warn("Failed to delete {}{}: {}", file, suffix, ec.message());
    }
  }
}

// Opens a cache database, deleting and recreating it when PRAGMA quick_check finds
// it damaged. A cache can always be rebuilt; a damaged one must never be fed to the
// GPU. Other failures (locked, unwritable) leave the files alone. Returns SQLITE_OK
// with *db open, or an error code with *db closed. quick_check reads every page, so
// a large cache passes quickCheck = false and relies on per-row checks instead.
inline int open_cache_db(const std::string& file, sqlite3** db, Module& log, bool quickCheck = true) {
  for (int attempt = 0;; ++attempt) {
    int ret = sqlite3_open(file.c_str(), db);
    bool damaged = false;
    if (ret == SQLITE_OK && !quickCheck) {
      return SQLITE_OK;
    }
    if (ret == SQLITE_OK) {
      bool ok = false;
      ret = exec(*db, "PRAGMA quick_check;", [&ok](int argc, char** argv, char**) {
        ok = argc == 1 && argv[0] != nullptr && std::string_view{argv[0]} == "ok";
        return ok ? 0 : 1;
      });
      if (ret == SQLITE_OK && ok) {
        return SQLITE_OK;
      }
      // SQLITE_ABORT: the callback stopped on a row that wasn't "ok".
      damaged = ret == SQLITE_ABORT || is_corrupt(*db);
    } else {
      damaged = is_corrupt(*db);
    }
    if (ret == SQLITE_OK) {
      ret = SQLITE_CORRUPT;
    }
    log.warn("Cache database {} failed its check: {}", file,
             ret == SQLITE_ABORT ? "quick_check found damage" : sqlite3_errmsg(*db));
    sqlite3_close(*db);
    *db = nullptr;
    if (!damaged || attempt > 0) {
      return ret;
    }
    log.warn("Deleting the damaged cache {}; it will be rebuilt", file);
    delete_db_files(file, log);
  }
}

class Transaction {
public:
  Transaction(sqlite3* db, Module& log, bool immediate = false) : m_db(db), m_log(log) {
    const auto type = immediate ? "BEGIN IMMEDIATE" : "BEGIN";
    const auto ret = sqlite3_exec(m_db, type, nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
      m_log.error("Failed to start transaction: {}", sqlite3_errmsg(m_db));
      return;
    }
    m_active = true;
  }

  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;
  Transaction(Transaction&&) = delete;
  Transaction& operator=(Transaction&&) = delete;

  ~Transaction() {
    if (!m_active) {
      return;
    }

    const auto ret = sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
      m_log.error("Failed to roll back transaction (uh oh?): {}", sqlite3_errmsg(m_db));
    }
  }

  void commit() {
    if (!m_active) {
      return;
    }

    const auto ret = sqlite3_exec(m_db, "COMMIT", nullptr, nullptr, nullptr);
    if (ret != SQLITE_OK) {
      m_log.error("Failed to commit transaction: {}", sqlite3_errmsg(m_db));
      return;
    }

    m_active = false;
  }

  explicit operator bool() const { return m_active; }

private:
  sqlite3* m_db = nullptr;
  Module& m_log;
  bool m_active = false;
};

} // namespace aurora::sqlite
