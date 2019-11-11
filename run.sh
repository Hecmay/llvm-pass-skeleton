rm -rf build; mkdir build; cd build/; cmake ../; make; cd -
clang -Xclang -load -Xclang build/skeleton/libSkeletonPass.so example.c
./a.out
