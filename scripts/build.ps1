param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$nativeBuild = 'artifacts/native'
$package = 'artifacts/package/BepInEx/plugins/LeafClip'
Push-Location $repoRoot
try {
    cmake -S native -B $nativeBuild -G 'Visual Studio 17 2022' -A x64
    if ($LASTEXITCODE -ne 0) { throw 'Native configure failed' }
    cmake --build $nativeBuild --config $Configuration
    if ($LASTEXITCODE -ne 0) { throw 'Native build failed' }
    ctest --test-dir $nativeBuild -C $Configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Native renderer tests failed' }
    dotnet build src/LeafClip/LeafClip.csproj -c $Configuration
    if ($LASTEXITCODE -ne 0) { throw 'Managed build failed' }
    $stage = Join-Path $repoRoot $package
    New-Item -ItemType Directory -Force $stage | Out-Null
    Copy-Item -LiteralPath "src/LeafClip/bin/$Configuration/net472/LeafClip.dll" -Destination $stage
    Copy-Item -LiteralPath "$nativeBuild/$Configuration/LeafClip.Native.dll" -Destination $stage
    Copy-Item -LiteralPath 'native/README.md' -Destination (Join-Path $stage 'README.md')
    $licenses = Join-Path $stage 'licenses'
    New-Item -ItemType Directory -Force $licenses | Out-Null
    Copy-Item -LiteralPath 'native/third_party/NOTICE.md','native/third_party/DXC-LICENSE.txt','native/third_party/AMD-LICENSE.txt','native/third_party/DXBCChecksum.h' -Destination $licenses
    Copy-Item -LiteralPath 'native/third_party/minhook/LICENSE.txt' -Destination (Join-Path $licenses 'MinHook-LICENSE.txt')
    Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $stage 'LeafClip.dll'),(Join-Path $stage 'LeafClip.Native.dll') |
        Format-Table Hash,Path -AutoSize
    Write-Output "Test files staged in $stage. Nothing was installed or published."
}
finally { Pop-Location }
