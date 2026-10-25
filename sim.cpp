#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ESP32 + MPU6050 + SIM800L
// I2C MPU6050: SDA=21, SCL=22 (ajusta si tu placa usa otros pines)
// SIM800L TX -> ESP32 GPIO16 (RX2)
// SIM800L RX <- ESP32 GPIO17 (TX2) mediante adaptación de nivel si hace falta
// Botón de prueba: GPIO25 a GND
// Buzzer: GPIO32 a través de un driver adecuado si consume más corriente que un GPIO.

Adafruit_MPU6050 mpu;
HardwareSerial sim800(2);

const int SIM_RX = 16;
const int SIM_TX = 17;
const int BUTTON_PIN = 25;
const int BUZZER_PIN = 32;

const char TELEFONO[] = "+52XXXXXXXXXX"; // Cambia por el número autorizado, con lada
const float IMPACT_THRESHOLD_G = 3.0;    // AJUSTAR con pruebas controladas
const unsigned long CONFIRM_MS = 1500;   // El impacto debe mantenerse/confirmarse
const unsigned long COOLDOWN_MS = 60000; // Evita llamadas repetidas durante 60 s

unsigned long impactStart = 0;
unsigned long lastCall = 0;
bool impactPending = false;
bool alertSent = false;

void sendAT(const char *cmd, unsigned long waitMs = 800) {
  sim800.println(cmd);
  unsigned long start = millis();
  while (millis() - start < waitMs) {
    while (sim800.available()) Serial.write(sim800.read());
  }
}

void makeCall() {
  if (lastCall != 0 && millis() - lastCall < COOLDOWN_MS) {
    Serial.println("Alerta omitida: periodo de espera activo.");
    return;
  }

  Serial.println("Marcando numero de emergencia configurado...");
  sim800.print("ATD");
  sim800.print(TELEFONO);
  sim800.println(";");
  lastCall = millis();

  // Deja sonar la llamada hasta 25 s; después cuelga.
  delay(25000);
  sendAT("ATH", 1000);
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Wire.begin(21, 22);
  if (!mpu.begin()) {
    Serial.println("ERROR: no se detecta MPU6050. Revisa SDA/SCL y alimentacion.");
    while (true) {
      digitalWrite(BUZZER_PIN, !digitalRead(BUZZER_PIN));
      delay(250);
    }
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  sim800.begin(9600, SERIAL_8N1, SIM_RX, SIM_TX);
  delay(1500);
  sendAT("AT", 1000);
  sendAT("ATE0", 500);
  sendAT("AT+CPIN?", 1000);
  sendAT("AT+CSQ", 1000);
  sendAT("AT+CREG?", 1000);

  Serial.println("Sistema listo. Boton = llamada de prueba; sensor = deteccion de impacto.");
}

void loop() {
  // Botón manual para probar la llamada. Pulsación a GND.
  if (digitalRead(BUTTON_PIN) == LOW) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(150);
    digitalWrite(BUZZER_PIN, LOW);
    makeCall();
    while (digitalRead(BUTTON_PIN) == LOW) delay(20);
  }

  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // La aceleración del MPU6050 viene en m/s². Se calcula magnitud en unidades g.
  const float magnitudeG = sqrt(
    a.acceleration.x * a.acceleration.x +
    a.acceleration.y * a.acceleration.y +
    a.acceleration.z * a.acceleration.z
  ) / 9.80665;

  // Un impacto suele ser breve: al superar el umbral se inicia una ventana
  // de confirmación, aunque la aceleración vuelva a bajar.
  if (magnitudeG >= IMPACT_THRESHOLD_G && !impactPending && !alertSent) {
    impactPending = true;
    impactStart = millis();
    Serial.println("Posible impacto detectado; esperando confirmacion...");
  }

  if (impactPending && !alertSent &&
      millis() - impactStart >= CONFIRM_MS) {
    alertSent = true;
    digitalWrite(BUZZER_PIN, HIGH);
    delay(2000);
    digitalWrite(BUZZER_PIN, LOW);
    makeCall();
  }

  // Rearme después del periodo de espera para permitir una nueva detección.
  if (alertSent && millis() - lastCall >= COOLDOWN_MS) {
    alertSent = false;
    impactPending = false;
  }

  delay(20);
}