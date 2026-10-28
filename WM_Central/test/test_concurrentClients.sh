#!/bin/bash

TotalClientes=100                  
HostCentral="localhost"
PuertoCentral=8080
ProtocolVer=1

AckCodeInicio=100
StationIdBase=1000

Ubicaciones=("Alicante" "Barcelona" "Madrid" "Valencia" "Sevilla" "Bilbao")
NumUbicaciones=${#Ubicaciones[@]}

echo -e "\033[32mIniciando $TotalClientes clientes hacia ${HostCentral}:${PuertoCentral}...\033[0m"

for ((i=1; i<=TotalClientes; i++)); do
    AckCode=$((AckCodeInicio + i))
    StationId=$((StationIdBase + i))
    
    # Índice del array (ajustado a base 0)
    idx=$(( (i - 1) % NumUbicaciones ))
    Location="${Ubicaciones[$idx]}"

    Mensaje="NAP#$ProtocolVer#$AckCode#0#$StationId#$Location#FIN"

    # Lanzar proceso de Python en segundo plano con '&'
    python3 ./test_concurrentClient.py "$HostCentral" "$PuertoCentral" "$Mensaje" &

    sleep 0.05
done

echo -e "\n\033[32m¡Se han lanzado $TotalClientes procesos cliente exitosamente!\033[0m"