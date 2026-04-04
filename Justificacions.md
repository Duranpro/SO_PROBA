==================================================

RESUMEN GLOBAL DE LA ARQUITECTURA DE LA PRÁCTICA
==================================================
El sistema se organiza alrededor de un proceso principal (Maester) que integra config, stock, network y envoys en un único contexto.
Hay tres niveles de ejecución concurrente: proceso principal (terminal/comandos), un hilo de red (servidor TCP), y varios procesos hijo Envoy.
La comunicación local Maester-Envoy se hace con pipes bidireccionales y mensajes binarios de control (asignar misión, completar, apagar).
La comunicación remota Maester-Maester se hace con sockets TCP y un protocolo propio de tramas fijas.
Maester actúa como orquestador: valida comandos, controla alianzas, delega misiones a envoys y coordina stock/red.
Los Envoys representan recursos de trabajo limitados: permiten modelar ocupación/liberación de “agentes” para misiones.
El stock se carga desde stock.db binario al arrancar, se opera en memoria y se persiste con open/read/write.
La comunicación remota usa routing por tabla de rutas + DEFAULT, permitiendo envío directo o por saltos.
La concurrencia se resuelve con mutex en estructuras compartidas de red/envoys y con bucles no bloqueantes (select) para no congelar la terminal.
==================================================
2. LISTA DE DECISIONES IMPORTANTES DETECTADAS
Contexto central MaesterContext para agrupar estado global del nodo.
Arquitectura híbrida: proceso principal + hilo de red + procesos Envoy.
Modelar Envoys como pool limitado de workers con estado (busy/alive/mission).
IPC Maester-Envoy con pipes y mensajes binarios fijos.
Hilo servidor TCP dedicado con select y accept en bucle.
Terminal interactiva con select + polling de tareas en background.
Uso explícito de syscalls (open/read/write/close/lseek/access) en vez de stdio alto nivel.
Persistencia de stock en fichero binario de registros fijos.
Aplicación de pedidos con validación previa + rollback si falla persistencia.
Protocolo propio de tramas de tamaño fijo con checksum.
Comunicación distribuida con sockets TCP entre reinos.
Routing por destino de reino + ruta DEFAULT + reenvío por hops.
Máquina de estados de alianzas y control de autorización.
Transferencia de ficheros por fases (header, bloques, ACK, MD5_ACK).
Integridad de archivos con MD5 calculado mediante fork+exec de md5sum.
Manejo de señales (SIGINT) para parada limpia.
Sincronización con pthread_mutex en red y envoys.
Modularización fuerte por responsabilidad (config/stock/network/trade/transfer/terminal/envoy/utils).
Validación estricta de entradas y errores de protocolo/red/ficheros.
Elección consciente de simplicidad (serialización de transferencias y sin thread pool complejo).
==================================================
3. JUSTIFICACIÓN DETALLADA DECISIÓN POR DECISIÓN
DECISIÓN 1: Contexto central MaesterContext

Qué se ha hecho en la práctica:
Se encapsulan config, stock, network y envoys en una sola estructura que se inicializa/destruye de forma ordenada.
Justificación técnica:
Evita estado disperso y facilita ciclo de vida claro de recursos.
Relación con la teoría de clase:
Separación de responsabilidades y control del estado del sistema como haría un núcleo con sus subsistemas.
Alternativa posible:
Variables globales sueltas por módulo.
Por qué la decisión elegida es defendible:
Reduce acoplamiento implícito y hace más mantenible la práctica.
Riesgos o limitaciones:
Si el contexto crece demasiado, puede concentrar demasiadas dependencias.
Posible mejora futura:
Subcontextos más finos (por ejemplo, RuntimeContext y StorageContext).
DECISIÓN 2: Arquitectura híbrida (proceso principal + hilo + procesos)

Qué se ha hecho en la práctica:
El comando de usuario vive en el proceso principal, la red entrante en un hilo, y los envoys son procesos hijo.
Justificación técnica:
Combina buen aislamiento (procesos) con baja latencia para E/S de red (hilo).
Relación con la teoría de clase:
Multiprogramación real, concurrencia entre actividades distintas y uso mixto proceso/hilo.
Alternativa posible:
Todo con threads o todo con procesos.
Por qué la decisión elegida es defendible:
Para una práctica SO es muy didáctica: se ven varios mecanismos del sistema operativo a la vez.
Riesgos o limitaciones:
Más compleja de razonar que una arquitectura única.
Posible mejora futura:
Documentar explícitamente el diagrama de concurrencia y estados de cada actor.
DECISIÓN 3: Pool limitado de Envoys

Qué se ha hecho en la práctica:
El número de envoys viene de config y cada uno tiene estado (busy, mission, assigned_at, etc.).
Justificación técnica:
Modela recursos finitos y evita lanzar trabajo ilimitado.
Relación con la teoría de clase:
Planificación, equidad y control de throughput bajo recursos limitados.
Alternativa posible:
Crear un proceso por cada misión sin límite.
Por qué la decisión elegida es defendible:
Simula planificación de recursos del SO de forma controlada.
Riesgos o limitaciones:
Si todos están ocupados, comandos se retrasan.
Posible mejora futura:
Cola explícita de tareas pendientes con política FIFO/prioridad.
DECISIÓN 4: IPC Maester-Envoy con pipes

Qué se ha hecho en la práctica:
Por cada Envoy hay dos pipes (ida y vuelta) con mensajes de control.
Justificación técnica:
IPC simple, robusto y adecuado para relación padre-hijo.
Relación con la teoría de clase:
Pipes unidireccionales, sincronización por flujo y aislamiento entre espacios de memoria.
Alternativa posible:
Shared memory + semáforos o sockets locales.
Por qué la decisión elegida es defendible:
Menos complejidad y muy alineado con contenidos clásicos de SO.
Riesgos o limitaciones:
Mensajería rígida y dependiente del formato binario.
Posible mejora futura:
Añadir versionado de mensaje o capa de serialización.
DECISIÓN 5: Hilo servidor con select

Qué se ha hecho en la práctica:
El servidor TCP corre en un hilo propio, con select de timeout para aceptar conexiones y revisar timeouts.
Justificación técnica:
Evita bloquear la interfaz de comandos.
Relación con la teoría de clase:
Concurrencia, respuesta del sistema y evitar bloqueos innecesarios en E/S.
Alternativa posible:
Red en el hilo principal bloqueante.
Por qué la decisión elegida es defendible:
Mejora tiempo de respuesta percibido por el usuario.
Riesgos o limitaciones:
Un solo hilo de red procesa clientes secuencialmente.
Posible mejora futura:
Modelo thread-per-connection o event loop no bloqueante completo.
DECISIÓN 6: Terminal con polling de background

Qué se ha hecho en la práctica:
Mientras espera teclado, se ejecuta polling de eventos de envoys y estado de operaciones remotas.
Justificación técnica:
La consola no “se queda ciega” mientras hay trabajo en segundo plano.
Relación con la teoría de clase:
Concurrencia cooperativa y planificación orientada a interactividad.
Alternativa posible:
Bloqueo total en lectura de línea.
Por qué la decisión elegida es defendible:
Da sensación de sistema vivo y reactivo.
Riesgos o limitaciones:
Polling periódico puede ser menos eficiente que eventos puros.
Posible mejora futura:
Integrar notificaciones por condición/eventfd.
DECISIÓN 7: Uso de syscalls de bajo nivel

Qué se ha hecho en la práctica:
Se usa open/read/write/close/lseek/access en stock, transfer y utilidades.
Justificación técnica:
Control fino de E/S, reintentos y comportamiento ante interrupciones.
Relación con la teoría de clase:
“Crides al sistema” y gestión de file descriptors por el kernel.
Alternativa posible:
fopen/fread/fwrite con stdio buffered.
Por qué la decisión elegida es defendible:
Alinea directamente práctica y teoría de SO.
Riesgos o limitaciones:
Más código manual y más fácil cometer errores sutiles.
Posible mejora futura:
Wrappers adicionales para homogeneizar manejo de errores.
DECISIÓN 8: Stock binario con registros fijos

Qué se ha hecho en la práctica:
stock.db guarda name[100] + amount + weight y se carga/guarda registro a registro.
Justificación técnica:
Formato simple y eficiente en lectura secuencial.
Relación con la teoría de clase:
E/S de bloques, persistencia y diseño de estructuras de datos en disco.
Alternativa posible:
CSV/JSON textual.
Por qué la decisión elegida es defendible:
Menos parsing en runtime y formato determinista.
Riesgos o limitaciones:
Menos portable entre arquitecturas (endianness/tamaño) y menos legible.
Posible mejora futura:
Encabezado de versión + serialización portable.
DECISIÓN 9: Aplicación de pedidos con rollback lógico

Qué se ha hecho en la práctica:
Primero valida disponibilidad completa, luego descuenta, y si stock_save falla revierte cantidades.
Justificación técnica:
Evita estado parcial inconsistente.
Relación con la teoría de clase:
Consistencia de recursos compartidos y sección crítica sobre estado persistente.
Alternativa posible:
Descontar “sobre la marcha” sin rollback.
Por qué la decisión elegida es defendible:
Es una mini transacción razonable para práctica académica.
Riesgos o limitaciones:
No hay journal real; un crash entre pasos puede dejar inconsistencias.
Posible mejora futura:
Write-ahead log o fichero temporal + rename atómico.
DECISIÓN 10: Protocolo propio de tramas fijas + checksum

Qué se ha hecho en la práctica:
Trama de 320 bytes con tipo, origen, destino, longitud, datos y checksum.
Justificación técnica:
Parsing simple y validación rápida.
Relación con la teoría de clase:
Diseño de protocolo de comunicación, encapsulado y detección de errores.
Alternativa posible:
Protocolo variable tipo texto delimitado.
Por qué la decisión elegida es defendible:
Reduce ambigüedad y facilita routing por campos fijos.
Riesgos o limitaciones:
Overhead en mensajes pequeños y rigidez del formato.
Posible mejora futura:
Añadir campo versión y longitud variable opcional.
DECISIÓN 11: Sockets TCP para comunicación distribuida

Qué se ha hecho en la práctica:
Cada reino escucha en TCP y envía tramas a otros nodos por connect/send/recv.
Justificación técnica:
Fiabilidad de transporte y simplicidad para una práctica.
Relación con la teoría de clase:
Sistemas distribuidos sobre red y abstracción de comunicación entre procesos remotos.
Alternativa posible:
UDP con control propio de fiabilidad.
Por qué la decisión elegida es defendible:
TCP evita reinventar retransmisión/orden en un proyecto docente.
Riesgos o limitaciones:
Más coste por conexión si se abre/cierra por trama.
Posible mejora futura:
Reutilizar conexiones persistentes por vecino.
DECISIÓN 12: Routing por hops con tabla y DEFAULT

Qué se ha hecho en la práctica:
Si el destino no es local, se reenvía según rutas directas o DEFAULT; si falla, se devuelve UNKNOWN_REALM.
Justificación técnica:
Permite topologías parciales sin conectividad total directa.
Relación con la teoría de clase:
Concepto de sistema distribuido y encaminamiento multi-salto.
Alternativa posible:
Solo comunicación directa punto a punto.
Por qué la decisión elegida es defendible:
Da realismo de red y permite escalar escenarios.
Riesgos o limitaciones:
No hay métrica de coste ni prevención de bucles complejos.
Posible mejora futura:
TTL/hop-count en trama y tabla de rutas con coste.
DECISIÓN 13: Máquina de estados de alianzas + autorización

Qué se ha hecho en la práctica:
Estados PENDING/ALLIED/REJECTED/FAILED/INACTIVE; solo aliados pueden listar productos o comerciar.
Justificación técnica:
Regla de negocio explícita y verificable.
Relación con la teoría de clase:
Estados de proceso (análogos a PCB) y control de acceso sobre recursos.
Alternativa posible:
Booleano simple allied.
Por qué la decisión elegida es defendible:
Evita transiciones ambiguas y mejora trazabilidad del flujo.
Riesgos o limitaciones:
Máquina de estados no formalizada en tabla de transición.
Posible mejora futura:
Validación centralizada de transiciones permitidas.
DECISIÓN 14: Transferencia por fases con ACK y MD5_ACK

Qué se ha hecho en la práctica:
Se envía cabecera con nombre/tamaño/md5, luego datos en bloques, luego verificación MD5 y respuesta final.
Justificación técnica:
Separa control y datos, y permite detectar corrupción.
Relación con la teoría de clase:
Sincronización de emisor-receptor, control de flujo lógico e integridad.
Alternativa posible:
Todo en un único mensaje o sin checksum final.
Por qué la decisión elegida es defendible:
Protocolo claro, depurable y razonable para ficheros.
Riesgos o limitaciones:
Solo una transferencia de salida y una de entrada activas a la vez.
Posible mejora futura:
Multiplexar transferencias por identificador de sesión.
DECISIÓN 15: MD5 con fork+exec y pipe

Qué se ha hecho en la práctica:
Se crea un hijo que ejecuta md5sum; el padre captura salida por pipe.
Justificación técnica:
Reutiliza herramienta del sistema y evita implementar hash manual.
Relación con la teoría de clase:
fork, exec, redirección dup2, pipes y waitpid.
Alternativa posible:
Librería criptográfica embebida.
Por qué la decisión elegida es defendible:
Muy didáctica desde punto de vista SO.
Riesgos o limitaciones:
Dependencia de md5sum (en Windows no funciona en esta implementación).
Posible mejora futura:
Implementar fallback portable con librería hash.
DECISIÓN 16: Señales para parada limpia (SIGINT)

Qué se ha hecho en la práctica:
SIGINT marca sig_atomic_t y el bucle principal sale limpiando recursos.
Justificación técnica:
Evita abortos bruscos y fuga de recursos.
Relación con la teoría de clase:
Señales como mecanismo asíncrono de control de proceso.
Alternativa posible:
Ignorar señal y depender de cierre forzado.
Por qué la decisión elegida es defendible:
Es robusta y segura para defensa oral.
Riesgos o limitaciones:
Solo maneja SIGINT; no cubre más señales.
Posible mejora futura:
Manejar SIGTERM y unificar política de shutdown.
DECISIÓN 17: Exclusión mutua con mutex en red/envoys

Qué se ha hecho en la práctica:
Estado compartido de alianzas, transferencias y envoys protegido con pthread_mutex.
Justificación técnica:
Evita corrupción por accesos concurrentes de hilo de red y hilo principal.
Relación con la teoría de clase:
Exclusión mutua, sincronización y secciones críticas.
Alternativa posible:
Sin bloqueo o lock-free ad hoc.
Por qué la decisión elegida es defendible:
Es el mecanismo clásico, comprensible y suficiente para el alcance.
Riesgos o limitaciones:
El stock no tiene mutex dedicado; hay ventanas de carrera en escenarios límite.
Posible mejora futura:
Añadir mutex propio de stock y revisar orden de cierre (parar red antes de stock_save final).
DECISIÓN 18: Modularización por dominios

Qué se ha hecho en la práctica:
Cada dominio tiene su módulo (config, stock, network, trade, transfer, terminal, envoy, utils).
Justificación técnica:
Cada módulo tiene una responsabilidad clara.
Relación con la teoría de clase:
Separación núcleo/servicios, mantenibilidad y reducción de acoplamiento.
Alternativa posible:
Archivo monolítico.
Por qué la decisión elegida es defendible:
Facilita pruebas, lectura y evolución sin romper todo.
Riesgos o limitaciones:
Más interfaces que mantener.
Posible mejora futura:
Contratos de interfaz más estrictos y tests por módulo.
DECISIÓN 19: Validación y control de errores en todas las capas

Qué se ha hecho en la práctica:
Se validan comandos, rutas, checksums, tipos de trama, estados de alianza, parseos numéricos y operaciones de archivo/red.
Justificación técnica:
Sistema más estable ante entradas inválidas y fallos de red/E/S.
Relación con la teoría de clase:
Robustez del SO y tratamiento defensivo de llamadas al sistema.
Alternativa posible:
“Happy path” con pocas comprobaciones.
Por qué la decisión elegida es defendible:
En distribución real los fallos son normales, no excepciones.
Riesgos o limitaciones:
Algunas rutas de error no distinguen causas muy finas.
Posible mejora futura:
Códigos de error unificados y logging estructurado.
DECISIÓN 20: Simplicidad deliberada para controlar complejidad

Qué se ha hecho en la práctica:
Se limita paralelismo de transferencias y se evita un diseño excesivamente sofisticado.
Justificación técnica:
Prioriza corrección y defendibilidad sobre optimización prematura.
Relación con la teoría de clase:
Trade-off clásico entre throughput y simplicidad/robustez.
Alternativa posible:
Alta concurrencia con múltiples transferencias y scheduler complejo.
Por qué la decisión elegida es defendible:
Para una práctica académica, un diseño claro y estable suele ser mejor que uno “muy potente” pero frágil.
Riesgos o limitaciones:
Throughput menor bajo carga alta; además hay detalles mejorables (envío TCP en una sola llamada send).
Posible mejora futura:
send/recv en bucle completo y cola de transferencias concurrentes con IDs.
==================================================
4. RELACIÓN GLOBAL ENTRE LA PRÁCTICA Y LA ASIGNATURA
La práctica representa muy bien Sistemas Operativos porque usa mecanismos reales del SO, no abstracciones “mágicas”: procesos (fork), señales, pipes, sockets, mutex y syscalls de E/S.
Integra concurrencia real en varios niveles: proceso principal interactivo, hilo servidor y procesos hijos envoys.
Trabaja IPC local (pipes) y comunicación distribuida remota (TCP), conectando perfectamente IPC + redes.
Aplica planificación práctica: recursos limitados (envoys), control de esperas, polling no bloqueante y estados de misión.
Refuerza modularización y separación de responsabilidades: cada parte del sistema tiene su función clara.
Demuestra robustez: validación de entradas, verificación de checksum/MD5, gestión de errores de red/fichero y limpieza de recursos en shutdown.
Introduce una visión de sistema distribuido realista: rutas, hop forwarding, destinos no alcanzables y respuestas de error de protocolo.
La parte de exclusión mutua es coherente con teoría: se protege estado compartido crítico con mutex, aunque queda margen de mejora en lock de stock.
En conjunto, no es solo “hacer comandos”: es diseñar un mini-sistema operativo distribuido orientado a procesos, E/S y sincronización.
==================================================
5. VERSIÓN RESUMIDA PARA DEFENSA ORAL
Mi práctica está montada como un nodo Maester que coordina configuración, stock, red y envoys.
No lo hice monolítico: separé por módulos para no mezclar red, persistencia, comandos y lógica de comercio.
Uso procesos hijo para los envoys porque quería modelar recursos reales y aislamiento de memoria.
Maester y envoys se hablan por pipes, que es IPC clásico padre-hijo y encaja con la teoría.
Para la red uso sockets TCP entre reinos porque necesito fiabilidad en envío de tramas y ficheros.
Implementé protocolo propio con trama fija, tipo de mensaje y checksum para validar integridad.
Soporto routing por saltos con tabla de rutas y DEFAULT, no solo envío directo.
Hay máquina de estados de alianzas para controlar autorización antes de comerciar.
La terminal no se bloquea: uso select y voy atendiendo eventos de fondo mientras espero comandos.
También tengo hilo de servidor para que la recepción de red no congele la interfaz.
El stock se persiste con open/read/write en binario y se actualiza con validación previa.
En pedidos hago lógica tipo transacción: valido, aplico, y si falla guardado revierto.
En transferencias uso cabecera + datos + ACK + verificación MD5, para robustez real.
Gestiono SIGINT para cerrar limpio, guardar estado y liberar recursos.
La solución prioriza corrección y defendibilidad; no es la más “bestia” en rendimiento, pero es sólida y coherente con SO.