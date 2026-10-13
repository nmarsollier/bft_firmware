# Driver para motor paso a paso DiGiYes TB6600 — documento técnico

**Referencia comercial:** ASIN B0CHRWD54S.
**Fecha de revisión:** 13 de octubre de 2026.
**Alcance:** publicación de Amazon y siete imágenes proporcionadas por el usuario. Documento de consulta e integración; no es un manual oficial ni una verificación eléctrica del equipo.

## 1. Identificación y alcance de los datos

La publicación ofrece dos drivers DiGiYes, denominados TB6600 versión mejorada, para motores de 4/6/8 cables, tamaños 39/42/57 mm, incluidos NEMA 17/23, y pasos de 1,8° o 0,9°. Declara entradas optoaisladas de 3,3–24 V sin resistencia externa en serie y control por pulso/dirección. [Fuente: publicación de Amazon](https://www.amazon.com/dp/B0CHRWD54S).

La denominación comercial **TB6600 no confirma qué circuito integrado está instalado**. Las fotografías muestran un equipo rotulado únicamente como `MicrostepDriver`. No deben trasladarse automáticamente especificaciones de otros módulos llamados TB6600.

Se distinguen tres tipos de información:

- **Serigrafía e imágenes:** conexiones, tablas DIP y cotas visibles en las imágenes I1–I7.
- **Declaración comercial:** información atribuida expresamente a Amazon.
- **Criterio de integración:** explicaciones, cálculos y procedimientos generales; no constituyen prestaciones medidas del driver.

## 2. Resumen de especificaciones visibles

| Característica | Dato | Fuente / alcance |
|---|---|---|
| Alimentación | 9–42 V CC | Serigrafía I2, I4, I7 |
| Regulación de corriente | Ocho combinaciones DIP | I2, I4 |
| Columna `Current(A)` | 0,5–3,5 A | No indica explícitamente RMS |
| Columna `PK Current` | 0,7–4,0 A | Pico, según rótulo |
| Subdivisiones | 1, 2/A, 2/B, 4, 8, 16 y 32 | Más una combinación `NC` |
| Pulsos por vuelta impresos | 200–6400 | Corresponden a motor de 1,8° |
| Entradas de mando | PUL±, DIR±, ENA± | Tres pares de terminales |
| Salidas al motor | A± y B± | Dos bobinas / fases |
| Configuración | Seis interruptores: SW1–SW6 | S1–S6 en las tablas |
| Indicador | PWR/ALARM | Códigos y colores no especificados |
| Construcción visible | Disipador con aletas, carcasa, bornes a tornillo y ranuras de montaje | I1–I7 |

**Los 4 A corresponden al máximo de la columna de pico. No equivalen a una capacidad comprobada de 4 A RMS continuos.**

## 3. Bornes y funciones

Orden de arriba hacia abajo en la **imagen I4**, con el texto derecho y los bornes a la derecha. Los números siguientes son referencias de este documento, no una numeración impresa en el equipo.

| Posición | Borne | Función |
|---|---|---|
| 1 | ENA− | Terminal negativo de la entrada de habilitación |
| 2 | ENA+ | Terminal positivo de la entrada de habilitación |
| 3 | DIR− | Terminal negativo de la entrada de dirección |
| 4 | DIR+ | Terminal positivo de la entrada de dirección |
| 5 | PUL− | Terminal negativo de la entrada de pulsos, también denominada STEP |
| 6 | PUL+ | Terminal positivo de la entrada de pulsos |
| 7 | B− | Un extremo de la bobina B |
| 8 | B+ | Otro extremo de la bobina B |
| 9 | A− | Un extremo de la bobina A |
| 10 | A+ | Otro extremo de la bobina A |
| 11 | GND | Negativo / retorno de la fuente de alimentación |
| 12 | VCC | Positivo de la fuente, 9–42 V CC |

Fuente: imágenes I2 e I4. En una fotografía girada, el orden visual se invierte: conectar siempre por el rótulo del borne.

### Definiciones de las señales

- **PUL / STEP:** cada pulso aceptado ordena un incremento de posición según la subdivisión seleccionada. La frecuencia de pulsos determina la velocidad solicitada.
- **DIR:** selecciona uno de los dos sentidos de giro. La correspondencia entre nivel lógico y giro horario depende del cableado y de la polaridad lógica usada.
- **ENA:** entrada que controla la habilitación de la etapa de potencia. Las fuentes no aclaran su polaridad activa ni el comportamiento al dejarla desconectada.
- **A y B:** bobinas del motor. Sus signos identifican extremos; no son entradas de alimentación.
- **GND:** retorno de potencia. No debe confundirse automáticamente con tierra de protección, chasis o referencia de las entradas optoaisladas.
- **PWR/ALARM:** indicación de alimentación/alarma. No se dispone de tabla de fallos o secuencias de parpadeo.

La marca `High Voltage` agrupa alimentación y salidas al motor. **No autoriza a conectar tensión de red:** la entrada indicada es exclusivamente 9–42 V CC.

## 4. Interruptores DIP: orientación y grupos

| Interruptores | Función |
|---|---|
| SW1, SW2, SW3 = S1, S2, S3 | Subdivisión / micropasos |
| SW4, SW5, SW6 = S4, S5, S6 | Corriente |

La imagen I4 indica que el desplazamiento hacia abajo en su recuadro es **ON**. La regla que vale independientemente de la orientación del equipo es mover el interruptor hacia el lado marcado **ON y su flecha**. La imagen I3 está girada respecto de ese recuadro.

Cambiar las posiciones con la alimentación desconectada y verificar la numeración grabada en el bloque rojo. Las posiciones fotografiadas no constituyen una configuración de fábrica garantizada.

# Motor paso a paso NEMA 17 iMetrx 42-60 — versión de 2,0 A

**Referencia comercial:** ASIN B0C36DP9XD.  
**Versión del usuario:** 2,0 A, confirmada expresamente en esta conversación.  
**Fecha:** 13 de octubre de 2026.  
**Alcance:** ficha técnica de consulta, conexiones y configuración con el driver DiGiYes documentado previamente. No sustituye una hoja de datos específica del fabricante.

## 1. Identificación y distinción de versiones

La [publicación de Amazon](https://www.amazon.com/dp/B0C36DP9XD) muestra seleccionada la variante **42-60, tres unidades**, con marca iMetrx y modelo comercial `IMETRX-42-60-3PCS`. El título y la descripción indican **2,0 A, dos fases, cuatro cables, paso de 1,8° y cable de 1 m**. El paquete anunciado contiene tres motores y tres cables.

**Este documento corresponde a la versión de 2,0 A confirmada por el usuario.**

La imagen adjunta presenta un plano 42-60, pero su leyenda eléctrica indica **1,7 A, 5,4 V y 850 mN·m**. Por ello, sus cotas se conservan como referencia mecánica suministrada, mientras que los valores eléctricos y de par no se trasladan automáticamente a la versión de 2,0 A.

| Dato | Estado para este documento |
|---|---|
| Corriente de la versión | **2,0 A**, confirmada por el usuario y coincidente con la ficha actual |
| Tamaño 42-60 | Coincide entre la publicación y el plano |
| Paso nominal 1,8°, dos fases, cuatro cables | Coincide entre la publicación y la imagen |
| Tensión 5,4 V | Figura en la imagen de 1,7 A; no confirmada para 2,0 A |
| Par 850 mN·m | Figura en la imagen de 1,7 A; no confirmado para 2,0 A |
| Tolerancia angular ±0,09° | Figura en la imagen; pendiente de confirmar para 2,0 A |
| Cotas, colores y posiciones de conector | Referencia del plano; contrastar con la unidad recibida |

La tabla general de Amazon también muestra **38 V** y un campo de potencia de **5,7 W**. No se adoptan como especificaciones eléctricas verificadas: no aclaran condiciones y no permiten resolver las discrepancias entre versiones. [Fuente: ficha actual de Amazon](https://www.amazon.com/dp/B0C36DP9XD).

## 2. Definiciones importantes

- **NEMA 17:** designación del tamaño de bastidor y montaje. No define por sí sola corriente, par, longitud del cuerpo ni potencia.
- **42-60:** identificación dimensional comercial; aproximadamente 42 mm de frente y 60 mm de longitud de cuerpo. El plano limita el frente a 42,3 mm.
- **Motor bipolar de dos fases:** tiene dos bobinas independientes y requiere que el driver invierta el sentido de la corriente en cada una.
- **Cuatro cables:** dos extremos por bobina, sin tomas centrales accesibles. No hay selección externa serie/paralelo como en ciertos motores de ocho cables.
- **Paso completo:** desplazamiento nominal de 1,8°; corresponde a **200 pasos por vuelta**.
- **Micropaso:** incremento eléctrico menor que un paso completo, generado por el driver. El motor no tiene interruptores DIP propios.
- **Corriente nominal:** el ajuste debe relacionarse con la corriente admisible del bobinado, habitualmente especificada por fase. La ficha disponible no detalla su criterio de medición RMS/pico ni las condiciones térmicas.
- **Par de retención:** par resistido por el motor detenido y energizado en condiciones definidas. No equivale al par disponible mientras gira ni al par sin alimentación.
- **Lazo abierto:** el controlador cuenta los pulsos enviados; sin realimentación no sabe automáticamente si el motor perdió pasos. No se documenta encoder en este producto.

## 3. Dimensiones del plano suministrado

Todas las cotas siguientes están en **milímetros**. Son transcripciones o interpretaciones identificadas del plano adjunto; verificar la unidad de 2,0 A antes de mecanizar piezas definitivas.

| Elemento | Cota | Observación |
|---|---:|---|
| Frente del motor | 42,3 máx. × 42,3 máx. | Vista frontal |
| Longitud del cuerpo | 60 máx. | No incluye la salida del eje |
| Distancia entre centros de fijación | 31 ±0,2 en ambos ejes | Patrón cuadrado |
| Agujeros de montaje | 4 × M3 | Roscas en la cara frontal |
| Profundidad roscada indicada | 3 mín. | `DEEP3MIN`; no define profundidad máxima segura |
| Diámetro del resalte de centrado | Ø22, tolerancia +0/−0,052 | Intervalo 21,948–22,000 |
| Proyección del resalte | 2 | Respecto de la cara frontal |
| Diámetro del eje | Ø5, tolerancia +0/−0,012 | Intervalo 4,988–5,000 |
| Salida del eje desde la cara frontal | 22 ±0,5 | Según las referencias de la vista lateral |
| Longitud indicada del tramo con plano | 18 ±0,5 | Desde la punta del eje |
| Medida del eje hasta el plano | 4,5 | Sección P–P: del plano al lado circular opuesto |
| Ancho indicado del conector | 17 | Vista frontal |
| Proyección inferior del conector | 6 | Vista lateral |
| Cota axial del conector hacia la cara trasera | 11 | Confirmar referencias en la pieza real |

### Consecuencias para el montaje

El eje es de tipo **D**: diámetro nominal de 5 mm con un plano. La medida de 4,5 mm implica, geométricamente, un rebaje nominal de 0,5 mm respecto del diámetro completo; no indica la profundidad de una chaveta.

La longitud nominal cuerpo más salida del eje resulta aproximadamente **82 mm**, sin margen para acople, cableado o tolerancias. Es una suma orientativa, no una cota total garantizada.

Para un soporte, contemplar el patrón 31 × 31 mm, el resalte de Ø22 y la salida inferior del conector. La cota `M3, profundidad mínima 3 mm` no autoriza a introducir un tornillo de cualquier longitud: elegir el largo en función del espesor del soporte y la profundidad disponible medida.

No se especifican cargas radiales o axiales admisibles del eje. Evitar que el motor actúe como soporte estructural del telescopio y no montar poleas o acoples a golpes.

## 4. Bobinados, colores y conector

El esquema de la imagen identifica las bobinas como **A–C** y **B–D**. Estas letras son los extremos del motor y **no equivalen directamente a las letras de fase del driver**.

| Extremo en el plano | Color / abreviatura | Posición dibujada del conector | Bobina |
|---|---|---:|---|
| A | Negro / BLK | 1 | A–C |
| B | Azul / BLU | 3 | B–D |
| C | Verde / GRN | 4 | A–C |
| D | Rojo / RED | 6 | B–D |
| Sin asignación en el plano | — | 2 y 5 | No usar sin verificar |

```text
Bobina 1: negro (A, posición 1) ─── bobinado ─── verde (C, posición 4)
Bobina 2: azul  (B, posición 3) ─── bobinado ─── rojo  (D, posición 6)
```

La numeración corresponde a la **vista del plano**. Vista desde el lado opuesto del conector puede parecer invertida. No se identifica fabricante, serie ni paso entre contactos; no se presupone un modelo de conector JST concreto.

Un cable adaptador puede cambiar el orden en su extremo libre. Antes de conectar la versión recibida, medir continuidad con el motor completamente separado del driver:

1. Negro y verde deberían formar un par con resistencia finita.
2. Azul y rojo deberían formar el otro par.
3. Entre pares independientes no debería existir continuidad.
4. Si el resultado no coincide, usar la identificación real de bobinas y revisar el cable; no forzar el mapa de colores de la imagen.

Un multímetro permite identificar bobinas, pero no certifica el aislamiento eléctrico completo del motor.

## 5. Conexión con el driver DiGiYes TB6600

Referencia: [documento técnico del driver](/Users/nestor/.codex/.chatgpt-projects/g-p-6abaaf5686a08191bcb6bee0aa6d1d4d/entregables/Driver_TB6600_DiGiYes_B0CHRWD54S.md).

La interfaz de dos bobinas independientes es compatible en principio con las salidas A±/B± del driver. La configuración final depende de confirmar los límites eléctricos y térmicos de la unidad.

**Asignación propuesta, sujeta a la comprobación de continuidad anterior:**

| Cable del motor | Borne del driver |
|---|---|
| Negro, extremo A del plano | A+ |
| Verde, extremo C del plano | A− |
| Azul, extremo B del plano | B+ |
| Rojo, extremo D del plano | B− |

```text
Controlador STEP/DIR/ENA ── entradas PUL/DIR/ENA del driver
Fuente CC ──────────────── VCC/GND del driver

Driver A+ ── negro       Driver B+ ── azul
Driver A− ── verde       Driver B− ── rojo
```

**Los cables del motor no se conectan a VCC, GND ni directamente a un GPIO.** El driver conmuta y regula la corriente en las bobinas.

Para cambiar el sentido, invertir DIR en el controlador. Otra posibilidad es intercambiar los extremos de una sola bobina con todo desenergizado. No conectar o desconectar el motor mientras el driver está alimentado.

## 6. Ajuste de corriente para la versión de 2,0 A

La corriente confirmada del motor es **2,0 A**. Sin embargo, en el driver documentado existen dos columnas: `Current(A)` y `PK Current`. La primera no está identificada explícitamente como RMS y sus números no mantienen una relación fija de √2 con la segunda.

Por ello, **no se puede asegurar que seleccionar la fila “2,0” sea equivalente a aplicar la corriente nominal correcta al motor**.

| Uso / interpretación | Current(A) del driver | Pico indicado | SW4 | SW5 | SW6 |
|---|---:|---:|---|---|---|
| Prueba inicial con menor corriente | 1,0 A | 1,2 A | ON | OFF | ON |
| Ajuste provisional conservador para este motor | 1,5 A | 1,7 A | ON | ON | OFF |
| Fila rotulada 2,0 A: pendiente de confirmar equivalencia | 2,0 A | 2,2 A | ON | OFF | OFF |

La fila de **1,5 / 1,7 A** mantiene el pico declarado por debajo de 2,0 A y es una referencia provisional prudente. Puede reducir el par disponible; no garantiza alcanzar el par nominal del motor. Se puede comenzar más bajo si la carga de prueba lo permite.

Para aprovechar la corriente nominal completa, confirmar cómo define el fabricante la corriente del motor y cómo regula la corriente esta revisión del driver. No elevar el ajuste sólo porque el motor no se mueva: primero revisar bobinas, habilitación, pulsos, aceleración y carga.

## 7. Micropasos y configuración del controlador

Con el paso nominal de 1,8° se obtienen **200 pasos completos por vuelta**. La siguiente tabla usa las combinaciones del driver ya documentado; no son configuraciones internas del motor.

| Modo | Micropasos por paso | Pulsos/vuelta | Incremento ideal del motor | SW1 | SW2 | SW3 |
|---|---:|---:|---:|---|---|---|
| Paso completo | 1 | 200 | 1,8° | ON | ON | OFF |
| 2/A | 2 | 400 | 0,9° | ON | OFF | ON |
| 2/B | 2 | 400 | 0,9° | OFF | ON | ON |
| 1/4 | 4 | 800 | 0,45° | ON | OFF | OFF |
| 1/8 | 8 | 1600 | 0,225° | OFF | ON | OFF |
| 1/16 | 16 | 3200 | 0,1125° | OFF | OFF | ON |
| 1/32 | 32 | 6400 | 0,05625° | OFF | OFF | OFF |

Evitar SW1/SW2/SW3 = ON/ON/ON, rotulado `NC`. No está documentada la diferencia funcional entre 2/A y 2/B.

### Ejemplo de configuración conjunta

Para una prueba en **1/16 de paso** y con la fila conservadora de corriente **1,5 A / pico 1,7 A**:

| SW1 | SW2 | SW3 | SW4 | SW5 | SW6 |
|---|---|---|---|---|---|
| OFF | OFF | ON | ON | ON | OFF |

Configurar el controlador para **3200 pulsos por vuelta del motor**. Este ejemplo permite comprobar escala y movimiento; no constituye una selección definitiva para el seguimiento del telescopio.

Cambiar los DIP con el equipo apagado. La posición ON se identifica por la marca y flecha del propio bloque, no por una dirección universal “arriba/abajo”.

## 8. Alimentación, velocidad y temperatura

El driver anterior admite **9–42 V CC** según su serigrafía. Ese rango corresponde a la **fuente del driver**, no a una tensión que deba aplicarse directamente a las bobinas del motor.

Con regulación de corriente, la alimentación del driver puede superar la tensión resistiva nominal del bobinado. La corriente debe seguir limitada correctamente. La cifra de 5,4 V de la imagen pertenece a la variante rotulada 1,7 A y no se adopta para este motor de 2,0 A.

No se conoce la resistencia ni la inductancia de la versión de 2,0 A. Por tanto:

- No se calcula su resistencia como 5,4 V / 2,0 A, porque mezclaría datos de variantes diferentes.
- No se deduce el consumo de la fuente multiplicando automáticamente 2,0 A por dos fases.
- No se fija una velocidad máxima o aceleración sin ensayar motor, fuente, driver y carga.
- No se establece una temperatura admisible a partir de la sensación al tacto; hace falta el límite del fabricante y una medición apropiada.

El par disponible normalmente disminuye con la velocidad y depende del sistema de excitación. Los arranques deben usar rampas; una frecuencia final aceptable no implica que el motor pueda arrancar instantáneamente a esa frecuencia.

## 9. Par y tolerancia angular de la imagen

La leyenda del plano indica **850 mN·m**, equivalente a:

```text
850 mN·m = 0,85 N·m = 85 N·cm ≈ 8,67 kgf·cm
```

Es una conversión del dato de la imagen, **no una confirmación del par del motor de 2,0 A**. La imagen tampoco identifica expresamente las condiciones de medición ni si corresponde al par de retención. No usarlo como par garantizado de marcha para dimensionar la transmisión.

El valor **1,8° ±0,09°** de la imagen equivale a una tolerancia de ±5 % del paso nominal. No debe interpretarse como error acumulado garantizado por vuelta ni como exactitud de cada micropaso. Su aplicación a la versión de 2,0 A también queda pendiente.

## 10. Cálculos útiles para el telescopio

Relaciones cinemáticas generales, suponiendo que el motor sigue todos los pulsos:

```text
M = subdivisión seleccionada
P = 200 × M                         [pulsos/vuelta del motor]
rpm_motor = 60 × f / P              [f en Hz]
f = rpm_motor × P / 60
```

Por ejemplo, con 1/16 de paso, P = 3200. Una frecuencia de 1000 Hz solicita **18,75 rpm**.

Si R es la cantidad de vueltas del motor necesarias para una vuelta del eje del telescopio:

```text
P_eje = 200 × M × R
Resolución ideal = 1 296 000 / P_eje       [segundos de arco/pulso]
f = velocidad_eje × P_eje / 360           [velocidad_eje en grados/s]
```

La resolución ideal no incluye holgura, flexión, excentricidad, error periódico ni error de micropaso. El tamaño NEMA y la corriente no bastan para determinar si el conjunto moverá un telescopio de 10 pulgadas: hacen falta relación de reducción, balance, fricción, inercia y velocidad requerida.

## 11. Verificación inicial

1. Comprobar identificación de la unidad de **2,0 A**, largo de cuerpo y conector.
2. Identificar por continuidad las dos bobinas y el orden del cable recibido.
3. Medir cotas críticas para soporte, centrado y acople.
4. Con todo apagado, conectar motor, fuente y señales al driver.
5. Seleccionar corriente provisional y un modo válido de micropasos.
6. Probar a baja velocidad con rampa y sin carga que pueda caer al deshabilitarse.
7. Ordenar una vuelta con 200 × M pulsos; verificar desplazamiento y sentido.
8. Evaluar temperatura, pérdida de pasos y margen de par bajo la carga real.

| Síntoma | Comprobaciones prioritarias |
|---|---|
| Vibra sin girar | Pares mezclados, cable abierto, frecuencia inicial excesiva |
| Gira al revés | Lógica DIR o extremos de una bobina |
| Gira una fracción incorrecta | Micropasos, pulsos programados, relación de reducción |
| Pierde posición | Aceleración, balance, fricción, corriente y ruido de señales |
| Calienta demasiado | Ajuste de corriente, excitación en reposo, montaje y temperatura ambiente |
| No sostiene posición al apagar | Comportamiento esperable sin par electromagnético; evaluar freno o soporte mecánico |

## 12. Información que falta de la versión de 2,0 A

| Parámetro | Estado |
|---|---|
| Tensión resistiva nominal por fase | No confirmada |
| Resistencia e inductancia por fase | No disponibles |
| Criterio exacto de corriente y condiciones térmicas | No detallados |
| Par de retención y curva par–velocidad | No confirmados |
| Inercia del rotor y par de detención sin corriente | No disponibles |
| Temperatura máxima, clase de aislamiento y servicio permitido | No disponibles |
| Cargas radial/axial y vida de rodamientos | No cuantificadas |
| Masa, tolerancias de montaje completas y apriete | No confirmados |
| Modelo del conector y sección del cable | No disponibles |

## 13. Registro de la instalación

| Parámetro | Valor |
|---|---|
| Motor | iMetrx NEMA 17, 42-60 |
| Versión de corriente | **2,0 A, confirmada por el usuario** |
| Paso nominal | 1,8°; 200 pasos/vuelta |
| Eje del telescopio | Por completar |
| Pares de bobinas medidos | Por completar |
| Resistencia de cada bobina | Por completar |
| Fuente del driver | Por completar |
| Fila de corriente seleccionada, Current / PK | Por completar |
| Micropasos y pulsos/vuelta | Por completar |
| SW1–SW6 | Por completar |
| Relación de reducción | Por completar |
| Velocidad y aceleración verificadas | Por completar |

## 14. Fuentes

- [Amazon — iMetrx NEMA 17, ASIN B0C36DP9XD](https://www.amazon.com/dp/B0C36DP9XD). Consultado en el navegador el 13 de octubre de 2026; variante visible 42-60, tres unidades, 2,0 A.
- **Confirmación del usuario:** su versión es de 2,0 A; prevalece sobre la leyenda de 1,7 A de la imagen.
- **Imagen proporcionada:** [plano original 6130aWQzx3L._SL1200_.jpg](/Users/nestor/Downloads/NEMA/6130aWQzx3L._SL1200_.jpg). Fuente de cotas y esquema de bobinas; original sin modificar.
- **Driver asociado:** [DiGiYes TB6600, documento técnico previo](/Users/nestor/.codex/.chatgpt-projects/g-p-6abaaf5686a08191bcb6bee0aa6d1d4d/entregables/Driver_TB6600_DiGiYes_B0CHRWD54S.md). Tablas de corriente y micropasos tomadas de las imágenes del driver aportadas en esta conversación.

Las ecuaciones y procedimientos son criterios generales de integración. Los datos no confirmados de la variante de 1,7 A se conservan únicamente para identificar la discrepancia, sin tratarlos como prestaciones de la unidad de 2,0 A.
