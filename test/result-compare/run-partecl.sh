#run ParTeCL and copy the tests to the output dir

rm -r /home/vanya/partecl-runtime/kernel-gen/
mkdir /home/vanya/partecl-runtime/kernel-gen/
/home/vanya/clang-llvm/build/bin/partecl-codegen $1 -- $2 /home/vanya/partecl-runtime/kernel-gen/
cp $3 /home/vanya/partecl-runtime/kernel-gen/tests.txt
