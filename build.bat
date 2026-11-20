@echo off
rem MSYS2 MinGW64 kabugunda (veya PATH'te g++/windres varken) calistirin.
windres src\app.rc -O coff -o src\app.res.o
g++ -O2 -std=c++17 -municode -mwindows -static -s src\main.cpp src\app.res.o -o NovaBar.exe -ld2d1 -ldwrite -ld3d11 -ldxgi -ldcomp -lwindowscodecs -ldwmapi -lole32 -loleaut32 -luuid -lshell32 -lshlwapi -lpropsys -lshcore -liphlpapi -lgdi32 -luser32 -lruntimeobject -lversion
