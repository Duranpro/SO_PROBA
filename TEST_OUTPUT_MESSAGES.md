# TEST_OUTPUT_MESSAGES.md

| Accion | Mensaje esperado |
| ------ | ---------------- |
| `PLEDGE TheVale dragonstone.png` | `Pledge sent to TheVale.` |
| `PLEDGE STATUS` sin pledges | `You have no pledges awaiting or accepted` |
| `PLEDGE STATUS` con estados | `- TheVale: PENDING` / `- TheVale: ACCEPTED` / `- TheVale: REJECTED` / `- TheVale: FAILED` |
| Recepcion de `PLEDGE` | `>>> Alliance request received from Dragonstone.` |
| `PLEDGE RESPOND Dragonstone ACCEPT` | `Alliance with Dragonstone established.` |
| `PLEDGE RESPOND Dragonstone REJECT` | `Alliance with Dragonstone rejected.` |
| `PLEDGE RESPOND Dragonstone ACCEPT` sin pendiente | `No pending pledge from Dragonstone.` |
| `PLEDGE` o `PLEDGE RESPOND` sin Envoys libres | `All envoys are occupied. Your command must wait.` |
| `ENVOY STATUS` libre | `- Envoy 1: FREE` |
| `ENVOY STATUS` ocupado | `- Envoy 1: ON MISSION (PLEDGE to TheVale)` |
| `LIST PRODUCTS` local | `--- Trade Ledger ---` seguido de la tabla local y `Total Entries: <N>` |
| `LIST PRODUCTS TheVale` sin alianza | `The gates of commerce with TheVale remain closed; no alliance binds you.` |
| `LIST PRODUCTS TheVale` con alianza | `Listing products from TheVale:` seguido de `1. <Product> (<amount> units)` |
| Recepcion de `LIST PRODUCTS` | `>>>LIST PRODUCTS request from Dragonstone.` + `Unveiling the vault of goods...` + `The list has been sealed and sent forth` |
| `START TRADE` sin argumentos | `Missing arguments, can't start a trade. Please review the syntax.` |
| `START TRADE TheVale` correcto | `Trade with TheVale begins.` + `A direct path is open; your houses are allied, and no intermediaries stand in between.` + `Available products: <Product1>, <Product2>.` |
| `START TRADE TheVale` sin catalogo remoto | `No products available. Use LIST PRODUCTS` y en la linea siguiente `first.` |
| Prompt de trade | `(trade)> ` |
| `send` con cesta vacia | `Trade list is empty.` |
| `send` correcto | `Trade list has been dispatched to TheVale.` |
| `cancel` | `Trade cancelled.` |
| `add/remove` con producto invalido | `Product not available.` |
| `add/remove` con cantidad invalida | `Invalid amount.` |
| Recepcion de `TRADE` | `>>> Trade request received from Dragonstone.` |
| Trade entrante sin stock suficiente | `The vaults stand empty; the order cannot be fulfilled` |
| Trade entrante aceptado | `Order processed successfully. Stock updated.` |
| Resultado de trade aceptado en origen | `>>> Order accepted by TheVale. Stock updated.` |
| Resultado de trade rechazado en origen | `>>> Order rejected by TheVale.` |
| Timeout de trade | `>>> Trade with TheVale failed (TIMEOUT).` |
| Fallo generico de trade | `>>> Trade with TheVale failed.` |
| Hop intermedio | `>>> Received hop: Dragonstone -> TheVale (PLEDGE)` + `Found route: TheVale -> <ip>:<port>` + `Forwarding...` |
| `EXIT` | `The Maester of Dragonstone signs off. The ravens rest.` |
| Inicializacion correcta | `Maester of Dragonstone initialized. The board is set.` |
| Error de conexion | `Connection failed.` |
| Error de ruta | `Route not found.` |
| Error de checksum | `Invalid checksum.` |
| NACK / descarte | `Frame discarded.` |
