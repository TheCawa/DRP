g++ -Iinclude src/drp.cpp examples/sender.cpp -o drp_sender.exe -lws2_32 -lpthread
g++ -Iinclude src/drp.cpp examples/file_sender.cpp -o drp_sender_file.exe -lws2_32 -lpthread
pause