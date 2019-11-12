# compile pass to generate shared lib
rm -rf build; mkdir build; cd build/; cmake ../; make; cd -
# compile to llvm bitcode with clang fromtemd
clang -c -emit-llvm -O0 -Xclang -disable-O0-optnone example.c -o original.bc
# generate llvm ir from bitcode
llvm-dis original.bc 
# optimze with llvm opt
opt -S -load build/skeleton/libSkeletonPass.so -mem2reg -mypass original.ll -o opt.ll
./a.out
