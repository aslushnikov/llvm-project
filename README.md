# KodSearch branch

Instructions:

```sh
$ git clone --single-branch --branch kodsearch https://github.com/aslushnikov/llvm-project
$ cd llvm-project
$ mkdir build
$ cd build
$ cmake -G Ninja -DLLVM_ENABLE_PROJECTS='clang;clang-tools-extra;' -DCMAKE_BUILD_TYPE=Release ../llvm/
$ cmake --build .
```

This will yield a `./bin/clangd-indexer` file that has a custom `--format=sqlite` option.
