#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <filesystem>
#include <thread>
#include <vector>

#include "../internal.hpp"
#include "../io.hpp"
#include "../sqlite_utils.hpp"

#include <sqlite3.h>
#include <fmt/format.h>
#if defined(AURORA_CACHE_USE_ZSTD)
#include <zstd.h>
#endif
#define XXH_STATIC_LINKING_ONLY
#include <xxhash.h>

namespace aurora::webgpu {
static Module Log("aurora::gpu::cache");

static sqlite3* db;
static sqlite3_stmt* load_stmt;
static sqlite3_stmt* store_stmt;
static sqlite3_stmt* touch_stmt;
static bool cache_broken;
static std::mutex cache_mutex;
static std::vector<XXH128_hash_t> cache_keys_used;
#if defined(AURORA_CACHE_USE_ZSTD)
static std::vector<uint8_t> compress_buffer;
#endif
// Counts the launches that opened the cache; rows record the last one that used them.
static int64_t cache_launch;

// 3: rows carry an XXH3-64 checksum of the stored value.
// 4: rows carry last_used (a launch number); new files use incremental auto_vacuum.
constexpr int CACHE_SCHEMA = 4;
// % of rows pruned to trigger a full VACUUM
constexpr uint64_t VacuumPrunePercentThreshold = 25;
// Lazy pipelines never get a full-session prune (cache_prune needs every pipeline
// loaded), so the cache would grow forever across builds and drivers. Instead, a
// background pass drops the least recently used rows while the file holds more than
// AgedPruneCapBytes, sparing rows used in the last AgedPruneKeepLaunches launches.
constexpr uint64_t AgedPruneCapBytes = 256ull << 20;
constexpr int64_t AgedPruneKeepLaunches = 5;
constexpr int AgedPruneBatchRows = 128;
constexpr auto AgedPruneDelay = std::chrono::seconds(30);

static std::filesystem::path cache_path() {
  return io::fs_path_from_string(g_config.cachePath) / "dawn_cache.db";
}

// Set right after a failed operation found the file damaged, while a transaction
// may still hold db; the next cache_init() (or cache_shutdown()) does the abort.
static bool abort_pending;
static void note_if_corrupt() {
  if (db != nullptr && sqlite::is_corrupt(db)) {
    abort_pending = true;
  }
}

// Stops using the cache for this session. A damaged database is deleted so the
// next start rebuilds it instead of handing Dawn bad pipeline blobs.
static void cache_abort() {
  cache_broken = true;
  const bool corrupt = abort_pending || (db != nullptr && sqlite::is_corrupt(db));
  abort_pending = false;
  if (load_stmt != nullptr) {
    sqlite3_finalize(load_stmt);
    load_stmt = nullptr;
  }
  if (store_stmt != nullptr) {
    sqlite3_finalize(store_stmt);
    store_stmt = nullptr;
  }
  if (touch_stmt != nullptr) {
    sqlite3_finalize(touch_stmt);
    touch_stmt = nullptr;
  }
  sqlite3_close(db);
  db = nullptr;
  if (corrupt) {
    Log.warn("Dawn cache is damaged; deleting it");
    sqlite::delete_db_files(io::fs_path_to_string(cache_path()), Log);
  }
}


static int check(int ret) {
  if (ret != SQLITE_OK) {
    Log.error("SQLite operation failed: {}", sqlite3_errmsg(db));
  }

  return ret;
}

static std::optional<uint64_t> select_uint64(const char* sql) {
  sqlite3_stmt* stmt = nullptr;
  auto ret = sqlite3_prepare_v3(db, sql, -1, 0, &stmt, nullptr);
  if (ret != SQLITE_OK) {
    Log.error("Failed to prepare statement '{}': {}", sql, sqlite3_errmsg(db));
    return std::nullopt;
  }

  std::optional<uint64_t> result;
  ret = sqlite3_step(stmt);
  if (ret == SQLITE_ROW) {
    result = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
  } else if (ret != SQLITE_DONE) {
    Log.error("Failed to execute statement '{}': {}", sql, sqlite3_errmsg(db));
  }

  sqlite3_finalize(stmt);
  return result;
}

// The schema the file was written with: 0 for a new file, nullopt on error.
static std::optional<uint64_t> stored_schema() {
  const auto hasTable = select_uint64("SELECT COUNT(*) FROM sqlite_master WHERE name = 'aurora_schema'");
  if (!hasTable || *hasTable == 0) {
    return hasTable;
  }
  return select_uint64("SELECT COALESCE(MAX(value), 0) FROM aurora_schema");
}

// Creates the tables of a new file and bumps the launch counter.
static bool ensure_schema_up_to_date() {
  sqlite::Transaction tx(db, Log, true);
  if (!tx) {
    Log.error("Failed to open schema check transaction: {}", sqlite3_errmsg(db));
    return false;
  }

  auto ret = sqlite::exec(db, "CREATE TABLE IF NOT EXISTS aurora_schema(value INTEGER);");
  if (ret != SQLITE_OK) {
    Log.error("Failed to create schema table: {}", sqlite3_errmsg(db));
    return false;
  }

  bool match = false;
  auto cmd = fmt::format("SELECT * FROM aurora_schema WHERE value = {}", CACHE_SCHEMA);
  ret = sqlite::exec(db, cmd.c_str(), [&match](int, char**, char**) { match = true; }, nullptr);
  if (ret != SQLITE_OK) {
    Log.error("Failed to check schema table: {}", sqlite3_errmsg(db));
    return false;
  }

  if (!match) {
    cmd = fmt::format(
        R"(DROP TABLE IF EXISTS cache;
CREATE TABLE cache (
  key BLOB PRIMARY KEY NOT NULL,
  value BLOB NOT NULL,
  size INTEGER NOT NULL,
  compressed INTEGER NOT NULL,
  checksum INTEGER NOT NULL,
  last_used INTEGER NOT NULL
);
CREATE INDEX cache_last_used ON cache (last_used);
DROP TABLE IF EXISTS aurora_launch;
CREATE TABLE aurora_launch (value INTEGER NOT NULL);
INSERT INTO aurora_launch VALUES (0);
DELETE FROM aurora_schema;
INSERT INTO aurora_schema VALUES ({});)",
        CACHE_SCHEMA);
    ret = sqlite::exec(db, cmd.c_str());
    if (ret != SQLITE_OK) {
      Log.error("Failed to update schema: {}", sqlite3_errmsg(db));
      return false;
    }
  }

  ret = sqlite::exec(db, "UPDATE aurora_launch SET value = value + 1;");
  const auto launch = ret == SQLITE_OK ? select_uint64("SELECT value FROM aurora_launch") : std::nullopt;
  if (!launch) {
    Log.error("Failed to count the launch: {}", sqlite3_errmsg(db));
    return false;
  }
  cache_launch = static_cast<int64_t>(*launch);

  tx.commit();
  return true;
}

static int open_db(const std::string& file) {
  // quick_check reads every page: 7.5 s for an 800 MiB cache on a phone. Rows carry
  // their own checksums, and a damaged page found later deletes the file anyway.
  auto ret = sqlite::open_cache_db(file, &db, Log, false);
  if (ret != SQLITE_OK) {
    return ret;
  }
  // Takes effect only on a file without tables yet, so pruned pages can go back to
  // the filesystem; an older file keeps its mode.
  return sqlite::exec(db, "PRAGMA auto_vacuum=INCREMENTAL;");
}

static bool cache_init_core() {
  Log.debug("SQLite version {}", sqlite3_libversion());

  std::string file = io::fs_path_to_string(cache_path());
  Log.debug("Using dawn cache at {}", file);
  auto ret = open_db(file);
  if (ret != SQLITE_OK) {
    Log.error("Failed to open database {}", file);
    return false;
  }

  // An older schema would be dropped anyway; deleting the file instead also returns
  // its space, which a dropped table keeps as free pages.
  const auto schema = stored_schema();
  if (!schema) {
    note_if_corrupt();
    return false;
  }
  if (*schema != 0 && *schema != CACHE_SCHEMA) {
    Log.info("Dawn cache schema {} is outdated; starting a new cache", *schema);
    sqlite3_close(db);
    db = nullptr;
    sqlite::delete_db_files(file, Log);
    ret = open_db(file);
    if (ret != SQLITE_OK) {
      Log.error("Failed to open database {}", file);
      return false;
    }
  }

  // WAL mode + NORMAL = no need for disk syncs, consistent but not durable is fine.
  ret = sqlite::exec(db, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;");
  if (ret != SQLITE_OK) {
    Log.error("Failed to set pragmas: {}", sqlite3_errmsg(db));
    return false;
  }

  if (!ensure_schema_up_to_date()) {
    Log.error("Failed to validate schema");
    note_if_corrupt();
    return false;
  }

  ret = sqlite3_prepare_v3(db, "SELECT value, size, compressed, checksum, last_used FROM cache WHERE key = ?", -1,
                           SQLITE_PREPARE_PERSISTENT, &load_stmt, nullptr);
  if (ret != SQLITE_OK) {
    Log.error("Failed to prepare statement: {}", sqlite3_errmsg(db));
    return false;
  }

  ret = sqlite3_prepare_v3(
      db, "REPLACE INTO cache (key, value, size, compressed, checksum, last_used) VALUES (?, ?, ?, ?, ?, ?)", -1,
      SQLITE_PREPARE_PERSISTENT, &store_stmt, nullptr);
  if (ret != SQLITE_OK) {
    Log.error("Failed to prepare statement: {}", sqlite3_errmsg(db));
    return false;
  }

  ret = sqlite3_prepare_v3(db, "UPDATE cache SET last_used = ? WHERE key = ?", -1, SQLITE_PREPARE_PERSISTENT,
                           &touch_stmt, nullptr);
  if (ret != SQLITE_OK) {
    Log.error("Failed to prepare statement: {}", sqlite3_errmsg(db));
    return false;
  }

  return true;
}

static bool fill_used_key_table(const std::vector<XXH128_hash_t>& usedKeys) {
  auto ret = sqlite::exec(db,
                          "CREATE TEMP TABLE IF NOT EXISTS cache_keys_used ("
                          "key BLOB PRIMARY KEY NOT NULL"
                          ");"
                          "DELETE FROM cache_keys_used;");
  if (ret != SQLITE_OK) {
    Log.error("Failed to prepare dawn cache prune key table: {}", sqlite3_errmsg(db));
    return false;
  }

  sqlite3_stmt* stmt = nullptr;
  ret = sqlite3_prepare_v3(db, "INSERT OR IGNORE INTO cache_keys_used (key) VALUES (?)", -1, 0, &stmt, nullptr);
  if (ret != SQLITE_OK) {
    Log.error("Failed to prepare dawn cache prune key insert: {}", sqlite3_errmsg(db));
    return false;
  }

  for (const auto& keyHash : usedKeys) {
    ret = sqlite3_bind_blob(stmt, 1, &keyHash, sizeof(keyHash), SQLITE_TRANSIENT);
    if (ret != SQLITE_OK) {
      Log.error("Failed to bind dawn cache prune key: {}", sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return false;
    }

    ret = sqlite3_step(stmt);
    if (ret != SQLITE_DONE) {
      Log.error("Failed to insert dawn cache prune key: {}", sqlite3_errmsg(db));
      sqlite3_finalize(stmt);
      return false;
    }

    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
  }

  sqlite3_finalize(stmt);
  return true;
}

// Bytes in pages that hold data; free pages are reused before the file grows.
static std::optional<uint64_t> used_bytes() {
  const auto pages = select_uint64("PRAGMA page_count");
  const auto freePages = select_uint64("PRAGMA freelist_count");
  const auto pageSize = select_uint64("PRAGMA page_size");
  if (!pages || !freePages || !pageSize) {
    return std::nullopt;
  }
  return (*pages - std::min(*freePages, *pages)) * *pageSize;
}

static constexpr double to_mib(uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }

// Runs aged_prune once, AgedPruneDelay after the cache opens, so it stays out of
// startup. Each step holds cache_mutex briefly; a pipeline load waits one batch at most.
class AgedPruneThread {
public:
  ~AgedPruneThread() { stop(); }

  void start() {
    if (m_thread.joinable()) {
      return;
    }
    m_stop = false;
    m_thread = std::thread([this] { run(); });
  }

  void stop() {
    {
      std::lock_guard lock(m_mutex);
      m_stop = true;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) {
      m_thread.join();
    }
  }

  bool stopping() {
    std::lock_guard lock(m_mutex);
    return m_stop;
  }

private:
  void run();

  std::thread m_thread;
  std::mutex m_mutex;
  std::condition_variable m_cv;
  bool m_stop = false;
};
static AgedPruneThread aged_prune_thread;

static void aged_prune() {
  const int64_t keepAfter = cache_launch - AgedPruneKeepLaunches;
  {
    std::lock_guard lock(cache_mutex);
    if (db == nullptr) {
      return;
    }
    const auto rows = select_uint64("SELECT COUNT(*) FROM cache");
    const auto recentSql = fmt::format("SELECT COUNT(*) FROM cache WHERE last_used > {}", keepAfter);
    const auto recent = select_uint64(recentSql.c_str());
    const auto used = used_bytes();
    if (!rows || !recent || !used) {
      note_if_corrupt();
      return;
    }
    Log.info("Dawn cache: {} entries, {} used in the last {} launches, {:.1f} MiB", *rows, *recent,
             AgedPruneKeepLaunches, to_mib(*used));
    if (*used <= AgedPruneCapBytes) {
      return;
    }
  }

  const auto deleteSql = fmt::format("DELETE FROM cache WHERE key IN ("
                                     "SELECT key FROM cache WHERE last_used <= {} ORDER BY last_used LIMIT {})",
                                     keepAfter, AgedPruneBatchRows);
  sqlite3_int64 deletedRows = 0;
  std::optional<uint64_t> used;
  while (!aged_prune_thread.stopping()) {
    std::lock_guard lock(cache_mutex);
    if (db == nullptr) {
      return;
    }
    used = used_bytes();
    if (!used) {
      note_if_corrupt();
      return;
    }
    if (*used <= AgedPruneCapBytes) {
      break;
    }
    sqlite::Transaction tx(db, Log, true);
    if (!tx) {
      return;
    }
    if (sqlite::exec(db, deleteSql.c_str()) != SQLITE_OK) {
      Log.error("Failed to prune aged dawn cache rows: {}", sqlite3_errmsg(db));
      note_if_corrupt();
      return;
    }
    const auto changes = sqlite3_changes64(db);
    tx.commit();
    if (changes == 0) {
      break;
    }
    deletedRows += changes;
  }
  if (deletedRows == 0) {
    return;
  }
  Log.info("Pruned {} Dawn cache entries unused in the last {} launches; {:.1f} MiB in use", deletedRows,
           AgedPruneKeepLaunches, used ? to_mib(*used) : 0.0);

  // Hand the freed pages back to the filesystem (incremental auto_vacuum files only).
  bool incremental = false;
  {
    std::lock_guard lock(cache_mutex);
    incremental = db != nullptr && select_uint64("PRAGMA auto_vacuum") == 2u;
  }
  uint64_t lastFree = UINT64_MAX;
  while (incremental && !aged_prune_thread.stopping()) {
    std::lock_guard lock(cache_mutex);
    if (db == nullptr) {
      return;
    }
    const auto freePages = select_uint64("PRAGMA freelist_count");
    if (!freePages || *freePages == 0 || *freePages >= lastFree) {
      break;
    }
    lastFree = *freePages;
    if (sqlite::exec(db, "PRAGMA incremental_vacuum(1024);") != SQLITE_OK) {
      Log.warn("Failed to vacuum dawn cache: {}", sqlite3_errmsg(db));
      break;
    }
  }

  std::lock_guard lock(cache_mutex);
  if (db != nullptr && sqlite::exec(db, "PRAGMA wal_checkpoint(TRUNCATE);") != SQLITE_OK) {
    Log.warn("Failed to checkpoint dawn cache WAL: {}", sqlite3_errmsg(db));
  }
}

void AgedPruneThread::run() {
  {
    std::unique_lock lock(m_mutex);
    if (m_cv.wait_for(lock, AgedPruneDelay, [this] { return m_stop; })) {
      return;
    }
  }
  aged_prune();
}

static bool cache_init() {
  if (cache_broken) {
    return false;
  }

  if (abort_pending) {
    cache_abort();
    return false;
  }

  if (db) {
    return true;
  }

  if (!cache_init_core()) {
    Log.error("SQLite DB init failed");
    cache_abort();
    return false;
  }

  Log.debug("SQLite cache init succeeded");
  if (g_config.pipelineCacheLazy) {
    aged_prune_thread.start();
  }

  return true;
}

size_t load_from_cache(void const* key, size_t keySize, void* value, size_t valueSize, void*) {
  std::lock_guard lock(cache_mutex);

  if (!cache_init()) {
    return 0;
  }

  sqlite::Transaction tx(db, Log);
  if (!tx) {
    Log.error("Failed to open load transaction");
    return 0;
  }

  const auto keyHash = XXH128(key, keySize, 0);
  check(sqlite3_bind_blob(load_stmt, 1, &keyHash, sizeof(keyHash), SQLITE_TRANSIENT));

  const auto ret = sqlite3_step(load_stmt);
  size_t foundSize;
  bool loadSucceeded = false;
  bool stale = false;
  if (ret == SQLITE_ROW) {
    // Hit
    const auto foundPtr = sqlite3_column_blob(load_stmt, 0);
    const auto foundBytes = static_cast<size_t>(sqlite3_column_bytes(load_stmt, 0));
    foundSize = sqlite3_column_int64(load_stmt, 1);
    const bool compressed = sqlite3_column_int(load_stmt, 2) != 0;
    const auto checksum = static_cast<XXH64_hash_t>(sqlite3_column_int64(load_stmt, 3));
    stale = sqlite3_column_int64(load_stmt, 4) != cache_launch;

    // A row that doesn't match its checksum is reported as a miss, so Dawn
    // recompiles and overwrites it instead of passing garbage to the driver.
    if (XXH3_64bits(foundPtr, foundBytes) != checksum) {
      Log.warn("Dropping a Dawn cache entry with a bad checksum");
      foundSize = 0;
    } else if (value && valueSize == foundSize) {
      loadSucceeded = true;
      if (compressed) {
#if defined(AURORA_CACHE_USE_ZSTD)
        const auto compSize = sqlite3_column_bytes(load_stmt, 0);
        const auto zstdRet = ZSTD_decompress(value, valueSize, foundPtr, compSize);
        if (ZSTD_isError(zstdRet)) {
          Log.error("zstd decompression error: {}", ZSTD_getErrorName(zstdRet));
          foundSize = 0;
          loadSucceeded = false;
        } else if (zstdRet != foundSize) {
          Log.error("zstd decompression size mismatch: expected {}, got {}", foundSize, zstdRet);
          foundSize = 0;
          loadSucceeded = false;
        }
#else
        Log.error("Cache entry is zstd-compressed but zstd support is disabled");
        foundSize = 0;
        loadSucceeded = false;
#endif
      } else {
        if (foundSize != 0 && !foundPtr) {
          Log.error("Cache entry is missing raw value data");
          foundSize = 0;
          loadSucceeded = false;
        } else if (foundSize != 0) {
          std::memcpy(value, foundPtr, foundSize);
        }
      }
    }
  } else if (ret == SQLITE_DONE) {
    // Miss
    foundSize = 0;
  } else {
    Log.error("Looking up cache key failed: {}", sqlite3_errmsg(db));
    note_if_corrupt();
    sqlite3_reset(load_stmt);
    return 0;
  }

  check(sqlite3_reset(load_stmt));

  if (loadSucceeded) {
    cache_keys_used.push_back(keyHash);
    // Marks the row used by this launch, once per launch, for the aged prune.
    if (stale) {
      check(sqlite3_bind_int64(touch_stmt, 1, cache_launch));
      check(sqlite3_bind_blob(touch_stmt, 2, &keyHash, sizeof(keyHash), SQLITE_TRANSIENT));
      if (sqlite3_step(touch_stmt) == SQLITE_DONE) {
        tx.commit();
      } else {
        Log.warn("Failed to mark a Dawn cache entry used: {}", sqlite3_errmsg(db));
        note_if_corrupt();
      }
      sqlite3_reset(touch_stmt);
    }
  }

  return foundSize;
}

void store_to_cache(void const* key, size_t keySize, void const* value, size_t valueSize, void*) {
  std::lock_guard lock(cache_mutex);

  if (!cache_init()) {
    return;
  }

  sqlite::Transaction tx(db, Log, true);
  if (!tx) {
    Log.error("Failed to open store transaction");
    return;
  }

  const void* storedValue = value;
  sqlite3_uint64 storedValueSize = valueSize;
  int compressed = 0;
#if defined(AURORA_CACHE_USE_ZSTD)
  const auto bound = ZSTD_compressBound(valueSize);
  if (ZSTD_isError(bound)) {
    Log.error("Failed to calculate ZSTD_compressBound: {}", ZSTD_getErrorName(bound));
    return;
  }

  if (compress_buffer.size() < bound) {
    compress_buffer.resize(bound);
  }

  const auto compressRet = ZSTD_compress(compress_buffer.data(), compress_buffer.size(), value, valueSize, 0);
  if (ZSTD_isError(compressRet)) {
    Log.error("ZSTD compression error: {}", ZSTD_getErrorName(compressRet));
    return;
  }

  if (compressRet < valueSize) {
    storedValue = compress_buffer.data();
    storedValueSize = compressRet;
    compressed = 1;
  }
#endif

  const auto keyHash = XXH128(key, keySize, 0);
  check(sqlite3_bind_blob64(store_stmt, 1, &keyHash, sizeof(keyHash), SQLITE_TRANSIENT));
  check(
      sqlite3_bind_blob64(store_stmt, 2, storedValue, storedValueSize, compressed ? SQLITE_STATIC : SQLITE_TRANSIENT));
  check(sqlite3_bind_int64(store_stmt, 3, static_cast<sqlite3_int64>(valueSize)));
  check(sqlite3_bind_int(store_stmt, 4, compressed));
  check(sqlite3_bind_int64(store_stmt, 5,
                           static_cast<sqlite3_int64>(XXH3_64bits(storedValue, static_cast<size_t>(storedValueSize)))));
  check(sqlite3_bind_int64(store_stmt, 6, cache_launch));

  const auto ret = sqlite3_step(store_stmt);
  if (ret != SQLITE_DONE) {
    // Error or something
    Log.error("Failed to insert row: {}", sqlite3_errmsg(db));
    note_if_corrupt();
    sqlite3_reset(store_stmt);
    return;
  }

  check(sqlite3_reset(store_stmt));
  check(sqlite3_bind_null(store_stmt, 2));
  check(sqlite3_bind_null(store_stmt, 4));

  tx.commit();
  cache_keys_used.push_back(keyHash);
}

void cache_prune() {
  std::lock_guard lock(cache_mutex);
  if (!cache_init()) {
    return;
  }
  if (cache_keys_used.empty()) {
    Log.warn("Skipping Dawn cache prune because no cache keys were used");
    return;
  }

  uint64_t totalRows = 0;
  sqlite3_int64 deletedRows = 0;
  {
    sqlite::Transaction tx(db, Log, true);
    if (!tx) {
      Log.error("Failed to open dawn cache prune transaction");
      return;
    }

    if (!fill_used_key_table(cache_keys_used)) {
      note_if_corrupt();
      return;
    }

    const auto totalRowsResult = select_uint64("SELECT COUNT(*) FROM cache");
    if (!totalRowsResult) {
      note_if_corrupt();
      return;
    }
    totalRows = *totalRowsResult;

    const auto ret = sqlite::exec(db,
                                  "DELETE FROM cache "
                                  "WHERE NOT EXISTS ("
                                  "  SELECT 1 FROM cache_keys_used WHERE cache_keys_used.key = cache.key"
                                  ")");
    if (ret != SQLITE_OK) {
      Log.error("Failed to prune dawn cache rows: {}", sqlite3_errmsg(db));
      note_if_corrupt();
      return;
    }
    deletedRows = sqlite3_changes64(db);

    tx.commit();
  }

  if (deletedRows == 0) {
    Log.debug("Dawn cache prune completed; no stale entries found");
    return;
  }

  Log.info("Pruned {} stale Dawn cache entries", deletedRows);

  // VACUUM if we removed at least 25% of the rows
  if (totalRows != 0 && static_cast<uint64_t>(deletedRows) * 100ull >= totalRows * VacuumPrunePercentThreshold) {
    if (const auto ret = sqlite::exec(db, "VACUUM;"); ret != SQLITE_OK) {
      Log.warn("Failed to vacuum dawn cache after pruning: {}", sqlite3_errmsg(db));
      return;
    }
  }

  if (const auto ret = sqlite::exec(db, "PRAGMA wal_checkpoint(TRUNCATE);"); ret != SQLITE_OK) {
    Log.warn("Failed to checkpoint dawn cache WAL: {}", sqlite3_errmsg(db));
  }
}

void cache_shutdown() {
  aged_prune_thread.stop();
#if defined(AURORA_CACHE_USE_ZSTD)
  compress_buffer.clear();
#endif
  cache_keys_used.clear();
  if (abort_pending) {
    cache_abort();
    return;
  }
  if (load_stmt != nullptr) {
    check(sqlite3_finalize(load_stmt));
    load_stmt = nullptr;
  }
  if (store_stmt != nullptr) {
    check(sqlite3_finalize(store_stmt));
    store_stmt = nullptr;
  }
  if (touch_stmt != nullptr) {
    check(sqlite3_finalize(touch_stmt));
    touch_stmt = nullptr;
  }
  if (db != nullptr) {
    check(sqlite3_close(db));
    db = nullptr;
  }
}

} // namespace aurora::webgpu
