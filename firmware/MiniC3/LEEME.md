# Mini C3 — firmware v1.11.0

Firmware para la Mini C3, adaptado de **Tinytosh** de Vladimir Gitsarev (licencia MIT, ver `LICENSE`).

Hardware:
- Waveshare ESP32-C3-Zero.
- OLED 1.3" SH1106.
- 4 flechas.
- Táctil TTP223 como botón A.
- Batería opcional en GPIO 0.

Pantallas: hora, Spotify, **juegos (Arkanoid, truco contra la compu, Tetris, Flappy, Viborita, 2048, Buscaminas, Invasores y Simón)**, **anotador de truco**, calendario, clima, luz del día, luna, ISS, acciones, cripto, monedas, noticias, mensajes, monitor de PC e impresora Bambu (calidad del aire y población se quitaron en v1.4.1; vuelos en v1.11.0). Se configuran desde el panel web.

## Al prender

Presentación de 4,5 s, también al prenderla después de apagarla con ↑: pasa una luz, se dibuja la consolita, gira una vuelta, se le prende la pantalla y aparece "MINI C3 / FINELLI".

Si **tocás A durante la presentación** (al enchufarla o prenderla con el interruptor), abre la red MiniC3-Setup para agregar un WiFi. Ver [WiFi](#wifi).

## Controles

| Tecla | Qué hace |
|---|---|
| ↑ | Pantalla siguiente |
| ↓ | Pantalla anterior |
| → | **Más datos** de la pantalla (fichas con el detalle; después de la última vuelve a la pantalla) |
| ← | Siguiente acción / cripto / moneda / noticia (vuelve a empezar al final). En las demás pantallas, cierra la ficha. |
| A (toque) | En juegos arranca el juego. En el resto abre el **panel del equipo** (ver abajo). |
| A (mantener 0.7 s) | Activa o desactiva el auto-ciclo de pantallas |
| ↑ (mantener 3 s) | **Apaga** (suspende). Aparece una barra; si soltás antes, se cancela. |
| ← o → | Con la Mini C3 apagada, **la prenden** donde estaba. (A y ↓ no la prenden, así no se activa sola en el bolsillo.) |

- De noche, si la pantalla está apagada, el primer toque solo la despierta.
- El táctil se ignora mientras hay una flecha apretada y durante 0.2 s después, así no se dispara si lo rozás con el pulgar. (En los juegos no: ahí A responde aunque tengas una flecha apretada.)

## Panel del equipo

Tocá **A** en cualquier pantalla donde A no tenga otro uso (hora, clima, calendario, etc.).

| Tecla | Qué hace |
|---|---|
| ← / → | Cambian de página: **Red · Equipo · Servicios · Acciones** |
| ↑ / ↓ | Bajan por la lista de servicios / eligen una acción |
| A | En Acciones ejecuta la opción. En las otras páginas cierra. |
| A mantenida | Cierra |

Se cierra sola a los 20 s sin tocar nada.

- **Red:** a qué WiFi está conectada, señal, IP, minic3.local y si la OTA está lista.
- **Equipo:** versión, batería, cuánto hace que está prendida, memoria libre y auto-ciclo.
- **Servicios:** cada dato con su estado: ✓ ok (y hace cuánto se actualizó), ✗ error / sin datos, … esperando, » salteado.
- **Acciones:**
  - **Recargar datos:** vuelve a cargar todo en el momento, incluido lo que se había salteado. Sin reiniciar.
  - **Reiniciar:** pide tocar A de nuevo para confirmar. Después del reinicio prueba otra vez todos los servicios, también los que se habían salteado.
  - **Agregar WiFi:** también pide confirmar. Reinicia y abre la red MiniC3-Setup.
  - **Apagar:** lo mismo que mantener ↑ 3 s.
  - **Cerrar.**

## Spotify (segunda pantalla)

- Muestra qué está sonando en tu cuenta (en cualquier dispositivo): tapa del disco en 1 bit, título y artista que se desplazan si son largos, álbum, barra de progreso que avanza sola, tiempos, aleatorio/repetir y un ecualizador animado. Sin tapa aparece un vinilo que gira.
- Controles en esa pantalla: **A** play/pausa · **←/→** tema anterior/siguiente (con animación) · **A mantenida** abre el volumen y se cambia con **↑/↓** (A o 3 s sin tocar para cerrar). ↑/↓ fuera del volumen siguen cambiando de pantalla.
- Cuando cambia el tema, el nuevo entra deslizándose.
- **Vincular (una vez):** http://minic3.local/spotify — ahí están los 3 pasos (crear la app en developer.spotify.com con la Redirect URI `http://127.0.0.1:8888/callback`, pegar el Client ID, autorizar y pegar la dirección a la que te lleva). No se guarda ninguna contraseña: usa PKCE y un token de refresco.
- Requiere **Spotify Premium** (Spotify lo exige desde febrero de 2026 para las apps en modo desarrollo, y siempre lo exigió para controlar la reproducción).
- Mientras la pantalla está abierta consulta cada 2,5 s; fuera de ella, cada 1 minuto.

## Juegos (tercera pantalla)

El menú muestra un juego por vez con una demo animada. **← / →** cambian de juego (Arkanoid · Truco · Tetris · Flappy · Viborita · 2048 · Buscaminas · Invasores · Simón) y **A** lo arranca. Arriba a la derecha dice cuál de los 9 estás viendo, y abajo el récord de cada uno.

**Arkanoid**
| Tecla | Qué hace |
|---|---|
| ← / → | Mueven la paleta (acelera si la mantenés apretada) |
| A | Lanza la pelota. Jugando, pausa. |
| ↓ | Pausa |
| En la pausa | ↑ / ↓ eligen **Seguir** o **Salir**, A confirma |
| ↑ (mantener 3 s) | Apaga igual que siempre (el juego queda en pausa) |

- 6 niveles: Clásico, Pirámide, Damero, Corazón, Invasor y Fortaleza. Después se repiten más rápido y valen el doble.
- Ladrillos llenos se rompen de un golpe; los punteados necesitan dos.
- La pelota sale con más ángulo cuanto más cerca del borde de la paleta pega, y se acelera de a poco.
- Al romper ladrillos a veces cae una cápsula. Agarrala con la paleta:
  - **|=|** paleta ancha (15 s)
  - **▼** pelota lenta
  - **···** tres pelotas
  - **♥** vida extra (hasta 5)
- Empezás con 3 vidas. Al pasar de nivel sumás un bonus por nivel y por vidas.
- La luz de la placa acompaña: destello al romper, azul en ladrillos duros, rojo al perder una vida, amarilla al pasar de nivel.
- El **récord** queda guardado (también el nivel más alto). Lo ves en el menú.
- Si llega un mensaje mientras jugás, el juego se pausa; volvé a Juegos y tocá A para seguir.
- Mientras jugás no corre el auto-ciclo ni se apaga la pantalla de noche. Los datos se siguen actualizando de fondo.
- A la pantalla solo se manda lo que cambia en cada cuadro, así va a unos 60 cuadros por segundo.

**Tetris**

Pozo de 10 × 16 (la pantalla mide 64 px de alto, 4 px por bloque), así entra entero sin girar la consola.

| Tecla | Qué hace |
|---|---|
| ← / → | Mueven la pieza (si la mantenés, se repite) |
| A | Gira la pieza |
| ↑ | La tira de golpe |
| ↓ | La baja más rápido |
| A mantenida | Pausa (↑ ↓ eligen Seguir o Salir, A confirma) |
| Al terminar | A juega otra vez · ← vuelve al menú |

- Los puntitos marcan dónde va a caer la pieza. A la derecha, la que sigue y el récord.
- Las 7 piezas salen de a una por "bolsa" (no se repite la misma muchas veces seguidas).
- Cada 10 líneas sube el nivel y cae más rápido. Puntos: 100 / 300 / 500 / 800 por 1 a 4 líneas, por el nivel; más 1 por fila bajando con ↓ y 2 por fila al tirarla con ↑.
- Las líneas completas titilan antes de irse; con 4 de una vez el LED se prende amarillo.

**Flappy**

Un pajarito propio que vuela entre columnas de ladrillos.

| Tecla | Qué hace |
|---|---|
| A o ↑ | Aletea |
| ↓ | Pausa (↑ ↓ eligen, A confirma) |
| Al caerte | A juega otra vez · ← vuelve al menú |

- Cada columna que pasás suma 1. Con los puntos va un poco más rápido y el hueco se achica.
- Si chocás, el pajarito se cae con plumitas y el LED se pone rojo. El récord queda guardado.

**Viborita**

| Tecla | Qué hace |
|---|---|
| Flechas | La doblan (recuerda hasta 2 giros si apretás rápido; no se puede dar media vuelta) |
| A | Pausa (↑ ↓ eligen Seguir o Salir, A confirma) |
| Al chocar | A juega otra vez · ← vuelve al menú |

- Arranca quieta: tocá una flecha para salir.
- Cada manzana suma 1 y la alarga. Cuanto más larga, más rápido va.
- Cada 5 manzanas aparece una **manzana bonus** que titila 5 segundos: vale 5 más los segundos que le quedaban.
- Chocar contra el borde o contra ella misma termina la partida.

**2048**

| Tecla | Qué hace |
|---|---|
| Flechas | Deslizan todas las fichas para ese lado |
| A | Pausa |
| Sin movidas | A juega otra vez · ← vuelve al menú |

- Dos fichas iguales que chocan se juntan en una del doble y suman esos puntos. Después de cada movida aparece un 2 (a veces un 4).
- Las fichas de 8 en adelante se ven rellenas; desde 128 tienen un borde adentro. 1024 y 2048 se ven como "1k" y "2k".
- Al llegar a 2048 avisa, y podés seguir jugando. Termina cuando no queda ninguna movida.

**Buscaminas** (16 × 7, 16 minas)

| Tecla | Qué hace |
|---|---|
| Flechas | Mueven el cursor (si las mantenés, se repite; al llegar al borde sigue del otro lado) |
| A (toque) | Abre la casilla. Sobre un número que ya tiene todas sus banderas, abre las de alrededor. |
| A mantenida (medio segundo) | Pone o saca una bandera |
| A mantenida 1,5 s | Pausa (la bandera que se puso al pasar se saca sola) |

- La primera casilla que abrís nunca tiene mina (ni las de alrededor), así siempre arrancás con un hueco.
- Arriba: minas que faltan marcar y el tiempo. El récord es el **mejor tiempo** ganando.

**Invasores** (dibujos propios)

| Tecla | Qué hace |
|---|---|
| ← / → | Mueven la nave |
| A o ↑ | Disparan (un tiro por vez) |
| ↓ | Pausa |
| Al perder | A juega otra vez · ← vuelve al menú |

- 4 filas de 7 marcianos de 3 tipos (10, 20 y 30 puntos). Cuantos menos quedan, más rápido marchan. Limpiar una oleada suma 100.
- 3 refugios que se gastan con los tiros, los tuyos y los de ellos.
- A veces cruza un platillo por arriba: vale entre 50 y 150.
- 3 vidas. Si los marcianos llegan abajo, se termina. Cada oleada nueva arranca un poco más abajo.

**Simón**

| Tecla | Qué hace |
|---|---|
| Flechas | Repetís la secuencia |
| A | Pausa (al seguir, te la vuelve a mostrar) |
| Al equivocarte | A juega otra vez · ← vuelve al menú |

- Cada flecha se prende en la pantalla y en el **LED con su color**: ↑ verde, → rojo, ↓ amarillo, ← azul.
- Cada ronda suma una más y va un poco más rápido. El récord es la secuencia más larga que completaste.

**Truco contra la compu** (1 contra 1, sin flor, a 15)

| Tecla | Qué hace |
|---|---|
| ← / → | Eligen la carta (la elegida sube un poco) |
| A | Tira la carta |
| ↓ | Abre los cantos: Truco / Retruco / Vale 4, Envido, Real envido, Falta envido, Al mazo, Abandonar |
| En los menús | Las flechas se mueven, A elige. ↑ en la fila de arriba cierra el de cantar. |
| A durante un cartel | Lo saltea |
| En el menú de Juegos, A mantenida | Cambia el nivel: **Normal** o **Difícil** |

- Pantalla: arriba los puntos (el cuadradito marca quién es mano) y el truco en juego. A la izquierda las bazas ya jugadas: la carta de la CPU, la tuya, y la ganadora resaltada. En el medio la mesa (arriba la CPU, abajo vos; el recuadro punteado marca a quién le toca). Arriba a la derecha las cartas que le quedan a la CPU. Abajo tu mano y tus tantos ("tant") mientras se puede cantar envido.
- Cuando la CPU canta, aparece el cartel y después tus respuestas: Quiero, No quiero, subir (Retruco, Vale 4, Envido, Real, Falta). Si te cantan truco en la primera, podés contestar con envido: "el envido está primero".
- Reglas: orden de cartas clásico (ancho de espada, de basto, 7 de espada, 7 de oro, treses...). Pardas: si se empata la primera gana la segunda; si después de ganar la primera se empata, gana el de la primera; todo parda gana el mano. Envido, envido-envido, real y falta (la falta vale lo que le falta al que va ganando para llegar a 15). Tantos iguales: gana el mano. Truco, retruco y vale cuatro: solo puede subir el que dijo quiero. Irse al mazo le da al otro lo que valga la mano.
- Al final de cada mano muestra qué cartas tenía la CPU (y sus tantos si hubo envido), así ves cuándo te mintió.
- **La compu:** antes de cada jugada imagina muchas veces qué cartas podrías tener vos (solo las que no vio, y si dijiste tus tantos, solo manos con esos tantos) y juega la mano hasta el final para ver con qué carta le va mejor y si le conviene cantar o querer. El envido lo decide por sus tantos y por si es mano.
- **Miente:** a veces canta truco o envido sin nada, sube sin cartas, o se queda callada con una mano bárbara para que cantes vos y contestarte retruco.
- **Normal** comete errores y miente menos; **Difícil** piensa con más casos.
- Partidas ganadas y perdidas quedan guardadas (se ven en el menú y al final). Abandonar no cuenta.

## Truco (cuarta pantalla)

Anotador con fósforos: cuadraditos de 5, las malas arriba y las buenas abajo de la línea punteada, y el número grande abajo.

| Tecla | Qué hace |
|---|---|
| ← | +1 para **ellos** (columna izquierda) |
| → | +1 para **nosotros** (columna derecha) |
| ← / → mantenida 0,6 s | Borra un tanto de ese lado |
| A mantenida | Nueva partida: pregunta, y A la confirma (otra tecla cancela) |
| A con 0 a 0 | Cambia entre partida a 30 y a 15 |
| ↑ / ↓ | Otras pantallas, como siempre |

- En esta pantalla las flechas ← → suman **al soltarlas**, así se puede distinguir el toque de mantenerla.
- Cada fósforo se dibuja con una animación y el LED da un destello (rojo al borrar).
- Al pasar a buenas avisa ("Ellos en buenas" / "Entramos en buenas").
- Al llegar a 30 (o 15): "GANAMOS!" o "GANARON ELLOS", titila y el LED festeja en amarillo. El nombre del ganador queda resaltado. Si fue un error, mantener la flecha deshace el último tanto y la partida sigue.
- Los puntos quedan guardados: podés apagarla o reiniciarla y siguen ahí.
- Con una partida en curso el auto-ciclo no se va de esta pantalla.

## Mensajes

- Desde cualquier celular o compu en tu WiFi: **http://minic3.local/mensaje** (también hay un botón en el panel). Escribís tu nombre y el mensaje (hasta 500 letras) y tocás Enviar.
- En la Mini C3: animación de un sobre que se abre ("Te llego un mensaje!") y se abre el mensaje.
- **La luz de la placa titila** (dos destellos blancos) hasta que tocás cualquier tecla. Esa primera tecla solo apaga la luz (y si no estabas en mensajes, te lleva al mensaje).
- En la página que lo mandó aparece "✓ Entregado" y, cuando se lee en la Mini C3, "✓✓ Leído" (si lo borrás también figura leído).
- Para mandarlo desde un atajo del celular: `POST http://minic3.local/mensaje` con los campos `de` y `texto` (formulario).
- Con la Mini C3 apagada (↑ 3 s) o jugando (WiFi apagado) los mensajes no llegan.

**Bandeja de entrada** (se guardan los últimos **8**, sobreviven a un reinicio)

| Dónde | Tecla | Qué hace |
|---|---|---|
| Lista | ← / → | Eligen el mensaje (← sube, → baja) |
| Lista | A | Abre el mensaje |
| Lista | ↑ / ↓ | Otras pantallas, como siempre |
| Mensaje abierto | (solo) | El texto sube solo si es largo; **mantener A lo pausa** |
| Mensaje abierto | ↑ / ↓ | Lo subís o bajás a mano |
| Mensaje abierto | ← | Vuelve a la lista |
| Mensaje abierto | A o → | **Opciones** |
| Opciones | ↑ / ↓ y A | **Volver**, **Borrar** (ese mensaje) o **Borrar todos** (pide A de nuevo para confirmar). ← cancela. |

- En la lista cada mensaje muestra de quién es, la hora y el comienzo del texto; el puntito marca los que no abriste.
- Al abrir o volver, la pantalla se desliza. Al borrar aparece "Mensaje borrado".
- Mientras leés un mensaje el auto-ciclo no se va de la pantalla.

## Noticias

- Titulares de Argentina de Google Noticias, con Clarín y La Nación de respaldo. Se actualizan cada 20 minutos.
- Todas las noticias van una debajo de la otra (fuente subrayada y el titular) y **suben solas**, sin parar: después de la última vuelve a aparecer la primera. Arranca a los 0,5 s.
- **Mantener A pausa el texto** (arriba dice PAUSA); al soltarla sigue. En noticias, A no abre la info ni cambia el auto-ciclo.
- ← salta al comienzo de la noticia siguiente. → muestra la ficha de la noticia de arriba (fuente y hora).
- El auto-ciclo espera a que pasen todas las noticias una vez.

## Si una carga de datos se traba

- Durante la carga inicial, abajo dice "-> o A: saltar". Con → o A se saltea el paso que está cargando.
- Si el paso no termina en 4 segundos, la placa se reinicia sin ese paso.
- Si un paso tarda más de 45 segundos, pasa lo mismo solo, también en las actualizaciones automáticas.
- Lo salteado vuelve a cargarse al apagar y prender con el interruptor, al guardar en el panel web, o desde el panel del equipo (A → Acciones → Recargar datos o Reiniciar).
- El panel web y la OTA arrancan **antes** de cargar los datos, así que siempre se puede actualizar el firmware.

## Ahorro de batería al jugar

El WiFi es lo que más batería gasta, y los juegos y el anotador no lo necesitan.

- **Juegos:** al arrancar cualquier partida el WiFi se apaga. Al salir al menú de Juegos vuelve solo y en unos segundos se reconecta.
- **Anotador de truco:** después de **1 minuto** en esa pantalla el WiFi se apaga (aparece "WiFi apagado: ahorro"). Al cambiar de pantalla vuelve solo.
- Si en ese momento se estaba actualizando algún dato, espera a que termine para no dejarlo trabado.
- Mientras está apagado no llegan mensajes, no se actualizan los datos ni Spotify, y no anda la OTA ni el panel web. Al volver el WiFi se pone al día solo.
- El **Bluetooth** este firmware no lo prende nunca, así que no gasta nada.

## Apagado (suspensión)

- Mantené ↑ 3 segundos: se apagan la pantalla, el WiFi y el LED. Lo que tenía en memoria queda guardado.
- Solo **← o →** la prenden, al instante y en la misma pantalla. El táctil y ↓ no la despiertan, así se puede llevar en el bolsillo. El WiFi vuelve solo en unos segundos y después se actualizan los datos.
- Es *light sleep* y no *deep sleep* porque el ESP32-C3 solo puede despertar de deep sleep con los GPIO 0 a 5: → (GPIO 6) no podría prenderla.
- La placa sigue consumiendo un poco: el LED RGB y el regulador de la C3-Zero gastan más que el chip dormido. Para guardarla varios días, usá el interruptor.

## Primera carga (por USB, una sola vez)

**Antes de enchufar el USB-C, apagá el interruptor de la batería.**

Hay dos opciones.

**A. Con PlatformIO**
1. En VS Code, abrí esta carpeta con la extensión PlatformIO.
2. Elegí el entorno `minic3` y tocá **Upload**.
   - Por consola: `pio run -e minic3 -t upload`.
   - Las librerías ya vienen en `lib/`, no hace falta descargar nada.

**B. Sin instalar nada (navegador Chrome o Edge)**
1. Entrá a https://espressif.github.io/esptool-js/ y conectá la placa.
2. Cargá `binarios/firmware.factory.bin` en la dirección **0x0** y programá.

Si la placa no aparece, ponela en modo descarga: mantené **BOOT**, tocá **RESET** y soltá BOOT.

## WiFi

**Cambiar de WiFi no borra nada.** Las redes se guardan aparte de la configuración: pantallas, Spotify, mensajes y récords quedan como estaban.

- Guarda hasta **5 redes**. Al prender se conecta a la guardada con mejor señal.
- Si se corta el WiFi, a los 15 s busca otra red de la lista y se pasa sola (sirve para llevarla de tu casa a la oficina).
- La red que tenías de antes se importa sola a la lista la primera vez.

**Agregar una red, opción 1: desde el panel**
1. Abrí http://minic3.local/wifi (usuario `admin`, clave `minic3`, la misma de la OTA).
2. Tocá "Buscar redes cercanas", elegí una, poné la clave y "Guardar red".
3. Podés cargar la red de otro lugar antes de llevarla: cuando la tenga cerca se conecta sola. Con "Conectarse ahora" se pasa en el momento; si la clave está mal, vuelve sola a una red conocida.
4. Ahí mismo ves a cuál está conectada, y podés **Conectar** u **Olvidar** cada una.

**Agregar una red, opción 2: sin otra red a mano**
1. Prendé la Mini C3 y **tocá A mientras está la presentación**.
2. Aparece la red **MiniC3-Setup** (clave `minic3setup`). Conectate desde el celular y abrí `192.168.4.1`.
3. Elegí el WiFi y poné la clave. Se suma a la lista.

- Si al prender no encuentra ninguna red guardada, abre MiniC3-Setup sola durante 5 minutos. Después sigue sin WiFi y, si tenés redes guardadas, las sigue buscando de fondo.
- El panel del equipo (tocá A) muestra la red, la señal y la IP, y tiene la opción **Agregar WiFi**.

**Panel web:** http://minic3.local (o la IP que muestra la info).

## Actualizaciones por WiFi (OTA)

A partir de ahora todo se actualiza sin cable, de dos formas:

**1. Desde el navegador**
1. Abrí http://minic3.local/ota.
2. Usuario `admin`, clave `minic3`.
3. Elegí **`firmware.bin`** (no el factory) y tocá "Subir y actualizar".
4. La pantalla muestra el progreso y la placa se reinicia sola.

**2. Desde PlatformIO**
- Entorno `minic3-ota`, o `pio run -e minic3-ota -t upload`.
- Si `minic3.local` no resuelve en tu PC, poné la IP en `upload_port` dentro de `platformio.ini`.

**Seguridad**
- Si subís el `firmware.factory.bin` por error, la placa lo rechaza antes de escribir nada.
- Si una subida se corta, sigue funcionando el firmware anterior: la partición nueva solo se activa cuando llega completa y verificada.

**Cambiar la clave de OTA:** está en dos lugares de `platformio.ini`, en `-D OTA_PASSWORD` y en `--auth`. Cambiá los dos. La clave nueva vale después de cargar ese firmware.

## Pines

| Qué | GPIO |
|---|---|
| OLED SDA / SCL | 8 / 9 |
| ↑ ↓ ← → (al GND) | 3 · 4 · 5 · 6 |
| Táctil A (OUT) | 7 |
| Batería (divisor 100k/100k) | 0 (opcional; sin él la info dice "sin medir") |
| LED RGB de la placa | 10 (se prende azul durante una OTA) |

## Notas

- **Particiones:** `min_spiffs` tiene dos espacios de 1,9 MB (1.966.080 bytes) para que funcione la OTA. El `firmware.bin` mide 1,76 MB, así que quedan unos 200 KB libres. Lo que cuenta es el tamaño del `.bin`, no el porcentaje que muestra PlatformIO. Se compila sin excepciones de C++ y sin los mensajes de depuración de WiFiManager para ahorrar lugar.
- **Imagen girada:** la pantalla va con los pines abajo (como en la caja), así que la imagen se rota 180°. Si la montás al revés, poné `OLED_FLIP 0` en `MiniC3.h`.
- **Si la imagen sale corrida o con basura en el costado:** tu pantalla es SSD1306 y no SH1106. Avisame y lo cambio, son dos líneas.
- **Idioma:** pantalla y panel web en español. La pantalla no tiene tildes porque las fuentes de la OLED no las traen.
## Archivos que cambiaron respecto de Tinytosh

- `MiniC3.ino`: entradas, OTA y bloqueo entre tareas (antes `TinytoshESP32.ino`).
- `MiniC3.h`: pines, versión y clave.
- `HardwareService`: las 5 entradas con antirrebote, repetición y el filtro del táctil.
- `OtaService`: OTA por PlatformIO y pantalla de progreso.
- `DisplayService`: driver SH1106, pantallas, info y OTA. (La pantalla de vuelos y `FlightService` se quitaron en v1.11.0: fallaban por falta de memoria.)
- `WebServerService`: páginas `/ota`, `/wifi`, `/mensaje`, `/spotify` y panel con el mapa de pines.
- `WifiService`: varias redes guardadas, conexión a la mejor y cambio automático (nuevo en v1.5.0).
- `GameService` y `Arkanoid`: la pantalla de juegos (nuevo en v1.5.0).
- `MessageInbox`: la bandeja de mensajes con lista, lectura y borrado (nuevo en v1.9.0).
- `InfoPanel`: el panel del equipo con páginas y acciones (nuevo en v1.6.0).
- `Tetris` y `Flappy`: los juegos 3 y 4 (nuevo en v1.10.0).
- `PolledGame` y `GameUi`: base común de los juegos nuevos (pausa, pantalla final, récords, LED) (nuevo en v1.11.0).
- `Snake`, `Game2048`, `Minesweeper`, `Invaders` y `Simon`: Viborita, 2048, Buscaminas, Invasores y Simón (nuevo en v1.11.0).
- `TrucoEngine`, `TrucoAI` y `TrucoGame`: el truco contra la compu (nuevo en v1.8.0).
- `TrucoService`: el anotador de truco (nuevo en v1.7.0). `HardwareService` ahora sabe distinguir toque y mantener en ← → para esta pantalla.
- `structs.h`, `JsonSerializer.cpp`, `ConfigManager.cpp`, `DataSyncService.cpp`: ajustes chicos.
