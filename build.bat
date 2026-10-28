@echo off
chcp 65001 >nul
rem ===== 第三方依赖路径（可通过环境变量覆盖）=====
rem   litehtml：HTML 气泡排版（含头文件 include\litehtml.h 与 liblitehtml.a）
rem   gumbo  ：HTML 解析（含头文件 src\gumbo.h 与 libgumbo.a）
if "%LITEHTML_DIR%"=="" set LITEHTML_DIR=third_party\litehtml
if "%GUMBO_DIR%"=="" set GUMBO_DIR=third_party\gumbo

echo 正在编译 mobile ...
g++ -O2 -std=c++17 -mwindows -static -s -o mobile.exe mobile.cpp ^
    -I"%LITEHTML_DIR%\include" ^
    -I"%GUMBO_DIR%\src" ^
    -L"%LITEHTML_DIR%" ^
    -L"%GUMBO_DIR%" ^
    -luser32 -lgdi32 -lmsimg32 -lgdiplus -lwinhttp -limm32 -ladvapi32 -lcrypt32 ^
    -lshell32 -lwinmm -lole32 -luuid -lcomctl32 ^
    -llitehtml -lgumbo
if %errorlevel%==0 (
    echo 编译成功：mobile.exe
) else (
    echo 编译失败，请确认：
    echo   1. 已安装 MinGW-w64 g++ 并加入 PATH
    echo   2. 已按 README 准备好 litehtml / gumbo 依赖
)
pause
