import asyncio
import json
import random
import os
import requests
from datetime import datetime
from azure.iot.device.aio import ProvisioningDeviceClient, IoTHubDeviceClient
from azure.iot.device import Message

ID_SCOPE = "0ne010F6F29"
DEVICE_ID = "27ticbo9kjp"
SYMMETRIC_KEY = "O+MJetf2knmwdWYLv/nY1iJs8geEan377VbYkpdEP7A="
MODEL_ID = "dtmi:granjadecacao:CalidadDeAireRural_2x1;1"

LAT = 7.0
LON = -73.05

def safe_float(value, default=None):
    if value is None:
        return default
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def calcular_aqi_desde_pm25(pm25):
    # Fórmula simplificada EPA por tramos (breakpoints típicos)
    if pm25 <= 12.0:
        return round((50/12.0) * pm25)
    elif pm25 <= 35.4:
        return round(51 + (49/(35.4-12.1)) * (pm25-12.1))
    else:
        return round(101 + (49/(55.4-35.5)) * (pm25-35.5))


def obtener_calidad_aire():
    url = (
        f"https://air-quality-api.open-meteo.com/v1/air-quality?"
        f"latitude={LAT}&longitude={LON}"
        f"&current=pm2_5,relative_humidity_2m"
    )

    try:
        r = requests.get(url, timeout=10)
        r.raise_for_status()
        data = r.json().get("current", {})
    except requests.RequestException as e:
        print(f"[WARN] Error al consultar la API de calidad del aire: {e}")
        return {
            "PM2_5": 15.0,
            "HR": 70.0,
            "AQI": 50,
        }

    pm25 = safe_float(data.get("pm2_5"), None)
    hr = safe_float(data.get("relative_humidity_2m"), None)

    if pm25 is None:
        pm25 = 15.0

    indice = int(calcular_aqi_desde_pm25(pm25))

    payload = {
        "PM2_5": float(pm25),
        "AQI": int(indice),
    }

    if hr is not None:
        payload["HR"] = float(hr) #Recordar: La api no devuelve un valor de humedad y por eso no se envia nada a IoT

    return payload

async def main():
    provisioning_client = ProvisioningDeviceClient.create_from_symmetric_key(
        provisioning_host="global.azure-devices-provisioning.net",
        registration_id=DEVICE_ID,
        id_scope=ID_SCOPE,
        symmetric_key=SYMMETRIC_KEY,
        websockets=True,
    )
    provisioning_client.provisioning_payload = {"modelId": MODEL_ID}
    registration_result = await provisioning_client.register()

    device_client = IoTHubDeviceClient.create_from_symmetric_key(
        symmetric_key=SYMMETRIC_KEY,
        hostname=registration_result.registration_state.assigned_hub,
        device_id=DEVICE_ID,
        product_info=MODEL_ID,
        websockets=True,
    )
    await device_client.connect()
    print(f"[{DEVICE_ID}] Conectado.")

    try:
        while True:
            payload = obtener_calidad_aire()
            msg = Message(json.dumps(payload))
            msg.content_encoding = "utf-8"
            msg.content_type = "application/json"
            await device_client.send_message(msg)
            print(f"[{datetime.now().strftime('%H:%M:%S')}] Enviado (real):", payload)
            await asyncio.sleep(300)
    except KeyboardInterrupt:
        pass
    finally:
        await device_client.disconnect()

if __name__ == "__main__":
    asyncio.run(main())