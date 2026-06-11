# TEST_ENVOYS.md

## 1. Compilacion

```bash
make clean && make
```

## 2. Lanzar Maesters

Ejemplo:

```bash
./Maester data/dragonstone/dragonstone.dat data/dragonstone/dragonstone.db
./Maester data/thevale/thevale.dat data/thevale/thevale.db
```

Adapta las rutas si en tu servidor estan en otra ubicacion.

## 3. Estado inicial

Dentro del Maester:

```text
ENVOY STATUS
```

Esperado:

```text
Envoy 1: FREE
Envoy 2: FREE
...
```

## 4. PLEDGE con Envoy

En Dragonstone:

```text
PLEDGE TheVale data/dragonstone/dragonstone.png
```

En TheVale:

```text
PLEDGE RESPOND Dragonstone ACCEPT
```

Esperado:

- El prompt no se bloquea.
- Aparece algo como `Envoy <id> launched with pid <pid>`.
- No aparece `Envoy <id> ready to fly <pid>`.
- Al terminar aparece `>>> Alliance with TheVale forged successfully!` o el mensaje de rechazo/fallo.

## 5. LIST PRODUCTS remoto con Envoy

En Dragonstone:

```text
LIST PRODUCTS TheVale
```

Esperado:

- El prompt no se bloquea.
- Se lanza Envoy.
- Al terminar se imprime el catalogo remoto.

## 6. LIST PRODUCTS local

```text
LIST PRODUCTS
```

Esperado:

- Muestra stock local.
- No lanza Envoy.

## 7. START TRADE con Envoy

En Dragonstone:

```text
START TRADE TheVale
```

Dentro del prompt trade, por ejemplo:

```text
add Direwolf Pelt 2
add Iron Dagger 1
send
```

Esperado:

- `Trade list sent to TheVale.`
- Vuelve al prompt principal.
- Aparece `Envoy <id> launched with pid <pid>`.
- Al terminar aparece `>>> Order accepted by TheVale.` o rechazo/fallo.

## 8. Concurrencia

Lanza varias operaciones rapidamente, por ejemplo:

```text
PLEDGE TheVale data/dragonstone/dragonstone.png
LIST PRODUCTS TheVale
START TRADE TheVale
```

Esperado:

- Si hay Envoys libres, pueden convivir varias misiones.
- `ENVOY STATUS` puede mostrar varias `ON_MISSION`.
- No debe aparecer bloqueo por outbound global.

## 9. Restricciones

Comprobar:

```bash
grep -R "CITADEL_ID\|libwesteros\|westeros:\|Raven lost\|RAVEN-CLOSED\|Dance completed\|DRAGON_WAKE\|Citadel-Auth-2025\|ready to fly\|raven_fd\|raven_socket\|Synchronizing with Iron Throne" .
```

No debe salir nada nuevo relevante.

Comprobar pipe padre -> hijo:

```bash
grep -R "parent_to_child\|to_child_fd\|CMD_RUN\|CMD_COMPLETE\|CMD_SHUTDOWN" .
```

No debe salir nada.

## 10. Zombies

```bash
ps -ef | grep defunct
```

No deben quedar Envoys zombie.

## 11. FDs

Opcional:

```bash
valgrind --leak-check=full --track-fds=yes ./Maester data/dragonstone/dragonstone.dat data/dragonstone/dragonstone.db
```

## 12. Prueba de ruta directa aprendida tras alianza

Escenario:

- A no tiene ruta directa a C, pero puede llegar a C por B o por DEFAULT.
- A hace `PLEDGE` a C usando hops.
- C acepta.
- A debe guardar el endpoint directo de C.
- Despues `LIST PRODUCTS C` o `START TRADE C` debe usar el endpoint directo aprendido.

Pasos:

1. Lanzar A, B y C segun las configs reales del proyecto.
2. Desde A:

```text
PLEDGE C <sigil>
```

3. Aceptar en C.
4. Verificar en A:

```text
PLEDGE STATUS
```

5. Ejecutar desde A:

```text
LIST PRODUCTS C
```

6. Apagar B o romper temporalmente la ruta DEFAULT o el hop usado.
7. Repetir desde A:

```text
LIST PRODUCTS C
```

Esperado:

- Debe seguir funcionando si C sigue vivo.
- Si falla solo porque B esta apagado, el bug sigue y no se esta usando la ruta directa aprendida.

Comprobacion recomendada:

```bash
grep -R "direct_endpoint\|known_endpoint\|network_get_direct_endpoint_for_realm" envoy network
```

## 13. PLEDGE RESPOND delegado a Envoy

Escenario de servidores:

- Montserrat: KingsLanding y Dragonstone.
- Matagalls: Driftmark y TheVale.
- Puigpedros: Winterfell.

Usa siempre los `config.dat` reales. No hardcodees IPs ni puertos.

Prueba basica:

1. Lanzar Dragonstone en Montserrat.
2. Lanzar TheVale en Matagalls.
3. Si la ruta lo necesita, lanzar tambien KingsLanding o Driftmark como hop.
4. En Dragonstone:

```text
PLEDGE TheVale data/dragonstone/dragonstone.png
```

5. En TheVale:

```text
PLEDGE STATUS
PLEDGE RESPOND Dragonstone ACCEPT
ENVOY STATUS
```

Esperado:

- TheVale lanza un Envoy para responder.
- El prompt no se bloquea.
- `ENVOY STATUS` puede mostrar `PLEDGE_RESPONSE` mientras siga activa.
- Dragonstone recibe la aceptacion.
- El Envoy vuelve a `FREE`.
- Ambos lados quedan aliados.

Prueba de rechazo:

```text
PLEDGE RESPOND Dragonstone REJECT
```

Esperado:

- Se lanza Envoy.
- Dragonstone recibe rechazo.
- TheVale no guarda endpoint directo como alianza activa.

Prueba sin Envoys libres:

1. Ocupa todos los Envoys de TheVale con otras misiones.
2. Ejecuta:

```text
PLEDGE RESPOND Dragonstone ACCEPT
```

Esperado:

- Debe aparecer `No free Envoy available.`
- No debe enviarse respuesta directa desde el Maester.
- El pledge debe seguir pendiente para poder reintentarlo.

Comprobaciones recomendadas:

```bash
grep -R "network_send_pledge_response" terminal network envoy
```

Interpretacion:

- Puede aparecer en `network.c`.
- No debe aparecer en `terminal/commands.c`.

```bash
grep -R "ENVOY_MISSION_PLEDGE_RESPONSE\|pledge-response" envoy terminal network
```

## 14. Pruebas de exclusion mutua de stock

### Trade entrante y LIST PRODUCTS local simultaneo

1. Lanzar dos Maesters aliados.
2. Desde A iniciar un trade hacia B.
3. Mientras B procesa el pedido, ejecutar varias veces en B:

```text
LIST PRODUCTS
```

Esperado:

- no segmentation fault;
- no datos corruptos;
- no cantidades negativas;
- no bloqueo permanente.

### Trade saliente aceptado y LIST PRODUCTS local simultaneo

1. Desde A hacer `START TRADE B`.
2. Enviar un pedido aceptado.
3. Mientras A recibe el resultado del Envoy, ejecutar:

```text
LIST PRODUCTS
```

Esperado:

- stock actualizado o estado anterior, pero nunca corrupcion;
- no crash;
- no deadlock.

### EXIT con Envoys o red activos

1. Lanzar `PLEDGE`, `LIST PRODUCTS <REALM>` o `START TRADE`.
2. Antes de que termine, ejecutar:

```text
EXIT
```

Esperado:

- se detiene la red;
- se destruyen Envoys;
- se guarda stock al final;
- no quedan zombies;
- `stock.db` no queda truncado ni corrupto.

### Ctrl+C con red activa

1. Lanzar una operacion de trade.
2. Pulsar `Ctrl+C`.

Esperado:

- shutdown limpio;
- stock guardado despues de parar red y Envoys;
- sin zombies;
- sin FDs abiertos.

Comandos utiles:

```bash
ps -ef | grep Maester
ps -ef | grep defunct
lsof -p <pid>
valgrind --leak-check=full --track-fds=yes ./Maester <config> <stock>
```

### Greps de validacion para stock

```bash
grep -R "pthread_mutex_t.*mutex\|mutex_initialized\|stock_lock\|stock_unlock" stock
grep -R "stock.count\|->stock.count" .
grep -n "network_shutdown\|envoy_manager_destroy\|stock_save" realm/maester.c
```

Interpretacion:

- `trade.c` y `commands.c` no deberian leer `stock.count` directamente.
- `stock_save` debe ir despues de `network_shutdown` y `envoy_manager_destroy`.
