import asyncio
import json
import random
import os
import requests
from datetime import datetime
from azure.iot.device.aio import ProvisioningDeviceClient, IoTHubDeviceClient
from azure.iot.device import Message

ID_SCOPE = "0ne010F6F29"
DEVICE_ID = "27el4qswjxl"
SYMMETRIC_KEY = "eUv6c9rxr4AvYV2XhdW1lP5244H41tiDvp7qsD+XB40="
MODEL_ID = "dtmi:granjadecacao:MeterologiaDelPredio_36a;1"
AZURE_MAPS_KEY = "CMdZsWUO2Ke0R9kNHRwmV2TSBGLTjQEY0R4wtrH7hUq4xvnboV3KJQQJ99CIACYeBjFa4UkmAAAgAZMP2SHo"

LAT = 7.0
LON = -73.05

def obtener_clima_azure_maps():
    url = (
        f"https://atlas.microsoft.com/weather/currentConditions/json"
        f"?api-version=1.1&query={LAT},{LON}"
        f"&subscription-key={AZURE_MAPS_KEY}&unit=metric"
    )
    r = requests.get(url, timeout=10)
    r.raise_for_status()
    result = r.json()["results"][0]

    return { 
        "temperatura": result["temperature"]["value"],
        "HR": result["relativeHumidity"],
        "Lluvia": result.get("precipitationSummary", {}).get("pastHour", {}).get("value", 0.0),
        "viento": result["wind"]["speed"]["value"],
        "radiacion": round(random.uniform(0, 1200), 2),  # no disponible en este endpoint, se simula
    }

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
            payload = obtener_clima_azure_maps()
            msg = Message(json.dumps(payload))
            msg.content_encoding = "utf-8"
            msg.content_type = "application/json"
            await device_client.send_message(msg)
            print(f"[{datetime.now().strftime('%H:%M:%S')}] Enviado (Azure Maps):", payload)
            await asyncio.sleep(300)
    except KeyboardInterrupt:
        pass
    finally:
        await device_client.disconnect()

if __name__ == "__main__":
    asyncio.run(main())