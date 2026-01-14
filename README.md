# SIDEBIKE - ESP32-C3 Navigation Display v2.2

Dispositivo wearable que muestra hora/fecha, recibe indicaciones de navegación de Google Maps y notificaciones de apps (WhatsApp, Telegram, etc.) vía Bluetooth BLE desde Tasker.

## 📋 Requisitos

### Hardware
- ESP32-C3
- Pantalla OLED SH1106 128x64 (I2C)
- Módulo táctil TTP223
- Buzzer pasivo (para melodía de inicio)

### Software Android
- **Tasker** (~3.49€)
- **BLE Tasker Plugin** (gratuito)

## 🔌 Conexiones

| Componente | Pin ESP32-C3 |
|------------|--------------|
| OLED SDA   | GPIO 20*     |
| OLED SCL   | GPIO 21*     |
| TTP223     | GPIO 1       |
| Buzzer     | GPIO 2       |

> ⚠️ *Los pines I2C pueden variar según tu modelo de ESP32-C3. Edita `SDA_PIN` y `SCL_PIN` en `sidebike.ino`

## 📚 Librerías Arduino

Instalar desde el Library Manager de Arduino IDE:

1. **NimBLE-Arduino** - BLE ligero para ESP32
2. **U8g2** - Display OLED

## 🛠️ Instalación

1. Abre `sidebike.ino` en Arduino IDE
2. Selecciona placa: `ESP32C3 Dev Module`
3. Ajusta los pines I2C si es necesario
4. Compila y sube al ESP32-C3

## 📱 Configuración Tasker

### UUIDs BLE (copiar exactamente)
```
Service UUID:        4fafc201-1fb5-459e-8fcc-c5c9c331914b
Enviar (NOTIFY UUID): beb5483e-36e1-4688-b7f5-ea07361b26a8 
Recibir (WRITE UUID): beb5483e-36e1-4688-b7f5-ea07361b26a9
```

### Perfil 1: Sincronizar Hora
```
Trigger: Tasker → Event → Device Boot
Task:
  1. Wait 30 seconds
  2. Variable Set: %time_data = TIME|%DATE|%TIME
  3. BLE Tasker Plugin → Write
     - Device: SIDEBIKE
     - Service UUID: (copiar de arriba)
     - TX/RX UUID: (copiar de arriba)
     - Value: %time_data
```

### Perfil 2: Navegación Google Maps
```
Trigger: Event → Notification → App: Google Maps
Task:
  1. Variable Set: %nav_data = NAV|%NTITLE|%NTEXT|%ansubtext
  2. BLE Tasker Plugin → Write
     - Device: SIDEBIKE
     - Service UUID: (copiar de arriba)
     - TX/RX UUID: (copiar de arriba)
     - Value: %nav_data
```

> 💡 **Nota**: El campo `%NTITLE` contiene el hash MD5 del icono de navegación, que permite mostrar flechas específicas.

### Perfil 3: Fin Navegación
```
Trigger: Event → Notification Removed → App: Google Maps
Task:
  1. BLE Tasker Plugin → Write
     - Device: SIDEBIKE
     - Value: END
```

### Perfil 4: Notificaciones de Apps (WhatsApp, Telegram, etc.)
```
Trigger: Event → Notification → App: WhatsApp (o Telegram, Instagram, etc.)
Task:
  1. Variable Set: %notif_data = NOTIF|%anapp|%antitle|%antext|%TIME
  2. BLE Tasker Plugin → Write
     - Device: SIDEBIKE
     - Service UUID: (copiar de arriba)
     - TX/RX UUID: (copiar de arriba)
     - Value: %notif_data
```

> 💡 **Tip**: Puedes crear un solo perfil para múltiples apps seleccionándolas todas en el trigger.

### Perfil 5: Descartar Notificación
```
Trigger: Event → Notification Removed → App: WhatsApp (u otras)
Task:
  1. BLE Tasker Plugin → Write
     - Device: SIDEBIKE
     - Value: DISMISS
```

## 📡 Protocolo de Mensajes

| Comando | Formato | Descripción |
|---------|---------|-------------|
| NAV | `NAV\|md5_icono\|distancia\|calle\|eta` | Indicación de navegación |
| TIME | `TIME\|DD-MM-YY\|HH.MM.SS` o `TIME\|DD-MM-YY\|timestamp` | Sincronizar hora |
| NOTIF | `NOTIF\|app\|remitente\|mensaje\|hora` | Notificación de app |
| END | `END` | Fin de navegación |
| DISMISS | `DISMISS` | Descartar notificación actual |

> 💡 **Nota**: El campo TIME acepta tanto formato `HH:MM:SS` como timestamp Unix (`%TIMES` de Tasker). La zona horaria está configurada para España (CET/CEST).

### Iconos de navegación por MD5

El dispositivo identifica las flechas de navegación mediante el hash MD5 del icono enviado por Google Maps:

| MD5 Hash | Flecha |
|----------|--------|
| `13e68aacc62531a385e2b3e9705e0701` | Continuar recto (discontinuo) |
| `3cc9cfaca8339431dfa25b4d26337d38` | Recto continuo |
| `1608d2493a2650b2aa05f0f11588d8be` | Girar derecha |
| `0ad898f6410fe51971fe1b7159994f26` | Girar izquierda |
| `f467a04ac3ffa41cbce03096b28bd44b` | Girar leve izquierda |
| `5710fb9ddabf6d18b95e424783ca8fae` | Girar leve derecha |
| `9dd54fb607c8a7b11c4cf98adf8d5d4d` | Girar leve derecha (alt) |
| `4373638104f4cc57e201b63157aedacc` | Girar leve izquierda (alt) |
| `19ff9ca1c8a743205da0e893c65bcbbe` | Giro brusco derecha |
| `627c26a2d87e696a2b73d624145235a8` | Rotonda salida izquierda |
| `356e3bf9bdb7f69a77e845464f489aba` | Rotonda arriba-izquierda |
| `a294573bb87f9b91ac109ca1feb60253` | Rotonda arriba-derecha |
| `c61a34040606ee47fda0f67864f6dcf0` | Rotonda cambio sentido |
| `04ccb66fe89793824169db323392aeae` | Recto dos carriles |

Para MD5 no reconocidos, se muestra "-" para identificarlo.

### Ejemplos de notificaciones
```
NOTIF|WhatsApp|Mamá|Hola, ¿cómo estás?|10:30
NOTIF|Telegram|Juan|Llegando en 5 min :D|18:45
NOTIF|Instagram|maria_93|Te ha enviado un mensaje|
```

> Si no envías la hora, se usará la hora actual del dispositivo.

## 🚀 Uso

1. **Encender**: Reproduce melodía de inicio estilo Mario Bros
2. **Buscando**: Muestra "Buscando..." durante 2 minutos
3. **Emparejar**: Conectar desde BLE Tasker Plugin
4. **Modo reloj**: Muestra hora y fecha
5. **Navegación**: Al iniciar Google Maps, muestra flechas, distancia, calle y ETA
6. **Notificaciones**: Al recibir mensaje, muestra remitente, texto, app y hora (se oculta tras 5s)
7. **Touch corto**: Alterna entre reloj/navegación/notificación
8. **Touch largo** (1.5s): Reactiva modo emparejamiento



## 🔧 Solución de Problemas

### No aparece en Bluetooth
- Reinicia el dispositivo
- El emparejamiento solo está activo 2 min después de encender
- Usa toque prolongado (1.5s) para reactivar el modo emparejamiento

### Display no funciona
- Verifica los pines I2C en el código
- Comprueba la dirección I2C del display (normalmente 0x3C)

### No recibe notificaciones
- Verifica permisos de Tasker para leer notificaciones
- Comprueba la conexión BLE en la app BLE Tasker Plugin

### Flechas de navegación no reconocidas
- El MD5 del icono no está en la lista soportada
- Se mostrará "continuar recto" por defecto
- Puedes añadir nuevos MD5 editando `images.h` y `showNavigation()` en `sidebike.ino`



## 📁 Estructura del proyecto

```
sidebike/
├── sidebike.ino      # Código principal
├── images.h          # Bitmaps XBM de flechas de navegación
└── README.md         # Este archivo
```

## 📝 Changelog

### v2.2
- Reloj funciona en segundo plano (hora correcta al volver de navegación)
- Soporte para timestamp Unix en TIME (`%TIMES` de Tasker)
- Zona horaria España (CET/CEST) configurada automáticamente
- Logs optimizados y sin duplicados
- Soporte para 14 tipos de flechas de navegación

### v2.1
- Sistema de navegación basado en MD5 para identificar flechas
- Campo ETA (tiempo estimado de llegada)
- Notificaciones con timeout automático (5s)
- Toque prolongado para reactivar emparejamiento
- Melodía de inicio estilo Mario Bros
- Soporte para caracteres acentuados (UTF-8 a Latin-1)
