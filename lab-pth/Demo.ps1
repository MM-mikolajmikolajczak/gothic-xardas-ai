param([switch]$Automatic, [string]$BasePython = '')
$ErrorActionPreference = 'Stop'
$labRoot = $PSScriptRoot
$labPython = Join-Path $labRoot '.venv\Scripts\python.exe'
if (-not $BasePython) { $BasePython = (& py -3.12 -c 'import sys; print(sys.executable)') }
& $BasePython (Join-Path $labRoot 'lab.py') setup
if ($LASTEXITCODE -ne 0) { throw 'Nie udalo sie przygotowac laboratorium.' }
$Host.UI.RawUI.BackgroundColor = 'Black'
$Host.UI.RawUI.ForegroundColor = 'White'
$Host.UI.RawUI.WindowTitle = 'Python - laboratorium plikow .pth'
Clear-Host
Write-Host 'PYTHON / PLIK STARTOWY .pth' -ForegroundColor Cyan
Write-Host 'Osobne srodowisko. Wylacznie dane demonstracyjne.' -ForegroundColor DarkGray
Write-Host ''
Write-Host 'site-packages\cook.pth:' -ForegroundColor Gray
Get-Content -LiteralPath (Join-Path $labRoot '.venv\Lib\site-packages\cook.pth')
Write-Host ''
if (-not $Automatic) { [void](Read-Host 'Enter: uruchom Python bez importowania aplikacji') }
Write-Host 'PS> python' -ForegroundColor Yellow
if ($Automatic) {
    'exit()' | & $labPython -i
} else {
    & $labPython
}
Write-Host ''
if (-not $Automatic) { [void](Read-Host 'Enter: porownaj start z wylaczonym modulem site') }
Write-Host 'PS> python -S' -ForegroundColor Yellow
if ($Automatic) {
    'raise SystemExit' | & $labPython -S -i
} else {
    Write-Host 'Zakonczenie bez modulu site: wpisz raise SystemExit.' -ForegroundColor DarkGray
    & $labPython -S
}
Write-Host ''
Write-Host 'Wynik: .pth wykonal sie przed kodem aplikacji; -S pominelo hak.' -ForegroundColor Green
Write-Host 'To lokalna demonstracja mechanizmu, a nie uruchomienie zlosliwej paczki.' -ForegroundColor DarkGray
