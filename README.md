# KodSearch branch

### Compilation Instructions

```sh
$ git clone --single-branch --branch kodsearch https://github.com/aslushnikov/llvm-project
$ cd llvm-project
$ mkdir build
$ cd build
$ cmake -G Ninja -DLLVM_ENABLE_PROJECTS='clang;clang-tools-extra;' -DCMAKE_BUILD_TYPE=Release ../llvm/
$ cmake --build .
```

This will yield a `./bin/clangd-indexer` file that has a custom `--format=sqlite` option.

### Running

To run indexer:

```sh
$ ./bin/clangd-indexer path-to-compile-commands-json --executor=all-TUs --format=sqlite --execute-concurrency=0
```

The indexer produces a `db.sqlite` in `$PWD` which is an `sqlite3` database.

### SQLite Index Format

The `db.sqlite` database has the following tables:

1. table `PATHS` - list of all indexed paths.
    * column `pathid`: number
    * column `path`: text, an absolute file URL.
2. table `SYMBOLS` - list of all indexed symbols.
    * column `usr`: number - symbol ID. Historically called `USR` since that's what it is in clang-world.
    * column `type`: number.
        * `1` - symbol is a reference
        * `2` - symbol is a definition
        * `3` - symbol is a declaration
    * column `offset1`: number - start offset in file, 0-based
    * column `offset2`: number - end offset in file, 0-based
    * column `pathid`: number - id of file path
3. table `RELATIONS` - list of all relations between symbols
    * column `subject_usr`: number - subject symbol ID
    * column `object_usr`: number - object symbol ID
    * column `predicate`: number
        * `1` - `subject_usr` "is base of " `object_usr`
        * `2` - `subject_usr` "is overriden by" `object_usr`

> **NOTE** Symbols in the database might overlap with each other.
