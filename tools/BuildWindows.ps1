param(
    [ValidateSet('CPU','CUDA')][string]$Backend='CPU',
    [string]$SdkPath='',
    [string]$SdkArchive='',
    [ValidateSet('','14.29')][string]$ToolsetVersion='',
    [switch]$Interactive
)
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot -Parent
try {
    if(!$SdkPath) {
        if(!$SdkArchive) {
            $candidate=Join-Path $project 'AfterEffectsSDK_26.5_win.zip'
            if(Test-Path $candidate){$SdkArchive=$candidate}
            elseif($env:AE_SDK_ROOT){$SdkPath=$env:AE_SDK_ROOT}
            elseif($Interactive) {
                Add-Type -AssemblyName System.Windows.Forms
                $picker=New-Object System.Windows.Forms.OpenFileDialog
                $picker.Title='Select AfterEffectsSDK_26.5_win.zip'
                $picker.Filter='Adobe native Windows SDK ZIP|*.zip'
                if($picker.ShowDialog() -ne 'OK'){throw 'SDK selection cancelled.'}
                $SdkArchive=$picker.FileName
                $picker.Dispose()
            }
            else{throw 'Supply -SdkPath or -SdkArchive, or place the Windows SDK ZIP next to START_HERE.txt.'}
        }
        if($SdkArchive) {
            $unpacked=Join-Path $project '.sdk'
            New-Item -ItemType Directory -Force $unpacked | Out-Null
            Expand-Archive -LiteralPath $SdkArchive -DestinationPath $unpacked -Force
            $header=Get-ChildItem $unpacked -Filter AE_Effect.h -Recurse |
                Where-Object {$_.FullName -match '[\\/]Examples[\\/]Headers[\\/]AE_Effect.h$'} | Select-Object -First 1
            if(!$header){throw 'This ZIP does not contain the native After Effects SDK headers.'}
            $SdkPath=$header.Directory.Parent.Parent.FullName
        }
    }
    $SdkPath=(Resolve-Path -LiteralPath $SdkPath).Path
    if(!(Test-Path (Join-Path $SdkPath 'Examples\Resources\PiPLtool.exe'))){throw 'The SDK is missing the Windows PiPL compiler. Choose the native Windows SDK ZIP.'}
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if(!(Test-Path $vswhere)){throw 'Install Visual Studio 2022 Build Tools with Desktop development with C++, MSVC v143 and a Windows SDK, then run this builder again.'}
    $installation=& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if(!$installation){throw 'Visual Studio is missing the C++ build workload.'}
    $devcmd=Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
    if(!(Test-Path $devcmd)){throw 'Cannot locate the Visual Studio developer environment.'}
    # Import the environment using a temporary batch file, avoiding shell interpolation of SDK paths.
    $environmentBatch=Join-Path $project 'build-environment.cmd'
    $toolsetSwitch=if($ToolsetVersion){' -vcvars_ver='+$ToolsetVersion}else{''}
    @('@echo off', ('call "'+$devcmd+'" -arch=x64 -host_arch=x64'+$toolsetSwitch+' >nul'), 'if errorlevel 1 exit /b 1', 'set') | Set-Content -LiteralPath $environmentBatch -Encoding ascii
    $environmentLines=& $env:ComSpec /d /c "`"$environmentBatch`""
    if($LASTEXITCODE -ne 0){throw 'Visual Studio environment initialization failed.'}
    foreach($line in $environmentLines) {
        $at=$line.IndexOf('=')
        if($at -gt 0){[Environment]::SetEnvironmentVariable($line.Substring(0,$at),$line.Substring($at+1),'Process')}
    }
    Remove-Item -LiteralPath $environmentBatch
    # Run the adapter's simulated-host tests with the real Windows compiler and SDK.
    $testFolder=Join-Path $project 'build\tests'
    New-Item -ItemType Directory -Force $testFolder | Out-Null
    Push-Location $testFolder
    try {
        $testExe=Join-Path $testFolder 'native_host_test.exe'
        & cl /nologo /EHsc /std:c++17 /O2 /fp:precise /MT /D_WINDOWS /DMSWindows /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS "/I$SdkPath\Examples\Headers" "/I$SdkPath\Examples\Headers\SP" "/I$SdkPath\Examples\Util" (Join-Path $project 'tests\native_host_test.cpp') (Join-Path $project 'src\Engine.cpp') "/Fe:$testExe"
        if($LASTEXITCODE -ne 0){throw 'The Windows adapter test could not compile. Send its compiler output.'}
        & $testExe
        if($LASTEXITCODE -ne 0){throw 'The Windows adapter tests failed. The plug-in build has been stopped.'}
    } finally {Pop-Location}
    $enableCuda=if($Backend -eq 'CUDA'){'true'}else{'false'}
    if($Backend -eq 'CUDA') {
        if(!$env:CUDA_PATH -or !(Test-Path (Join-Path $env:CUDA_PATH 'bin\nvcc.exe'))){throw 'Install a compatible CUDA 12.8 toolkit for the developer CUDA build, or use BUILD_WINDOWS_CPU.cmd first.'}
        $version=& (Join-Path $env:CUDA_PATH 'bin\nvcc.exe') --version
        if(($version -join ' ') -notmatch 'release 12\.8'){throw 'This CUDA configuration targets toolkit 12.8. Select that toolkit in CUDA_PATH; a different toolkit needs a separately validated build configuration.'}
    }
    $buildArgs=@((Join-Path $project 'windows\CineverseLite.vcxproj'),'/m','/t:Build','/p:Configuration=Release','/p:Platform=x64',"/p:SdkRoot=$SdkPath","/p:CvlCudaEnabled=$enableCuda")
    if($ToolsetVersion -eq '14.29') {
        $buildArgs+='/p:PlatformToolset=v142'
        $buildArgs+='/p:VCToolsVersion='+$env:VCToolsVersion.TrimEnd('\')
    }
    & msbuild @buildArgs
    if($LASTEXITCODE -ne 0){throw 'Compilation failed. The existing installed plug-in was not touched. Send the build error text for correction.'}
    $binary=Join-Path $project "dist\$Backend\Cineverse_Lite.aex"
    if(!(Test-Path $binary)){throw 'The build did not produce Cineverse_Lite.aex.'}
    Write-Host "Built: $binary" -ForegroundColor Green
    Write-Host 'This is a TEST BUILD. Follow START_HERE.txt to test in After Effects before distribution.'
    if($Interactive){Start-Process explorer.exe (Split-Path $binary -Parent)}
    exit 0
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
