"""Compile and exercise the actual firmware send functions without a device."""
import json
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "esp32/esp32.ino").read_text()
heartbeat = source[source.index("void enviarHeartbeat("):source.index("void registrarEstado(")]
send = source[source.index("void processarResultadoEnvio("):source.index("void abrirTrava(")]
with tempfile.TemporaryDirectory(prefix="14bis-envios-") as directory:
    directory = Path(directory)
    (directory / "firmware-envios.inc").write_text(heartbeat + send)
    executable = directory / "envios-test"
    subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(directory), str(root / "tests/firmware-envios.test.cpp"),
                    "-o", str(executable)], check=True)
    output = subprocess.check_output([str(executable)], text=True)
    for line in output.splitlines():
        payload = json.loads(line)
        assert payload["devices/geladeira"]["wifi"]["ssid"] == 'Wi-Fi "clube"\\teste\n'
        assert payload["orders/pedido-1/execution/state"] == "opened"
        assert payload["commands/geladeira/pedido-1"] is None
print("firmware-envios: PATCH, JSON, cadência, timeout, retentativas e reinício OK")
