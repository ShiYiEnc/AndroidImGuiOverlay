param(
    [string]$Sdk = $(if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { $env:ANDROID_SDK_ROOT }),
    [string]$Jdk = $env:JAVA_HOME,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
if (-not $Sdk -or -not (Test-Path -LiteralPath $Sdk)) { throw 'Set ANDROID_HOME or pass -Sdk with your Android SDK directory.' }
if (-not $Jdk -or -not (Test-Path -LiteralPath (Join-Path $Jdk 'bin/java.exe'))) { throw 'Set JAVA_HOME or pass -Jdk with your JDK 17 directory.' }
$project = Split-Path -Parent $PSScriptRoot
Push-Location $project
try {
    $env:JAVA_HOME = $Jdk
    if (-not $SkipBuild) {
        $env:ANDROID_HOME = $Sdk
        & .\gradlew.bat :app:assembleDebug :app:lintDebug :app:testDebugUnitTest --console=plain
        if ($LASTEXITCODE -ne 0) { throw 'Gradle checks failed' }
    }
    $apk = Join-Path $project 'app/build/outputs/apk/debug/app-debug.apk'
    $zipalign = Join-Path $Sdk 'build-tools/36.0.0/zipalign.exe'
    & $zipalign -c -P 16 -v 4 $apk
    if ($LASTEXITCODE -ne 0) { throw 'APK ZIP alignment failed' }
    & (Join-Path $Sdk 'build-tools/36.0.0/apksigner.bat') verify --verbose $apk
    if ($LASTEXITCODE -ne 0) { throw 'APK signature verification failed' }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($apk)
    $verifyDir = Join-Path $project 'app/build/verification'
    New-Item -ItemType Directory -Path $verifyDir -Force | Out-Null
    $readelf = Join-Path $Sdk 'ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-readelf.exe'
    try {
        $libraries = @($archive.Entries | Where-Object { $_.FullName -match '^lib/arm64-v8a/[^/]+\.so$' })
        if ($libraries.Count -eq 0) { throw 'APK contains no arm64 shared libraries' }
        foreach ($entry in $libraries) {
            $destination = Join-Path $verifyDir $entry.Name
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $destination, $true)
            $headers = & $readelf -lW $destination
            if ($LASTEXITCODE -ne 0) { throw "Unable to inspect $($entry.Name)" }
            $loads = @($headers | Where-Object { $_ -match '^\s*LOAD\s+' })
            if ($loads.Count -eq 0) { throw "No ELF LOAD segments in $($entry.Name)" }
            foreach ($load in $loads) {
                $alignment = ($load.Trim() -split '\s+')[-1]
                if ([Convert]::ToInt64($alignment.Substring(2), 16) -lt 16384) {
                    throw "ELF segment not 16 KB aligned: $($entry.Name): $load"
                }
            }
            Write-Output "ELF alignment PASS: $($entry.Name)"
            if ($entry.Name -eq 'libimgui_overlay.so') {
                $symbols = & $readelf --dyn-syms -W $destination
                foreach ($method in @('create', 'attach', 'resize', 'detach', 'touch', 'metrics', 'pause', 'text', 'destroy')) {
                    if (-not ($symbols -match "Java_dev_imgui_overlay_NativeBridge_$method\s*$")) { throw "Missing JNI export: $method" }
                }
                $dependencies = & $readelf -dW $destination
                if ($dependencies -match 'libgui\.so|libutils\.so') { throw 'Private Android library dependency found' }
                Write-Output 'JNI exports and public-library dependencies PASS'
            }
        }
    } finally { $archive.Dispose() }
    [xml]$lint = Get-Content 'app/build/reports/lint-results-debug.xml'
    $errors = @($lint.issues.issue | Where-Object { $_.severity -in @('Error', 'Fatal') })
    if ($errors.Count -gt 0) { throw 'Lint errors found' }
    $warnings = @($lint.issues.issue | Where-Object { $_.severity -eq 'Warning' })
    Write-Output "Lint PASS: 0 errors, $($warnings.Count) warnings"
    $testCount = 0
    foreach ($testFile in Get-ChildItem 'app/build/test-results/testDebugUnitTest/TEST-*.xml') {
        [xml]$tests = Get-Content $testFile.FullName
        if ([int]$tests.testsuite.failures -ne 0 -or [int]$tests.testsuite.errors -ne 0) { throw "Regression tests failed: $($testFile.Name)" }
        $testCount += [int]$tests.testsuite.tests
    }
    if ($testCount -lt 10) { throw 'Expected regression test reports are missing' }
    Write-Output "Regression tests PASS: $testCount tests"
    Write-Output "APK: $apk"
} finally { Pop-Location }
