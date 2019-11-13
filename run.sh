set -x

# setup env variable 
export PATH="/work/zhang-x1/common/install/llvm-8.0/build/bin/":$PATH
export LLVM_DIR=/work/zhang-x1/common/install/llvm-8.0/build/lib/cmake/llvm/
export EMBENCH_DIR=/work/zhang-x1/users/yz882/cs6120/llvm-pass-skeleton/embench-iot/
# compile pass to generate shared lib
#rm -rf build; mkdir build; cd build/; cmake ../; make; cd -
# compile to llvm bitcode with clang fromtemd
clang -c -emit-llvm -O0 -Xclang -disable-O0-optnone $1/*.c $EMBENCH_DIR/support/*.c -I$1 \
-I$EMBENCH_DIR/support -DCPU_MHZ=1024

# generate llvm ir from bitcode
for f in *.bc
do
  llvm-dis ${f} 
done

for f in *.ll
do
  # optimze with llvm opt
  opt -S -load build/skeleton/libSkeletonPass.so -mem2reg -sr ${f} -o opt_${f}
  opt -S -load build/skeleton/libSkeletonPass.so -mem2reg -sr -dce ${f} -o opt_${f}.bc
  llc -filetype=obj opt_${f}.bc; 
done

gcc *.o; 
./a.out
