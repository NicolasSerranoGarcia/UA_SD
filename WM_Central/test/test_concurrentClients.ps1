# ==========================================
# SIMULADOR DE CLIENTES MULTIHILO EN POWERSHELL
# ==========================================

# Configuración de pruebas
$TotalClientes = 100                  # Cambia a 100 según tus necesidades
$HostCentral   = "localhost"
$PuertoCentral = 8080
$ProtocolVer   = 1

# Puntos de partida para los contadores
$AckCodeInicio = 100
$StationIdBase = 1000

# Ubicaciones de ejemplo para rotar
$Ubicaciones   = @("Alicante", "Barcelona", "Madrid", "Valencia", "Sevilla", "Bilbao")

Write-Host "Iniciando $TotalClientes clientes hacia$HostCentral`:$PuertoCentral..." -ForegroundColor Green

for ($i = 1; $i -le $TotalClientes; $i++) {
    $AckCode   = $AckCodeInicio + $i
    $StationId = $StationIdBase + $i
    $Location  = $Ubicaciones[$i % $Ubicaciones.Count]

    # Construcción de la cadena según el formato:
    # NAP#<protocol-version>#<ack-code>#0#<station_id>#<location>#FIN
    $Mensaje = "NAP#$ProtocolVer#$AckCode#0#$StationId#$Location#FIN"

    $ArgsList = @(
        ".\test_concurrentClient.py",
        $HostCentral,
        $PuertoCentral,
        $Mensaje
    )

    # Se añade -WorkingDirectory $PSScriptRoot
    Start-Process -FilePath "py" -ArgumentList $ArgsList -WorkingDirectory $PSScriptRoot -NoNewWindow

    # Pequeña pausa (50 ms) para evitar saturar la CPU de golpe durante el arranque
    Start-Sleep -Milliseconds 50
}

Write-Host "`n¡Se han lanzado $TotalClientes procesos cliente exitosamente!" -ForegroundColor Green