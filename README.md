# CDSP — Cyber Defense Simulation Protocol

## 1. Descripción

CDSP (Cyber Defense Simulation Protocol) es un sistema distribuido basado en sockets TCP que simula un entorno de ciberseguridad en tiempo real. Los clientes se conectan a un servidor central para participar en partidas donde asumen roles de atacante o defensor.

El sistema integra:

- Un protocolo de aplicación propio basado en texto
- Un servidor concurrente multicliente
- Un servicio de autenticación independiente
- Clientes en C (CLI) y Python (GUI)
- Un servidor HTTP para visualización de partidas

---

## 2. Arquitectura

El sistema sigue una arquitectura modular con separación de responsabilidades:

- **Servidor de juego**
  - Maneja lógica del juego y estado global
  - Gestiona múltiples clientes concurrentes
- **Identity Service**
  - Encargado de la autenticación de usuarios
- **Clientes**
  - Cliente en C: interacción por consola
  - Cliente en Python: interfaz gráfica
- **Servidor HTTP**
  - Permite autenticación web y consulta de partidas

### Comunicación

- Cliente ↔ Servidor: TCP
- Servidor ↔ Identity Service: TCP
- Navegador ↔ HTTP Server: HTTP

---

## 3. Estructura del Proyecto

```

.
├── client-c/
├── client-python/
├── server/
├── identity-service/
├── docs/

```

---

## 4. Protocolo CDSP

### 4.1 Formato de mensajes

Todos los mensajes siguen el formato:

```

COMMAND arg1 arg2 ...\r\n

```

- Codificación: texto plano
- Delimitador: `\r\n`
- Sensible a mayúsculas

---

### 4.2 Comandos del Cliente

| Comando | Parámetros | Descripción |
|--------|-----------|------------|
| HELLO | username password | Autenticación |
| LIST | — | Listar partidas activas |
| CREATE | — | Crear nueva sala |
| JOIN | room_id | Unirse a sala |
| MOVE | x y | Movimiento en el mapa |
| ATTACK | resource_id | Iniciar ataque |
| DEFEND | resource_id | Defender recurso |
| QUIT | — | Cerrar conexión |

---

### 4.3 Respuestas del Servidor

| Respuesta | Parámetros | Descripción |
|----------|-----------|------------|
| WELCOME | user role | Autenticación exitosa |
| JOINED | room width height | Entrada a sala |
| START | — | Inicio de partida |
| MOVED | user x y | Movimiento |
| FOUND | id x y | Recurso descubierto |
| ALERT | id x y time | Ataque iniciado |
| MITIGATED | id user | Ataque detenido |
| BREACH | id | Recurso comprometido |
| GAMEOVER | result | Fin de partida |
| ERR | code message | Error |

---

## 5. Flujo de Operación

1. Cliente se conecta al servidor
2. Cliente envía `HELLO`
3. Servidor valida credenciales con identity-service
4. Cliente accede al lobby (`LIST`, `CREATE`, `JOIN`)
5. Inicio automático cuando hay roles suficientes
6. Interacción en tiempo real:

```

ATTACK → ALERT → DEFEND → MITIGATED / BREACH

````

---

## 6. Concurrencia

- Modelo: **thread-per-client**
- Cada conexión es manejada por un hilo independiente
- Estado global protegido mediante mutex:

```c
pthread_mutex_t lock;
````

* Acceso concurrente controlado en:

  * Registro de jugadores
  * Salas
  * Recursos

---

## 7. Lógica del Juego

* Tamaño del mapa: `100 x 100`
* Recursos críticos:

  * (25, 40)
  * (75, 70)

### Estados de recursos

* `SAFE`
* `UNDER_ATTACK`
* `BREACHED`

### Reglas

* Solo atacantes pueden ejecutar `ATTACK`
* Solo defensores pueden ejecutar `DEFEND`
* Un ataque activa un temporizador de 30 segundos
* Si no se defiende → `BREACH`
* Si se defiende → `MITIGATED`

---

## 8. Servicio de Autenticación

### Protocolo

```
AUTH username password
```

### Respuestas

```
OK ROLE
FAIL
```

### Usuarios disponibles

| Usuario | Contraseña  | Rol      |
| ------- | ----------- | -------- |
| user1   | password123 | ATTACKER |
| user2   | password123 | DEFENDER |

---

## 9. Servidor HTTP

Disponible en:

```
http://localhost:<puerto_http>
```

### Funcionalidades

* Login web
* Visualización de partidas activas
* Consulta de estado del sistema

---

## 10. Ejecución del Sistema

### 10.1 Identity Service

```bash
cd identity-service
make
./identity 9090
```

---

### 10.2 Servidor

```bash
cd server
make
export AUTH_HOST=localhost
export AUTH_PORT=9090
./servidor 8080 log.txt
```

---

### 10.3 Cliente C

```bash
cd client-c
make
./cliente localhost 8080
```

---

### 10.4 Cliente Python

```bash
cd client-python
python3 client.py
```

---

## 11. Ejemplo de Sesión

```
HELLO user1 password123
→ WELCOME user1 ATTACKER

CREATE
→ JOINED 1 100 100

MOVE 10 20
→ MOVED user1 10 20

ATTACK 1
→ ALERT 1 25 40 30
```

---

## 12. Manejo de Errores

El servidor responde con:

```
ERR <codigo> <descripcion>
```

Ejemplos:

* `ERR 401 UNAUTHORIZED`
* `ERR 404 ROOM_NOT_FOUND`
* `ERR 422 INVALID_COMMAND`
* `ERR 403 FORBIDDEN`