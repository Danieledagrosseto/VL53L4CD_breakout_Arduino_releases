$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$executable = Join-Path ([System.IO.Path]::GetTempPath()) ("vl53l4cd-tests-" + [guid]::NewGuid().ToString() + ".exe")

Push-Location $root
try {
    foreach ($configuration in @('', '-DTEST_WIRE_WITHOUT_TIMEOUT')) {
      foreach ($test in @('.\tests\VL53L4CD_test.cpp', '.\tests\ChangeAddress_test.cpp',
                         '.\tests\MultipleContinuous_test.cpp')) {
        $arguments = @('-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic',
            '-I', '.\tests\fakes', '-I', '.', '.\VL53L4CD.cpp',
            $test, '-o', $executable)
        if ($configuration) { $arguments += $configuration }
        & g++ @arguments
        if ($LASTEXITCODE -ne 0) { throw 'Host test compilation failed.' }
        & $executable
        if ($LASTEXITCODE -ne 0) { throw 'Host tests failed.' }
      }
    }
}
finally {
    Pop-Location
    if (Test-Path -LiteralPath $executable) { Remove-Item -LiteralPath $executable }
}
