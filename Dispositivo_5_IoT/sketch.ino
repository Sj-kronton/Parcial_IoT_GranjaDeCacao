#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#include <time.h>

const char* ssid = "Wokwi-GUEST";
const char* password = "";

// --- Credenciales del dispositivo 05 ---
const char* deviceId = "290lbx1pawb";
const char* idScope = "0ne010F6F29";
const char* primaryKey = "Yz7MOHLHMV4v7iNimoJCmD8GSfMkiAww5ktRK3sOMTM=";
const char* modelId = "dtmi:granjadecacao:AmbienteDeDoselYCampo_rn;1";
const char* iotHubHost = "iotc-d56f0dee-f8d2-4958-9948-b3d323c4f2d9.azure-devices.net"; // del bootstrap

WiFiClientSecure net;
PubSubClient client(net);
String sasToken = "";

String base64Encode(const unsigned char* input, size_t inputLen) {
  size_t outLen = 0;
  size_t encodedLen = 4 * ((inputLen + 2) / 3) + 1;
  char* output = (char*)malloc(encodedLen);
  if (!output) return "";

  int ret = mbedtls_base64_encode((unsigned char*)output, encodedLen, &outLen, input, inputLen);
  if (ret != 0) {
    free(output);
    return "";
  }

  output[outLen] = '\0'; // fix: terminador nulo explícito
  String result(output);
  free(output);
  return result;
}

String urlEncode(String value) {
  String encoded = "";
  for (unsigned int i = 0; i < value.length(); i++) {
    char c = value.charAt(i);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      char hex[4];
      snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
      encoded += hex;
    }
  }
  return encoded;
}

String generateSasToken(String resourceUri, String key, unsigned long expiry) {
  size_t keyLength = 0;
  unsigned char decodedKey[64] = {0};

  int ret = mbedtls_base64_decode(decodedKey, sizeof(decodedKey), &keyLength,
                                  (const unsigned char*)key.c_str(), key.length());
  if (ret != 0) {
    Serial.println("Error: clave base64 inválida");
    return "";
  }

  String encodedUri = urlEncode(resourceUri);                  // fix: codificar ANTES de firmar
  String stringToSign = encodedUri + "\n" + String(expiry);    // fix: firmar la versión codificada

  unsigned char hmacResult[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&ctx, decodedKey, keyLength);
  mbedtls_md_hmac_update(&ctx, (const unsigned char*)stringToSign.c_str(), stringToSign.length());
  mbedtls_md_hmac_finish(&ctx, hmacResult);
  mbedtls_md_free(&ctx);

  String signature = base64Encode(hmacResult, sizeof(hmacResult));
  String encodedSignature = urlEncode(signature);               // fix: encode completo, no .replace()

  return "SharedAccessSignature sr=" + encodedUri + "&sig=" + encodedSignature + "&se=" + String(expiry);
}

void connectWiFi() {
  WiFi.begin(ssid, password);
  Serial.print("Conectando WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" OK");

  configTime(0, 0, "pool.ntp.org");
  Serial.println("Sincronizando hora NTP...");
  while (time(nullptr) < 1000000000UL) {
    delay(200);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("Hora sincronizada.");
}

void connectMQTT() {
  net.setInsecure();
  client.setBufferSize(2048);
  client.setKeepAlive(60);
  client.setServer(iotHubHost, 8883);

  unsigned long expiry = time(nullptr) + 3600;
  String resourceUri = String(iotHubHost) + "/devices/" + deviceId;
  sasToken = generateSasToken(resourceUri, primaryKey, expiry);

  if (sasToken.length() == 0) {
    Serial.println("SAS token vacío. Revisa primaryKey o Base64.");
    delay(5000);
    return;
  }

  String username = String(iotHubHost) + "/" + deviceId +
                    "/?api-version=2021-04-12&model-id=" + urlEncode(modelId);

  Serial.println("Conectando a IoT Central...");
  while (!client.connected()) {
    if (client.connect(deviceId, username.c_str(), sasToken.c_str())) {
      Serial.println("Conectado a IoT Central!");
    } else {
      Serial.print("Fallo, rc=");
      Serial.println(client.state());
      delay(3000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  connectWiFi();
  connectMQTT();
}

void loop() {
  if (!client.connected()) {
    connectMQTT();
  }
  client.loop();

  float lluvia = random(0, 200) / 10.0;         // 0–20 mm
  float humedadFoliar = random(0, 1000) / 10.0; // 0–100%

  String payload = "{\"LluviaLote\":" + String(lluvia, 2) +
                    ",\"HumedadFoliar\":" + String(humedadFoliar, 2) + "}";


  String topic = "devices/" + String(deviceId) + "/messages/events/";
  client.publish(topic.c_str(), payload.c_str());
  Serial.println("Enviado: " + payload);

  delay(15000); // intervalo 15s
}