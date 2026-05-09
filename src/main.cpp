#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <math.h>

// ====== CONFIGURACIÓN DE WIFI ======
#define WIFI_SSID  "Tacos de pla"   
#define WIFI_PASS  "vivamiura"

// Pines I2C (ESP32-C3)
#define SDA_PIN 8
#define SCL_PIN 9

// Twilio (WhatsApp Sandbox)
const char* ACCOUNT_SID = "ACfebca606adfe40675f993f00ec0f635f";
const char* AUTH_TOKEN  = "0bd126c9c269983e55d0abd107e4600f";
const char* FROM_NUMBER = "whatsapp:+14155238886";     // número del sandbox Twilio
const char* TO_NUMBER   = "whatsapp:+5216761027971";  // numero del receptor

// Detección de caída (PARAMETROS)
const float G_PEAK_TH       = 1.8;     
const float G_FREEFALL_TH   = 0.5;      // g: casi ingravidez
const unsigned long QUIET_MS    = 500; // ms de quietud posterior
const unsigned long COOLDOWN_MS = 10000;// ms entre alertas (anti-spam)

// LED local para aviso (si tu placa no tiene en el 10, puedes cambiarlo)
#define LED_PIN 10
// ===========================

// --- MPU6050 ---
#define MPU_ADDR 0x68
#define REG_PWR_MGMT_1   0x6B
#define REG_ACCEL_XOUT_H 0x3B
const float LSB_PER_G = 16384.0;

// Estado de la máquina
enum FallState {IDLE, PEAK, FREEFALL, QUIET};
FallState fstate = IDLE;
unsigned long state_ts = 0;
unsigned long last_alert_ts = 0;
bool mpu_ok = false;

// -------- Utilidades ----------
static inline float mag3(float x, float y, float z) {
  return sqrtf(x*x + y*y + z*z);
}

bool mpuInit() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_PWR_MGMT_1);
  Wire.write(0x00);             // wake up
  return (Wire.endTransmission() == 0);
}

bool readAccelG(float& axg, float& ayg, float& azg) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_ACCEL_XOUT_H);
  if (Wire.endTransmission(false) != 0) return false;

  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)6, (bool)true);
  if (Wire.available() < 6) return false;

  int16_t ax = (Wire.read() << 8) | Wire.read();
  int16_t ay = (Wire.read() << 8) | Wire.read();
  int16_t az = (Wire.read() << 8) | Wire.read();

  axg = ax / LSB_PER_G;
  ayg = ay / LSB_PER_G;
  azg = az / LSB_PER_G;
  return true;
}

// url-encode general (para To, From y Body; convierte '+' a %2B, etc.)
String urlEncode(const String& s) {
  String o; o.reserve(s.length()*3);
  for (size_t i = 0; i < s.length(); ++i) {
    unsigned char c = s[i];
    if (isalnum(c) || c=='-' || c=='_' || c=='.' || c=='~') {
      o += char(c);
    } else if (c == ' ') {
      o += "%20";
    } else {
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", c);
      o += buf;
    }
  }
  return o;
}

bool sendWhatsApp(const String& text) {
  String url  = String("https://api.twilio.com/2010-04-01/Accounts/") + ACCOUNT_SID + "/Messages.json";

  String body =
    "To="   + urlEncode(String(TO_NUMBER)) +
    "&From="+ urlEncode(String(FROM_NUMBER)) +
    "&Body="+ urlEncode(text);

  Serial.println("Cuerpo enviado a Twilio:");
  Serial.println(body);

  WiFiClientSecure client;
  client.setInsecure(); // PRODUCCIÓN: instala root CA de api.twilio.com

  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("http.begin() falló");
    return false;
  }
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  http.setAuthorization(ACCOUNT_SID, AUTH_TOKEN);

  int code = http.POST(body);
  String resp = http.getString();
  http.end();

  Serial.printf("Twilio HTTP %d\n", code);
  Serial.println("Respuesta Twilio:");
  Serial.println(resp);

  return (code >= 200 && code < 300);
}

// -------------- Setup --------------
void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("===== ESP32-C3 + MPU6050 + Twilio =====");

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000); // 100 kHz, más seguro
  mpu_ok = mpuInit();
  if (!mpu_ok) {
    Serial.println("ERROR: MPU6050 no responde (revisa VCC, GND, SDA, SCL, direccion 0x68/0x69).");
  } else {
    Serial.println("MPU6050 inicializado correctamente ✅");
  }

  // WiFi
  Serial.printf("Conectando WiFi a SSID: '%s'\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }
  Serial.println("\nWiFi OK ✅");
  Serial.print("IP local: ");
  Serial.println(WiFi.localIP());

  Serial.println("\nComenzando lectura de aceleraciones...");
  Serial.println("ax[g]\tay[g]\taz[g]\t|g|\testado");
  Serial.println("============================================");

  state_ts = millis();
  last_alert_ts = 0;
}

// -------------- Loop --------------
void loop() {
  if (!mpu_ok) {
    // Si el sensor no está, no intentamos leer (evita NACKs constantes)
    delay(500);
    return;
  }

  float ax, ay, az;
  if (!readAccelG(ax, ay, az)) {
    Serial.println("Lectura I2C fallida (NACK)");
    delay(50);
    return;
  }

  float g = mag3(ax, ay, az);  // magnitud total (en g)
  unsigned long now = millis();

  // Mostrar aceleración en tiempo real
  Serial.print(ax, 3); Serial.print("\t");
  Serial.print(ay, 3); Serial.print("\t");
  Serial.print(az, 3); Serial.print("\t");
  Serial.print(g, 3);  Serial.print("\t");

  // Máquina de estados de caída
  switch (fstate) {
    case IDLE:
      Serial.println("IDLE");
      if (g > G_PEAK_TH) {
        fstate = PEAK;
        state_ts = now;
      }
      break;

    case PEAK:
      Serial.println("PEAK");
      if (g < G_FREEFALL_TH) {
        fstate = FREEFALL;
        state_ts = now;
      } else if (now - state_ts > 1000) {
        fstate = IDLE;
      }
      break;

    case FREEFALL:
      Serial.println("FREEFALL");
      if (g > 0.8 && g < 1.3) { // cerca de 1g
        fstate = QUIET;
        state_ts = now;
      } else if (now - state_ts > 1500) {
        fstate = IDLE;
      }
      break;

    case QUIET:
      Serial.println("QUIET");
      if (now - state_ts >= QUIET_MS) {
        if (now - last_alert_ts > COOLDOWN_MS) {
          digitalWrite(LED_PIN, HIGH);

          String msg = "ALERTA: el viejito se cayó.\n"
                       "g_peak>=" + String(G_PEAK_TH,1) +
                       " | free-fall<=" + String(G_FREEFALL_TH,1) + "\n" +
                       "ax=" + String(ax,2) + "g ay=" + String(ay,2) +
                       "g az=" + String(az,2) + "g";

          bool ok = sendWhatsApp(msg);
          if (!ok) Serial.println("No se pudo enviar WhatsApp ❌");
          else     Serial.println("WhatsApp enviado/encolado ✅");

          last_alert_ts = now;
          delay(800);
          digitalWrite(LED_PIN, LOW);
        }
        fstate = IDLE;
      }
      if (g > 1.6) fstate = IDLE; // se movió, cancelar
      break;
  }

  delay(50); // ~20 Hz de muestreo (suficiente para monitoreo)
}
