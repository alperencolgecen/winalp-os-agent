@echo off
setlocal enabledelayedexpansion

REM Find the short path (project root = scripts\..)
for /f "tokens=*" %%a in ('powershell -Command "(New-Object -ComObject Scripting.FileSystemObject).GetFolder('%~dp0..').ShortPath"') do set BASE_SHORT=%%a

REM Build llama.cpp as static library using CMake + MinGW
set BUILD_DIR=%BASE_SHORT%\lib\llama.cpp\build_mingw

cmake -S "%BASE_SHORT%\lib\llama.cpp" -B "%BUILD_DIR%" ^
    -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DBUILD_SHARED_LIBS=OFF ^
    -DLLAMA_BUILD_TESTS=OFF ^
    -DLLAMA_BUILD_EXAMPLES=OFF ^
    -DLLAMA_BUILD_TOOLS=OFF ^
    -DLLAMA_BUILD_SERVER=OFF ^
    -DLLAMA_BUILD_COMMON=OFF ^
    -DLLAMA_BUILD_APP=OFF ^
    -DGGML_CUDA=OFF ^
    -DGGML_VULKAN=OFF ^
    -DGGML_METAL=OFF ^
    -DGGML_NATIVE=OFF ^
    -DGGML_CCACHE=OFF

cmake --build "%BUILD_DIR%" --config Release --target llama -j4

REM Copy libraries to lib/llama/
if not exist "%~dp0..\lib\llama" mkdir "%~dp0..\lib\llama"

copy /Y "%BUILD_DIR%\src\libllama.a" "%~dp0..\lib\llama\libllama.a"
copy /Y "%BUILD_DIR%\ggml\src\ggml-base.a" "%~dp0..\lib\llama\libggml-base.a"
copy /Y "%BUILD_DIR%\ggml\src\ggml-cpu.a" "%~dp0..\lib\llama\libggml-cpu.a"
copy /Y "%BUILD_DIR%\ggml\src\ggml.a" "%~dp0..\lib\llama\libggml.a"

echo llama.cpp libraries built and copied to lib/llama/
