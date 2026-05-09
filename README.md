# SafeStep

Sistema de detección de caídas usando ESP32-C3 + MPU6050.

El proyecto detecta caídas mediante aceleración y envía alertas automáticas por WhatsApp utilizando Twilio.

---

##  Características

- Detección de caídas
- Monitoreo en tiempo real
- Alertas por WhatsApp
- LED indicador
- Protección anti-spam
- Comunicación WiFi

---

##  Hardware

- ESP32-C3
- MPU6050
- LED
- WiFi

---

##  Librerías utilizadas

- WiFi.h
- WiFiClientSecure.h
- HTTPClient.h
- Wire.h

---

##  Pines I2C

| Señal | GPIO |
|---|---|
| SDA | 8 |
| SCL | 9 |

---

##  Compilación

Proyecto desarrollado con PlatformIO.

```bash
pio run
