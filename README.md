# SIDEBIKE - ESP32-C3 Navigation Display

Dispositivo que muestra hora/fecha, recibe indicaciones de navegación de Google Maps y notificaciones de apps (WhatsApp, Telegram, etc.) vía Bluetooth BLE desde Tasker.

## 📋 Requisitos

### Hardware
- ESP32-C3
- Pantalla OLED SH1106 128x64 (I2C)
- Módulo táctil TTP223
- Buzzer
- Módulo de carga IP5306

### Software Android
- **Tasker** (~3.49€)
- **BLE Tasker Plugin** (gratuito)

## 🔌 Conexiones

| Componente | Pin ESP32-C3 |
|------------|--------------|
| OLED SDA   | GPIO 20*      |
| OLED SCL   | GPIO 21*      |
| TTP223     | GPIO 1       |
| Buzzer     | GPIO 2       |

> ⚠️ *Los pines I2C pueden variar según tu modelo de ESP32-C3. Edita `SDA_PIN` y `SCL_PIN` en `sidebike.ino`

## 📚 Librerías Arduino

Instalar desde el Library Manager de Arduino IDE:

1. **NimBLE-Arduino** - BLE ligero para ESP32
2. **U8g2** - Display OLED

## 🛠️ Instalación

1. Abre `sidekar.ino` en Arduino IDE
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
  1. Variable Set: %nav_data = NAV|%NTITLE|%NTEXT|
  2. BLE Tasker Plugin → Write
     - Device: SIDEBIKE
     - Service UUID: (copiar de arriba)
     - TX/RX UUID: (copiar de arriba)
     - Value: %nav_data
```

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
| NAV | `NAV\|icono\|distancia\|calle` | Indicación de navegación |
| TIME | `TIME\|DD/MM/YYYY\|HH:MM:SS` | Sincronizar hora |
| **NOTIF** | `NOTIF\|app\|remitente\|mensaje\|hora` | Notificación de app |
| END | `END` | Fin de navegación |
| DISMISS | `DISMISS` | Descartar notificación actual |

### Iconos de navegación
- `left`, `izq` → Girar izquierda
- `right`, `der` → Girar derecha  
- `uturn`, `vuelta` → Dar la vuelta
- Cualquier otro → Seguir recto

### Ejemplos de notificaciones
```
NOTIF|WhatsApp|Mamá|Hola, ¿cómo estás?|10:30
NOTIF|Telegram|Juan|Llegando en 5 min :D|18:45
NOTIF|Instagram|maria_93|Te ha enviado un mensaje|
```

> Si no envías la hora, se usará la hora actual del dispositivo.

## 🚀 Uso

1. **Encender**: Muestra "Buscando..." durante 2 minutos
2. **Emparejar**: Conectar desde BLE Tasker Plugin
3. **Modo reloj**: Muestra hora y fecha
4. **Navegación**: Al iniciar Google Maps, muestra indicaciones
5. **Notificaciones**: Al recibir mensaje, muestra remitente, texto y app
6. **Touch corto**: Alterna entre reloj/navegación/notificación
7. **Touch largo** (1.5s): Reactiva modo emparejamiento

## 🔧 Solución de Problemas

### No aparece en Bluetooth
- Reinicia el dispositivo
- El emparejamiento solo está activo 2 min después de encender

### Display no funciona
- Verifica los pines I2C en el código
- Comprueba la dirección I2C del display (normalmente 0x3C)

### No recibe notificaciones
- Verifica permisos de Tasker para leer notificaciones
- Comprueba la conexión BLE en la app BLE Tasker Plugin
