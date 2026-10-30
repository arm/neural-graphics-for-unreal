@echo off
setlocal
:: Get debug_type from first argument, default to Release
set "debug_type=%1"

if /I "%debug_type%"=="" (
    echo User does not specify debug_type, default to Release
    set "debug_type=Release"
) else if /I not "%debug_type%"=="Debug" if /I not "%debug_type%"=="Release" (
    echo WARNING: Invalid debug_type "%debug_type%". Only "Debug" or "Release" are allowed. Defaulting to Release.
    set "debug_type=Release"
)

echo WARNING: This will overwrite binaries in the SDK prebuilt_binaries folder!
set /p userinput="Type 'yes' to continue: "
if /I not "%userinput%"=="yes" (
    echo Aborted by user.
    exit /b
)

pushd Source\NG-SDK
    if not exist build\ (
        mkdir build
    )
    pushd build
        :: Clear out CMakeCache
        if exist CMakeFiles\ (
            rmdir /S /Q CMakeFiles
        )
        if exist CMakeCache.txt (
            del /S /Q CMakeCache.txt
        )

        cmake -A x64 .. -DCMAKE_BUILD_TYPE=%debug_type% -DFFX_API_BACKEND=vk_windows_x64 -DFFX_FSR3_AS_LIBRARY=OFF -DFFX_BUILD_AS_DLL=ON
        cmake --build ./ --verbose --config %debug_type% --parallel 4 -- /p:CL_MPcount=16
    popd
    if not exist .\prebuilt_binaries mkdir prebuilt_binaries
    if /I "%debug_type%"=="Debug" (
        echo Copying and renaming debug binaries to be used by the plugin
        xcopy /y /v .\bin\ngsdk_windows_x64d.dll .\prebuilt_binaries\ngsdk_windows_x64.dll
        xcopy /y /v .\bin\ngsdk_windows_x64d.lib .\prebuilt_binaries\ngsdk_windows_x64.lib
    ) else (
        xcopy /y /v .\bin\ngsdk_windows_x64.dll .\prebuilt_binaries\
        xcopy /y /v .\bin\ngsdk_windows_x64.lib .\prebuilt_binaries\
    )
popd
pause
