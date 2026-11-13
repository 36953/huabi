$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$name = '36953' + (-join @([char]0x753B, [char]0x7B14)) + '.lnk'
$old = Join-Path $root ((-join @([char]0x753B, [char]0x7B14)) + '.lnk')
if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old -Force }
$lnk = Join-Path $root $name
$shell = New-Object -ComObject WScript.Shell
$s = $shell.CreateShortcut($lnk)
$exe = Join-Path $root 'HuaBi.exe'
$s.TargetPath = $exe
$s.WorkingDirectory = $root
$s.Description = '36953' + (-join @([char]0x753B, [char]0x7B14)) + ' V1.0'
$s.Save()
Write-Output "shortcut $lnk"
