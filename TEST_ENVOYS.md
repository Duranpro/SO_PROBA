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
