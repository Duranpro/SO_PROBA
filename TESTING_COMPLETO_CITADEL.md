# TESTING COMPLETO CITADEL

Documento generado por análisis estático del repositorio `SO_PROBA`, sin modificar código, sin compilar y sin ejecutar tests.

Fuentes revisadas:

- `Practica_SO_2025-26 v1.4.pdf`
- `Practica_SO_2025-26_-_testing_Fase_1_(1).pdf`
- `Practica_SO_2025-26_-_testing_Fase_2.pdf`
- `Practica_SO_2025-26_-_Testing_Fase_3.pdf`
- `Practica_SO_2025-26_-_Testing_Fase_4.pdf`
- `Practica_SO_2025-26_-_Testing_Global.pdf`
- `Memoria_SO.pdf`
- `TEST_ENVOYS.md`
- Código en `realm/`, `terminal/`, `network/`, `trade/`, `transfer/`, `envoy/`, `config/`, `stock/`

Notas previas importantes:

- El PDF `Memoria_Grup_13.pdf` no pertenece a The Citadel System; parece otro proyecto y no se ha usado como referencia funcional.
- El proyecto actual es una integración final de fases. Algunas expectativas de Fase 1 ya no se comportan como el PDF aislado de Fase 1.
- En el código actual, `START TRADE <REALM>` solo es útil si ya existe alianza y, para operar con catálogo remoto, conviene haber hecho antes `LIST PRODUCTS <REALM>`.
- En el código actual, `START TRADE <REALM>` entra igualmente en `(trade)>` aunque todavía no haya catálogo remoto; dentro avisará `No products available. Use LIST PRODUCTS first.`.
- `add <product> <amount>` solo valida que el producto exista en el catálogo remoto cacheado. No valida el stock remoto real en ese momento. El rechazo final ocurre en destino.
- `remove <product> <amount>` con cantidad mayor o igual a la existente elimina la línea del carrito y se considera correcto.
- Si pasas una ruta de `config` o `stock` inexistente, el `main` actual suele mostrar `Usage: ... <config.dat> <stock.db>` en lugar de un error más específico, porque intenta resolver ficheros existentes antes de cargar nada.
- El sigil de Winterfell del repo se llama `data/winterfell/winterfall.png`, no `winterfell.png`.
- Los Envoys son procesos hijo reales creados con `fork()+execv()` del mismo binario usando `--envoy-worker`.
- El diseño actual sí usa un pipe de resultado hijo -> padre. No se ve un pipe padre -> hijo ni comandos `CMD_RUN`/`CMD_COMPLETE`/`CMD_SHUTDOWN`.
- Sigue existiendo lógica legacy basada en `network->outbound.active` en `network/network.c`; por eso conviene probar explícitamente que no limite indebidamente las misiones propias de Envoys.

## 1. Preparación del entorno

### 1.1 Limpieza previa

Ejecuta siempre desde la raíz del proyecto en Linux:

```bash
cd /ruta/al/proyecto/SO_PROBA
pkill -f Maester
ps -ef | grep Maester
ps -ef | grep defunct
ss -tulpn | grep -E '8170|8171|8172|8173|8174'
lsof -iTCP -sTCP:LISTEN -P -n | grep -E '8170|8171|8172|8173|8174'
```

Qué comprobar:

- No quedan `Maester` viejos.
- No quedan procesos `defunct`.
- Los puertos que vayas a usar están libres.

Fallo grave:

- Quedan procesos zombie.
- Quedan listeners antiguos ocupando puertos.

### 1.2 Compilación en Linux

No la ejecuto aquí; para tu prueba manual:

```bash
make clean && make
```

Qué comprobar:

- Se genera `./Maester`.
- No hay warnings graves.

### 1.3 Copia de seguridad de `stock.db`

Haz backup antes de cualquier prueba destructiva:

```bash
cp data/dragonstone/dragonstone.db data/dragonstone/dragonstone.db.bak
cp data/thevale/thevale.db data/thevale/thevale.db.bak
cp data/kingslanding/kingslanding.db data/kingslanding/kingslanding.db.bak
cp data/driftmark/driftmark.db data/driftmark/driftmark.db.bak
cp data/winterfell/winterfell.db data/winterfell/winterfell.db.bak
```

Restauración:

```bash
cp data/dragonstone/dragonstone.db.bak data/dragonstone/dragonstone.db
cp data/thevale/thevale.db.bak data/thevale/thevale.db
cp data/kingslanding/kingslanding.db.bak data/kingslanding/kingslanding.db
cp data/driftmark/driftmark.db.bak data/driftmark/driftmark.db
cp data/winterfell/winterfell.db.bak data/winterfell/winterfell.db
```

### 1.4 Preparar configs para Linux si todo corre en una sola máquina

Los `.dat` del repo contienen IPs `192.168.1.3`, `192.168.1.4` y `192.168.1.5`. Si tu servidor Linux no tiene esas IPs configuradas, las conexiones salientes fallarán aunque el listener local pueda hacer `bind()` en `INADDR_ANY`.

Recomendación: no toques los originales; crea copias temporales para test local:

```bash
mkdir -p tmp-linux
cp data/dragonstone/dragonstone.dat tmp-linux/dragonstone.local.dat
cp data/thevale/thevale.dat tmp-linux/thevale.local.dat
cp data/kingslanding/kingslanding.dat tmp-linux/kingslanding.local.dat
cp data/driftmark/driftmark.dat tmp-linux/driftmark.local.dat
cp data/winterfell/winterfell.dat tmp-linux/winterfell.local.dat
sed -i 's/192\.168\.1\.[345]/127.0.0.1/g' tmp-linux/*.dat
```

Usa estas copias solo para las pruebas de red si todo vive en el mismo host.

### 1.5 Lanzar cada Maester

Comandos directos con los ficheros del repo:

```bash
./Maester data/kingslanding/kingslanding.dat data/kingslanding/kingslanding.db
./Maester data/dragonstone/dragonstone.dat data/dragonstone/dragonstone.db
./Maester data/thevale/thevale.dat data/thevale/thevale.db
./Maester data/driftmark/driftmark.dat data/driftmark/driftmark.db
./Maester data/winterfell/winterfell.dat data/winterfell/winterfell.db
```

Si usas las copias locales:

```bash
./Maester tmp-linux/kingslanding.local.dat data/kingslanding/kingslanding.db
./Maester tmp-linux/dragonstone.local.dat data/dragonstone/dragonstone.db
./Maester tmp-linux/thevale.local.dat data/thevale/thevale.db
./Maester tmp-linux/driftmark.local.dat data/driftmark/driftmark.db
./Maester tmp-linux/winterfell.local.dat data/winterfell/winterfell.db
```

### 1.6 Abrir 5 terminales

Escenario recomendado:

- Terminal 1: KingsLanding
- Terminal 2: Dragonstone
- Terminal 3: TheVale
- Terminal 4: Driftmark
- Terminal 5: Winterfell

Si prefieres `tmux`:

```bash
tmux new-session -s citadel
```

Luego abre 5 panes o 5 ventanas y ejecuta un Maester por terminal.

## 2. Escenario mínimo de 5 Maesters

El PDF global usa una topología genérica A-B-C-D-E que no coincide exactamente con los `.dat` del repo. Para esta práctica concreta, usa los 5 realms reales del código.

Equivalencia práctica recomendada:

- Maester A = KingsLanding
- Maester B = Dragonstone
- Maester C = TheVale
- Maester D = Driftmark
- Maester E = Winterfell

Topología real deducida de `data/*.dat`:

- KingsLanding <-> Dragonstone
- Dragonstone <-> TheVale
- TheVale <-> Driftmark
- TheVale <-> Winterfell
- Fallbacks por `DEFAULT` para llegar a destinos no directos

| Maester | Realm real | Envoys | IP:Puerto configurado | Rutas conocidas | DEFAULT | Comando recomendado |
|---|---|---:|---|---|---|---|
| A | KingsLanding | 2 | 192.168.1.4:8170 | Dragonstone | Dragonstone | `./Maester data/kingslanding/kingslanding.dat data/kingslanding/kingslanding.db` |
| B | Dragonstone | 3 | 192.168.1.4:8171 | KingsLanding, TheVale | TheVale | `./Maester data/dragonstone/dragonstone.dat data/dragonstone/dragonstone.db` |
| C | TheVale | 4 | 192.168.1.3:8173 | Driftmark, Dragonstone, Winterfell | Dragonstone | `./Maester data/thevale/thevale.dat data/thevale/thevale.db` |
| D | Driftmark | 2 | 192.168.1.3:8172 | TheVale | TheVale | `./Maester data/driftmark/driftmark.dat data/driftmark/driftmark.db` |
| E | Winterfell | 2 | 192.168.1.5:8174 | TheVale | Dragonstone | `./Maester data/winterfell/winterfell.dat data/winterfell/winterfell.db` |

Ficheros de sigil existentes en el repo:

- Dragonstone: `data/dragonstone/dragonstone.png`
- TheVale: `data/thevale/thevale.png`
- KingsLanding: `data/kingslanding/kingslanding.png`
- Driftmark: `data/driftmark/driftmark.png`
- Winterfell: `data/winterfell/winterfall.png`

## 3. Tests de Fase 1

En la tabla siguiente, “resultado esperado” describe el comportamiento observable del código actual, no necesariamente el texto exacto del PDF.

| Test | Comando | Resultado esperado | Qué comprobar | Fallo grave |
|---|---|---|---|---|
| F1-01 | `./Maester data/dragonstone/dragonstone.dat data/dragonstone/dragonstone.db` | Arranca y muestra `Maester of Dragonstone initialized. The board is set.` | Carga config, stock, listener y prompt `$ ` | Crash al arrancar |
| F1-02 | `./Maester noexiste.dat data/dragonstone/dragonstone.db` | Muestra `Usage: ... <config.dat> <stock.db>` | Manejo limpio de ruta inexistente | Segfault o cuelgue |
| F1-03 | `./Maester data/dragonstone/dragonstone.dat noexiste.db` | Muestra `Usage: ... <config.dat> <stock.db>` | Manejo limpio de stock inexistente | Segfault o cuelgue |
| F1-04 | `./Maester` | Muestra `Usage: ... <config.dat> <stock.db>` | Rechaza argumentos insuficientes | Arranque inconsistente |
| F1-05 | `./Maester a b c` | Muestra `Usage: ... <config.dat> <stock.db>` | Rechaza argumentos de más | Ignora extras y arranca mal |
| F1-06 | `LIST REALMS` | Lista los 4 realms del `.dat`, sin `DEFAULT` | No incluye `DEFAULT`; nombres correctos | Lista vacía o corrupta |
| F1-07 | `LIST PRODUCTS` | Muestra `--- Trade Ledger ---` y el inventario local | Coincide con `stock.db` | Datos corruptos o crash |
| F1-08 | `PLEDGE STATUS` | Imprime un estado por realm conocido | Sale un estado por realm; no se cuelga | Crash o estados corruptos |
| F1-09 | `ENVOY STATUS` | Muestra tantos Envoys como indique el `.dat` | Coincide con `envoy_count` | Número incorrecto |
| F1-10 | `PLEDGE TheVale data/dragonstone/dragonstone.png` | Lanza misión Envoy y deja el prompt libre | No bloquea el prompt | Prompt bloqueado |
| F1-11 | `START TRADE TheVale` | Si no hay alianza: error de autorización | No entra a comerciar sin alianza | Permite comerciar sin alianza |
| F1-12 | `EXIT` | Termina limpiamente | No quedan hijos ni puertos abiertos | Zombie o cuelgue al salir |
| F1-13 | `LiSt REAlms` | Igual que `LIST REALMS` | Case-insensitive | Comando rechazado |
| F1-14 | `PLedgE TheVale data/dragonstone/dragonstone.png` | Igual que `PLEDGE` | Case-insensitive | Comando rechazado |
| F1-15 | `CHECK MY REALMS` | `Unknown command` | Parser robusto | Crash o comportamiento extraño |
| F1-16 | `START TRADE` | Mensaje de incompleto | No entra en estado raro | Cuelgue |
| F1-17 | `START TRADE TheVale now` | `Unknown command` | Rechaza tokens extra | Acepta sintaxis inválida |
| F1-18 | `LIST REALMS please` | `Unknown command` | Rechaza tokens extra | Acepta sintaxis inválida |
| F1-19 | `LIST PRODUCTS TheVale extra` | `Unknown command` | Rechaza tokens extra | Acepta sintaxis inválida |
| F1-20 | `PLEDGE` | Mensaje de comando incompleto | No crashea | Crash |
| F1-21 | `PLEDGE TheVale` | Mensaje de que falta sigil | Parser correcto | Crash |
| F1-22 | `PLEDGE TheVale sigil extra` | `Unknown command` | Rechaza tokens extra | Acepta sintaxis inválida |
| F1-23 | `PLEDGE RESPOND` | Mensaje de incompleto | No crashea | Crash |
| F1-24 | `PLEDGE RESPOND TheVale` | Mensaje de incompleto | No crashea | Crash |
| F1-25 | `PLEDGE RESPOND TheVale ACCEPT extra` | `Unknown command` | Rechaza tokens extra | Acepta sintaxis inválida |
| F1-26 | `ENVOY` | Mensaje de incompleto | Parser correcto | Crash |
| F1-27 | `ENVOY STATUS extra` | `Unknown command` | Rechaza tokens extra | Acepta sintaxis inválida |

### 3.1 Submenú `(trade)>`

Para probarlo de forma útil en el código actual:

1. Forja alianza con un realm.
2. Ejecuta `LIST PRODUCTS <REALM>`.
3. Luego `START TRADE <REALM>`.

Pruebas del submenú:

| Test | Comando en `(trade)>` | Resultado esperado | Qué comprobar | Fallo grave |
|---|---|---|---|---|
| F1-T01 | `add Direwolf Pelt 2` | `Product added to trade list.` | Acepta nombre con espacios | Parser roto |
| F1-T02 | `remove Direwolf Pelt 1` | `Product removed from trade list.` | Resta o elimina línea | Cantidad corrupta |
| F1-T03 | `send` con carrito vacío | `Trade is empty. Add at least one product before sending.` | No envía basura | Crea order vacía |
| F1-T04 | `cancel` | `Trade cancelled.` y vuelve a `$ ` | Sale limpio del submenú | Se queda atrapado |
| F1-T05 | `add producto 0` | Mensaje de comando incompleto/uso | No acepta 0 | Inserta cantidad 0 |
| F1-T06 | `add producto -2` | Mensaje de comando incompleto/uso | No acepta negativos | Inserta negativos |
| F1-T07 | `add producto abc` | Mensaje de comando incompleto/uso | No acepta no numérico | Inserta texto |
| F1-T08 | `remove producto 9999` | Si existía, elimina la línea | Semántica real del código | Underflow o crash |
| F1-T09 | `send` con carrito válido | Lanza Envoy trade o avisa de no disponibilidad | No bloquea prompt principal al salir | Cuelgue |

## 4. Tests de Fase 2

### 4.1 Escenario mínimo de dos Maesters directos

Usa:

- KingsLanding
- Dragonstone

Comandos:

```bash
./Maester ...kingslanding...
./Maester ...dragonstone...
```

Pruebas:

| Test | Acción | Resultado esperado | Qué comprobar | Fallo grave |
|---|---|---|---|---|
| F2-01 | `PLEDGE Dragonstone data/kingslanding/kingslanding.png` desde KingsLanding | Envía TYPE 0x01 y queda pendiente | Prompt no bloqueado | Bloqueo total |
| F2-02 | `PLEDGE RESPOND KingsLanding ACCEPT` desde Dragonstone | Se establece alianza | Ambos lados registran alianza | Estados incoherentes |
| F2-03 | `PLEDGE STATUS` en origen mientras está pendiente | Muestra estado pendiente o transición | Prompt operativo | Cuelgue |
| F2-04 | `PLEDGE RESPOND KingsLanding REJECT` | No quedan aliados | Estado final rechazado/fallido | Queda aliado indebidamente |
| F2-05 | `LIST PRODUCTS KingsLanding` sin alianza desde Dragonstone | Error de autorización | No fuga operación remota | Devuelve catálogo sin permiso |

### 4.2 Escenario de tres Maesters con hop

Usa:

- KingsLanding -> Dragonstone -> TheVale

Pruebas:

| Test | Acción | Resultado esperado | Qué comprobar | Fallo grave |
|---|---|---|---|---|
| F2-06 | `PLEDGE TheVale data/kingslanding/kingslanding.png` desde KingsLanding | Llega vía Dragonstone por `DEFAULT`/ruta conocida | Dragonstone actúa como hop | No reenvía |
| F2-07 | Aceptar en TheVale | KingsLanding y TheVale quedan aliados | Se puede comunicar después sin error de autorización | Alianza a medias |
| F2-08 | Después de alianza, repetir `LIST PRODUCTS TheVale` desde KingsLanding | Debe funcionar por ruta efectiva disponible | No bloquea el prompt | Cuelgue |

### 4.3 Errores y robustez de protocolo

| Test | Acción manual | Resultado esperado | Qué comprobar | Fallo grave |
|---|---|---|---|---|
| F2-09 | Lanzar `PLEDGE` a realm inexistente | Rechazo inmediato en parser | No sale a red | Segfault |
| F2-10 | Parar Maester destino antes de `PLEDGE` | Falla por no reach / timeout | Prompt sigue libre | Bloqueo |
| F2-11 | Parar Maester intermedio hop | Origen termina en fallo o timeout | No se queda colgado eternamente | Espera infinita |
| F2-12 | Cambiar temporalmente puerto destino en copia de `.dat` | Error de conexión o timeout | Log razonable | Crash |
| F2-13 | Comando mientras hay `PLEDGE` pendiente | Debe aceptar otros comandos locales | CLI no bloqueante | Prompt congelado |
| F2-14 | `Ctrl+C` en aliado | Si está implementado, envía TYPE `0x27` | El otro lo marca `INACTIVE` | No limpia recursos |

### 4.4 Cómo provocar errores de red

Parar un destino:

```bash
kill <pid_del_maester_destino>
```

Cambio temporal de puerto en copia de config:

```bash
sed -i 's/8173/9199/' tmp-linux/dragonstone.local.dat
```

Realm inexistente:

```text
PLEDGE NoExiste data/dragonstone/dragonstone.png
```

Checksum incorrecto, prueba opcional avanzada:

```bash
python3 - <<'PY'
import socket, struct
host, port = "127.0.0.1", 8170
buf = bytearray(320)
buf[0] = 0x01
origin = b"127.0.0.1:9999"
dest = b"KingsLanding"
data = b"FakeRealm&fake.png&0&00000000000000000000000000000000"
buf[1:1+len(origin)] = origin
buf[21:21+len(dest)] = dest
buf[41:43] = struct.pack("!H", len(data))
buf[43:43+len(data)] = data
buf[318:320] = struct.pack("!H", 0x1234)
s = socket.socket()
s.connect((host, port))
s.sendall(buf)
s.close()
PY
```

Qué no debe pasar:

- `segmentation fault`
- bucle infinito
- bloqueo del prompt
- cierre inesperado del proceso

## 5. Tests de Fase 3

### 5.1 PLEDGE y sigil

Usa por ejemplo Dragonstone <-> TheVale.

| Test | Acción | Resultado esperado | Qué comprobar | Fallo grave |
|---|---|---|---|---|
| F3-01 | `PLEDGE TheVale data/dragonstone/dragonstone.png` | Se lanza misión, ACK, envío de fichero, MD5 ACK y respuesta | Flujo completo | Cuelgue |
| F3-02 | `PLEDGE TheVale noexiste.png` | Falla al preparar sigil | No sale a red | Crash |
| F3-03 | `truncate -s 0 vacio.png` y `PLEDGE TheVale vacio.png` | Debe manejar fichero 0 con rechazo controlado o fallo limpio | No crash | Segfault |
| F3-04 | `PLEDGE TheVale archivo_grande.png` | Debe trocear en frames | No se rompe transferencia | Bloqueo o corrupción |
| F3-05 | No responder en destino durante >120 s | TIMEOUT | El origen sale de pendiente | Queda pendiente eterno |
| F3-06 | `PLEDGE RESPOND Dragonstone REJECT` | Origen queda rechazado/no aliado | No se usa ruta directa después | Queda aliado por error |
| F3-07 | `PLEDGE RESPOND Dragonstone ACCEPT` | Alianza activa | `LIST PRODUCTS <REALM>` pasa autorización | Alianza no usable |

### 5.2 Productos remotos

| Test | Acción | Resultado esperado | Qué comprobar | Fallo grave |
|---|---|---|---|---|
| F3-08 | `LIST PRODUCTS TheVale` sin alianza | Error de autorización | No lanza misión útil | Fuga de datos |
| F3-09 | `LIST PRODUCTS TheVale` con alianza | Catálogo remoto impreso y cacheado | Luego `START TRADE TheVale` ya tiene catálogo | No guarda catálogo |
| F3-10 | Apagar destino durante `LIST PRODUCTS` | TIMEOUT o fallo limpio | Envoy termina | Envoy eterno |
| F3-11 | MD5 KO en catálogo si logras simularlo | Rechazo limpio | No acepta catálogo corrupto | Usa datos corruptos |

### 5.3 Trade

| Test | Acción | Resultado esperado | Qué comprobar | Fallo grave |
|---|---|---|---|---|
| F3-12 | `START TRADE TheVale` sin alianza | Error de autorización | No entra al submenú | Permite comerciar sin alianza |
| F3-13 | `START TRADE TheVale` con alianza pero sin `LIST PRODUCTS` | Entra a trade y avisa que no hay productos cargados | No se rompe el submenú | Cuelgue |
| F3-14 | `START TRADE TheVale` tras `LIST PRODUCTS TheVale` | Entra con catálogo disponible | Lista nombres correctos | Catálogo vacío |
| F3-15 | `add Direwolf Pelt 2` | Añade | Parser con espacios correcto | Parser roto |
| F3-16 | `add ProductoInexistente 2` | Rechaza: no está en catálogo remoto | No añade basura | Producto fantasma |
| F3-17 | `add Direwolf Pelt 0` | Rechaza sintaxis | No añade 0 | Inserta 0 |
| F3-18 | `add Direwolf Pelt -1` | Rechaza sintaxis | No añade negativos | Inserta negativos |
| F3-19 | `add Direwolf Pelt abc` | Rechaza sintaxis | No añade texto | Corrupción |
| F3-20 | `add Direwolf Pelt 999999` | Lo acepta localmente pero el destino debería rechazar después | Validación remota real | Acepta orden imposible |
| F3-21 | `remove Direwolf Pelt 1` | Resta o elimina | Semántica correcta | Underflow |
| F3-22 | `remove ProductoInexistente 1` | Rechaza: no está en carrito | No altera carrito | Crash |
| F3-23 | `remove Direwolf Pelt 999` | Elimina la línea | El carrito queda consistente | Cantidad negativa |
| F3-24 | `send` con carrito vacío | Mensaje de vacío | No crea envío | Orden vacía |
| F3-25 | `send` con carrito válido | Lanza Envoy TRADE | Vuelve al prompt principal | Bloqueo |
| F3-26 | `cancel` | Sale del submenú | No deja estado raro | Sigue dentro |

### 5.4 Persistencia de stock

Antes del trade:

```bash
cp data/dragonstone/dragonstone.db data/dragonstone/dragonstone.db.pretrade
cp data/thevale/thevale.db data/thevale/thevale.db.pretrade
```

Prueba:

1. Haz alianza.
2. Ejecuta `LIST PRODUCTS TheVale`.
3. Desde Dragonstone compra productos reales de TheVale, por ejemplo:

```text
START TRADE TheVale
add Direwolf Pelt 2
add Grain Sack 1
send
```

Comprobaciones:

- En destino, `thevale.db` decrementa esas cantidades.
- En origen, `dragonstone.db` incrementa esas cantidades.
- Cierra ambos Maesters.
- Relánzalos.
- `LIST PRODUCTS` local debe seguir reflejando el nuevo stock.

Fallo grave:

- Cambios solo en memoria.
- `stock.db` truncado o corrupto.

### 5.5 Rechazos de trade

Pruebas:

- Solicitar más unidades de las disponibles en destino.
- Incluir producto inexistente en el fichero de order si quieres simular edición manual.
- Parar destino durante transferencia.
- Parar origen durante transferencia.
- Simular MD5 KO si preparas un payload alterado con helper.

Qué esperar:

- Respuesta `REJECT` o fallo limpio.
- No se actualiza stock en origen si el pedido no fue aceptado.
- No queda Envoy colgado.

## 6. Tests de Fase 4

### 6.1 Estado inicial y número de Envoys

Pruebas:

| Test | Acción | Resultado esperado | Qué comprobar | Fallo grave |
|---|---|---|---|---|
| F4-01 | `ENVOY STATUS` recién arrancado en Dragonstone | 3 Envoys `FREE` | Coincide con `dragonstone.dat` | Conteo incorrecto |
| F4-02 | `ENVOY STATUS` recién arrancado en TheVale | 4 Envoys `FREE` | Coincide con `thevale.dat` | Conteo incorrecto |

### 6.2 Ocupación de Envoys

| Test | Acción | Resultado esperado | Qué comprobar | Fallo grave |
|---|---|---|---|---|
| F4-03 | Lanzar un `PLEDGE` | Un Envoy pasa a `ON_MISSION` | `ENVOY STATUS` refleja pid y target | No cambia de estado |
| F4-04 | Lanzar dos `PLEDGE` simultáneos en Dragonstone | Dos Envoys ocupados | El prompt sigue libre | Bloqueo |
| F4-05 | Lanzar una tercera misión cuando todos están ocupados en un Maester con pocos Envoys | `No free Envoy available.` o equivalente | Error controlado | Crash |
| F4-06 | Esperar a que termine una misión | Envoy vuelve a `FREE` | Se libera el slot | Queda ocupado eternamente |

### 6.3 Concurrencia real

Pruebas recomendadas en Dragonstone o TheVale:

```text
PLEDGE KingsLanding data/dragonstone/dragonstone.png
PLEDGE TheVale data/dragonstone/dragonstone.png
LIST PRODUCTS KingsLanding
START TRADE KingsLanding
```

Valida:

- `PLEDGE` concurrente.
- `LIST PRODUCTS` concurrente.
- `TRADE` concurrente.
- mezcla de `PLEDGE + LIST PRODUCTS + TRADE`.
- todos los Envoys ocupados.

Qué comprobar:

- La limitación visible es el número de Envoys, no que exista una sola operación global permitida.
- El prompt no se bloquea.
- `SIGCHLD` no rompe la línea de comandos.

### 6.4 Inspección de procesos, FDs y puertos

Comandos:

```bash
ps -ef | grep Maester
ps -ef | grep defunct
ps -ef | grep -- --envoy-worker
lsof -p <PID_DEL_MAESTER>
lsof -p <PID_DEL_ENVOY>
ls /proc/<PID_DEL_MAESTER>/fd
ls /proc/<PID_DEL_ENVOY>/fd
netstat -tulpn | grep Maester
ss -tulpn | grep Maester
```

Qué validar:

- Los Envoys son hijos del Maester.
- El worker aparece como `Maester --envoy-worker ...`.
- No arranca otro “Maester normal” con prompt propio.
- El Envoy usa listener privado de puerto efímero.
- El padre tiene el extremo de lectura del pipe de resultado.
- El hijo tiene el extremo de escritura del pipe de resultado.
- No se observa un segundo pipe padre -> hijo para pasar órdenes.

### 6.5 Ctrl+C y EXIT con Envoys activos

Pruebas:

- `Ctrl+C` con 1 Envoy activo.
- `Ctrl+C` con varios Envoys activos.
- `EXIT` con Envoys activos.

Qué comprobar:

- El padre mata Envoys vivos.
- No quedan `defunct`.
- No quedan puertos efímeros abiertos.
- No queda ningún `--envoy-worker` huérfano.

## 7. Tests de señales y apagado

| Test | Acción | Qué debería ocurrir | Qué comprobar en otros Maesters | Fallo grave |
|---|---|---|---|---|
| SIG-01 | `Ctrl+C` sin operaciones | Cierre limpio | Nada extraño en peers | Zombie o FD abierto |
| SIG-02 | `Ctrl+C` con `PLEDGE` pendiente | Cierra limpio, mata Envoys, notifica si procede | El aliado puede marcar inactivo si recibe `0x27` | Envoy huérfano |
| SIG-03 | `Ctrl+C` con `LIST PRODUCTS` pendiente | Igual | El peer no queda esperando indefinidamente | Cuelgue remoto |
| SIG-04 | `Ctrl+C` con `START TRADE` pendiente | Igual | El peer maneja corte | Stock corrupto |
| SIG-05 | `Ctrl+C` en Maester hop | Resto de flujos fallan o expiran limpiamente | Origen detecta fallo/timeout | Bucle o bloqueo |
| SIG-06 | `Ctrl+C` en destino durante recepción de fichero | Origen acaba en fallo/timeout | No aceptación falsa | Stock corrupto |
| SIG-07 | `Ctrl+C` en origen durante envío | Destino no deja transferencia fantasma | No entrada parcial persistente | Fichero intermedio corrupto |
| SIG-08 | `EXIT` con Envoys activos | Limpieza equivalente | Sin hijos vivos | Zombies |
| SIG-09 | `kill <pid>` | Cierre por señal no interactiva | Peers detectan caída eventual | Recursos colgando |
| SIG-10 | `kill -9 <pid>` | Caída abrupta | Peers deben fallar o marcar inactivo tras siguiente interacción | Deadlocks |

Logs esperables:

- Mensajes de `Closing Maester cleanly after SIGINT.`
- Mensajes de timeout o fallo de pledge/trade.
- Mensajes de inactividad en aliados si reciben `DISCONNECT`.

## 8. Tests de errores de red y protocolo

Casos a cubrir:

- puerto incorrecto
- IP incorrecta
- realm inexistente
- `DEFAULT` inexistente
- hop intermedio caído
- destino caído
- origen caído
- conexión rechazada
- timeout
- checksum incorrecto
- `NACK`
- `ACK KO`
- `MD5_ACK KO`
- transferencia incompleta
- fichero tamaño 0
- fichero grande
- fichero inexistente
- permisos insuficientes de fichero
- directorio sin permisos de escritura
- `stock.db` corrupto o mal formado
- `config.dat` corrupto o mal formado

Comandos útiles:

```bash
chmod 000 data/thevale/thevale.png
chmod 500 data/dragonstone
truncate -s 17 data/dragonstone/dragonstone.db
cp data/dragonstone/dragonstone.dat tmp-linux/dragonstone.bad.dat
sed -i 's/DEFAULT.*/DEFAULT *.*.*.* 0/' tmp-linux/dragonstone.bad.dat
```

Qué no debe pasar nunca:

- `segmentation fault`
- bucle infinito
- prompt bloqueado
- zombie
- fuga de FDs
- `stock.db` corrupto
- Maester colgado
- Envoy vivo eternamente

## 9. Tests de memoria, FDs y Valgrind

No ejecutar ahora; usar después en Linux.

Comando exacto:

```bash
valgrind --leak-check=full --show-leak-kinds=all --track-fds=yes ./Maester data/dragonstone/dragonstone.dat data/dragonstone/dragonstone.db
```

Pruebas mínimas dentro de Valgrind:

- arrancar y salir con `EXIT`
- `LIST REALMS`
- `LIST PRODUCTS`
- `PLEDGE`
- `LIST PRODUCTS <REALM>`
- `START TRADE <REALM>`
- `cancel`
- `send`
- `Ctrl+C`

Resultados aceptables:

- `0 definitely lost`
- sin `invalid read`
- sin `invalid write`
- sin `use-after-free`
- sin `double free`
- FDs cerrados al salir
- sin procesos `defunct`

Resultados no aceptables:

- `definitely lost > 0`
- `invalid read/write`
- `still reachable` creciendo de manera anómala tras ciclos repetidos
- FDs de sockets o pipes abiertos tras `EXIT`

Inspección complementaria:

```bash
lsof -p <pid>
ls /proc/<pid>/fd
```

## 10. Tests de regresión contra requisitos trampa

Ejecuta:

```bash
grep -R "CITADEL_ID" .
grep -R "libwesteros" .
grep -R "westeros:" Makefile
grep -R "Peace through trade" .
grep -R "Synchronizing with Iron Throne" .
grep -R "Even-port route engaged" .
grep -R "Odd-port route engaged" .
grep -R "Raven lost in storm" .
grep -R "RAVEN-CLOSED" .
grep -R "Dance completed" .
grep -R "ready to fly" .
grep -R "raven_fd" .
grep -R "raven_socket" .
grep -R "Citadel-Auth-2025" .
grep -R "Routing Protocols in Decentralized Multi-Hop Networks" .
grep -R "32768" .
grep -R "DRAGON_WAKE" .
grep -R "Els corbs" .
```

Interpretación:

- No debería salir nada nuevo relevante en código fuente o Makefile.
- Si alguna coincidencia aparece solo en documentación de testing, en este propio documento o en `TEST_ENVOYS.md`, no es una implementación funcional, pero conviene revisarla antes de entregar.

### Requisitos trampa detectados que deben eliminarse

Detectados por análisis del repo actual:

- La cadena `ready to fly` aparece en `TEST_ENVOYS.md` como texto documental de validación negativa, no en el código ejecutable.

No he detectado en código fuente implementaciones activas de:

- `CITADEL_ID`
- `libwesteros`
- target `westeros`
- `Synchronizing with Iron Throne`
- `Even-port route engaged`
- `Odd-port route engaged`
- `Raven lost in storm`
- `RAVEN-CLOSED`
- `Dance completed`
- `raven_fd`
- `raven_socket`
- `Citadel-Auth-2025`
- `DRAGON_WAKE`
- `Els corbs`

## 11. Tests de diseño Envoy correcto

Ejecuta:

```bash
grep -R "parent_to_child" .
grep -R "to_child_fd" .
grep -R "from_child_fd" .
grep -R "CMD_RUN" .
grep -R "CMD_COMPLETE" .
grep -R "CMD_SHUTDOWN" .
grep -R "network_send_pledge" terminal trade realm envoy network
grep -R "network_request_remote_products" terminal trade realm envoy network
grep -R "network_send_trade_offer" terminal trade realm envoy network
grep -R "outbound.active" network terminal trade envoy
```

Cómo interpretar resultados:

- Las funciones legacy de red pueden existir en `network/network.c`.
- `terminal/commands.c` y `trade/trade.c` no deberían usarlas para lanzar misiones propias; deberían pasar por `envoy_spawn_mission()`.
- `outbound.active` existe en `network/network.c`; por eso debes probar que no bloquee indebidamente la concurrencia de Envoys.
- No debería haber un pipe padre -> hijo.
- No debería haber comandos del padre al hijo.
- El hijo no debería esperar órdenes posteriores del padre.
- El Envoy debe realizar la misión completa por sí mismo y devolver el resultado por pipe al padre.

Hechos observados en este repo:

- Sí hay un pipe de hijo -> padre para el resultado final.
- No he encontrado identificadores `parent_to_child`, `to_child_fd`, `from_child_fd`, `CMD_RUN`, `CMD_COMPLETE`, `CMD_SHUTDOWN` en código fuente.
- `commands.c` usa `envoy_spawn_mission()` para `PLEDGE` y `LIST PRODUCTS`.
- `trade.c` usa `envoy_spawn_mission()` para `send` de trade.
- El worker privado crea un listener efímero con `bind(..., puerto 0)` y recupera el puerto con `getsockname()`.

## 12. Matriz final de aceptación

| ID test | Fase | Objetivo | Comando/acción | Resultado esperado | Cómo comprobarlo | Prioridad | Estado |
|---|---|---|---|---|---|---|---|
| MAT-01 | 1 | Compila | `make clean && make` | Binario `Maester` generado | `ls -l Maester` | Alta | Pendiente |
| MAT-02 | 1 | Arranque válido | Lanzar Dragonstone | Prompt operativo | Mensaje inicial y `$ ` | Alta | Pendiente |
| MAT-03 | 1 | Parser robusto | `CHECK MY REALMS` | `Unknown command` | No crash | Alta | Pendiente |
| MAT-04 | 1 | Listado de realms | `LIST REALMS` | 4 realms del config | Comparar con `.dat` | Alta | Pendiente |
| MAT-05 | 1 | Listado local | `LIST PRODUCTS` | Inventario local correcto | Comparar con `stock.db` | Alta | Pendiente |
| MAT-06 | 2 | Red directa | `PLEDGE` entre dos directos | Flujo correcto | Ambos lados cambian estado | Alta | Pendiente |
| MAT-07 | 2 | Hop | `PLEDGE` KingsLanding -> TheVale | Reenvío por hop | Logs y alianza final | Alta | Pendiente |
| MAT-08 | 2 | Timeout | No responder 120 s | Falla por timeout | `PLEDGE STATUS` y log | Alta | Pendiente |
| MAT-09 | 2 | Realm inválido | `PLEDGE NoExiste ...` | Rechazo inmediato | No tráfico útil | Media | Pendiente |
| MAT-10 | 3 | Alianza aceptada | `PLEDGE RESPOND ... ACCEPT` | Realm aliado | `LIST PRODUCTS <REALM>` funciona | Alta | Pendiente |
| MAT-11 | 3 | Alianza rechazada | `PLEDGE RESPOND ... REJECT` | No aliado | `START TRADE` sigue vetado | Alta | Pendiente |
| MAT-12 | 3 | Catálogo remoto | `LIST PRODUCTS <REALM>` | Catálogo impreso | Luego trade conoce productos | Alta | Pendiente |
| MAT-13 | 3 | Trade válido | `START TRADE`, `add`, `send` | Pedido aceptado | Stock origen/destino cambia | Alta | Pendiente |
| MAT-14 | 3 | Trade rechazado | Pedir más de lo disponible | `REJECT` | Stock no cambia | Alta | Pendiente |
| MAT-15 | 3 | Persistencia | Reiniciar tras trade | Cambios persisten | Comparar `stock.db` antes/después | Alta | Pendiente |
| MAT-16 | 4 | Estado Envoys | `ENVOY STATUS` | Conteo correcto | Coincide con `.dat` | Alta | Pendiente |
| MAT-17 | 4 | Concurrencia | Varias misiones simultáneas | Varias `ON_MISSION` | Prompt no bloqueado | Alta | Pendiente |
| MAT-18 | 4 | Todos ocupados | Lanzar misión extra | Error controlado | Sin crash | Alta | Pendiente |
| MAT-19 | Señales | Ctrl+C limpio | `Ctrl+C` con y sin Envoys | Limpia hijos y FDs | `ps`, `lsof`, `ss` | Alta | Pendiente |
| MAT-20 | Robustez | kill -9 | Caída inesperada tolerada | Peers fallan limpio | Alta | Pendiente |
| MAT-21 | Memoria | Valgrind básico | Arrancar y salir | Sin errores graves | Informe Valgrind | Alta | Pendiente |
| MAT-22 | FDs | Pipes/sockets | `lsof -p <pid>` | Sin fuga de FDs | Antes/después de misiones | Alta | Pendiente |
| MAT-23 | Zombies | Recolector SIGCHLD | Múltiples Envoys | Sin `defunct` | `ps -ef | grep defunct` | Alta | Pendiente |
| MAT-24 | Trampas | Greps de cadenas prohibidas | Ejecutar bloque grep | Sin coincidencias funcionales | Revisar salidas | Alta | Pendiente |
| MAT-25 | Diseño Envoy | Sin pipe padre -> hijo | Greps de diseño | Sin comandos legacy de control | Revisar resultados | Alta | Pendiente |

## 13. Orden recomendado de ejecución

1. Compilación.
2. Arranque individual de cada realm.
3. Fase 1 local y parser robusto.
4. Dos Maesters directos.
5. Tres Maesters con hop.
6. `PLEDGE` aceptado, rechazado y timeout.
7. `LIST PRODUCTS` remoto.
8. `START TRADE` aceptado y rechazado.
9. Envoys concurrentes.
10. Escenario global de 5 Maesters.
11. Fallos de red.
12. Señales y `Ctrl+C`.
13. Valgrind.
14. Greps de requisitos trampa.
15. Prueba final limpia desde cero restaurando `stock.db`.

## 14. Checklist final

- [ ] Compila sin warnings graves.
- [ ] Arranca con configs válidos.
- [ ] Rechaza configs inválidos sin petar.
- [ ] Parser robusto.
- [ ] `LIST REALMS` correcto.
- [ ] `LIST PRODUCTS` local correcto.
- [ ] `PLEDGE` funciona.
- [ ] `PLEDGE reject` funciona.
- [ ] `PLEDGE timeout` funciona.
- [ ] Hops funcionan.
- [ ] `LIST PRODUCTS` remoto funciona.
- [ ] `START TRADE` funciona.
- [ ] Stock origen actualizado.
- [ ] Stock destino actualizado.
- [ ] Stock persistente tras reiniciar.
- [ ] `ENVOY STATUS` correcto.
- [ ] Envoys concurrentes.
- [ ] Todos ocupados produce error controlado.
- [ ] `SIGCHLD` no rompe prompt.
- [ ] `Ctrl+C` limpia recursos.
- [ ] `EXIT` limpia recursos.
- [ ] No zombies.
- [ ] No FDs abiertos de más.
- [ ] Valgrind sin errores graves.
- [ ] No requisitos trampa.
- [ ] No pipe padre -> hijo.
- [ ] No bloqueo por `outbound.active` global en misiones propias.
