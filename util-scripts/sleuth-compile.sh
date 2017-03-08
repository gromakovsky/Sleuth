clang --std=c++11 -S -emit-llvm "$1" -g -o tmp_xxx_228
llvm-as < tmp_xxx_228 | opt -mem2reg | llvm-dis > out.ll
rm -f tmp_xxx_228
