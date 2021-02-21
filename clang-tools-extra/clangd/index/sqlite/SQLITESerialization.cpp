#include "../Index.h"
#include "../Relation.h"
#include "../Serialization.h"
#include "../SymbolLocation.h"
#include "../SymbolOrigin.h"
#include "./sqlite3.h"
#include <cstdint>

namespace{

class SQLite {
 public:
  static std::unique_ptr<SQLite> create(std::string filepath);

  SQLite () { }
  ~SQLite() {
    sqlite3_finalize(insert_file_stmt_);
    sqlite3_finalize(insert_record_stmt_);
    sqlite3_close(db_);
  }

  void persist(const clang::clangd::IndexFileOut& O);

 private:
  bool execute(const char* sql);

  sqlite3 *db_ = nullptr;
  sqlite3_stmt* insert_file_stmt_ = nullptr;
  sqlite3_stmt* insert_record_stmt_ = nullptr;
  sqlite3_stmt* insert_relation_stmt_ = nullptr;
};

static int callback(void *NotUsed, int argc, char **argv, char **azColName) {
   int i;
   for(i = 0; i<argc; i++) {
      printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
   }
   printf("\n");
   return 0;
}

// static
std::unique_ptr<SQLite> SQLite::create(std::string filepath) {
  std::unique_ptr<SQLite> result = std::make_unique<SQLite>();
  std::remove(filepath.c_str());
  int rc = sqlite3_open(filepath.c_str(), &result->db_);
  if (rc) {
    fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(result->db_));
    return nullptr;
  }
  if (!result->execute(
        "CREATE TABLE PATHS("
        "  pathid         INT     NOT NULL,"
        "  path           TEXT    NOT NULL);"
      )) {
    return nullptr;
  }
  if (!result->execute(
        "CREATE TABLE SYMBOLS("
        "  pathid     INT         NOT NULL,"
        "  usr        INT         NOT NULL,"
        "  loc1       INT         NOT NULL,"
        "  loc2       INT         NOT NULL,"
        "  type       INT         NOT NULL);"
      )) {
    return nullptr;
  }
  if (!result->execute(
        "CREATE TABLE RELATIONS("
        "  subject_usr    INT         NOT NULL,"
        "  predicate      INT         NOT NULL,"
        "  object_usr     INT         NOT NULL);"
      )) {
    return nullptr;
  }

  if (!result->execute(
        "CREATE INDEX idx_paths_pathid on PATHS(pathid);"
      )) {
    return nullptr;
  }
  if (!result->execute(
        "CREATE INDEX idx_relations_subject_usr on RELATIONS(subject_usr);"
      )) {
    return nullptr;
  }
  if (!result->execute(
        "CREATE INDEX idx_relations_object_usr on RELATIONS(object_usr);"
      )) {
    return nullptr;
  }
  if (!result->execute(
        "CREATE INDEX idx_symbols_pathid on SYMBOLS(pathid);"
      )) {
    return nullptr;
  }
  if (!result->execute(
        "CREATE INDEX idx_symbols_usr on SYMBOLS(usr, type);"
      )) {
    return nullptr;
  }

  const char* insert_path_sql = "INSERT INTO PATHS (PATHID, PATH) VALUES (?, ?);";
  if (sqlite3_prepare_v2(result->db_, insert_path_sql, -1, &result->insert_file_stmt_, NULL) != SQLITE_OK) {
    fprintf(stderr, "Prepare error: %s\n", sqlite3_errmsg(result->db_));
    return nullptr;
  }

  const char* insert_relation_sql = "INSERT INTO RELATIONS (SUBJECT_USR, PREDICATE, OBJECT_USR) VALUES (?, ?, ?);";
  if (sqlite3_prepare_v2(result->db_, insert_relation_sql, -1, &result->insert_relation_stmt_, NULL) != SQLITE_OK) {
    fprintf(stderr, "Prepare error: %s\n", sqlite3_errmsg(result->db_));
    return nullptr;
  }

  const char* insert_symbol_sql = "INSERT INTO SYMBOLS (PATHID, USR, LOC1, LOC2, TYPE) VALUES (?, ?, ?, ?, ?);";
  if (sqlite3_prepare_v2(result->db_, insert_symbol_sql, -1, &result->insert_record_stmt_, NULL) != SQLITE_OK) {
    fprintf(stderr, "Prepare error: %s\n", sqlite3_errmsg(result->db_));
    return nullptr;
  }

  return result;
}

void SQLite::persist(const clang::clangd::IndexFileOut& O) {
  sqlite3_exec(db_, "BEGIN TRANSACTION;", NULL, NULL, NULL);

  std::map<llvm::StringRef, int> paths;
  if (O.Refs) {
    for (auto &Refs : *O.Refs) {
      for (auto &R : Refs.second) {
        llvm::StringRef file = llvm::StringRef(R.Location.FileURI);
        if (paths.find(file) == paths.end()) {
          auto hash = std::hash<const char*>()(file.data());
          paths.insert(std::make_pair(file, hash));
        }
      }
    }
  }

  // Insert all paths.
  for (auto &path_and_hash : paths) {
    sqlite3_reset(insert_file_stmt_);
    if (sqlite3_bind_int(insert_file_stmt_, 1, path_and_hash.second) != SQLITE_OK) {
      fprintf(stderr, "Bind Error while binding pathid!\n- %s\n- error: %s\n", path_and_hash.first.data(), sqlite3_errmsg(db_));
      continue;
    }
    if (sqlite3_bind_text(insert_file_stmt_, 2, path_and_hash.first.data(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
      fprintf(stderr, "Bind error while binding path!\n- %s\n- error: %s\n", path_and_hash.first.data(), sqlite3_errmsg(db_));
      continue;
    }
    if (sqlite3_step(insert_file_stmt_) != SQLITE_DONE) {
      fprintf(stderr, "Step error while inserting path!\n- %s\n- error: %s\n", path_and_hash.first.data(), sqlite3_errmsg(db_));
      continue;
    }
  }

  if (O.Refs) {
    for (auto &Refs : *O.Refs) {
      unsigned long long symbol_id = Refs.first.number();
      for (auto &R : Refs.second) {
        sqlite3_reset(insert_record_stmt_);
        llvm::StringRef file = llvm::StringRef(R.Location.FileURI);
        auto pathid = paths[file];
        if (sqlite3_bind_int(insert_record_stmt_, 1, pathid) != SQLITE_OK) {
          fprintf(stderr, "Bind Error while binding record.pathid!\n- error: %s\n", sqlite3_errmsg(db_));
          continue;
        }
        if (sqlite3_bind_int(insert_record_stmt_, 2, symbol_id) != SQLITE_OK) {
          fprintf(stderr, "Bind Error while binding record.usr!\n- error: %s\n", sqlite3_errmsg(db_));
          continue;
        }
        if (sqlite3_bind_int(insert_record_stmt_, 3, R.Location.Start.rep()) != SQLITE_OK) {
          fprintf(stderr, "Bind Error while binding record.loc1!\n- error: %s\n", sqlite3_errmsg(db_));
          continue;
        }
        if (sqlite3_bind_int(insert_record_stmt_, 4, R.Location.End.rep()) != SQLITE_OK) {
          fprintf(stderr, "Bind Error while binding record.loc2!\n- error: %s\n", sqlite3_errmsg(db_));
          continue;
        }
        uint8_t original_kind = static_cast<uint8_t>(R.Kind);
        int kind = 0;
        if ((original_kind & static_cast<uint8_t>(clang::clangd::RefKind::Declaration)) > 0)
          kind = 3;
        else if ((original_kind & static_cast<uint8_t>(clang::clangd::RefKind::Definition)) > 0)
          kind = 2;
        else if ((original_kind & static_cast<uint8_t>(clang::clangd::RefKind::Reference)) > 0)
          kind = 1;
        else
          continue;
        if (sqlite3_bind_int(insert_record_stmt_, 5, kind) != SQLITE_OK) {
          fprintf(stderr, "Bind Error while binding record.type!\n- error: %s\n", sqlite3_errmsg(db_));
          continue;
        }
        if (sqlite3_step(insert_record_stmt_) != SQLITE_DONE) {
          fprintf(stderr, "Step Error white inserting record!\n- error: %s\n", sqlite3_errmsg(db_));
          continue;
        }
      }
    }
  }
  if (O.Relations) {
    for (auto &R : *O.Relations) {
      unsigned long long subj_id = R.Subject.number();
      unsigned long long obj_id = R.Object.number();
      uint8_t kind = 0;
      if (R.Predicate == clang::clangd::RelationKind::BaseOf) {
        kind = 1;
      } else if (R.Predicate == clang::clangd::RelationKind::OverriddenBy) {
        kind = 2;
      } else {
        fprintf(stderr, "WARN: new relationship kind is available!");
        continue;
      }
      sqlite3_reset(insert_relation_stmt_);
      if (sqlite3_bind_int(insert_relation_stmt_, 1, subj_id) != SQLITE_OK) {
        fprintf(stderr, "Bind Error while binding relation subject_usr!\n- error: %s\n", sqlite3_errmsg(db_));
        continue;
      }
      if (sqlite3_bind_int(insert_relation_stmt_, 2, kind) != SQLITE_OK) {
        fprintf(stderr, "Bind Error while binding relation kind!\n- error: %s\n", sqlite3_errmsg(db_));
        continue;
      }
      if (sqlite3_bind_int(insert_relation_stmt_, 3, obj_id) != SQLITE_OK) {
        fprintf(stderr, "Bind Error while binding relation object_usr!\n- error: %s\n", sqlite3_errmsg(db_));
        continue;
      }
      if (sqlite3_step(insert_relation_stmt_) != SQLITE_DONE) {
        fprintf(stderr, "Step Error white inserting record!\n- error: %s\n", sqlite3_errmsg(db_));
        continue;
      }
    }
  }

  sqlite3_exec(db_, "END TRANSACTION;", NULL, NULL, NULL);
}

bool SQLite::execute(const char* sql) {
  char *zErrMsg = 0;
  int rc = sqlite3_exec(db_, sql, callback, 0, &zErrMsg);
  if (rc != SQLITE_OK) {
    fprintf(stderr, "SQL error: %s\n", zErrMsg);
    sqlite3_free(zErrMsg);
    return false;
  }
  return true;
}

} // namespace

namespace clang {
namespace clangd {

void writeSQLITE(const IndexFileOut &O, llvm::raw_ostream &OS) {
  auto sqlite = SQLite::create("db.sqlite");
  sqlite->persist(O);
}

} // namespace clangd
} // namespace clang
