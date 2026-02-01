/*
 * SIDEBIKE - ESP32-C3 Navigation Display
 *
 * BLE implementado como ChronosESP32 (2 características TX/RX)
 * Compatible con Tasker + BLE Tasker Plugin
 */

#define VERSION "v2.2"

#include <Wire.h>
#include <U8g2lib.h>
#include <NimBLEDevice.h>
#include <time.h>
#include <FS.h>
#include <SPIFFS.h>
#include "images.h"

// ==================== PINES ====================
#define SDA_PIN 20
#define SCL_PIN 21
#define TOUCH_PIN 1
#define BUZZER_PIN 2

// ==================== BLE UUIDs ====================
#define DEVICE_NAME "SIDEBIKE"
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHAR_UUID_TX        "beb5483e-36e1-4688-b7f5-ea07361b26a8"  // Para enviar (NOTIFY)
#define CHAR_UUID_RX        "beb5483e-36e1-4688-b7f5-ea07361b26a9"  // Para recibir (WRITE)

#define PAIRING_WINDOW_MS 120000



// ==================== DISPLAY ====================
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// ==================== VARIABLES ====================
enum DeviceState { STATE_CLOCK, STATE_NAVIGATION, STATE_PAIRING, STATE_NOTIFICATION };
DeviceState currentState = STATE_PAIRING;

NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pCharTX = nullptr;
NimBLECharacteristic* pCharRX = nullptr;
bool deviceConnected = false;
bool pairingMode = true;
unsigned long startTime = 0;

// Buffer para datos recibidos
volatile bool newData = false;
String receivedData = "";
String lastProcessedMsg = "";  // Evitar duplicados callback/polling

// Navegación
String navIcon = "";
String navDistance = "";
String navStreet = "";
String navETA = "";  // Tiempo restante al destino
bool navActive = false;

// Notificaciones de apps
String notifApp = "";       // Nombre de la app (WhatsApp, Telegram, etc.)
String notifSender = "";    // Remitente del mensaje
String notifMessage = "";   // Contenido del mensaje
String notifTime = "";      // Hora de la notificación
bool notifActive = false;   // Hay notificación pendiente
unsigned long notifStartTime = 0;  // Tiempo cuando se mostró la notificación
DeviceState stateBeforeNotif = STATE_CLOCK;  // Estado al que volver después de notif

// Tiempo
struct tm timeInfo;
bool timeSet = false;
unsigned long lastTimeUpdate = 0;  // Para actualizar el tiempo en segundo plano
const char* diasSemana[] = {"dom", "lun", "mar", "mie", "jue", "vie", "sab"};

// Log NAV (SPIFFS)
const char* NAV_LOG_PATH = "/nav_log.txt";
const size_t NAV_LOG_MAX_BYTES = 16384;
bool spiffsReady = false;



// ==================== FORWARD DECLARATIONS ====================
void beep(int ms);
void processMessage(String msg);
void logNavNotification(const String& rawMsg, bool parsed, const String& icon, const String& distance, const String& street, const String& eta);
void handleSerialCommands();




// ==================== UTF-8 A LATIN-1 ====================
// Convierte UTF-8 a ISO-8859-1 para que las fuentes _te muestren acentos
String utf8ToLatin1(String utf8) {
  String result = "";
  for (int i = 0; i < utf8.length(); i++) {
    uint8_t c = utf8.charAt(i);
    if (c < 0x80) {
      result += (char)c;
    } else if (c == 0xC2) {
      // Caracteres 0x80-0xBF (ej: °, ², etc.)
      if (i + 1 < utf8.length()) {
        result += (char)utf8.charAt(++i);
      }
    } else if (c == 0xC3) {
      // Caracteres latinos acentuados (á=0xC3 0xA1 -> 0xE1)
      if (i + 1 < utf8.length()) {
        result += (char)(utf8.charAt(++i) + 0x40);
      }
    } else if (c >= 0xC4) {
      // Otros caracteres multibyte - saltar
      if (i + 1 < utf8.length()) i++;
    }
  }
  return result;
}

String sanitizeLogValue(String value) {
  value.replace('\r', ' ');
  value.replace('\n', ' ');
  value.replace('|', '/');
  return value;
}

String getTimestamp() {
  char buf[24];
  if (timeSet) {
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday,
             timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);
  } else {
    snprintf(buf, sizeof(buf), "ms:%lu", millis());
  }
  return String(buf);
}

void logNavNotification(const String& rawMsg, bool parsed, const String& icon, const String& distance, const String& street, const String& eta) {
  if (!spiffsReady) return;

  String line = getTimestamp();
  line += "|NAV";
  line += "|parsed=" + String(parsed ? "1" : "0");
  line += "|icon=" + sanitizeLogValue(icon);
  line += "|dist=" + sanitizeLogValue(distance);
  line += "|street=" + sanitizeLogValue(street);
  line += "|eta=" + sanitizeLogValue(eta);
  line += "|raw=" + sanitizeLogValue(rawMsg);
  line += "\n";

  size_t currentSize = 0;
  if (SPIFFS.exists(NAV_LOG_PATH)) {
    File readFile = SPIFFS.open(NAV_LOG_PATH, FILE_READ);
    if (readFile) {
      currentSize = readFile.size();
      readFile.close();
    }
  }

  if (currentSize + line.length() > NAV_LOG_MAX_BYTES) {
    SPIFFS.remove(NAV_LOG_PATH);
    File rotateFile = SPIFFS.open(NAV_LOG_PATH, FILE_WRITE);
    if (rotateFile) {
      rotateFile.println("LOG ROTATED");
      rotateFile.close();
    }
  }

  File file = SPIFFS.open(NAV_LOG_PATH, FILE_APPEND);
  if (file) {
    file.print(line);
    file.close();
  }
}

void dumpNavLog() {
  if (!spiffsReady) {
    Serial.println("LOG: SPIFFS no listo");
    return;
  }
  if (!SPIFFS.exists(NAV_LOG_PATH)) {
    Serial.println("LOG: vacio");
    return;
  }
  File file = SPIFFS.open(NAV_LOG_PATH, FILE_READ);
  if (!file) {
    Serial.println("LOG: error al abrir");
    return;
  }
  Serial.println("--- NAV LOG START ---");
  while (file.available()) {
    Serial.write(file.read());
  }
  Serial.println("--- NAV LOG END ---");
  file.close();
}

void clearNavLog() {
  if (!spiffsReady) {
    Serial.println("LOG: SPIFFS no listo");
    return;
  }
  if (SPIFFS.exists(NAV_LOG_PATH)) {
    SPIFFS.remove(NAV_LOG_PATH);
  }
  Serial.println("LOG: borrado");
}

void handleSerialCommands() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd.equalsIgnoreCase("LOG")) {
    dumpNavLog();
  } else if (cmd.equalsIgnoreCase("LOGCLEAR")) {
    clearNavLog();
  } else if (cmd.equalsIgnoreCase("LOGSIZE")) {
    if (!spiffsReady) {
      Serial.println("LOG: SPIFFS no listo");
      return;
    }
    if (SPIFFS.exists(NAV_LOG_PATH)) {
      File file = SPIFFS.open(NAV_LOG_PATH, FILE_READ);
      if (file) {
        Serial.printf("LOG: %u bytes\n", (unsigned int)file.size());
        file.close();
      }
    } else {
      Serial.println("LOG: 0 bytes");
    }
  }
}

// ==================== CALLBACKS ====================
// Clase que hereda de ambos callbacks (como ChronosESP32)
class MyCallbacks : public NimBLEServerCallbacks, public NimBLECharacteristicCallbacks {
public:
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
    deviceConnected = true;
    Serial.println(">>> CONECTADO!");
    Serial.printf("    Peer: %s\n", connInfo.getAddress().toString().c_str());
    beep(100);
    if (currentState == STATE_PAIRING) {
      pairingMode = false;
      currentState = STATE_CLOCK;
    }
  }

  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    deviceConnected = false;
    Serial.printf(">>> DESCONECTADO (reason: %d)\n", reason);
    if (pairingMode) {
      NimBLEDevice::startAdvertising();
    }
  }

  void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
    NimBLEAttValue value = pChar->getValue();
    if (value.length() > 0) {
      receivedData = String((char*)value.data(), value.length());
      newData = true;
    }
  }
};

MyCallbacks* callbacks = nullptr;

// ==================== BUZZER ====================
void beep(int ms) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(ms);
  digitalWrite(BUZZER_PIN, LOW);
}

// Melodía de inicio tipo Mario Bros
void playStartupMelody() {
  // Notas: E5, E5, E5, C5, E5, G5, G4
  int melody[] = {659, 659, 659, 523, 659, 784, 392};
  int durations[] = {150, 150, 150, 150, 150, 300, 300};
  int pauses[] = {130, 130, 200, 100, 180, 200, 0};
  
  for (int i = 0; i < 7; i++) {
    tone(BUZZER_PIN, melody[i], durations[i]);
    delay(durations[i] + pauses[i]);
  }
  noTone(BUZZER_PIN);
}

// ==================== PROCESAMIENTO ====================
void processMessage(String msg) {
  msg.trim();
  
  // Eliminar corchetes si existen
  if (msg.startsWith("[")) msg = msg.substring(1);
  if (msg.endsWith("]")) msg = msg.substring(0, msg.length() - 1);
  
  Serial.println("RX: " + msg);
  
  // Evitar procesar el mismo mensaje dos veces
  if (msg == lastProcessedMsg) return;
  lastProcessedMsg = msg;

  int p = msg.indexOf('|');
  String cmd = (p > 0) ? msg.substring(0, p) : msg;
  
  if (cmd == "NAV") {
    // Formato: NAV|icono|distancia|calle|eta
    int p1 = msg.indexOf('|');
    int p2 = msg.indexOf('|', p1 + 1);
    int p3 = msg.indexOf('|', p2 + 1);
    int p4 = msg.indexOf('|', p3 + 1);
    String icon = "";
    String distance = "";
    String street = "";
    String eta = "";
    bool parsed = false;
    bool distanceValid = false;
    if (p1 > 0 && p2 > p1) {
      icon = msg.substring(p1 + 1, p2);
      distance = (p3 > p2) ? msg.substring(p2 + 1, p3) : msg.substring(p2 + 1);
      street = (p3 > p2 && p4 > p3) ? msg.substring(p3 + 1, p4) : (p3 > p2) ? msg.substring(p3 + 1) : "";
      eta = (p4 > p3) ? msg.substring(p4 + 1) : "";
      parsed = true;
      
      // Si algún valor contiene % (variable Tasker no resuelta), mostrar "-"
      if (distance.indexOf('%') >= 0) distance = "";
      if (street.indexOf('%') >= 0) street = "";
      if (eta.indexOf('%') >= 0) eta = "";
      if (icon.indexOf('%') >= 0) icon = "";
      distanceValid = distance.length() > 0;
    }

    logNavNotification(msg, parsed, icon, distance, street, eta);

    if (parsed) {
      if (icon.length() > 0) {
        navIcon = icon;
      }
      if (distanceValid) {
        navDistance = distance;
      }
      navStreet = street;
      navETA = eta;
    }
    navActive = true;
    currentState = STATE_NAVIGATION;
    beep(50);
  }
  else if (cmd == "TIME") {
    int p1 = msg.indexOf('|');
    int p2 = msg.indexOf('|', p1 + 1);
    
    if (p1 > 0 && p2 > p1) {
      String dateStr = msg.substring(p1 + 1, p2);
      String timeStr = msg.substring(p2 + 1);
      
      dateStr.replace("-", "/");
      timeStr.replace(".", ":");
      
      int ds1 = dateStr.indexOf('/');
      int ds2 = dateStr.indexOf('/', ds1 + 1);
      
      if (ds1 > 0 && ds2 > ds1) {
        timeInfo.tm_mday = dateStr.substring(0, ds1).toInt();
        timeInfo.tm_mon = dateStr.substring(ds1 + 1, ds2).toInt() - 1;
        int year = dateStr.substring(ds2 + 1).toInt();
        if (year < 100) year += 2000;
        timeInfo.tm_year = year - 1900;
      }
      
      int ts1 = timeStr.indexOf(':');
      int ts2 = (ts1 > 0) ? timeStr.indexOf(':', ts1 + 1) : -1;
      
      if (ts1 > 0) {
        // Formato HH:MM o HH:MM:SS
        timeInfo.tm_hour = timeStr.substring(0, ts1).toInt();
        if (ts2 > ts1) {
          timeInfo.tm_min = timeStr.substring(ts1 + 1, ts2).toInt();
          timeInfo.tm_sec = timeStr.substring(ts2 + 1).toInt();
        } else {
          timeInfo.tm_min = timeStr.substring(ts1 + 1).toInt();
          timeInfo.tm_sec = 0;
        }
      } else if (timeStr.length() > 6) {
        // Formato timestamp Unix (segundos desde epoch)
        time_t epoch = (time_t)timeStr.toInt();
        struct tm* parsed = localtime(&epoch);
        if (parsed) {
          timeInfo = *parsed;
        }
      }

      mktime(&timeInfo);
      lastTimeUpdate = millis();  // Sincronizar el contador
      timeSet = true;
      Serial.printf("TIME: %02d:%02d:%02d %02d/%02d/%04d\n", 
        timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec,
        timeInfo.tm_mday, timeInfo.tm_mon + 1, timeInfo.tm_year + 1900);
    }
  }
  else if (cmd == "NOTIF") {
    // Formato: NOTIF|app|remitente|mensaje|hora
    // Ejemplo: NOTIF|WhatsApp|Mamá|Hola, ¿cómo estás?|10:30
    int p1 = msg.indexOf('|');
    int p2 = msg.indexOf('|', p1 + 1);
    int p3 = msg.indexOf('|', p2 + 1);
    int p4 = msg.indexOf('|', p3 + 1);
    
    if (p1 > 0 && p2 > p1 && p3 > p2) {
      notifApp = msg.substring(p1 + 1, p2);
      notifSender = msg.substring(p2 + 1, p3);
      notifMessage = (p4 > p3) ? msg.substring(p3 + 1, p4) : msg.substring(p3 + 1);
      notifTime = (p4 > p3) ? msg.substring(p4 + 1) : "";
      
      // Si no hay hora, usar la hora actual
      if (notifTime.length() == 0 && timeSet) {
        char timeBuf[6];
        sprintf(timeBuf, "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);
        notifTime = String(timeBuf);
      }
      
      // Guardar estado actual para volver después
      stateBeforeNotif = currentState;
      notifStartTime = millis();
      
      notifActive = true;
      currentState = STATE_NOTIFICATION;
      beep(100);
      delay(50);
      beep(100);  // Doble beep para notificación
      Serial.printf("NOTIF: %s - %s\n", notifApp.c_str(), notifSender.c_str());
    }
  }
  else if (cmd == "END") {
    navActive = false;
    notifActive = false;
    currentState = STATE_CLOCK;
    beep(100);
    Serial.println("NAV: Fin");
  }
  else if (cmd == "DISMISS") {
    notifActive = false;
    currentState = STATE_CLOCK;
  }
}

// ==================== DISPLAY ====================
void showClock() {
  display.clearBuffer();

  if (!timeSet) {
    display.setFont(u8g2_font_ncenB14_te);
    display.drawStr(10, 35, "Sin hora");
  } else {
    // El tiempo ya se actualiza en el loop(), aquí solo mostramos
    char buf[10];
    sprintf(buf, "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);
    display.setFont(u8g2_font_logisoso32_tn);
    int w = display.getStrWidth(buf);
    display.drawStr((128 - w) / 2, 40, buf);

    char date[25];
    sprintf(date, "%s %02d/%02d/%04d", diasSemana[timeInfo.tm_wday],
            timeInfo.tm_mday, timeInfo.tm_mon + 1, timeInfo.tm_year + 1900);
    display.setFont(u8g2_font_ncenB08_te);
    w = display.getStrWidth(date);
    display.drawStr((128 - w) / 2, 58, date);
  }

  // Indicador conexión
  if (deviceConnected) {
    display.drawDisc(120, 8, 4);
  } else {
    display.drawCircle(120, 8, 4);
  }

  display.sendBuffer();
}

void showNavigation() {
  display.clearBuffer();

  // Área de la flecha: ajustada para centrar mejor
  int arrowX = 2;   // Más a la izquierda
  int arrowY = 6;   // Más arriba
  
  // Dibujar flecha según MD5 usando bitmaps
  if (navIcon == "13e68aacc62531a385e2b3e9705e0701") {
    // Flecha "continuar recto" (con cuerpo discontinuo)
    display.drawXBMP(arrowX, arrowY, NAV_ARROW_W, NAV_ARROW_H, nav_arrow_ahead);
  }
  else if (navIcon == "3cc9cfaca8339431dfa25b4d26337d38") {
    // Flecha "recta continua"
    display.drawXBMP(arrowX, arrowY, NAV_ARROW_W, NAV_ARROW_H, nav_arrow_straight);
  }
  else if (navIcon == "1608d2493a2650b2aa05f0f11588d8be") {
    // Flecha "girar derecha"
    display.drawXBMP(arrowX, arrowY, NAV_ARROW_W, NAV_ARROW_H, nav_arrow_right);
  }
  else if (navIcon == "f467a04ac3ffa41cbce03096b28bd44b") {
    // Flecha "girar leve izquierda"
    display.drawXBMP(arrowX, arrowY, NAV_ARROW_W, NAV_ARROW_H, nav_arrow_left_light);
  }
  else if (navIcon == "0ad898f6410fe51971fe1b7159994f26") {
    // Flecha "girar izquierda"
    display.drawXBMP(arrowX, arrowY, NAV_ARROW_W, NAV_ARROW_H, nav_arrow_left);
  }
  else if (navIcon == "5710fb9ddabf6d18b95e424783ca8fae") {
    // Flecha "girar leve derecha"
    display.drawXBMP(arrowX, arrowY, NAV_ARROW_W, NAV_ARROW_H, nav_arrow_right_light);
  }
  else if (navIcon == "627c26a2d87e696a2b73d624145235a8") {
    // Flecha "rotonda con salida izquierda"
    display.drawXBMP(arrowX, arrowY, NAV_ARROW_W, NAV_ARROW_H, nav_arrow_round_left);
  }
  else if (navIcon == "356e3bf9bdb7f69a77e845464f489aba") {
    // Flecha "rotonda con salida arriba-izquierda"
    display.drawXBMP(arrowX, arrowY, NAV_ARROW_W, NAV_ARROW_H, nav_arrow_round_up_left);
  }
  else if (navIcon == "a294573bb87f9b91ac109ca1feb60253") {
    // Flecha "rotonda con salida arriba-derecha"
    display.drawXBMP(arrowX, arrowY, NAV_ARROW_W, NAV_ARROW_H, nav_arrow_round_up_right);
  }
  else if (navIcon == "8e35ee07dd4429bedab970ddae62577c") {
    // Flecha "rotonda con salida abajo-derecha"
    display.drawXBMP(arrowX, arrowY, NAV_ARROW_W, NAV_ARROW_H, nav_arrow_round_down_right);
  }
  else if (navIcon == "04ccb66fe89793824169db323392aeae") {
    // Flecha "recto con dos carriles"
    display.drawXBMP(arrowX, arrowY, NAV_ARROW_W, NAV_ARROW_H, nav_arrow_ahead_2);
  }
  else if (navIcon == "9dd54fb607c8a7b11c4cf98adf8d5d4d") {
    // Flecha "giro a leve derecha 2"
    display.drawXBMP(arrowX, arrowY, NAV_ARROW_W, NAV_ARROW_H, nav_arrow_right_light_2);
  }
  else if (navIcon == "4373638104f4cc57e201b63157aedacc") {
    // Flecha "giro a leve izquierda 2"
    display.drawXBMP(arrowX, arrowY, NAV_ARROW_W, NAV_ARROW_H, nav_arrow_left_light_2);
  }
  else if (navIcon == "c61a34040606ee47fda0f67864f6dcf0") {
    // Flecha "Rotonda cambio de sentido"
    display.drawXBMP(arrowX, arrowY, NAV_ARROW_W, NAV_ARROW_H, nav_arrow_round);
  }
  else if (navIcon == "19ff9ca1c8a743205da0e893c65bcbbe") {
    // Flecha "Giro brusco derecha"
    display.drawXBMP(arrowX, arrowY, NAV_ARROW_W, NAV_ARROW_H, nav_arrow_right_hard);
  }
  else {
    // MD5 no reconocido - mostrar "-" para identificarlo
    display.setFont(u8g2_font_ncenB24_tr);
    display.drawStr(arrowX + 8, arrowY + 36, "-");
  }

  // Nombre de calle a la derecha
  display.setFont(u8g2_font_ncenB10_te);
  int maxWidth = 90;
  int xText = 36;
  int yText = 16;
  int lineHeight = 14;
  
  String street = utf8ToLatin1(navStreet);  // Convertir acentos
  int currentLine = 0;
  int maxLines = 3;
  
  while (street.length() > 0 && currentLine < maxLines) {
    String line = "";
    int lastSpace = -1;
    
    for (int i = 0; i < street.length(); i++) {
      String testLine = line + street.charAt(i);
      if (display.getStrWidth(testLine.c_str()) > maxWidth) {
        if (lastSpace > 0) {
          line = street.substring(0, lastSpace);
          street = street.substring(lastSpace + 1);
        } else {
          line = street.substring(0, i);
          street = street.substring(i);
        }
        break;
      }
      if (street.charAt(i) == ' ') lastSpace = i;
      line = testLine;
      if (i == street.length() - 1) {
        street = "";
      }
    }
    
    display.drawStr(xText, yText + (currentLine * lineHeight), line.c_str());
    currentLine++;
  }

  // Distancia abajo a la izquierda
  display.setFont(u8g2_font_ncenB10_te);
  String dist = utf8ToLatin1(navDistance);
  display.drawStr(2, 50, dist.c_str());

  // ETA centrado abajo (tamaño similar a la fecha del reloj)
  if (navETA.length() > 0) {
    display.setFont(u8g2_font_ncenB08_te);
    String eta = navETA;
    // Eliminar "Llegada" del texto
    eta.replace("Llegada: ", "");
    eta.replace("Llegada:", "");
    eta.replace("Llegada ", "");
    eta.replace("Llegada", "");
    eta.trim();
    eta = utf8ToLatin1(eta);
    int w = display.getStrWidth(eta.c_str());
    display.drawStr((128 - w) / 2, 63, eta.c_str());
  }

  display.sendBuffer();
}

void showNotification() {
  display.clearBuffer();
  // Línea separadora superior
  display.drawHLine(0, 3, 128);
  
  // Nombre del remitente (grande, arriba)
  display.setFont(u8g2_font_ncenB12_te);
  String sender = utf8ToLatin1(notifSender);
  
  // Truncar si es muy largo
  int maxSenderWidth = 126;
  while (display.getStrWidth(sender.c_str()) > maxSenderWidth && sender.length() > 0) {
    sender = sender.substring(0, sender.length() - 1);
  }
  display.drawStr(1, 24, sender.c_str());
  
  // Mensaje (más pequeño, multilínea)
  display.setFont(u8g2_font_ncenB08_te);
  String message = utf8ToLatin1(notifMessage);
  int maxWidth = 126;
  int xText = 1;
  int yText = 36;
  int lineHeight = 10;
  int currentLine = 0;
  int maxLines = 2;  // 2 líneas para el mensaje
  
  while (message.length() > 0 && currentLine < maxLines) {
    String line = "";
    int lastSpace = -1;
    
    for (int i = 0; i < (int)message.length(); i++) {
      String testLine = line + message.charAt(i);
      if (display.getStrWidth(testLine.c_str()) > maxWidth) {
        if (lastSpace > 0) {
          line = message.substring(0, lastSpace);
          message = message.substring(lastSpace + 1);
        } else {
          line = message.substring(0, i);
          message = message.substring(i);
        }
        break;
      }
      if (message.charAt(i) == ' ') lastSpace = i;
      line = testLine;
      if (i == (int)message.length() - 1) {
        message = "";
      }
    }
    
    display.drawStr(xText, yText + (currentLine * lineHeight), line.c_str());
    currentLine++;
  }
  
  // Línea separadora inferior
  display.drawHLine(0, 53, 128);
  
  // Nombre de la app (abajo izquierda) y hora (abajo derecha)
  display.setFont(u8g2_font_ncenB08_te);
  String app = utf8ToLatin1(notifApp);
  display.drawStr(1, 63, app.c_str());
  
  // Hora a la derecha
  if (notifTime.length() > 0) {
    int timeWidth = display.getStrWidth(notifTime.c_str());
    display.drawStr(127 - timeWidth, 63, notifTime.c_str());
  }
  
  // Indicador de conexión
  if (deviceConnected) {
    display.drawDisc(120, 5, 3);
  } else {
    display.drawCircle(120, 5, 3);
  }
  
  display.sendBuffer();
}

void showPairing() {
  display.clearBuffer();

  display.setFont(u8g2_font_ncenB10_te);
  display.drawStr(25, 20, "SIDEBIKE");
  display.setFont(u8g2_font_ncenB08_te);
  display.drawStr(15, 38, "Buscando...");

  unsigned long rem = (PAIRING_WINDOW_MS - (millis() - startTime)) / 1000;
  char buf[20];
  sprintf(buf, "%lus restantes", rem);
  display.drawStr(25, 55, buf);

  display.sendBuffer();
}

// ==================== SETUP BLE ====================
void setupBLE() {
  Serial.println("Iniciando BLE (estilo Chronos)...");

  NimBLEDevice::init(DEVICE_NAME);
  NimBLEDevice::setMTU(517);  // MTU grande como Chronos

  pServer = NimBLEDevice::createServer();
  callbacks = new MyCallbacks();
  pServer->setCallbacks(callbacks);

  NimBLEService* pService = pServer->createService(SERVICE_UUID);

  // Característica TX (para enviar notificaciones al teléfono)
  pCharTX = pService->createCharacteristic(
    CHAR_UUID_TX,
    NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ
  );

  // Característica RX (para recibir datos del teléfono)
  pCharRX = pService->createCharacteristic(
    CHAR_UUID_RX,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
  );
  pCharRX->setCallbacks(callbacks);

  pService->start();

  NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
  pAdv->addServiceUUID(SERVICE_UUID);
  pAdv->setName(DEVICE_NAME);
  pAdv->start();

  Serial.println("BLE iniciado con 2 caracteristicas (TX/RX)");
  Serial.printf("Service: %s\n", SERVICE_UUID);
  Serial.printf("RX (write): %s\n", CHAR_UUID_RX);
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);
  delay(1000);
  Serial.println("\n=== SIDEBIKE " VERSION " ===");

  pinMode(TOUCH_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Wire.begin(SDA_PIN, SCL_PIN);
  display.begin();
  
  // Configurar zona horaria España (CET/CEST con cambio automático)
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();

  spiffsReady = SPIFFS.begin(true);
  if (spiffsReady) {
    Serial.println("SPIFFS OK");
  } else {
    Serial.println("SPIFFS ERROR");
  }

  // ==================== ANIMACIÓN DE INICIO ====================
  // Dimensiones del icono del casco: 60x60px
  const int HELMET_W = 60;
  const int HELMET_H = 60;
  
  // Posición inicial: centrado en pantalla (128x64)
  int helmetX = (128 - HELMET_W) / 2;  // 34
  int helmetY = (64 - HELMET_H) / 2;   // 2
  
  // 1. Mostrar casco centrado
  display.clearBuffer();
  display.drawXBMP(helmetX, helmetY, HELMET_W, HELMET_H, epd_bitmap_icono_casco);
  display.sendBuffer();
  delay(800);  // Pausa para ver el casco centrado
  
  // 2. Animar casco hacia la izquierda
  int targetX = 2;  // Posición final del casco a la izquierda
  for (int x = helmetX; x >= targetX; x -= 4) {
    display.clearBuffer();
    display.drawXBMP(x, helmetY, HELMET_W, HELMET_H, epd_bitmap_icono_casco);
    display.sendBuffer();
    delay(20);  // Velocidad de la animación
  }
  
  // 3. Mostrar texto SIDEBIKE y versión a la derecha
  display.clearBuffer();
  display.drawXBMP(targetX, helmetY, HELMET_W, HELMET_H, epd_bitmap_icono_casco);
  
  // Texto "SIDEBIKE" a la derecha del casco
  display.setFont(u8g2_font_ncenB08_tr);
  int textX = targetX + HELMET_W + 4;  // 66
  display.drawStr(textX, 30, "SIDEBIKE");
  
  // Versión debajo de SIDEBIKE
  display.setFont(u8g2_font_ncenB08_tr);
  display.drawStr(textX + 10, 48, VERSION);
  
  display.sendBuffer();
  
  playStartupMelody();  // Melodía tipo Mario Bros
  delay(500);

  setupBLE();

  startTime = millis();
  currentState = STATE_PAIRING;
  Serial.println("Setup OK");
}

// ==================== LOOP ====================
void loop() {
  handleSerialCommands();
  // Actualizar tiempo en segundo plano (siempre, independiente de la pantalla)
  if (timeSet && (millis() - lastTimeUpdate >= 1000)) {
    lastTimeUpdate = millis();
    timeInfo.tm_sec++;
    mktime(&timeInfo);  // Normaliza y ajusta día/hora si es necesario
  }


  // Timeout de pairing (solo afecta al mensaje en pantalla)
  if (pairingMode && (millis() - startTime > PAIRING_WINDOW_MS)) {
    pairingMode = false;
    currentState = STATE_CLOCK;
    // NO detenemos advertising - siempre permitimos reconexión
    Serial.println("Ventana de pairing cerrada, pero advertising continua");
  }
  
  // Timeout de notificación - volver al estado anterior después de 5 segundos
  if (currentState == STATE_NOTIFICATION && (millis() - notifStartTime > 5000)) {
    notifActive = false;
    // Volver al estado anterior (NAV si estaba activo, sino reloj)
    if (stateBeforeNotif == STATE_NAVIGATION && navActive) {
      currentState = STATE_NAVIGATION;
    } else {
      currentState = STATE_CLOCK;
    }
    Serial.println(">>> Timeout notificación, volviendo a estado anterior");
  }
  
  // Si no estamos conectados, asegurarnos de que advertising está activo
  if (!deviceConnected && !NimBLEDevice::getAdvertising()->isAdvertising()) {
    NimBLEDevice::startAdvertising();
    Serial.println("Reiniciando advertising...");
  }

  // Procesar datos recibidos (del callback)
  if (newData) {
    newData = false;
    processMessage(receivedData);
    receivedData = "";
  }

  // Polling de la característica RX (backup - solo si no hubo callback)
  if (deviceConnected && pCharRX != nullptr) {
    static String lastRxValue = "";
    NimBLEAttValue val = pCharRX->getValue();
    if (val.length() > 0) {
      String currentVal = String((char*)val.data(), val.length());
      if (currentVal != lastRxValue && currentVal.length() > 0) {
        processMessage(currentVal);  // El check de duplicados está en processMessage
        lastRxValue = currentVal;
      }
    }
  }

  // Touch con detección de toque prolongado
  static bool lastTouch = false;
  static unsigned long touchStartTime = 0;
  static bool longPressHandled = false;
  const unsigned long LONG_PRESS_MS = 1500;  // 1.5 segundos para toque prolongado
  
  bool touch = digitalRead(TOUCH_PIN);
  
  if (touch && !lastTouch) {
    // Inicio del toque
    touchStartTime = millis();
    longPressHandled = false;
  }
  else if (touch && lastTouch) {
    // Toque mantenido - verificar si es prolongado
    if (!longPressHandled && (millis() - touchStartTime >= LONG_PRESS_MS)) {
      longPressHandled = true;
      
      // Toque prolongado: activar emparejamiento si no está conectado
      if (!deviceConnected) {
        Serial.println(">>> TOQUE PROLONGADO - Activando modo emparejamiento");
        pairingMode = true;
        currentState = STATE_PAIRING;
        startTime = millis();  // Reiniciar contador de emparejamiento
        NimBLEDevice::startAdvertising();
        beep(200);  // Beep largo para confirmar
      } else {
        Serial.println(">>> TOQUE PROLONGADO - Ya conectado, ignorando");
        beep(50);
      }
    }
  }
  else if (!touch && lastTouch) {
    // Fin del toque - si fue corto, procesar como tap normal
    if (!longPressHandled && (millis() - touchStartTime < LONG_PRESS_MS)) {
      beep(30);
        if (currentState == STATE_PAIRING) {
          pairingMode = false;
          if (!deviceConnected) NimBLEDevice::stopAdvertising();
          currentState = STATE_CLOCK;
        } else if (currentState == STATE_NOTIFICATION) {
          // Descartar notificación con toque
          notifActive = false;
          currentState = STATE_CLOCK;
        } else if (notifActive && currentState == STATE_CLOCK) {
          // Volver a ver la notificación
          currentState = STATE_NOTIFICATION;
        } else if (navActive && currentState == STATE_CLOCK) {
        currentState = STATE_NAVIGATION;
      } else if (currentState == STATE_NAVIGATION) {
        currentState = STATE_CLOCK;
      }
    }
  }
  lastTouch = touch;

  // Display
  switch (currentState) {
    case STATE_PAIRING: showPairing(); break;
    case STATE_NAVIGATION: showNavigation(); break;
    case STATE_NOTIFICATION: showNotification(); break;
    default: showClock(); break;
  }

  delay(50);
}
