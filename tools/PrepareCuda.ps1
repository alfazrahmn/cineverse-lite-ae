$ErrorActionPreference='Stop'
$destination=Join-Path $env:RUNNER_TEMP 'cineverse-cuda-12.8'
New-Item -ItemType Directory -Force $destination | Out-Null
# Pinned official NVIDIA CUDA 12.8.1 redistributables. Build machine only.
$components=@(
    @('cuda_nvcc','12.8.93','9fdc70b4271ed9aad4d64cd7076a7d96ec36512d074b9995fe638de669197391',120912792),
    @('cuda_cudart','12.8.90','4a39058fd8519444a81cfc7ae055d136f48d1a31ffa41ae255b35b2edd61e13b',3037735),
    @('cuda_cccl','12.8.90','bd8548fa1ae82f92910bebc3079e14bd58c5a92aa64596d46bd610a478cb39d7',2911311)
)
foreach($component in $components) {
    $name=$component[0]; $version=$component[1]
    $archive=Join-Path $env:RUNNER_TEMP ($name+'.zip')
    $url='https://developer.download.nvidia.com/compute/cuda/redist/'+$name+'/windows-x86_64/'+$name+'-windows-x86_64-'+$version+'-archive.zip'
    Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing
    if((Get-Item $archive).Length -ne $component[3]){throw 'CUDA archive size mismatch.'}
    if((Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $component[2]){throw 'CUDA archive checksum mismatch.'}
    $unpacked=Join-Path $env:RUNNER_TEMP ($name+'-unpacked')
    Expand-Archive -LiteralPath $archive -DestinationPath $unpacked -Force
    $roots=@(Get-ChildItem $unpacked -Directory)
    if($roots.Count -ne 1){throw 'Unexpected CUDA archive layout.'}
    Get-ChildItem $roots[0].FullName | Copy-Item -Destination $destination -Recurse -Force
}
if(!(Test-Path (Join-Path $destination 'bin\nvcc.exe'))){throw 'CUDA compiler is missing.'}
if(!(Test-Path (Join-Path $destination 'include\cuda_runtime.h'))){throw 'CUDA runtime headers are missing.'}
if(!(Get-ChildItem $destination -Filter cuda.lib -Recurse)){throw 'CUDA driver import library is missing.'}
"CUDA_PATH=$destination" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
