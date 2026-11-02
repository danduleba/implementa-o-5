# Inicia uma sessao Linux gravada no mesmo formato dos projetos anteriores.
# Digite exit para encerrar e salvar a gravacao.
Write-Host 'Iniciando gravacao no Ubuntu. Digite exit para encerrar.'
wsl.exe -d Ubuntu --cd /mnt/c/Users/User/Desktop/implementacao-5 --exec script -a evidencias.log
if ($LASTEXITCODE -ne 0) { Write-Error "Falha ao iniciar ou encerrar a gravacao (codigo $LASTEXITCODE)." }
