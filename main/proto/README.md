# Protocolo de montura BFT (USB CDC-ACM)

Documento de diseño del protocolo serial entre la PC y el ESP32-S3. Acá está toda
la idea, de principio a fin: la motivación, el formato del cable, las 5 familias
de mensajes, y cómo se va a usar.

---

## 1. Motivación

La montura publica un estado y ejecuta un puñado de órdenes. Para eso no hace
falta una pila de red (TCP + HTTP + servidores): alcanza con un protocolo serial
simple y determinista por **USB CDC-ACM**.

**La app de escritorio (Go)** es la integración: habla con el ESP por serial y
traduce eso a Alpaca/REST/UI para N.I.N.A. y PHD2.

---

## 2. La idea en una frase

> El ESP es la **autoridad sobre la montura**: publica su estado y ejecuta pocas
> operaciones. La app es la **integración**: traduce eso a Alpaca y a la UI.

Se descartó explícitamente trasladar cada endpoint Alpaca a un comando USB
equivalente (conservaría toda la complejidad). En su lugar, un conjunto pequeño de
mensajes que cubre todo lo que Alpaca necesita.

### Distribución de responsabilidades

| ESP (autoridad sobre la montura) | App Go (integración) |
|---|---|
| Posición, movimiento y tracking reales | Servidor Alpaca y sus convenciones |
| Conversión de coordenadas | Conversión de unidades y formatos de interfaz |
| Límites y validación de movimientos | Gestión de N.I.N.A., PHD2 y UI |
| Ejecución y duración de pulsos de guiado | Propiedades exclusivas del cliente |
| Configuración persistente del hardware | Presentación, historial, diagnóstico |
| Fallos y condiciones de alimentación | Traducción de errores internos a errores Alpaca |

---

## 3. Transporte

| | Valor |
|---|---|
| Clase USB | CDC-ACM (puerto serial) |
| Stack | ninguno (stream de bytes) |
| Interfaz | puerto serial (`/dev/cu.usbmodem*`) |
| Cliente | la app Go (única) |
| Framing | longitud + JSON |
| Fiabilidad | USB (CRC + retransmisión a nivel transporte) |

---

## 4. Formato y transporte

- **Transporte**: USB CDC-ACM (TinyUSB), un único puerto serial.
- **Trama**: `[u32 little-endian length][JSON UTF-8]`. `length` = bytes del JSON,
  `0 < length <= 2048`.
- **Sin CRC de aplicación**: USB ya es fiable (CRC + retransmisión). No aporta
  autenticación ni garantiza ejecución correcta.
- **Resincronización**: ante error de parseo, se descartan bytes hasta hallar un
  `length` plausible seguido de `{` … `}` JSON válido.
- **Buffers preasignados** (sin `malloc`): RX 4096 B, TX como cola acotada.
- La PC **siempre consulta** (request/response). Se descartó el modelo push
  (`subscribe`/telemetría) por innecesario.

### Envolvente común

Toda trama es un objeto JSON:

```jsonc
// request
{ "type": "state", "id": 7, ...campos de la familia }

// respuesta
{ "type": "state", "id": 7, "ok": true, "error": "…", ...campos de la familia }
```

- `type`: `capabilities | state | config | control | action`.
- `id`: request_id del cliente, replicado en la respuesta.
- `boot_id`: u32 aleatorio por arranque — identifica reinicios/reconexión.

---

## 5. Las 5 familias de mensajes

### `capabilities` — descubrir qué dispositivo tenemos

Leída **una vez al conectar**. Devuelve identidad y capacidades estáticas (no
nombres Alpaca como `CanSlew` o `InterfaceVersion`; la app los deriva).

La cabecera es **común** con el controlador de accesorios (enfocador), para que
el cliente identifique el tipo de dispositivo con un único parser. El
discriminador es `device_type` (`mount` | `accessories`); `boot_id` es un string
hex en ambos.

```json
{
  "type": "capabilities", "id": 1, "ok": true,
  "boot_id": "16e38e47", "proto": 1,
  "device_type": "mount", "device_id": "bft",
  "name": "BFT", "firmware_version": "0.1.0",
  "capabilities": {
    "axes": ["ra", "dec"],
    "slew_speeds_dps": [1.0, 3.0, 4.5, 6.0],
    "tracking_modes": ["none", "sidereal", "lunar", "solar"],
    "caps": { "guide": true, "home": true, "park": true, "unpark": true,
              "move_axis": true, "slew": true, "tracking": true, "sync": false }
  }
}
```

### `state` — una lectura coherente de toda la montura

Una sola lectura reemplaza consultar RA, DEC, tracking, slewing y park por
separado. Campos que no se pueden calcular se marcan inválidos (nunca ceros
aparentemente válidos).

```json
{
  "type": "state", "id": 2, "ok": true,
  "boot_id": 383890973,
  "state": "ready | slewing | tracking | parked | error",
  "tracking": "none | sidereal | lunar | solar",
  "ra_steps": 123456, "dec_steps": -987654,     // int64 autoritativo (sin pérdida)
  "ra_axis_deg": 12.34, "dec_axis_deg": -5.6,   // posición física de ejes
  "ra": 1.2345, "dec": -33.4, "lst": 3.1415,    // horas / grados / horas
  "pier_side": "east | west",
  "ra_speed_dps": 0.0, "dec_speed_dps": 0.0,
  "guiding": false, "at_home": false, "at_park": false,
  "power": true, "time_valid": true,
  "limits": { "ra_min": -100.0, "ra_max": 100.0, "dec_min": -150.0, "dec_max": 150.0 },
  "config_rev": 3, "uptime_ms": 123456
}
```

### `config` — configuración, no movimiento

Modificaciones **parciales y atómicas** (se validan todos los campos antes de
aplicar; no se guarda en flash por órdenes frecuentes de movimiento).

```jsonc
// leer
{ "type": "config", "id": 4 }

// editar (solo los campos presentes cambian)
{ "type": "config", "id": 5,
  "lat": -32.89, "lon": -68.83, "elevation": 750,
  "guide_rate_ra": 0.5, "guide_rate_dec": 0.5,
  "utc": "2026-09-28T02:00:00Z" }
```

La respuesta (tanto de leer como de editar) devuelve la config actual **y la hora**
del reloj del ESP:

```jsonc
{ "type": "config", "id": 5, "ok": true, "config_rev": 3,
  "utc": "2026-09-28T02:00:00Z",           // hora actual del sistema (ISO 8601)
  "config": { "lat": -32.89, "lon": -68.83, "elevation": 750,
              "guide_rate_ra": 0.5, "guide_rate_dec": 0.5 } }
```

- `lat/lon/elevation` → persistido en NVS (valida rango).
- `guide_rate_ra/dec` → en RAM.
- `utc` → aplicado **no persistido**; es cómo la app provee la hora una vez fuera
  de la red (GOTO exige `time_valid`). La respuesta lo devuelve para que la app lea
  la hora actual del ESP.
- `config_rev` (contador persistido) se incrementa en cada write; la app detecta
  cambios de configuración.

### `control` — estado deseado del movimiento continuo

Un solo mensaje modifica uno o varios campos explícitos. Regla fundamental:

- **Campo omitido → conserva su valor.**
- **Velocidad cero → termina el movimiento manual de ese eje.**
- **Ambos ejes presentes → se actualizan juntos.**

```json
{ "type": "control", "id": 6,
  "tracking": "sidereal",           // omitido = conserva
  "manual_ra_dps": 0.0,             // 0 = detener ese eje
  "manual_dec_dps": 0.0 }
```

Así Alpaca puede modificar un eje y la UI ambos, sin que nadie reconstruya
velocidades por fuera. El ESP conserva por separado el tracking solicitado y las
órdenes manuales.

### `action` — operación con principio y fin

Un único formato con un conjunto pequeño de operaciones. Para operaciones largas la
respuesta es **aceptada/rechazada**; el desenlace se lee después en `state`.

```jsonc
{ "type": "action", "id": 7,  "action": "stop",  "scope": "all | manual" }
{ "type": "action", "id": 8,  "action": "goto",  "ra": 1.2, "dec": -33.0, "speed": 3 }
{ "type": "action", "id": 9,  "action": "guide", "direction": "north|south|east|west", "duration_ms": 500 }
{ "type": "action", "id": 10, "action": "home" }
{ "type": "action", "id": 11, "action": "park" }
{ "type": "action", "id": 12, "action": "unpark" }
{ "type": "action", "id": 13, "action": "sync", "ra": 1.0, "dec": -33.0 }   // diferida: rechaza
{ "type": "action", "id": 14, "action": "reset" }
{ "type": "action", "id": 15, "action": "move",  "axis": "ra|dec", "degrees": -5.0, "speed": 4 }
{ "type": "action", "id": 16, "action": "limits", "param": "set_home|set_ra_left|set_ra_right|set_dec_left|set_dec_right|clear_limits" }
```

- `goto`: RA en `[0,24)`, DEC en `[-90,90]`; velocidad `1..4` (1/3/4.5/6 °/s).
- `guide`: el pulso se temporiza entero en el ESP.
- `stop`: `all` = detiene todo (incluido tracking); `manual` = detiene el manual y
  restaura el tracking pausado.
- `reset`: reinicia el ESP (responde `ok` y reinicia ~200 ms después).
- `move`: mueve un eje una cantidad relativa de grados (`degrees`, positivo/negativo).
- `limits`: fija home o un límite desde la posición actual, o resetea los límites.

---

## 6. Sesión, idempotencia y recuperación

- **`boot_id`** (u32 aleatorio por arranque): la app detecta reinicios y relee el
  estado real; no se reproducen movimientos automáticamente.
- **Idempotencia**: un anillo con los últimos 8 `id` de `action` evita ejecutar dos
  veces una operación reintentada.
- **`config_rev`** persistido en NVS: la app detecta cambios de configuración.
- **Prioridad de STOP**: `stop` invalida los comandos de movimiento encolados
  (mecanismo `motors_stop_generation` ya existente).
- **Consultas no ocupan la cola de motores**: `state` lee el snapshot directo.

---

## 7. Decisiones de diseño (cronología)

1. **CDC como único transporte.** El ESP expone un único puerto CDC-ACM, sin
   interfaz de red. La app Go es el único cliente.
2. **SYNC diferida.** El protocolo define `action: sync` pero en esta iteración
   rechaza (el ajuste del mapeo eje↔ecuatorial queda para después).
3. **Formato longitud + JSON**, no COBS/binario: más simple de depurar y testear,
   y sigue siendo una simplificación enorme frente a HTTP.
4. **Se quitó el modelo push** (`subscribe`/telemetría, `revision`, placeholder
   `op`): la PC siempre consulta; no hay frames no solicitados.
5. **`hello` se renombró a `capabilities`** para reflejar su rol: identidad +
   capacidades estáticas (no estado vivo).

---

## 8. Cómo se va a usar (la app Go, futura)

La app de escritorio:

1. Abre el puerto serial CDC (`/dev/cu.usbmodem*`), envía `capabilities` y deriva
   las capacidades Alpaca (`CanSlew`, `CanPulseGuide`, `CanPark`, …).
2. Sirve Alpaca a N.I.N.A./PHD2 y una UI propia.
3. Traduce cada llamada Alpaca al protocolo:

| Solicitud Alpaca | Resolución |
|---|---|
| RA, DEC, tracking, slewing, park, guiado | campos de `state` |
| Capacidades (`can*`) | `capabilities` |
| `MoveAxis` | `control` del eje |
| Cambiar tracking | `control` |
| `PulseGuide` | `action: guide` |
| GoTo, home, park, unpark, sync | `action` correspondiente |
| Coordenadas objetivo | estado de la app hasta ejecutar el GoTo |
| Identificadores de clientes/transacciones | gestionados solo por la app |
| Errores Alpaca | traducción de errores internos |

Como el ESP ya no tiene red, la app también le provee la **hora** (`config.utc`)
cuando arranca, para que GOTO funcione.
