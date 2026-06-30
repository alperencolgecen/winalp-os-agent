@echo off
setlocal enabledelayedexpansion

REM Find the short path (project root = scripts\..)
for /f "tokens=*" %%a in ('powershell -Command "(New-Object -ComObject Scripting.FileSystemObject).GetFolder('%~dp0..').ShortPath"') do set BASE_SHORT=%%a

REM Build whisper.cpp as static library using CMake + MinGW
set BUILD_DIR=%BASE_SHORT%\lib\whisper.cpp\build_mingw

cmake -S "%BASE_SHORT%\lib\whisper.cpp" -B "%BUILD_DIR%" ^
    -G "MinGW Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DBUILD_SHARED_LIBS=OFF ^
    -DWHISPER_BUILD_EXAMPLES=OFF ^
    -DWHISPER_BUILD_TESTS=OFF ^
    -DWHISPER_BUILD_SERVER=OFF ^
    -DGGML_CUDA=OFF ^
    -DGGML_VULKAN=OFF ^
    -DGGML_METAL=OFF ^
    -DGGML_STATIC=ON ^
    -DGGML_NATIVE=OFF ^
    -DGGML_CCACHE=OFF

cmake --build "%BUILD_DIR%" --config Release --target whisper -j4

REM Copy libraries to lib/whisper/
copy /Y "%BUILD_DIR%\ggml\src\ggml-base.a" "%~dp0..\lib\whisper\libggml-base.a"
copy /Y "%BUILD_DIR%\ggml\src\ggml-cpu.a" "%~dp0..\lib\whisper\libggml-cpu.a"
copy /Y "%BUILD_DIR%\ggml\src\ggml.a" "%~dp0..\lib\whisper\libggml.a"
copy /Y "%BUILD_DIR%\src\libwhisper.a" "%~dp0..\lib\whisper\libwhisper.a"

echo Whisper libraries built and copied to lib/whisper/
