# TMC2209 — Referencia técnica de configuración

Fuente: [TMC2209 Datasheet, revisión 1.09, 16 de febrero de 2023](https://www.analog.com/media/en/technical-documentation/data-sheets/TMC2209_datasheet_rev1.09.pdf).

Documento independiente de plataforma, biblioteca, placa y aplicación. Recopilación de datos de configuración, interfaces y diagnóstico; no contiene imágenes. Las páginas indicadas corresponden a la numeración del PDF.

Los nombres de registros son los del circuito integrado. Los nombres de una biblioteca pueden diferir. Las direcciones son direcciones lógicas de registro, antes de aplicar el bit de escritura UART. Los campos `high:low` incluyen ambos extremos. `R`: lectura; `W`: escritura; `RW`: ambos; `R/W1C`: estado con borrado escribiendo uno. Los registros de solo escritura requieren una copia local si se pretende modificar un campo conservando los demás.

## 1. Alimentación, corriente y reloj

Referencia: secciones 19–20, páginas 75–80.

### 1.1 Rango de operación

| Parámetro | Valor | Condición |
|---|---:|---|
| VS | 5,5–29 V | Regulador interno de 5 V |
| VS para programar OTP | 6–29 V | Regulador interno |
| VS | 4,7–5,4 V | Configuración especial: VS y 5VOUT puenteados |
| VCC_IO | 3,00–5,25 V | Operación normal |
| VCC_IO | 1,50–5,25 V | Standby |
| Temperatura de unión | −40 a +125 °C | Rango operativo |
| Corriente por bobina | 1,4 A RMS | Guía de diseño continuo |
| Corriente por bobina | 2,0 A RMS | Guía con ciclo limitado; ejemplo: 1 s activo/1 s standby |
| Pico sinusoidal por salida | 2,8 A | Medición interna o externa |
| Reloj interno nominal | 12 MHz | Ajustado de fábrica mediante OTP |
| Reloj interno a 25 °C | 11,5–12,5 MHz | Mínimo–máximo |
| Reloj externo | 4–16 MHz | Respetar tiempos y ciclo de trabajo |
| Reloj externo, HIGH/LOW | 16 ns mínimo | Niveles 0,1/0,9 × VCC_IO |

La cifra de portada de 2 A RMS no constituye una especificación universal de corriente continua para cualquier módulo. La corriente utilizable depende del diseño térmico.

### 1.2 Máximos absolutos seleccionados

Estos son límites de estrés, no puntos de operación.

| Parámetro | Límite |
|---|---:|
| VS con carga inductiva | −0,5 a 32 V |
| Tensión máxima de alimentación/puentes | 33 V |
| VCC_IO y 5VOUT externo | −0,5 a 5,5 V |
| Entrada lógica | −0,5 V a VCC_IO + 0,5 V |
| VREF | −0,5 a 6 V; además, no superar simultáneamente VCC_IO y 5VOUT en más del 10 % |
| Corriente hacia/desde pines de señal | ±10 mA |
| Corriente total del regulador 5VOUT | 25 mA |
| Disipación continua del regulador | 0,5 W |
| Corriente repetitiva de salida del puente | 3 A |
| Temperatura de unión absoluta | −50 a +150 °C |
| Almacenamiento | −55 a +150 °C |

## 2. Pines del integrado

Referencia: sección 2, páginas 9–10. Numeración QFN28, no numeración de un módulo comercial.

| Pin | Señal | Función/configuración |
|---|---|---|
| 1 | OB2 | Bobina B, terminal 2 |
| 2 | ENN | HIGH: puentes deshabilitados; LOW: habilitación permitida |
| 3, 18 | GND | Masa |
| 4 | CPO | Bomba de carga |
| 5 | CPI | Condensador de 22 nF/50 V entre CPI y CPO |
| 6 | VCP | Condensador de 100 nF hacia VS |
| 7 | SPREAD | Selección de modo; invierte el significado de GCONF.en_SpreadCycle cuando está HIGH |
| 8 | 5VOUT | Regulador interno; desacoplo cerámico de 2,2–4,7 µF |
| 9 | MS1_AD0 | Selección de micropasos o bit 0 de dirección UART |
| 10 | MS2_AD1 | Selección de micropasos o bit 1 de dirección UART |
| 11 | DIAG | Diagnóstico y detección de bloqueo; activo HIGH |
| 12 | INDEX | Índice eléctrico, prealarma térmica o pulsos internos, según GCONF |
| 13 | CLK | LOW/GND para reloj interno; señal externa para reloj externo |
| 14 | PDN_UART | UART unifilar; función alternativa de reducción de corriente en reposo |
| 15 | VCC_IO | Alimentación de las interfaces digitales |
| 16 | STEP | Entrada de pasos |
| 17 | VREF | Referencia analógica de tensión o corriente según modo de medición |
| 19 | DIR | Dirección |
| 20 | STDBY | HIGH: standby del regulador interno; pull-down nominal de 100 kΩ |
| 21 | OA2 | Bobina A, terminal 2 |
| 22, 28 | VS | Alimentación de potencia |
| 23 | BRA | Resistencia de medida de A hacia GND; a GND directamente en medición interna |
| 24 | OA1 | Bobina A, terminal 1 |
| 25 | Sin uso | Puede conectarse a GND |
| 26 | OB1 | Bobina B, terminal 1 |
| 27 | BRB | Resistencia de medida de B hacia GND; a GND directamente en medición interna |
| Pad expuesto | GND | Conexión eléctrica y térmica al plano de masa |

### 2.1 Micropasos por pines y dirección UART

| MS2 | MS1 | Micropasos por paso completo | Dirección UART |
|---:|---:|---:|---:|
| 0 | 0 | 8 | 0 |
| 0 | 1 | 32 | 1 |
| 1 | 0 | 64 | 2 |
| 1 | 1 | 16 | 3 |

`GCONF.mstep_reg_select=1` sustituye la selección de resolución por pines por `CHOPCONF.MRES`. La dirección UART sigue dependiendo de MS1/MS2.

## 3. UART

Referencia: sección 4, páginas 18–21.

| Propiedad | Especificación |
|---|---|
| Interfaz | Un hilo, bidireccional, half-duplex |
| Formato de byte | 8 bits, sin paridad, un bit de parada |
| Estado inactivo | HIGH |
| Bits dentro de cada byte | LSB primero |
| Palabras de 32 bits | Byte más significativo primero |
| Sincronización habitual | Byte 0x05 |
| Direcciones de nodos | 0–3 |
| Dirección en respuesta al maestro | 0xFF |
| Selección escritura | Dirección de registro OR 0x80 |
| Selección lectura | Dirección de registro AND 0x7F |
| Baud rate | Autodetección en cada datagrama |
| Máximo baud rate | fCLK / 16 |
| Mínimo indicado | 9000 baud con hipótesis de reloj de 20 MHz; rango de uso descrito desde 9600 baud |
| Reinicio de comunicación | Separación mayor de 63 tiempos de bit entre comienzos de bytes sucesivos |
| Recuperación después de error | Al menos 12 tiempos de bit con bus inactivo |
| Liberación de transmisión del esclavo | 4 tiempos de bit después del último stop |
| Confirmación de escritura | Incremento de IFCNT; no hay respuesta de datos a cada escritura |

### 3.1 Tramas

```text
Escritura: 8 bytes
[0x05] [nodo] [registro | 0x80] [D31:24] [D23:16] [D15:8] [D7:0] [CRC]

Solicitud de lectura: 4 bytes
[0x05] [nodo] [registro & 0x7F] [CRC]

Respuesta de lectura: 8 bytes
[0x05] [0xFF] [registro & 0x7F] [D31:24] [D23:16] [D15:8] [D7:0] [CRC]
```

### 3.2 CRC

| Parámetro | Valor |
|---|---|
| Familia | CRC-8/ATM |
| Polinomio | x^8 + x^2 + x + 1; 0x07 |
| Inicialización | 0 |
| Datos cubiertos | Todos los bytes previos al CRC, incluidos sincronización y dirección |
| Orden de procesamiento del dato | Bit menos significativo primero por byte |

Regla de actualización para implementación: comparar el bit 7 del acumulador con el siguiente bit de dato; desplazar el acumulador a la izquierda y aplicar XOR con 0x07 si difieren. Conservar ocho bits. No sustituir este procedimiento por una función CRC con parámetros de reflexión diferentes.

## 4. Mapa de registros

Referencia: sección 5, páginas 22–37. El bus transporta palabras de 32 bits aunque no todos los bits estén implementados.

| Dirección | Registro | Acceso | Contenido |
|---|---|---|---|
| 0x00 | GCONF | RW | Configuración global |
| 0x01 | GSTAT | R/W1C | Estado general |
| 0x02 | IFCNT | R | Contador de escrituras válidas |
| 0x03 | NODECONF | W | Retardo de respuesta UART |
| 0x04 | OTP_PROG | W | Programación OTP |
| 0x05 | OTP_READ | R | Lectura OTP |
| 0x06 | IOIN | R | Entradas e identificación |
| 0x07 | FACTORY_CONF | RW | Ajustes de reloj y temperatura |
| 0x10 | IHOLD_IRUN | W | Corriente activa, reposo y transición |
| 0x11 | TPOWERDOWN | W | Retardo hasta reducción de corriente |
| 0x12 | TSTEP | R | Período normalizado a 1/256 de paso |
| 0x13 | TPWMTHRS | W | Umbral de modo |
| 0x14 | TCOOLTHRS | W | Umbral de CoolStep/StallGuard |
| 0x22 | VACTUAL | W | Generador interno de pasos |
| 0x40 | SGTHRS | W | Umbral de bloqueo |
| 0x41 | SG_RESULT | R | Resultado StallGuard4 |
| 0x42 | COOLCONF | W | Ajustes CoolStep |
| 0x6A | MSCNT | R | Posición en tabla eléctrica |
| 0x6B | MSCURACT | R | Valores de tabla de corriente |
| 0x6C | CHOPCONF | RW | Chopper, resolución y protecciones |
| 0x6F | DRV_STATUS | R | Estado de potencia y modo real |
| 0x70 | PWMCONF | RW | Ajustes StealthChop |
| 0x71 | PWM_SCALE | R | Escala PWM actual |
| 0x72 | PWM_AUTO | R | Parámetros PWM aprendidos |

### 4.1 GCONF — 0x00

| Bits | Campo | Valores/efecto |
|---|---|---|
| 0 | I_scale_analog | 0: referencia interna; 1: escalado por VREF. Reset: 1 |
| 1 | internal_Rsense | 0: resistencias externas; 1: medición interna por RDSon. Reset: OTP |
| 2 | en_SpreadCycle | 0: permite StealthChop y umbral; 1: SpreadCycle. SPREAD=HIGH invierte la selección. Reset: OTP |
| 3 | shaft | 1: invierte sentido |
| 4 | index_otpw | 0: índice eléctrico; 1: prealarma térmica en INDEX |
| 5 | index_step | 1: INDEX conmuta con cada paso del generador interno; prevalece sobre index_otpw |
| 6 | pdn_disable | 1: desactiva función de reducción de corriente del pin PDN_UART; configuración para uso UART |
| 7 | mstep_reg_select | 0: micropasos por pines; 1: por MRES |
| 8 | multistep_filt | 1: filtro/optimización de pasos; activo por encima de aproximadamente 750 pasos completos/s. Reset: 1 |
| 9 | test_mode | 0 para operación normal; 1 reservado a prueba del fabricante |

### 4.2 GSTAT — 0x01

| Bit | Campo | Estado |
|---|---|---|
| 0 | reset | Reset detectado; registros recargados con valores de arranque |
| 1 | drv_err | Apagado por cortocircuito o temperatura; consultar DRV_STATUS |
| 2 | uv_cp | Subtensión de bomba de carga; no enclavado |

Los bits borrables se limpian escribiendo uno. `drv_err` requiere que haya desaparecido la causa. `uv_cp` se actualiza con la condición física.

### 4.3 IFCNT — 0x02

- Campo: `[7:0]`.
- Secuencia: 0–255, retorno a 0.
- Escritura UART aceptada: +1.
- Lectura UART: sin incremento.

### 4.4 NODECONF — 0x03

Campo `SENDDELAY[11:8]`:

| Valor | Retardo de respuesta en tiempos de bit |
|---|---:|
| 0, 1 | 8 |
| 2, 3 | 24 |
| 4, 5 | 40 |
| 6, 7 | 56 |
| 8, 9 | 72 |
| 10, 11 | 88 |
| 12, 13 | 104 |
| 14, 15 | 120 |

Multinodo: `SENDDELAY >= 2`. Algunas bibliotecas denominan este registro `SLAVECONF`.

### 4.5 IOIN — 0x06

| Bits | Dato |
|---|---|
| 0 | ENN |
| 1 | 0 |
| 2 | MS1 |
| 3 | MS2 |
| 4 | DIAG |
| 5 | 0 |
| 6 | PDN_UART |
| 7 | STEP |
| 8 | SPREAD_EN |
| 9 | DIR |
| 31:24 | VERSION; identificación indicada: 0x21 |

### 4.6 FACTORY_CONF — 0x07

| Bits | Campo | Configuración |
|---|---|---|
| 4:0 | FCLKTRIM | 0–31, frecuencia creciente; valor calibrado de fábrica vía OTP |
| 9:8 | OTTRIM | Selección de temperaturas según tabla siguiente |

| OTTRIM | Apagado OT nominal | Prealarma OTPW nominal |
|---:|---:|---:|
| 0 | 143 °C | 120 °C |
| 1 | 150 °C | 120 °C |
| 2 | 150 °C | 143 °C |
| 3 | 157 °C | 143 °C |

Una protección configurada por encima del rango operativo no amplía dicho rango.

## 5. Corriente y reposo

Referencias: páginas 28, 52–57.

### 5.1 IHOLD_IRUN — 0x10

| Bits | Campo | Rango y unidad |
|---|---|---|
| 4:0 | IHOLD | 0–31; factor (IHOLD+1)/32 |
| 12:8 | IRUN | 0–31; factor (IRUN+1)/32; reset 31 |
| 19:16 | IHOLDDELAY | 0: reducción inmediata; 1–15: intervalos por decremento |

```text
t_intervalo_reduccion = IHOLDDELAY × 2^18 / fCLK
```

`IHOLD=0` normalmente representa 1/32 de escala, no cero amperios. En StealthChop habilita los comportamientos especiales de `FREEWHEEL`.

### 5.2 TPOWERDOWN — 0x11

| Dato | Valor |
|---|---|
| Campo | 7:0 |
| Rango | 0–255 |
| Reset | 20 |
| Unidad | 2^18/fCLK segundos |
| Inicio de cuenta | Después de detectar reposo |
| Mínimo para autoajuste PWM_OFS_AUTO | 2 |

```text
t_reposo_detectado = 2^20 / fCLK
t_espera_reduccion = TPOWERDOWN × 2^18 / fCLK
```

A 12 MHz: detección de reposo ≈87,38 ms; unidad de TPOWERDOWN ≈21,845 ms; valor 20 ≈436,91 ms adicionales.

### 5.3 Fórmula con resistencias externas

```text
I_RMS = ((CS + 1) / 32) × V_FS / (R_SENSE + 0,020 Ω) / sqrt(2)
I_pico = sqrt(2) × I_RMS

CS = IRUN, IHOLD o escala efectiva de CoolStep, según estado.
```

| VSENSE | V_FS nominal |
|---:|---:|
| 0 | 0,325 V |
| 1 | 0,180 V |

Con `I_scale_analog=1`, sustituir V_FS por la referencia escalada por VREF. Escalado nominal: `V_FS' = V_FS × VREF/2,5 V`, dentro del intervalo lineal; por encima de escala completa no aumenta proporcionalmente.

| VREF | Escala nominal |
|---|---:|
| 0,5 V | 20 % |
| 2,5 V o superior | 100 % |
| Abierto | Aproximadamente 73 % |

El intervalo práctico indicado para escalado analógico es aproximadamente 0,5–2,4 V. Valores inferiores a 0,5 V no son recomendados para precisión de control.

### 5.4 Medición interna

| Parámetro | Valor/condición |
|---|---|
| Selección | GCONF.internal_Rsense=1 |
| BRA y BRB | Directamente a GND |
| Referencia | Resistencia desde 5VOUT a VREF |
| Intervalo recomendado | 0,2–1,4 A RMS |
| Impedancia de VREF típica | 0,41 kΩ |
| Offset de corriente de referencia típico | 38 µA |
| Ganancia típica de referencia a corriente de bobina | 3300 |
| Tolerancia orientativa | ±10 %, en las condiciones de la tabla eléctrica |

| RREF | I_pico | I_RMS |
|---:|---:|---:|
| 6,2 kΩ | 2,6 A | 1,9 A |
| 6,8 kΩ | 2,4 A | 1,7 A |
| 7,5 kΩ | 2,2 A | 1,6 A |
| 8,2 kΩ | 2,0 A | 1,4 A |
| 9,1 kΩ | 1,8 A | 1,3 A |
| 10 kΩ | 1,7 A | 1,2 A |
| 12 kΩ | 1,4 A | 1,0 A |
| 15 kΩ | 1,2 A | 0,84 A |
| 18 kΩ | 1,0 A | 0,72 A |
| 22 kΩ | 0,85 A | 0,61 A |
| 27 kΩ | 0,73 A | 0,51 A |
| 33 kΩ | 0,62 A | 0,44 A |

Tabla con `CS=31`, `VSENSE=0`; las filas superiores no amplían el intervalo recomendado de medición interna ni los límites térmicos. `VSENSE=1` reduce la escala aproximadamente al 55 %.

## 6. CHOPCONF — 0x6C

Referencia: páginas 33–34. Valor de reset publicado: `0x10000053`, sujeto a campos configurables mediante OTP.

| Bits | Campo | Valores/efecto |
|---|---|---|
| 3:0 | TOFF | 0: puentes apagados; 1: solamente con TBL>=2; 2–15: regulación habilitada |
| 6:4 | HSTRT | 0–7; incremento efectivo 1–8 |
| 10:7 | HEND | 0–15; valor efectivo −3 a +12 |
| 14:11 | Reservados | Escribir 0 |
| 16:15 | TBL | 0/1/2/3: 16/24/32/40 ciclos de blanking |
| 17 | VSENSE | 0: 325 mV; 1: 180 mV nominales |
| 23:18 | Reservados | Escribir 0 |
| 27:24 | MRES | Resolución según tabla |
| 28 | INTPOL | 1: interpolación a 256 |
| 29 | DEDGE | 0: flanco ascendente STEP; 1: ambos flancos |
| 30 | DISS2G | 1: desactiva protección contra cortocircuito a masa |
| 31 | DISS2VS | 1: desactiva protección del lado bajo/cortocircuito a VS |

```text
N_off = 24 + 32 × TOFF     [ciclos de reloj]
HEND_efectivo = HEND - 3
HSTRT_efectivo = HSTRT + 1
HEND_efectivo + HSTRT_efectivo <= 16
```

`CHOPCONF[14]` no selecciona SpreadCycle. `DEDGE=1` no es compatible con `multistep_filt=1`.

### 6.1 MRES

| MRES | Micropasos por paso completo | Incremento de tabla por STEP |
|---:|---:|---:|
| 0 | 256 | 1 |
| 1 | 128 | 2 |
| 2 | 64 | 4 |
| 3 | 32 | 8 |
| 4 | 16 | 16 |
| 5 | 8 | 32 |
| 6 | 4 | 64 |
| 7 | 2 | 128 |
| 8 | 1 | 256 |

Valores 9–15: sin resolución válida especificada. La tabla eléctrica tiene 1024 posiciones, equivalentes a cuatro pasos completos.

## 7. Selección de modos y umbrales

Referencias: páginas 23, 28, 44–45, 67.

### 7.1 Selección efectiva

Definición lógica derivada de los dos controles:

```text
spread_forzado = GCONF.en_SpreadCycle XOR nivel_del_pin_SPREAD
```

| spread_forzado | TPWMTHRS | Resultado |
|---:|---:|---|
| 1 | Cualquiera | SpreadCycle |
| 0 | 0 | StealthChop; cambio por velocidad deshabilitado |
| 0 | >0 | StealthChop a baja velocidad; SpreadCycle por encima del umbral |

### 7.2 TSTEP — 0x12

- Campo: `[19:0]`, lectura.
- Unidad: ciclos de reloj por micropaso de 1/256.
- Reposo/desbordamiento: `0xFFFFF`.
- Independiente de la resolución externa MRES.

```text
μ = micropasos configurados por paso completo
f_STEP = pasos de entrada por segundo
f_256 = f_STEP × 256 / μ
TSTEP = fCLK / f_256
```

### 7.3 TPWMTHRS — 0x13

- Campo: `[19:0]`.
- Rango: 0–1.048.575.
- Cero: sin transición automática por este umbral.
- Valores positivos: tiempo normalizado de transición.
- Umbral mayor: velocidad de transición menor.

```text
TPWMTHRS_nominal = fCLK / (f_STEP_umbral × 256 / μ)
f_STEP_umbral_nominal = fCLK × μ / (256 × TPWMTHRS)
```

Histéresis de comparadores TSTEP: 1/16. El límite inferior temporal se expresa como `Txxx × 15/16 − 1`; la transición al acelerar ocurre a una velocidad algo mayor que la nominal de retorno. No tratar TPWMTHRS como frecuencia en Hz.

### 7.4 TCOOLTHRS — 0x14

- Campo: `[19:0]`.
- Habilitación por velocidad mínima de CoolStep y señal de bloqueo en DIAG.
- Ventana indicada: `TCOOLTHRS >= TSTEP > TPWMTHRS`.
- CoolStep requiere además `SEMIN != 0` y operación en StealthChop.
- Cero: no habilita una ventana útil de velocidad.

## 8. PWMCONF — 0x70

Referencia: páginas 35–36. Reset publicado: `0xC10D0024`; campos dependientes de OTP.

| Bits | Campo | Valores/efecto |
|---|---|---|
| 7:0 | PWM_OFS | 0–255; offset inicial; reset nominal 36 |
| 15:8 | PWM_GRAD | 0–255; gradiente con velocidad/inicialización del aprendizaje |
| 17:16 | PWM_FREQ | Frecuencia PWM según tabla |
| 18 | PWM_AUTOSCALE | 1: regulación automática de corriente; 0: amplitud PWM anticipada |
| 19 | PWM_AUTOGRAD | 1: aprendizaje del gradiente; requiere AUTOSCALE=1 |
| 21:20 | FREEWHEEL | Comportamiento de reposo con IHOLD=0 |
| 23:22 | Reservados | Escribir 0 |
| 27:24 | PWM_REG | 1–15; máxima variación de amplitud por media onda: valor/2 |
| 31:28 | PWM_LIM | 0–15; límite al volver de SpreadCycle; reset nominal 12 |

### 8.1 Frecuencia PWM

| PWM_FREQ | Fórmula | A 12 MHz |
|---:|---|---:|
| 0 | 2 × fCLK / 1024 | 23,438 kHz |
| 1 | 2 × fCLK / 683 | 35,139 kHz |
| 2 | 2 × fCLK / 512 | 46,875 kHz |
| 3 | 2 × fCLK / 410 | 58,537 kHz |

### 8.2 FREEWHEEL

| Código | Reposo con IHOLD=0 en StealthChop |
|---:|---|
| 0 | Funcionamiento normal |
| 1 | Salidas libres |
| 2 | Bobinas cortocircuitadas mediante MOSFET inferiores |
| 3 | Bobinas cortocircuitadas mediante MOSFET superiores |

### 8.3 Escalado y aprendizaje

Con `AUTOSCALE=0`:

```text
PWM = limitar_0_255(
    PWM_OFS × (CS_ACTUAL + 1)/32
    + PWM_GRAD × 256/TSTEP
)
```

En ese modo IRUN/IHOLD escalan amplitud; no constituyen una regulación cerrada de corriente.

Condiciones numéricas de aprendizaje de `PWM_GRAD_AUTO`:

```text
PWM_AUTOSCALE = 1
PWM_AUTOGRAD = 1
PWM_OFS_AUTO inicializado en reposo a IRUN
Reposo inicial indicado: >130 ms
-1 < PWM_SCALE_AUTO < 1, al estabilizar offset
PWM_SCALE_SUM < 255
1,5 × PWM_OFS_AUTO × (IRUN+1)/32 < PWM_SCALE_SUM
PWM_SCALE_SUM < 4 × PWM_OFS_AUTO × (IRUN+1)/32
```

Tiempo orientativo de adaptación: ocho pasos completos por modificación de ±1 del gradiente. `PWM_OFS=0` tiene comportamiento especial y elimina la reducción por debajo del umbral inferior de regulación; no equivale simplemente a una amplitud inicial nula.

### 8.4 Registros de lectura PWM

| Registro | Bits | Campo | Rango |
|---|---|---|---|
| PWM_SCALE, 0x71 | 7:0 | PWM_SCALE_SUM | 0–255 |
| PWM_SCALE, 0x71 | 24:16 | PWM_SCALE_AUTO | Con signo, −255 a +255 |
| PWM_AUTO, 0x72 | 7:0 | PWM_OFS_AUTO | 0–255 |
| PWM_AUTO, 0x72 | 23:16 | PWM_GRAD_AUTO | 0–255 |

## 9. StallGuard4 y CoolStep

Referencias: páginas 29–30 y 58–62.

### 9.1 SGTHRS — 0x40; SG_RESULT — 0x41

| Parámetro | Datos |
|---|---|
| SGTHRS | 8 bits, 0–255 |
| SG_RESULT | 10 bits; bits 9 y 0 siempre cero |
| Actualización | Una por paso completo |
| Bloqueo | SG_RESULT <= 2 × SGTHRS |
| Resultado mayor | Menor carga estimada/mayor margen de torque |
| Modo | StealthChop |

La actualización de SG_RESULT es independiente de TCOOLTHRS y SGTHRS. El umbral de velocidad determina la habilitación de la salida de bloqueo. SGTHRS más alto incrementa la sensibilidad de detección.

### 9.2 COOLCONF — 0x42

| Bits | Campo | Valores |
|---|---|---|
| 3:0 | SEMIN | 0: CoolStep desactivado; 1–15: umbral inferior |
| 4 | Reservado | 0 |
| 6:5 | SEUP | 0/1/2/3: incrementos de 1/2/4/8 |
| 7 | Reservado | 0 |
| 11:8 | SEMAX | 0–15: histéresis superior |
| 12 | Reservado | 0 |
| 14:13 | SEDN | 0/1/2/3: un decremento cada 32/8/2/1 resultados |
| 15 | SEIMIN | 0: mínimo 1/2, IRUN>=10; 1: mínimo 1/4, IRUN>=20 |

```text
SG_RESULT < SEMIN × 32                 => incrementar corriente
SG_RESULT >= (SEMIN+SEMAX+1) × 32       => decrementar corriente
```

IRUN establece el máximo de corriente para CoolStep. `DRV_STATUS.CS_ACTUAL` permite consultar la escala aplicada. No confundir el mecanismo StallGuard4 de este integrado con StallGuard2 de otros modelos.

## 10. Movimiento interno y secuenciador

Referencias: páginas 31, 63–67.

### 10.1 VACTUAL — 0x22

| Parámetro | Valor |
|---|---|
| Campo | 23:0, complemento a dos |
| Intervalo indicado | ±(2^23−1) |
| 0 | Movimiento mediante STEP/DIR |
| Distinto de 0 | Generador interno activo |
| Signo | Sentido |
| Rampas | No incorporadas; responsabilidad del controlador |

```text
f_pasos_internos = VACTUAL × fCLK / 2^24
VACTUAL = f_pasos_internos × 2^24 / fCLK
vueltas_por_segundo = f_pasos_internos / (μ × pasos_completos_por_vuelta)
```

A 12 MHz, una unidad VACTUAL corresponde a aproximadamente 0,715256 pasos configurados/s. No son RPM ni necesariamente micropasos de 1/256: la conversión incorpora la resolución configurada.

### 10.2 MSCNT — 0x6A

- `[9:0]`, 0–1023.
- Posición dentro de una onda eléctrica, no posición mecánica absoluta.
- Incremento/decremento por STEP: `256/μ`.
- Sentido normal: DIR=0 incrementa; DIR=1 decrementa; `shaft` puede invertirlo.

### 10.3 MSCURACT — 0x6B

| Bits | Campo | Formato |
|---|---|---|
| 8:0 | CUR_B | 9 bits con signo |
| 24:16 | CUR_A | 9 bits con signo |

Rango indicado: −255 a +255. Son valores del secuenciador sin el escalado de corriente; no mediciones directas en amperios.

### 10.4 Temporización STEP/DIR

| Parámetro | Valor |
|---|---|
| Frecuencia máxima a resolución máxima | fCLK/2 |
| Frecuencia máxima de pasos completos | fCLK/512 |
| STEP HIGH mínimo | max(tFILTSD, tCLK+20 ns) |
| STEP LOW mínimo | max(tFILTSD, tCLK+20 ns) |
| DIR antes de STEP | 20 ns mínimo |
| DIR después de STEP | 20 ns mínimo |
| Filtro STEP/DIR | 13/20/30 ns, mínimo/típico/máximo |

El número de 100 ns de la tabla no reemplaza la dependencia con tCLK: a 12 MHz, `tCLK+20 ns ≈103,33 ns`. Estas frecuencias son límites de interfaz/secuenciador, no garantías de velocidad mecánica.

## 11. DRV_STATUS — 0x6F

Referencia: páginas 37, 68–69.

| Bits | Campo | Significado cuando vale 1 |
|---|---|---|
| 0 | OTPW | Prealarma térmica |
| 1 | OT | Umbral de apagado térmico alcanzado |
| 2 | S2GA | Cortocircuito a masa, fase A |
| 3 | S2GB | Cortocircuito a masa, fase B |
| 4 | S2VSA | Cortocircuito a VS/lado bajo, fase A |
| 5 | S2VSB | Cortocircuito a VS/lado bajo, fase B |
| 6 | OLA | Carga abierta, fase A |
| 7 | OLB | Carga abierta, fase B |
| 8 | T120 | Comparador nominal 120 °C |
| 9 | T143 | Comparador nominal 143 °C |
| 10 | T150 | Comparador nominal 150 °C |
| 11 | T157 | Comparador nominal 157 °C |
| 15:12 | Reservados | Ignorar |
| 20:16 | CS_ACTUAL | Escala de corriente efectiva, 0–31 |
| 29:21 | Reservados | Ignorar |
| 30 | STEALTH | 1: StealthChop; 0: SpreadCycle |
| 31 | STST | Reposo detectado |

| Evento | Comportamiento |
|---|---|
| Cortocircuito | Puentes deshabilitados; flags enclavados hasta deshabilitación por ENN o TOFF=0 |
| OT | Puentes deshabilitados hasta enfriamiento suficiente para retirar también OTPW |
| OLA/OLB | Información; no provoca por sí sola apagado; puede ser falsa en reposo o alta velocidad |
| UV_CP | Puentes deshabilitados por subtensión de bomba de carga |

## 12. OTP y valores de arranque

Referencias: páginas 24–27, 73–74.

### 12.1 OTP_PROG — 0x04

| Bits | Campo | Valores |
|---|---|---|
| 2:0 | OTPBIT | 0–7 |
| 5:4 | OTPBYTE | 0–2 |
| 15:8 | OTPMAGIC | 0xBD |

Programación: un bit por operación, transición irreversible 0→1. Tiempo recomendado mínimo: 10 ms por bit. Comprobación: OTP_READ. No modificar los bits de calibración de reloj de fábrica como parte de una inicialización ordinaria.

### 12.2 OTP_READ — 0x05

| Bits | Contenido |
|---|---|
| 7:0 | OTP0 |
| 15:8 | OTP1 |
| 23:16 | OTP2 |

| Bits globales | Campo |
|---|---|
| 4:0 | OTP_FCLKTRIM |
| 5 | OTP_OTTRIM |
| 6 | OTP_internalRsense |
| 7 | OTP_TBL |
| 23 | OTP_en_SpreadCycle |
| 22:21 | OTP_IHOLD |
| 20:19 | OTP_IHOLDDELAY |
| 18 | OTP_PWM_FREQ |
| 17 | OTP_PWM_REG |

| Campo | Códigos → valores |
|---|---|
| OTP_en_SpreadCycle | 0→StealthChop; 1→SpreadCycle, antes de inversión por SPREAD |
| OTP_IHOLD | 0/1/2/3 → 16/2/8/24 |
| OTP_IHOLDDELAY | 0/1/2/3 → 1/2/4/8 |
| OTP_PWM_FREQ | 0→PWM_FREQ=1; 1→PWM_FREQ=2 |
| OTP_PWM_REG | 0→PWM_REG=8; 1→PWM_REG=2 |
| OTP_TBL | 0→TBL=2; 1→TBL=1 |
| OTP_OTTRIM | 0→143/120 °C; 1→150/120 °C, OT/OTPW |

### 12.3 Bits OTP de interpretación dependiente del modo

Con `OTP_en_SpreadCycle=0`:

| Bits | Campo | Códigos → valores |
|---|---|---|
| 16 | OTP_PWM_OFS | 0→PWM_OFS=36; 1→PWM_OFS=0 y AUTOGRAD=0 |
| 15:13 | OTP_TPWMTHRS | 0/1/2/3/4/5/6/7 → 0/200/300/400/500/800/1200/4000 |
| 12 | OTP_pwm_autograd | 0→AUTOGRAD=1; 1→AUTOGRAD=0 |
| 11:8 | OTP_PWM_GRAD | 0–15 → tabla siguiente |

```text
Código:  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14  15
Grad.:  14 16 18 21 24 27 31 35 40 46 52 59 67 77 88 100
```

Con `OTP_en_SpreadCycle=1`, los bits 8–16 representan directamente `CHOPCONF[0:8]`: TOFF, HSTRT y los dos bits inferiores de HEND. Los parámetros StealthChop vuelven a sus valores fijos indicados: PWM_GRAD=0, TPWMTHRS=0, PWM_OFS=36 y AUTOGRAD=1.

## 13. Parámetros eléctricos de señal y protección

Referencias: páginas 77–80. Los valores típicos no son límites garantizados.

| Parámetro | Mínimo | Típico | Máximo |
|---|---:|---:|---:|
| Entrada LOW | −0,3 V | — | 0,3 × VCC_IO |
| Entrada HIGH | 0,7 × VCC_IO | — | VCC_IO + 0,3 V |
| Histéresis Schmitt | — | 0,12 × VCC_IO | — |
| Salida LOW, 2 mA | — | — | 0,2 V |
| Salida HIGH, −2 mA | VCC_IO−0,2 V | — | — |
| Fuga de entrada | −10 µA | — | +10 µA |
| Pull-up/pull-down de señales | 132 kΩ | 166 kΩ | 200 kΩ |
| Pull-down STDBY | 80 kΩ | 100 kΩ | 120 kΩ |
| Capacidad de pin digital | — | 3,5 pF | — |
| Regulador 5VOUT sin carga, 25 °C | 4,80 V | 5,0 V | 5,25 V |
| Subtensión VS al subir | 3,5 V | 4,2 V | 4,6 V |
| Subtensión VCC_IO al subir | 2,1 V | 2,55 V | 3,0 V |
| Histéresis de subtensión VCC_IO | — | 0,3 V | — |
| Detección de corto a GND | 2 V | 2,5 V | 3 V |
| Detección de corto a VS | 1,6 V | 2 V | 2,3 V |
| Retardo de corto | 0,8 µs | 1,3 µs | 2 µs |
| Comparador 120 °C | 100 °C | 120 °C | 140 °C |
| Comparador 143 °C | 128 °C | 143 °C | 163 °C |
| Comparador 150 °C | 135 °C | 150 °C | 170 °C |
| Comparador 157 °C | 142 °C | 157 °C | 177 °C |

Condiciones de los umbrales de corto: tensión medida sobre el transistor correspondiente, no tensión absoluta genérica en el motor. La etapa de potencia puede estar aproximadamente 10 °C por encima del detector térmico.

| Dato térmico | Valor típico | Condición |
|---|---:|---|
| Resistencia unión–ambiente | 30 K/W | PCB de referencia JEDEC 2s2p; no universal |
| Resistencia unión–pad térmico | 6 K/W | Encapsulado |
| Disipación | 1,4 W | Ejemplo a 1 A RMS, 24 V, motor y condiciones del fabricante |
| Disipación | 2,8 W | Ejemplo a 1,4 A RMS, 24 V, operación corta |

## 14. Comprobaciones de implementación

Lista de verificación editorial para implementar los datos anteriores:

- Usar máscaras de campos y conservar únicamente bits definidos.
- No utilizar el bit 14 de CHOPCONF para seleccionar modo.
- Considerar SPREAD además de GCONF al seleccionar chopper.
- Consultar DRV_STATUS.STEALTH para verificar el modo efectivo.
- Distinguir lectura UART fallida de un registro que contiene cero.
- Confirmar escrituras con IFCNT y lectura de retorno cuando esté permitida.
- Mantener copias locales de registros W.
- Aplicar extensión de signo en campos de 9 y 24 bits.
- Usar fCLK del TMC2209 para tiempos y umbrales, no el reloj del microcontrolador.
- No equiparar micropasos externos con unidades normalizadas de TSTEP.
- Distinguir IRUN/IHOLD de amperios y VSENSE de voltaje de alimentación.
- No trasladar campos de otros TMC sin revisar su mapa específico.
- Reaplicar configuración volátil después de reset.
- Mantener OTP fuera de la inicialización rutinaria.

## 15. Localización de información en el PDF

| Tema | Páginas |
|---|---|
| Principios e interfaces | 4–8 |
| Pines | 9–10 |
| Circuitos y alimentación | 11–17 |
| UART | 18–21 |
| Registros generales y OTP | 22–27 |
| Corriente, tiempos, StallGuard y CoolStep | 28–30 |
| Secuenciador y registros chopper | 31–37 |
| StealthChop y transición | 38–47 |
| SpreadCycle | 48–51 |
| Resistencias y corriente | 52–57 |
| StallGuard4 y CoolStep | 58–62 |
| STEP/DIR e interpolación | 63–66 |
| Generador interno | 67 |
| Diagnóstico | 68–69 |
| Guías de ajuste | 70–73 |
| Reset y reloj | 74 |
| Límites y características eléctricas | 75–80 |
| PCB | 81–82 |
| Encapsulado | 83–84 |

Alcance: referencia de configuración y datos asociados; no reproduce las curvas, figuras, dimensiones mecánicas detalladas ni todas las condiciones de caracterización analógica del documento original.
