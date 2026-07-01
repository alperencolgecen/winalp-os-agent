@echo off
setlocal enabledelayedexpansion
set LUA_DIR=%~dp0..\lib\lua
set LUA_SRC=%LUA_DIR%\lua-5.4.7\src

if exist "%LUA_DIR%\liblua.a" exit /b 0
if not exist "%LUA_SRC%\luaconf.h" (
    echo Downloading Lua 5.4.7...
    powershell -Command "Invoke-WebRequest -Uri 'https://www.lua.org/ftp/lua-5.4.7.tar.gz' -OutFile '%LUA_DIR%\lua.tar.gz'"
    if exist "%LUA_DIR%\lua.tar.gz" (
        cd /d "%LUA_DIR%" && tar -xzf lua.tar.gz && del lua.tar.gz
    )
)
if not exist "%LUA_SRC%\luaconf.h" exit /b 1

cd /d "%LUA_SRC%"
set CFLAGS=-O2 -Wall -Wextra -DLUA_COMPAT_5_3 -DLUA_USE_WINDOWS
for %%f in (lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm lundump lvm lzio lauxlib lbaselib lcorolib ldblib liolib lmathlib loadlib loslib lstrlib ltablib lutf8lib linit) do (
    gcc %CFLAGS% -c %%f.c -o %%f.o
)
ar rcs liblua.a *.o
del *.o
copy /Y liblua.a "%LUA_DIR%\liblua.a"
if not exist "%LUA_DIR%\include" mkdir "%LUA_DIR%\include"
for %%h in (lua.h lualib.h lauxlib.h luaconf.h) do copy /Y %%h "%LUA_DIR%\include\%%h"
echo Built liblua.a
cd /d "%~dp0.."
exit /b 0
