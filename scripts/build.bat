@echo off
setlocal enabledelayedexpansion

cd ..

if not exist bin mkdir bin

echo [1/5] Compiling Core: drp.cpp...
g++ -c src/drp.cpp -Iinclude -o bin/drp.o -O2 -static-libgcc -static-libstdc++ -static

echo [2/5] Building: bin/drp_receiver.exe
g++ examples/receiver.cpp bin/drp.o -Iinclude -o bin/drp_receiver.exe -lws2_32 -O2 -static-libgcc -static-libstdc++ -static

echo [3/5] Building: bin/drp_receiver_file.exe
g++ examples/file_transfer/file_receiver.cpp bin/drp.o -Iinclude -o bin/drp_receiver_file.exe -lws2_32 -O2 -static-libgcc -static-libstdc++ -static 

echo [4/5] Building: bin/drp_sender.exe
g++ examples/sender.cpp bin/drp.o -Iinclude -o bin/drp_sender.exe -lws2_32 -lpthread -O2 -static-libgcc -static-libstdc++ -static 

echo [5/5] Building: bin/drp_sender_file.exe
g++ examples/file_transfer/file_sender.cpp bin/drp.o -Iinclude -o bin/drp_sender_file.exe -lws2_32 -lpthread -O2 -static-libgcc -static-libstdc++ -static

echo.
echo [DONE] All targets built in bin/ folder!
pause