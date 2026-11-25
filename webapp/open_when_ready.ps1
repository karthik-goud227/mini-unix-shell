for ($attempt = 0; $attempt -lt 80; $attempt++) {
    try {
        $response = Invoke-WebRequest -Uri 'http://127.0.0.1:8765/api/session' -UseBasicParsing -TimeoutSec 2
        if ($response.StatusCode -eq 200) {
            Start-Process 'http://127.0.0.1:8765'
            exit 0
        }
    } catch {
        Start-Sleep -Milliseconds 500
    }
}
exit 1
