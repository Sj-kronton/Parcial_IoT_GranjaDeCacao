#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#include <time.h>


const char* ssid = "Wokwi-GUEST";
const char* password = "";

// --- Credenciales IoT Central (reemplaza con las tuyas) ---
const char* deviceId = "7x1973kjgs";
const char* idScope = "0ne010F6F29";
const char* primaryKey = "PT5jzqUgbg62NGfjzcm13D42SzQB3o9qMyJnPSV2Dk0=";
const char* modelId = "dtmi:granjadecacao:LoteDeCultivo_15o;1";

// Tras aprovisionar manualmente una vez (ver nota abajo), el hub queda fijo:
const char* iotHubHost = "iotc-d56f0dee-f8d2-4958-9948-b3d323c4f2d9.azure-devices.net";
//granjadecacao
WiFiClientSecure net;
PubSubClient client(net);
String sasToken = "";

const int pinHumedad = 34; // potenciómetro
#define DHTPIN 4

class SimpleDHT22 {
  public:
    SimpleDHT22(uint8_t pin) : pinNumber(pin) {}

    void begin() {
      pinMode(pinNumber, INPUT_PULLUP);
    }

    float readTemperature() {
      uint8_t data[5] = {0};
      if (!readSensor(data)) {
        return NAN;
      }

      int16_t rawTemp = ((int16_t)data[2] << 8) | data[3];
      if (rawTemp & 0x8000) {
        rawTemp = -(rawTemp & 0x7FFF);
      }

      return rawTemp / 10.0f;
    }

    float readHumidity() {
      uint8_t data[5] = {0};
      if (!readSensor(data)) {
        return NAN;
      }

      uint16_t rawHumidity = ((uint16_t)data[0] << 8) | data[1];
      return rawHumidity / 10.0f;
    }

  private:
    uint8_t pinNumber;

    bool readSensor(uint8_t data[5]) {
      pinMode(pinNumber, OUTPUT);
      digitalWrite(pinNumber, LOW);
      delay(20);
      digitalWrite(pinNumber, HIGH);
      delayMicroseconds(30);
      pinMode(pinNumber, INPUT_PULLUP);

      if (pulseIn(pinNumber, LOW, 100000) == 0) return false;
      if (pulseIn(pinNumber, HIGH, 100000) == 0) return false;

      for (uint8_t i = 0; i < 40; i++) {
        uint32_t lowTime = pulseIn(pinNumber, LOW, 100000);
        uint32_t highTime = pulseIn(pinNumber, HIGH, 100000);

        if (lowTime == 0 || highTime == 0) {
          return false;
        }

        uint8_t bitIndex = i / 8;
        data[bitIndex] <<= 1;
        if (highTime > 40) {
          data[bitIndex] |= 1;
        }
      }

      uint8_t checksum = data[0] + data[1] + data[2] + data[3];
      if (checksum != data[4]) {
        return false;
      }

      return true;
    }
};

SimpleDHT22 dht(DHTPIN);

String base64Encode(const unsigned char* input, size_t inputLen) {
  size_t outLen = 0;
  size_t encodedLen = 4 * ((inputLen + 2) / 3) + 1;
  char* output = (char*)malloc(encodedLen);
  if (!output) {
    return "";
  }

  int ret = mbedtls_base64_encode((unsigned char*)output, encodedLen, &outLen, input, inputLen);
  if (ret != 0) {
    free(output);
    return "";
  }

  output[outLen] = '\0';   // <-- LA CORRECCIÓN: terminar el string explícitamente
  String result(output);
  free(output);
  return result;
}

String urlEncode(String value) {
  String encoded = "";
  for (unsigned int i = 0; i < value.length(); i++) {
    char c = value.charAt(i);
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
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

  String encodedUri = urlEncode(resourceUri);           // <-- CODIFICAR PRIMERO
  String stringToSign = encodedUri + "\n" + String(expiry);  // <-- FIRMAR LA VERSIÓN CODIFICADA

  unsigned char hmacResult[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&ctx, decodedKey, keyLength);
  mbedtls_md_hmac_update(&ctx, (const unsigned char*)stringToSign.c_str(), stringToSign.length());
  mbedtls_md_hmac_finish(&ctx, hmacResult);
  mbedtls_md_free(&ctx);

  String signature = base64Encode(hmacResult, sizeof(hmacResult));
  String encodedSignature = urlEncode(signature);       // <-- usar la función completa, no .replace()

  String token = "SharedAccessSignature sr=" + encodedUri + "&sig=" + encodedSignature + "&se=" + String(expiry);
  return token;
}


//setInsecure
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
  Serial.print("Epoch actual: ");//temporal
  Serial.println(time(nullptr));//Temporal
}


static const char* rootCACert =
"-----BEGIN CERTIFICATE-----\n"
"MIIDdzCCAl+gAwIBAgIEAgAAuTANBgkqhkiG9w0BAQsFADBaMQswCQYDVQQGEwJJ\n"
"RTESMBAGA1UEChMJQmFsdGltb3JlMRMwEQYDVQQLEwpDeWJlclRydXN0MSIwIAYD\n"
"VQQDExlCYWx0aW1vcmUgQ3liZXJUcnVzdCBSb290MB4XDTIwMDcyMTIzMzUwMFoX\n"
"DTI1MDcxOTIzMzUwMFowWjELMAkGA1UEBhMCSUUxEjAQBgNVBAoTCUJhbHRpbW9y\n"
"ZTETMBEGA1UECxMKQ3liZXJUcnVzdDEiMCAGA1UEAxMZQmFsdGltb3JlIEN5YmVy\n"
"VHJ1c3QgUm9vdDCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAKMEuyKr\n"
"mD1M6ZWmGtWZg5Pq7g9nR8UoZ5m6j3jF0Q5h2WQjJ2uVbK8KxY6XmWQyYg0C9i\n"
"mZqP2y0n2h0r3Y1sQ8v8g5nQ7jR6m3sR4p4v1kX0QcXZ5qO2wW9yL3pB0nM5V\n"
"-----END CERTIFICATE-----\n"; //temporal

void connectMQTT() {
  net.setInsecure();
  client.setBufferSize(2048);
  client.setKeepAlive(60);


  client.setServer(iotHubHost, 8883);

  String resourceUri = String(iotHubHost) + "/devices/" + deviceId;
  unsigned long expiry = time(nullptr) + 3600;
  sasToken = generateSasToken(resourceUri, primaryKey, expiry);

  if (sasToken.length() == 0) {
    Serial.println("SAS token vacío. Revisa primaryKey o Base64.");
    delay(5000);
    return;
  }
  
  String username = String(iotHubHost) + "/" + deviceId +
                    "/?api-version=2021-04-12&model-id=" + urlEncode(modelId);

  Serial.print("Usuario MQTT: ");
  Serial.println(username);
  Serial.print("Longitud SAS: ");
  Serial.println(sasToken.length());

  Serial.println("--- DEBUG SAS TOKEN ---");
  Serial.print("resourceUri: ");
  Serial.println(resourceUri);
  Serial.print("stringToSign: ");
  Serial.println(resourceUri + "\n" + String(expiry));
  Serial.print("SAS Token completo: "); //temporal
  Serial.println(sasToken);
  Serial.println("--- FIN DEBUG ---");

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
  dht.begin();
  connectWiFi();
  connectMQTT();
}



void loop() {
  if (!client.connected()) {
    connectMQTT();
  }
  client.loop();

  // --- Variación simulada de sensores (opción 1: sinusoidal) ---
  float t = millis() / 1000.0;  // segundos desde el arranque

  // Humedad del suelo: base 45%, oscila ±10% cada 30 segundos
  float humedadSuelo = 45.0 + 10.0 * sin(t * 2 * PI / 30.0);

  // Temperatura canopy: base 28°C, oscila ±3°C cada 45 segundos
  float tempCanopy = 28.0 + 3.0 * sin(t * 2 * PI / 45.0);

  String payload = "{\"humedadDeSuelo\":" + String(humedadSuelo, 2) +
                    ",\"TempCanopy\":" + String(tempCanopy, 2) + "}";

  String topic = "devices/" + String(deviceId) + "/messages/events/";
  client.publish(topic.c_str(), payload.c_str());
  Serial.println("Enviado: " + payload);

  delay(15000); // intervalo 15s, distinto a los Python (60s)
}

//Variacion entre simulados que varian (arriba) y los sensores reales (abajo)

//void loop() {
//  if (!client.connected()) {
//    connectMQTT();
//  }
//  client.loop();

//  float humedadSuelo = map(analogRead(pinHumedad), 0, 4095, 20, 70);
//  float tempCanopy = dht.readTemperature();
//  if (isnan(tempCanopy)) tempCanopy = 25.0;

//  String payload = "{\"humedadSuelo\":" + String(humedadSuelo, 2) +
//                    ",\"TempCanopy\":" + String(tempCanopy, 2) + "}";

//  String topic = "devices/" + String(deviceId) + "/messages/events/";
//  client.publish(topic.c_str(), payload.c_str());
//  Serial.println("Enviado: " + payload);

//  delay(15000); // intervalo 15s, distinto a los Python (60s)
//}//DNS Failed