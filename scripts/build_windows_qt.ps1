param(
    [switch] $Visualization
)

$ErrorActionPreference = "Stop"

$configurePreset = "windows-mingw-vcpkg"
$buildPreset = "windows-mingw-vcpkg-debug"

if ($Visualization) {
    $configurePreset = "windows-mingw-vcpkg-all"
    $buildPreset = "windows-mingw-vcpkg-all-debug"
}

cmake --preset $configurePreset
cmake --build --preset $buildPreset
