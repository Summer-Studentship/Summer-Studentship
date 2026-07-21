$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$externalDir = Join-Path $repoRoot "external"
$mathplotDir = Join-Path $externalDir "mathplot"
$mathplotRepo = "https://github.com/sebsjames/mathplot.git"
$mathplotCommit = "c2e17f21b8112c5fbbbfa5811ca92c21cbf3cd6a"

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "git was not found on PATH. Install Git or open a shell where git is available."
}

if (-not (Test-Path $externalDir)) {
    New-Item -ItemType Directory -Path $externalDir | Out-Null
}

if (-not (Test-Path $mathplotDir)) {
    Write-Host "Cloning mathplot into external/mathplot..."
    git clone --recursive $mathplotRepo $mathplotDir
} elseif (-not (Test-Path (Join-Path $mathplotDir ".git"))) {
    throw "external/mathplot exists but is not a git checkout. Move it aside or replace it with a clone of $mathplotRepo."
} else {
    Write-Host "Updating existing external/mathplot checkout and submodules..."
    git -C $mathplotDir fetch --tags origin
    git -C $mathplotDir submodule update --init --recursive
}

git -C $mathplotDir checkout $mathplotCommit
git -C $mathplotDir submodule update --init --recursive

$requiredPaths = @(
    "mplot/Visual.h",
    "mplot/GraphVisual.h",
    "maths/sm/vvec",
    "mplot/fonts/verafonts.h"
)

foreach ($relativePath in $requiredPaths) {
    $fullPath = Join-Path $mathplotDir $relativePath
    if (-not (Test-Path $fullPath)) {
        throw "Mathplot checkout is incomplete. Missing: external/mathplot/$relativePath"
    }
}

$visualFaceBase = Join-Path $mathplotDir "mplot/VisualFaceBase.h"
$content = Get-Content -Raw $visualFaceBase
$content = $content.Replace("#elif defined _MSC_VER", "#elif defined _WIN32")
$content = $content.Replace("#ifndef _MSC_VER", "#ifndef _WIN32")
$content = $content.Replace("#ifdef _MSC_VER", "#ifdef _WIN32")
Set-Content -Path $visualFaceBase -Value $content -NoNewline

Write-Host "External dependencies are ready."
