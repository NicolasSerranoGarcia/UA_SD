$TotalClientes = 100                  
$HostCentral   = "localhost"
$PuertoCentral = 8080
$ProtocolVer   = 1

$AckCodeInicio = 100
$StationIdBase = 1000

$Ubicaciones   = @("Alicante", "Barcelona", "Madrid", "Valencia", "Sevilla", "Bilbao")

Write-Host "Iniciando $TotalClientes clientes hacia$HostCentral`:$PuertoCentral..." -ForegroundColor Green

for ($i = 1; $i -le $TotalClientes; $i++) {
    $AckCode   = $AckCodeInicio + $i
    $StationId = $StationIdBase + $i
    $Location  = $Ubicaciones[$i % $Ubicaciones.Count]

    $Mensaje = "NAP#$ProtocolVer#$AckCode#0#$StationId#$Location#FIN"

    $ArgsList = @(
        ".\test_concurrentClient.py",
        $HostCentral,
        $PuertoCentral,
        $Mensaje
    )

    Start-Process -FilePath "py" -ArgumentList $ArgsList -WorkingDirectory $PSScriptRoot -NoNewWindow

    Start-Sleep -Milliseconds 50
}

Write-Host "`n¡Se han lanzado $TotalClientes procesos cliente exitosamente!" -ForegroundColor Green