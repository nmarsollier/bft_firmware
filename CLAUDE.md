# Reglas de Desarrollo del Proyecto BFT

Mantener este archivo en formato simple, para que pueda leerse y editarse rapidamente

El modelo matemático de coordenadas (absolutas, sin estado de orientación) está
documentado en `MOUNT.md`.

## Proyecto hermano (gateway Go)

Este firmware se comunica por USB CDC-ACM con una app de escritorio Go que expone
Alpaca/UI. Su código vive en **otro repo local**:

- **`~/Dev/mount_panel`** — gateway Go (módulo `github.com/nmarsollier/mount_panel`).
  Documentación en su `README.md` y `CLAUDE.md`.

Cuando se necesite buscar referencias de código del gateway, leer su protocolo de
consumo o consultar cómo se implementa el lado Alpaca/UI, mirar ahí.

## Definicion del proyecto

- Logica para manejar montura ecuatorial con harmonic drives para astrofotografia
- Montura BFT con harmonic drives de reduccion 100:1 y poleas GT2 9mm 5:1 (20T→100T)
- Reduccion total: 500:1 en ambos ejes
- Utiliza una placa ESP32-S3 44 pines
- Utiliza 2 motores Nema 17 (lazo abierto) con driver TMC2209 directo
- Los motores se alimentan directo de fuente 12V, la placa ESP32-S3 via LM2596 (12V→5.5V)
- Estructura metalica en hierro 1/8, dos cuerpos (RA y DEC)
- La montura posee 2 botones fisicos, Stop y Home
- Posee una pantalla minimalista OLED de 0.98 inch, se muestra verticalmente

### Harmonic Drives

- Harmonic Drive de reduccion 100:1 — F17 (https://www.ebay.com/itm/800393350640?var=657433441684)
- Reduccion de poleas 5:1 en la entrada del harmonico: GT2 9mm 20T → 100T
- Reduccion Total en ambos Ejes: 500:1
- El cuerpo DEC se enrosca a la estructura RA a traves de la salida del Harmonic
- Los cables que controlan el eje DEC se pasan por dentro del Harmonic

### Motores Nema 17

- Motores Nema 17 (lazo abierto) con driver TMC2209 directo (configuración en TMC.md)
- https://www.amazon.com/dp/B0C36DP9XD
- 64 microsteps configurados por software (CHOPCONF.MRES)
- Torque: no confirmado para la variante de 2.0 A
- Corriente de bobina: IRUN=17, IHOLD=14

## Placa ESP32-S3

### Identificacion

- **Modelo**: ESP32-S3 44 pines
- https://www.amazon.com/dp/B0F5QCK6X5
- **SoC**: ESP32-S3 (Xtensa 32-bit LX7, doble nucleo)
- **Chip USB-Serial**: Integrado en el SoC (USB Serial/JTAG nativo)
- **Flash**: 16 MB
- **PSRAM**: 8 MB (octal)

### Especificaciones tecnicas

- **CPU**: Tensilica Xtensa 32-bit LX7, doble nucleo, hasta 240 MHz
- **Alimentacion**: 5.5V via LM2596
- **Logica I/O**: 3.3V (no tolera 5V; LED WS2812 onboard en GPIO 48)
- **GPIO digitales**: 45 (configurables)
- **ADC**: 2 conversores SAR ADC de 12 bits, hasta 20 canales
- **UART**: 3 controladores UART
- **I2C**: 2 controladores I2C
- **SPI**: 4 controladores SPI
- **PWM**: 8 canales LEDC independientes
- **USB**: USB OTG 1.1 (nativo, sin chip externo)

### Pinout definitivo BFT

| GPIO | Funcion           | Notas                                              |
|------|-------------------|----------------------------------------------------|
| 10   | STEP- RA          | Pulso STEP eje ascension recta (directo al TMC2209)  |
| 9    | DIR- RA           | Direccion eje ascension recta (directo al TMC2209)   |
| 15   | STEP- DEC         | Pulso STEP eje declinacion (directo al TMC2209)      |
| 7    | DIR- DEC          | Direccion eje declinacion (directo al TMC2209)       |
| 12   | TMC RA TX         | UART1 TX → 1 kΩ → PDN_UART del TMC de RA             |
| 13   | TMC RA RX         | UART1 RX → PDN_UART del TMC de RA (directo)          |
| 17   | TMC DEC TX        | UART2 TX → 1 kΩ → PDN_UART del TMC de DEC            |
| 18   | TMC DEC RX        | UART2 RX → PDN_UART del TMC de DEC (directo)         |
| 48   | LED onboard       | WS2812 RGB, mostrado en verde (via RMT)              |
| 1    | BUZZER            | Buzzer activo (modulo) en GPIO 1 (HIGH = beep)       |

**Conexion a los TMC2209 (sin level shifting):**
STEP/DIR van directos a las entradas STEP/DIR del TMC2209 (lógica 3.3 V,
compatibles). Cada driver tiene su bus UART single-wire propio: el TX del ESP
pasa por una resistencia de 1 kΩ en serie hacia PDN_UART, y el RX va directo a
esa misma línea. No hay pin EN conectado — los drivers están siempre encendidos.
No hay UMC2003/ULN2003 (ya no se necesita level shifting ni conmutación de
buzzer).

**Alimentacion:**
| Pin  | Funcion           |
|------|-------------------|
| 12V  | Alimentacion de potencia (VM) de ambos TMC2209 |
| GND  | Tierra comun (placa y drivers comparten la misma tierra) |

**Uso futuro:**
| GPIO | Funcion     | Notas                              |
|------|-------------|------------------------------------|
| 6    | I2C SCL (2do bus) | Display / sensor adicional        |

### Pines con restricciones (usados / NO USAR)

| GPIO | Restriccion                                 |
|------|---------------------------------------------|
| 0    | Boot: LOW en reset = modo flash (ROM boot)  |
| 3    | Strapping JTAG (LOW al boot)                |
| 43   | UART0 TX, consola debug (USB-Serial nativo) |
| 44   | UART0 RX, consola debug (USB-Serial nativo) |
| 45   | Strapping (VDD_SPI voltage) |
| 46   | Strapping: modo de arranque (con GPIO0) |

## Arquitectura

Capas del sistema, de afuera hacia adentro:

```
App de escritorio Go (Alpaca/UI) → USB CDC-ACM → Proto → Mount (orquestacion) → Motors → TMC2209 (STEP/DIR + UART) → hardware
```

- **usb_cdc** (`main/usb_cdc/`) — Transporte USB CDC-ACM via TinyUSB: init, RX task, TX.
- **proto** (`main/proto/`) — Protocolo serial: framing, dispatch, handlers por familia, sesion.
- **Mount** (`main/mount/`) — Orquestacion logica del montaje: estado, coordenadas, sincronizacion.
- **Runtime** (`main/runtime/`) — Inicializacion y ciclo de vida del sistema.
- **Motors** (`main/motors/`) — Control de motores de alto nivel y ejecucion hardware: GPIO DIR, RMT para STEP.
- **TMC** (`main/tmc/`) — Configuracion y sondeo de los dos TMC2209 por UART single-wire: registros, microsteps, corriente y el sense "TMC funcionando".
- **LED** (`main/led/`) — LED WS2812 onboard (GPIO 48) via RMT, mostrado en verde. Estados: tenue (normal), brillante (slewing), respiracion (error).
- **Buzzer** (`main/buzzer/`) — Buzzer activo de eventos (modulo) en GPIO 1 (HIGH = beep). Beeps de arranque, inicio y fin de goto/move axis.

## Reglas generales

- Nunca hacer commit o push a github sin autorizacion explicita
- Commits solo cuando la feature completa este verificada y compilando
- Siempre basarse en codigo desde el disco como unica fuente de verdad. El codigo fuente es la unica referencia valida para entender detalles de implementacion
- Antes de cualquier cambio leer el estado actual del archivo a modificar
- Proyecto programado en C, siguiendo estilo funcional cuando sea posible
- Modulos separados por dominio, cada dominio en una carpeta dentro de `main/`
- Cada dominio expone:
    - `dominio.h` — API publica (funciones que otros modulos pueden llamar)
    - `dominio_internal.h` — API interna (funciones compartidas solo entre archivos del mismo modulo)
- Un archivo `.c` por caso de uso dentro de cada modulo
- Las funciones publicas comienzan con el nombre del modulo, seguido de `_`
- Las variables de estado de un modulo nunca se acceden directamente desde afuera. Solo a traves de funciones publicas del modulo
- Respetar principios: DRY - YAGNI - KISS
- La documentacion en headers describe el problema de negocio o caso de uso que resuelve la funcion, no los detalles de implementacion
- Solo comentar codigo si es complejo de comprender para un humano
- El codigo y sus comentarios dentro de archivos con extension .c y .h se escriben en ingles
- El README.md se escribe en inglés; los demás archivos markdown (.md) en español
- Los tags de comentarios deben definirse como constante y su valor es el mismo nombre del archivo en mayuscular y sin la extension .c

## Convenciones de Codigo

- **Lenguaje**: C (no C++)
- **snake_case** para funciones y variables
- **UPPER_CASE** para macros y defines
- **Headers**: `#pragma once`, includes organizados: primeros los propios del modulo, luego librerias del framework
- **Logging**: usar `ESP_LOGI()`, `ESP_LOGW()`, `ESP_LOGE()` con tag estatico por archivo
- **Resultados entre capas**: usar los tipos `MotorResultCode` y `MountResult` definidos en el proyecto

## Relaciones entre modulos (dependencias)

- usb_cdc depende de TinyUSB (componente gestionado `espressif/esp_tinyusb`)
- proto depende de usb_cdc (transporte) y de mount/motors (estado y comandos)
- Motors depende de TMC (sondeo "TMC funcionando" para el gating de movimiento)
- Motors es autocontenido para el control de GPIOs DIR y RMT para STEP
