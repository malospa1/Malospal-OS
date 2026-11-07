param(
    [ValidateSet('build', 'run', 'smoke', 'check', 'fault', 'memory', 'virtual', 'timer', 'console', 'screen', 'keyboard', 'usb', 'game', 'slots', 'unit')]
    [string]$Action = 'run',
    [ValidateRange(32, 65536)]
    [int]$Memory = 128,
    [switch]$Uefi,
    [switch]$Headless,
    [string]$Firmware,
    [string]$FirmwareVars,
    [ValidateSet('divide', 'invalid-opcode', 'general-protection', 'page-fault', 'double-fault',
                 'write-protect', 'no-execute', 'stack-guard', 'unmapped', 'text-write', 'stack-overflow')]
    [string]$Exception
)

$ErrorActionPreference = 'Stop'
if ($Uefi -and $Firmware) {
    throw 'Use either -Uefi or -Firmware, not both.'
}

$pythonExecutable = $env:MALOSPAL_PYTHON
if (-not $pythonExecutable) {
    foreach ($name in @('python', 'python3')) {
        $candidate = Get-Command $name -CommandType Application -ErrorAction SilentlyContinue |
            Where-Object { $_.Source -notlike '*\Microsoft\WindowsApps\*' } |
            Select-Object -First 1
        if ($candidate) {
            $pythonExecutable = $candidate.Source
            break
        }
    }
}
if (-not $pythonExecutable) {
    throw 'Python was not found. Install Python 3.9+ or set MALOSPAL_PYTHON to python.exe.'
}

$pythonArguments = @((Join-Path $PSScriptRoot 'tools\build.py'), $Action, '--memory', "$Memory")
if ($Uefi) { $pythonArguments += '--uefi' }
if ($Headless) { $pythonArguments += '--headless' }
if ($Firmware) { $pythonArguments += @('--firmware', $Firmware) }
if ($FirmwareVars) { $pythonArguments += @('--firmware-vars', $FirmwareVars) }
if ($Exception) { $pythonArguments += @('--exception', $Exception) }
& $pythonExecutable @pythonArguments
exit $LASTEXITCODE
