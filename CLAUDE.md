# Reglas de Desarrollo del Proyecto NRM-BFM

Mantener este archivo en formato simple, para que pueda leerse y editarse rapidamente

## Proyecto hermano (gateway Go)

Este firmware se comunica por USB CDC-ACM con una app de escritorio Go que expone
Alpaca/UI. Su código vive en **otro repo local**:

- **`~/Dev/nrm_ha_go`** — gateway Go (módulo `github.com/nmarsollier/nrm_ha_go`).
  Documentación en su `README.md` y `CLAUDE.md`.

Cuando se necesite buscar referencias de código del gateway, leer su protocolo de
consumo o consultar cómo se implementa el lado Alpaca/UI, mirar ahí.

## Definicion del proyecto

- Logica para manejar montura ecuatorial con harmonic drives para astrofotografia
- Montura NRM-BFM con harmonic drives de reduccion 100:1 y poleas GT2 9mm 5:1 (20T→100T)
- Reduccion total: 500:1 en ambos ejes
- Utiliza una placa ESP32-S3 44 pines
- Utiliza 2 motores Nema 17 (lazo abierto) con driver TB6600, configurados a 32 microsteps
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

- Motores Nema 17 (lazo abierto) con driver DiGiYes TB6600 (especificaciones en MOTOR.md)
- https://www.amazon.com/dp/B0C36DP9XD
- Configurados a 32 microsteps via DIP switches (6400 pasos/rev)
- Torque: no confirmado para la variante de 2.0 A (ver MOTOR.md)
- Micropasos por DIP switches SW1-SW3: 32 microsteps = SW1 OFF, SW2 OFF, SW3 OFF

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
- **Logica I/O**: 3.3V (no tolera 5V, LED y buzzer directos a GPIO 1 y 2)
- **GPIO digitales**: 45 (configurables)
- **ADC**: 2 conversores SAR ADC de 12 bits, hasta 20 canales
- **UART**: 3 controladores UART
- **I2C**: 2 controladores I2C
- **SPI**: 4 controladores SPI
- **PWM**: 8 canales LEDC independientes
- **USB**: USB OTG 1.1 (nativo, sin chip externo)

### Pinout definitivo NRM-BFM

| GPIO | Funcion           | Notas                                              |
|------|-------------------|----------------------------------------------------|
| 8    | PWR-SENSE         | Deteccion de energia externa 12V (ADC1_CH7, divisor 10k/10k sobre 5.5V) |
| 14   | STEP- RA          | Pulso STEP eje ascension recta (via UMC2003)         |
| 13   | DIR- RA           | Direccion eje ascension recta (via UMC2003)           |
| 12   | STEP- DEC         | Pulso STEP eje declinacion (via UMC2003)              |
| 11   | DIR- DEC          | Direccion eje declinacion (via UMC2003)               |
| 1    | LED externo       | LEDC PWM, indicador de estado (directo a GPIO)                      |
| 2    | BUZZER            | Buzzer pasivo 2 kHz (directo a GPIO)                  |

**Level shifting (UMC2003, array Darlington):**
Los drivers TB6600 usan optoacopladores en STEP/DIR. Las salidas
STEP y DIR de ambos ejes pasan por un UMC2003, un array
Darlington de 7 canales con salidas de colector abierto (sinking).
El LED y el buzzer van directos al GPIO (sin level shifting).

Cada GPIO va directo a una entrada del UMC2003 (resistencia de base interna,
no requiere resistor externo). La salida hunde corriente hacia GND:

```
GPIO (3.3V) → INx UMC2003        OUTx → carga → +V

  GPIO HIGH → OUTx a GND (carga activa)
  GPIO LOW  → OUTx en alta impedancia
```

Las salidas son "negativas" (active-low): la carga (opto del driver) se
conecta entre +V y la salida. El pin COM (anodo comun
de los diodos de proteccion) se ata al +V. No hay cargas inductivas en esta
revision (los drivers se controlan por optoacopladores, que son LED), asi
que los diodos no son estrictamente necesarios.

EN+ / EN- van **sin conectar**. La polaridad activa y el comportamiento en
flotacion del TB6600 no estan documentados en MOTOR.md: verificar en banco.

**Alimentacion:**
| Pin  | Funcion           |
|------|-------------------|
| 12V  | Alimentacion de potencia (VCC) y entradas de control (PUL+/DIR+). El TB6600 acepta 3.3-24 V en control (9-42 V de potencia) |
| GND  | Tierra comun (placa, UMC2003 y drivers comparten la misma tierra) |

**Uso futuro:**
| GPIO | Funcion     | Notas                              |
|------|-------------|------------------------------------|
| 6    | I2C SCL (2do bus) | Display / sensor adicional        |

### Pines con restricciones (NO USAR)

| GPIO | Restriccion                                 |
|------|---------------------------------------------|
| 0    | Boot: LOW en reset = modo flash (ROM boot)  |
| 3    | Strapping JTAG (LOW al boot)                |
| 43   | UART0 TX, consola debug (USB-Serial nativo) |
| 44   | UART0 RX, consola debug (USB-Serial nativo) |
| 45   | Strapping (VDD_SPI voltage)                 |
| 46   | Strapping: modo de arranque (con GPIO0). GPIO46=1 + GPIO0=0 es invalido |

## Arquitectura

Capas del sistema, de afuera hacia adentro:

```
App de escritorio Go (Alpaca/UI) → USB CDC-ACM → Proto → Mount (orquestacion) → Motors → STEP/DIR (hardware)
```

- **usb_cdc** (`main/usb_cdc/`) — Transporte USB CDC-ACM via TinyUSB: init, RX task, TX.
- **proto** (`main/proto/`) — Protocolo serial: framing, dispatch, handlers por familia, sesion.
- **Mount** (`main/mount/`) — Orquestacion logica del montaje: estado, coordenadas, sincronizacion.
- **Runtime** (`main/runtime/`) — Inicializacion y ciclo de vida del sistema.
- **Motors** (`main/motors/`) — Control de motores de alto nivel y ejecucion hardware: GPIO DIR, RMT para STEP.
- **LED** (`main/led/`) — Control PWM del LED externo en GPIO 1. Estados: tenue (normal), brillante (slewing), respiracion (error).
- **Buzzer** (`main/buzzer/`) — Buzzer pasivo de eventos en GPIO 2 (2 kHz via LEDC). Beeps de arranque, inicio y fin de goto/move axis.
- **Power** (`main/power/`) — Sensor de energia externa (ADC en GPIO 8). Bloquea el movimiento cuando la montura esta solo con USB (12V apagado).
- **Utils** (`main/utils/`) — Utilidades transversales (parser, validacion).

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
- Motors es autocontenido: controla GPIOs DIR y RMT para STEP
