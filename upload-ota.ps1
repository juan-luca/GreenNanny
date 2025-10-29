# Script para compilar y subir firmware y filesystem por OTA al ESP8266
# Uso: .\upload-ota.ps1

param(
    [Parameter(Mandatory=$false)]
    [string]$Hostname = "greennanny.local",
    
    [Parameter(Mandatory=$false)]
    [switch]$SkipCompile = $false
)

$ErrorActionPreference = "Stop"

function Write-ColorOutput($ForegroundColor) {
    $fc = $host.UI.RawUI.ForegroundColor
    $host.UI.RawUI.ForegroundColor = $ForegroundColor
    if ($args) {
        Write-Output $args
    }
    $host.UI.RawUI.ForegroundColor = $fc
}

Write-Host ""
Write-ColorOutput Yellow "=========================================="
Write-ColorOutput Yellow "   OTA Update - GreenNanny ESP8266"
Write-ColorOutput Yellow "=========================================="
Write-Host ""

Write-ColorOutput Cyan "Resolviendo hostname: $Hostname..."
try {
    $resolvedIP = [System.Net.Dns]::GetHostAddresses($Hostname) | Where-Object { $_.AddressFamily -eq 'InterNetwork' } | Select-Object -First 1
    
    if ($null -eq $resolvedIP) {
        Write-ColorOutput Red "ERROR: No se pudo resolver $Hostname"
        Write-Host "Verifica que el ESP8266 este encendido y conectado a WiFi"
        exit 1
    }
    
    $IP = $resolvedIP.IPAddressToString
    Write-ColorOutput Green "Hostname resuelto: $Hostname -> $IP"
    Write-Host ""
}
catch {
    Write-ColorOutput Red "ERROR: Fallo al resolver $Hostname"
    Write-Host "  Error: $($_.Exception.Message)"
    exit 1
}

if (-not $SkipCompile) {
    Write-ColorOutput Cyan "=========================================="
    Write-ColorOutput Cyan "   COMPILANDO FIRMWARE"
    Write-ColorOutput Cyan "=========================================="
    Write-Host ""
    
    $compileResult = & pio run --environment nodemcuv2 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-ColorOutput Red "Error al compilar firmware"
        exit 1
    }
    Write-ColorOutput Green "Firmware compilado exitosamente"
    Write-Host ""
    
    Write-ColorOutput Cyan "=========================================="
    Write-ColorOutput Cyan "   COMPILANDO FILESYSTEM"
    Write-ColorOutput Cyan "=========================================="
    Write-Host ""
    
    $buildfsResult = & pio run --target buildfs --environment nodemcuv2 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-ColorOutput Red "Error al compilar filesystem"
        exit 1
    }
    Write-ColorOutput Green "Filesystem compilado exitosamente"
    Write-Host ""
    
    Write-ColorOutput Cyan "Copiando binarios..."
    New-Item -ItemType Directory -Force -Path ".\bin\firmware" | Out-Null
    New-Item -ItemType Directory -Force -Path ".\bin\filesystem" | Out-Null
    
    Copy-Item ".\.pio\build\nodemcuv2\firmware.bin" ".\bin\firmware\firmware.bin" -Force
    Copy-Item ".\.pio\build\nodemcuv2\littlefs.bin" ".\bin\filesystem\littlefs.bin" -Force
    
    Write-ColorOutput Green "Binarios copiados"
    Write-Host ""
}

$firmwarePath = ".\bin\firmware\firmware.bin"
$filesystemPath = ".\bin\filesystem\littlefs.bin"

if (-not (Test-Path $firmwarePath)) {
    Write-ColorOutput Red "ERROR: No se encuentra $firmwarePath"
    exit 1
}

if (-not (Test-Path $filesystemPath)) {
    Write-ColorOutput Red "ERROR: No se encuentra $filesystemPath"
    exit 1
}

$firmwareSize = [math]::Round((Get-Item $firmwarePath).Length / 1KB, 2)
$filesystemSize = [math]::Round((Get-Item $filesystemPath).Length / 1KB, 2)

Write-ColorOutput Cyan "Binarios listos:"
Write-Host "  - Firmware:    $firmwareSize KB"
Write-Host "  - Filesystem:  $filesystemSize KB"
Write-Host ""

Write-ColorOutput Cyan "Verificando conexion con $IP..."
$ping = Test-Connection -ComputerName $IP -Count 2 -Quiet

if (-not $ping) {
    Write-ColorOutput Red "ERROR: No se puede conectar a $IP"
    exit 1
}

Write-ColorOutput Green "Conexion establecida"
Write-Host ""

function Upload-File {
    param($FilePath, $Name)
    
    Write-ColorOutput Cyan "Subiendo $Name..."
    
    try {
        $uri = "http://$IP/update"
        $fileBytes = [System.IO.File]::ReadAllBytes($FilePath)
        $fileName = Split-Path $FilePath -Leaf
        
        # Credenciales de autenticacion
        $username = "admin"
        $password = "greennanny2024"
        $base64AuthInfo = [Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes("${username}:${password}"))
        
        $boundary = [System.Guid]::NewGuid().ToString()
        $LF = "`r`n"
        
        $bodyLines = (
            "--$boundary",
            "Content-Disposition: form-data; name=`"file`"; filename=`"$fileName`"",
            "Content-Type: application/octet-stream$LF",
            [System.Text.Encoding]::GetEncoding("iso-8859-1").GetString($fileBytes),
            "--$boundary--$LF"
        ) -join $LF
        
        $headers = @{
            "Authorization" = "Basic $base64AuthInfo"
        }
        
        $response = Invoke-RestMethod -Uri $uri -Method Post -ContentType "multipart/form-data; boundary=$boundary" -Headers $headers -Body ([System.Text.Encoding]::GetEncoding("iso-8859-1").GetBytes($bodyLines)) -TimeoutSec 120
        
        Write-ColorOutput Green "$Name subido exitosamente"
        return $true
    }
    catch {
        Write-ColorOutput Red "Error al subir $Name"
        Write-Host "  Error: $($_.Exception.Message)"
        return $false
    }
}

Write-ColorOutput Yellow "Deseas continuar con la actualizacion OTA? (S/N)"
$confirmation = Read-Host

if ($confirmation -ne "S" -and $confirmation -ne "s") {
    Write-ColorOutput Yellow "Operacion cancelada"
    exit 0
}

Write-Host ""

$firmwareSuccess = Upload-File -FilePath $firmwarePath -Name "Firmware"

if ($firmwareSuccess) {
    Write-Host ""
    Write-ColorOutput Cyan "Esperando 10 segundos para que el ESP reinicie..."
    Start-Sleep -Seconds 10
    
    Write-ColorOutput Cyan "Verificando dispositivo..."
    $attempts = 0
    $maxAttempts = 30
    $online = $false
    
    while ($attempts -lt $maxAttempts -and -not $online) {
        $attempts++
        $online = Test-Connection -ComputerName $IP -Count 1 -Quiet
        if (-not $online) {
            Write-Host "." -NoNewline
            Start-Sleep -Seconds 2
        }
    }
    
    Write-Host ""
    
    if ($online) {
        Write-ColorOutput Green "Dispositivo online"
        Write-Host ""
        
        $filesystemSuccess = Upload-File -FilePath $filesystemPath -Name "Filesystem"
        
        if ($filesystemSuccess) {
            Write-Host ""
            Write-ColorOutput Green "=========================================="
            Write-ColorOutput Green "   ACTUALIZACION COMPLETADA"
            Write-ColorOutput Green "=========================================="
            Write-Host ""
            Write-Host "Espera 10-15 segundos antes de acceder."
            Write-Host ""
            Write-Host "Accede a:"
            Write-Host "  - Dashboard:  http://$IP/"
            Write-Host "  - Debug Logs: http://$IP/debug.html"
            Write-Host ""
        }
    } else {
        Write-ColorOutput Red "El dispositivo no respondio"
    }
} else {
    Write-ColorOutput Red "La actualizacion fallo"
    exit 1
}
