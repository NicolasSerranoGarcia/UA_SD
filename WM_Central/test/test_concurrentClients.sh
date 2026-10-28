#!/bin/bash

TotalClientes=100                  
HostCentral="localhost"
PuertoCentral=8080
ProtocolVer=1

AckCodeInicio=100
StationIdBase=1000

Ubicaciones=("Alicante" "Barcelona" "Madrid" "Valencia" "Sevilla" "Bilbao")
NumUbicaciones=${#Ubicaciones[@]}

# Función para calcular el LRC (XOR de todos los caracteres ASCII en formato HEX de 2 dígitos)
calc_lrc() {
    local str="$1"
    local lrc=0
    local i ascii
    for (( i=0; i<${#str}; i++ )); do
        printf -v ascii "%d" "'${str:$i:1}"
        lrc=$(( lrc ^ ascii ))
    done
    printf "%02X" "$lrc"
}

echo -e "\033[32mIniciando $TotalClientes clientes hacia ${HostCentral}:${PuertoCentral}...\033[0m"

for ((i=1; i<=TotalClientes; i++)); do
    AckCode=$((AckCodeInicio + i))
    StationId=$((StationIdBase + i))
    
    idx=$(( (i - 1) % NumUbicaciones ))
    Location="${Ubicaciones[$idx]}"

    # 1. Construir la trama base
    BaseMsg="NAP#$ProtocolVer#$AckCode#0#$StationId#$Location"
    
    # 2. Calcular el LRC sobre la trama base
    LRC=$(calc_lrc "$BaseMsg")

    # 3. Ensamblar la trama final con el LRC antes de FIN
    Mensaje="${BaseMsg}#${LRC}#FIN"

    # Ejecución en segundo plano usando '&'
    python3 ./test_concurrentClient.py "$HostCentral" "$PuertoCentral" "$Mensaje" &

    sleep 0.05
done

echo -e "\n\033[32m¡Se han lanzado $TotalClientes procesos cliente exitosamente!\033[0m"