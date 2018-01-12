#run ParTeCL and copy the tests to the output dir

rm -r ~/partecl-runtime/kernel-gen/
mkdir ~/partecl-runtime/kernel-gen/
~/clang-llvm/build/bin/partecl-codegen $1 -config $2 -output /home/vanya/partecl-runtime/kernel-gen/ --
cp $3 ~/partecl-runtime/kernel-gen/tests.txt
