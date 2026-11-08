# Builds DPDFNet's upstream contract tests against the plugin's runtime objects and runs them.
# They check every assembly kernel against scalar oracles, so a calling-convention mistake in
# the Windows port fails here instead of in the audio engine. Run after build.cmd:
#
#     pwsh tests/contracts.ps1

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$build = "$root\build"
$dpdf = "$root\third_party\dpdfnet"
$out = "$PSScriptRoot\out\contracts"
$commit = (Select-String -Path "$root\CMakeLists.txt" -Pattern 'set\(DPDF_COMMIT (\w+)\)').Matches[0].Groups[1].Value
$clang = (Select-String -Path "$build\CMakeCache.txt" -Pattern '^CMAKE_C_COMPILER:\w+=(.+)$').Matches[0].Groups[1].Value

$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -prerelease -products * -property installationPath
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 && set" | ForEach-Object {
	if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($matches[1])" $matches[2] }
}
$builtins = & $clang /clang:--rtlib=compiler-rt /clang:-print-libgcc-file-name

$tests = @{
	2 = 'full extended layout generated_gru conv_helpers depthwise int8 int8_dispatch fused pair wide pair_fused'
	8 = 'full extended layout generated_gru conv_helpers depthwise int8 int8_dispatch fused pair pair_fused'
}
New-Item -ItemType Directory -Force "$out\tests", "$out\tools" | Out-Null
$base = "https://raw.githubusercontent.com/ceva-ip/DPDFNet/$commit/optimized_inference"
Invoke-WebRequest "$base/tools/support.h" -OutFile "$out\tools\support.h"
foreach ($name in ($tests[2] -split ' ') + 'public') {
	$source = (Invoke-WebRequest "$base/tests/${name}_contract.c").Content
	# The Linux-only guards are what this port lifts.
	$source = $source -replace '(?m)^#error .*Linux.*$', ''
	# Upstream declares this kernel with a plain int32_t* in the test; windows_abi.h has the real type.
	$source = $source -replace '(void dpdf_qdot4pair64_avx2\([^;]*)int32_t \*\);', '$1__m256i *);'
	Set-Content "$out\tests\${name}_contract.c" $source -NoNewline
}

# The scalar oracles must keep their ordered multiply/add operations.
$flags = @('/nologo', '/O2', '/MT', '/std:c11', '/clang:-ffp-contract=off', '/clang:-frounding-math',
	'/clang:-fno-vectorize', '/clang:-fno-slp-vectorize',
	'-mavx2', '-mfma', '-mf16c', '-Wno-everything', "/I$dpdf\include", "/I$dpdf\src", "/I$dpdf\src\private",
	"/I$PSScriptRoot\posix_shim", '/DDPDF_X86_DISPATCH', '/DDPDF_CONV_PAIR', "/Fo$out\")
$failures = 0
function Run($name, $sources, $arguments) {
	$log = & $clang @flags @sources $builtins "/Fe$out\$name.exe" 2>&1
	if ($LASTEXITCODE -ne 0) { Write-Host ($log | Out-String); Write-Host "$name BUILD FAILED"; return 1 }
	$log = & "$out\$name.exe" @arguments 2>&1
	if ($LASTEXITCODE -ne 0) { Write-Host ($log | Out-String); Write-Host "$name FAILED ($LASTEXITCODE)"; return 1 }
	Write-Host "$name OK"
	return 0
}
foreach ($size in 2, 8) {
	$objects = Get-ChildItem -Recurse "$build\CMakeFiles\dpdf_backend$size.dir" -Filter *.obj | ForEach-Object FullName
	$include = "/FI$dpdf\src\private\namespace$size.h", "/FI$dpdf\windows_abi.h"
	foreach ($test in $tests[$size] -split ' ') {
		$failures += Run "model${size}_$test" ($include + "$out\tests\${test}_contract.c" + $objects) @("$build\weights\dpdfnet${size}_48khz_hr.weights.f32")
	}
}
$failures += Run 'public' @("$out\tests\public_contract.c", "$build\dpdfnet_runtime.lib") @(
	"$build\weights\dpdfnet2_48khz_hr.weights.f32", "$build\weights\dpdfnet8_48khz_hr.weights.f32")

if ($failures) { Write-Host "$failures FAILURE(S)"; exit 1 }
Write-Host 'ALL OK'
