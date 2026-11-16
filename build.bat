@echo off
chcp 65001 >nul
setlocal

rem 用法：
rem   build.bat          编 Turbo（零第三方依赖）
rem   build.bat mobile   编主线（要 litehtml + gumbo）
rem   build.bat check    编自检工具 check.exe
rem   build.bat all      两个都编
rem
rem 资源（图标 / 清单 / 版本号）走 .rc，先 windres 成 app_res.o 再链：
rem   app.rc      → Turbo（版本 1.2.1，产品名 mobile Turbo）
rem   app-main.rc → 主线（版本 1.2，产品名 mobile）
rem 版本号改在对应的 .rc 里，别只改这里。

set TARGET=%1
if "%TARGET%"=="" set TARGET=turbo
if "%LITEHTML_DIR%"=="" set LITEHTML_DIR=third_party\litehtml
if "%GUMBO_DIR%"=="" set GUMBO_DIR=third_party\gumbo

rem 主线 / Turbo 版本号与产品名不同，各用各的资源脚本
set RCFILE=app.rc
if /i "%TARGET%"=="mobile" set RCFILE=app-main.rc

echo [1/2] 编译资源 %RCFILE% ...
windres "%RCFILE%" -O coff -o app_res.o
if errorlevel 1 goto resfail

if /i "%TARGET%"=="mobile" goto domobile
if /i "%TARGET%"=="check" goto docheck

echo [2/2] 编译 Turbo ...
g++ -O2 -std=c++17 -mwindows -static -s -o mobile-v1.2-Turbo.exe turbo.cpp app_res.o ^
    -luser32 -lgdi32 -lmsimg32 -lgdiplus -lwinhttp -ladvapi32 -lversion ^
    -lshell32 -lwinmm -lole32 -luuid -lcomctl32
if errorlevel 1 goto buildfail
echo 编译成功：mobile-v1.2-Turbo.exe

if /i not "%TARGET%"=="all" goto end

:domobile
echo 编译主线 ...
g++ -O2 -std=c++17 -mwindows -static -s -o mobile-v1.2.exe mobile.cpp app_res.o ^
    -I"%LITEHTML_DIR%\include" ^
    -I"%GUMBO_DIR%\src" ^
    -L"%LITEHTML_DIR%" ^
    -L"%GUMBO_DIR%" ^
    -luser32 -lgdi32 -lmsimg32 -lgdiplus -lwinhttp -limm32 -ladvapi32 -lcrypt32 -lversion ^
    -lshell32 -lwinmm -lole32 -luuid -lcomctl32 ^
    -llitehtml -lgumbo
if errorlevel 1 goto buildfail
echo 编译成功：mobile-v1.2.exe
goto end

:docheck
echo 编译自检工具 ...
g++ -O2 -std=c++17 -municode -static -s -o check.exe check.cpp -ladvapi32 -lversion -lshell32
if errorlevel 1 goto buildfail
echo 编译成功：check.exe
goto end

:resfail
echo 资源编译失败：确认 windres 在 PATH 里，且 .rc / app.ico / app.manifest 都在当前目录
goto end

:buildfail
echo 编译失败，请确认：
echo   1. g++ 已在 PATH（MinGW-w64，支持 C++17）
echo   2. 主线还要按 README 准备好 litehtml / gumbo

:end
endlocal
pause
