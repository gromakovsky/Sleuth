rm -rf build/*
cd build
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=On ..
pvs-studio-analyzer analyze -o patak.log -e /usr/include
plog-converter -a GA:1,2 -t tasklist -o patak.tasks patak.log
cd ..
cat build/patak.tasks
