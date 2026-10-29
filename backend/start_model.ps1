$ErrorActionPreference='Stop'
$modelPath=Join-Path $PSScriptRoot 'models\Nemotron-3.5-Bolt-E4B-Q4_K_M.gguf'
$exePath=Join-Path $PSScriptRoot 'runner\llama-server.exe'
$hash=(Get-FileHash -LiteralPath $modelPath -Algorithm SHA256).Hash.ToLower()
if($hash -ne 'c8af2b8aea10a690a1d9491db744a9446660173cb38fc489612b556615a69612'){throw 'Model file hash mismatch'}
try { $health=Invoke-RestMethod -Uri 'http://127.0.0.1:18081/health' -TimeoutSec 2; if($health.status -eq 'ok'){Write-Output 'Local model already responds on port 18081.';exit 0} } catch {}
$logPath=Join-Path $PSScriptRoot 'logs'
$args=@('-m',('"'+$modelPath+'"'),'--alias','nemotron-bolt','--host','127.0.0.1','--port','18081','-c','4096','-ngl','999','--jinja','--skip-chat-parsing','--reasoning','off','--reasoning-budget','0','--parallel','1','--cache-type-k','q8_0','--cache-type-v','q8_0','--no-webui','--verbose')
$p=Start-Process -FilePath $exePath -ArgumentList $args -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $logPath 'llama-stdout.log') -RedirectStandardError (Join-Path $logPath 'llama-stderr.log')
$p.Id | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'runner.pid')
Write-Output ('Local model starting, PID '+$p.Id)
