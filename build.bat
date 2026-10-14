@echo off
chcp 65001 >nul
echo 正在编译 mobile ...
g++ -O2 -std=c++17 -mwindows -static -s -o mobile.exe mobile.cpp -luser32 -lgdi32 -lmsimg32 -lgdiplus -ladvapi32 -lshell32 -lwinmm -lole32 -luuid -lcomctl32
if %errorlevel%==0 (
    echo 编译成功：mobile.exe
) else (
    echo 编译失败，请确认已安装 MinGW-w64 g++ 并加入 PATH
)
pause
