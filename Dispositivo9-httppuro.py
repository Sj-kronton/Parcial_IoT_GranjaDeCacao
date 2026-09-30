

ID_SCOPE = ""
DEVICE_ID = ""
SYMMETRIC_KEY = ""
MODEL_ID = "dtmi:granjadecacao:ReservorioYRiego#####"

import time
import json
import random
import base64
import hmac
import hashlib
import urllib.parse
import requests

from azure.iot.device import (
    ProvisioningDeviceClient,
    IoTHubDeviceClient
)


# ============================================================
# DATOS DEL DISPOSITIVO
# ============================================================



# ============================================================
# BOOTSTRAP ÚNICO: OBTENER EL HUB ASIGNADO VÍA DPS
# ============================================================

provisioning_client = ProvisioningDeviceClient.create_from_symmetric_key(
    provisioning_host="global.azure-devices-provisioning.net",
    registration_id=DEVICE_ID,
    id_scope=ID_SCOPE,
    symmetric_key=SYMMETRIC_KEY,
    websockets=True,
)

# Enviar Model ID durante el registro
provisioning_client.provisioning_payload = {
    "modelId": MODEL_ID
}

registration_result = provisioning_client.register()

HUB_HOSTNAME = registration_result.registration_state.assigned_hub

print("Hub asignado:", HUB_HOSTNAME)


# ============================================================
# CLIENTE IoT HUB
# Se utiliza para actualizar PROPERTIES
# ============================================================

device_client = IoTHubDeviceClient.create_from_symmetric_key(
    device_id=DEVICE_ID,
    hostname=HUB_HOSTNAME,
    symmetric_key=SYMMETRIC_KEY,
)

device_client.connect()

print("Conectado al IoT Hub para actualizar propiedades")


# ============================================================
# GENERACIÓN MANUAL DEL SAS
# ============================================================

def generate_sas_token(uri, key, expiry=3600):

    ttl = int(time.time() + expiry)

    sign_key = f"{urllib.parse.quote_plus(uri)}\n{ttl}"

    signature = base64.b64encode(
        hmac.HMAC(
            base64.b64decode(key),
            sign_key.encode("utf-8"),
            hashlib.sha256
        ).digest()
    )

    return (
        f"SharedAccessSignature "
        f"sr={urllib.parse.quote_plus(uri)}"
        f"&sig={urllib.parse.quote_plus(signature)}"
        f"&se={ttl}"
    )


# ============================================================
# DATOS PARA EL ENVÍO HTTP DE TELEMETRÍA
# ============================================================

resource_uri = f"{HUB_HOSTNAME}/devices/{DEVICE_ID}"

url = (
    f"https://{HUB_HOSTNAME}"
    f"/devices/{DEVICE_ID}"
    f"/messages/events"
    f"?api-version=2020-09-30"
)


# ============================================================
# SIMULACIÓN DEL TANQUE
# ============================================================

nivel_tanque = 150.0


# ============================================================
# BUCLE PRINCIPAL
# ============================================================

while True:

    # --------------------------------------------------------
    # GENERAR SAS PARA EL ENVÍO DE TELEMETRÍA
    # --------------------------------------------------------

    sas_token = generate_sas_token(
        resource_uri,
        SYMMETRIC_KEY
    )

    headers = {
        "Authorization": sas_token,
        "Content-Type": "application/json",
    }


    # --------------------------------------------------------
    # SIMULACIÓN DEL NIVEL DEL TANQUE
    # --------------------------------------------------------

    nivel_tanque += random.uniform(-8, 5)

    # Mantener el nivel entre 0 y 200
    nivel_tanque = max(
        0,
        min(200, nivel_tanque)
    )


    # --------------------------------------------------------
    # LÓGICA DE LA BOMBA
    # --------------------------------------------------------

    # La bomba se enciende cuando el nivel
    # baja de 50 cm.

    bomba_encendida = nivel_tanque < 50


    # --------------------------------------------------------
    # SIMULACIÓN DEL CAUDAL
    # --------------------------------------------------------

    if bomba_encendida:
        caudal = round(
            random.uniform(5, 15),
            2
        )
    else:
        caudal = 0.0


    # ========================================================
    # 1. TELEMETRÍA
    # ========================================================

    # IMPORTANTE:
    # Bomba NO se incluye aquí porque es PROPERTY,
    # no TELEMETRY.

    payload = {
        "NivelTanque": float(
            round(nivel_tanque, 2)
        ),
        "Caudal": caudal,
    }


    # --------------------------------------------------------
    # ENVIAR TELEMETRÍA MEDIANTE HTTP
    # --------------------------------------------------------

    r = requests.post(
        url,
        headers=headers,
        json=payload,
        timeout=10
    )


    # ========================================================
    # 2. PROPERTY: BOMBA
    # ========================================================

    # Bomba es un Boolean.
    #
    # True  -> Encendida
    # False -> Apagada

    reported_properties = {
        "Bomba": bomba_encendida
    }


    # --------------------------------------------------------
    # REPORTAR PROPERTY AL DEVICE TWIN
    # --------------------------------------------------------

    device_client.patch_twin_reported_properties(
        reported_properties
    )


    # ========================================================
    # INFORMACIÓN EN CONSOLA
    # ========================================================

    print(
        f"[{r.status_code}] "
        f"Telemetría enviada: {payload} "
        f"| Bomba: "
        f"{'ON' if bomba_encendida else 'OFF'}"
    )


    # --------------------------------------------------------
    # ESPERAR 60 SEGUNDOS
    # --------------------------------------------------------

    time.sleep(60)
