/*
 * ESP32 - Lectura de termopar tipo K (señal acondicionada)
 *
 * La señal de la entrada del ADC ya viene acondicionada por el circuito
 * analógico externo (op-amps + sumador con compensación de unión fría).
 *
 * Mapeo (calibrado con mediciones reales):
 *   - V_MIN = 1.220 V  -> 20 °C  (T_MIN)
 *   - V_MAX = 3.258 V  -> 50 °C  (T_MAX)
 *
 * Requiere librería: LiquidCrystal_I2C (PCF8574) + Wire
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------------------------------------------------------------------------
// CONFIGURACIÓN EDITABLE
// ---------------------------------------------------------------------------

// Rango de temperatura (grados Celsius)
static const float T_MIN = 20.0f;   // °C al voltaje mínimo
static const float T_MAX = 50.0f;   // °C al voltaje máximo

// Voltaje medido correspondiente a T_MIN y T_MAX (calibración real del circuito)
static const float V_MIN = 1.220f;  // V a 20 °C
static const float V_MAX = 3.258f;  // V a 50 °C

// Pines ADC (ESP32 normal)
// GPIO34 = ADC1_CH6 (solo entrada, pin dedicado ideal para lectura analógica).
// Otras opciones válidas de ADC1: GPIO32, GPIO33, GPIO35, GPIO36, GPIO39.
// Evita pines de ADC2 (GPIO0,2,4,12-15,25-27) si usas WiFi.
static const int PIN_ADC = 34;      // Pin de la señal del termopar acondicionado

// Atenuación del ADC. En el core ESP32 3.3.11 el enum es:
//   ADC_0db = 0, ADC_2_5db = 1, ADC_6db = 2, ADC_11db = 3
// (no existe 12 dB en este core). 11 dB mide 0-3.1V, suficiente para 0-3.3V.
static const adc_attenuation_t ADC_ATTEN = ADC_11db;

// Número de muestras a promediar para reducir ruido del ADC
static const int NUM_SAMPLES = 64;

// Retardo entre actualizaciones (ms)
static const unsigned long UPDATE_MS = 500;

// Dirección I2C del LCD (0x27 o 0x3F según el módulo PCF8574)
static const uint8_t LCD_ADDR = 0x27;

// Alarma de temperatura: se activa cuando la temperatura real supera este valor.
static const float ALARM_TEMP = 50.0f;   // °C — umbral de alarma en pantalla

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------

LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

// Voltaje real de referencia a 3.3 V (calibrable si usas alimentación regulada)
static const float VREF = 3.3f;

static float readVoltage(int pin) {
  uint32_t sum = 0;
  for (int i = 0; i < NUM_SAMPLES; i++) {
    sum += analogRead(pin);
    delayMicroseconds(100);
  }
  float avg_raw = (float)sum / NUM_SAMPLES;
  // DEBUG: valor crudo del ADC (0-4095). Útil para diagnosticar lecturas erróneas.
  Serial.print("DEBUG raw_adc="); Serial.println(avg_raw, 1);
  // ESP32-S3 ADC: 12 bits -> valor 0..4095
  float v = (avg_raw / 4095.0f) * VREF;
  return v;
}

static float voltageToTempRaw(float v) {
  // Igual que voltageToTemp pero SIN limitar (clamp) el resultado.
  // Se usa para detectar si la temperatura real superó el rango calibrado,
  // incluso cuando voltageToTemp() ya la recortó a T_MAX para mostrarla.
  float span_v = V_MAX - V_MIN;           // voltaje total del span (2.038 V)
  float span_t = T_MAX - T_MIN;           // 30 °C
  return T_MIN + ((v - V_MIN) / span_v) * span_t;
}

static float voltageToTemp(float v) {
  float t = voltageToTempRaw(v);
  if (t < T_MIN) t = T_MIN;
  if (t > T_MAX) t = T_MAX;
  return t;
}

void setup() {
  Serial.begin(115200);
  analogSetPinAttenuation(PIN_ADC, ADC_ATTEN);

  Wire.begin(21, 22);                     // SDA=GPIO21, SCL=GPIO22 (por defecto ESP32)
  // Si tu LCD usa otros pines, cambia Wire.begin(SDA, SCL).

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Termopar K");
  lcd.setCursor(0, 1);
  lcd.print("Inicializando...");
  delay(1000);

  Serial.println("ESP32-S3 Termopar K - Listo");
  Serial.print("V_MIN="); Serial.print(V_MIN); Serial.print(" V (");
  Serial.print(T_MIN); Serial.println(" C)");
  Serial.print("V_MAX="); Serial.print(V_MAX); Serial.print(" V (");
  Serial.print(T_MAX); Serial.println(" C)");
}

void loop() {
  float v = readVoltage(PIN_ADC);
  float temp_raw = voltageToTempRaw(v);      // temperatura real, sin recortar
  float temp = voltageToTemp(v);             // temperatura recortada al rango 20-50°C

  bool alarm = (temp_raw >= ALARM_TEMP);

  // Resolución de 0.5 °C en la presentación
  float temp_display = round(temp * 2.0f) / 2.0f;

  char line1[17];
  snprintf(line1, sizeof(line1), "Temp: %4.1f C", temp_display);
  lcd.setCursor(0, 0);
  lcd.print(line1);

  char line2[17];
  if (alarm) {
    // Parpadea el aviso de alarma alternando cada ciclo de loop (cada UPDATE_MS)
    static bool blink_state = false;
    blink_state = !blink_state;
    if (blink_state) {
      snprintf(line2, sizeof(line2), "ALARMA: %5.1fC", temp_raw);
    } else {
      snprintf(line2, sizeof(line2), "V: %1.3f V", v);
    }
  } else {
    snprintf(line2, sizeof(line2), "V: %1.3f V", v);
  }
  lcd.setCursor(0, 1);
  lcd.print("                ");   // limpia la línea antes de reescribir
  lcd.setCursor(0, 1);
  lcd.print(line2);

  Serial.print("v="); Serial.print(v, 3);
  Serial.print(" V -> T="); Serial.print(temp, 2);
  if (alarm) {
    Serial.print("  [ALARMA: temp real="); Serial.print(temp_raw, 2); Serial.print("C >= "); Serial.print(ALARM_TEMP, 1); Serial.print("C]");
  }
  Serial.println();

  delay(UPDATE_MS);
}
