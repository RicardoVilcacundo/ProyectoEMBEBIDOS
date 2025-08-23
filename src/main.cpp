#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include <Keypad.h>
#include <Keypad_I2C.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "apwifieeprommode.h"
#include <EEPROM.h>




// ========== CONFIGURACIÓN ========== //
#define WEBSOCKET_PING_INTERVAL 30000
#define WEBSOCKET_PING_TIMEOUT 10000
#define HTTP_SERVER_TIMEOUT 5
#define I2C_SDA 21
#define I2C_SCL 22
#define I2C_FREQ 100000
#define I2C_RETRIES 3
#define I2C_TIMEOUT_MS 50
#define BUZZER_PIN 2  // Pin para el zumbador

// Configuración matrices LED
#define HARDWARE_TYPE MD_MAX72XX::PAROLA_HW
#define MAX_DEVICES 1

// Matriz 1 (Jugador 1)
#define DATA_PIN_1 23
#define CLK_PIN_1 18
#define CS_PIN_1 5
MD_MAX72XX mx1(HARDWARE_TYPE, DATA_PIN_1, CLK_PIN_1, CS_PIN_1, MAX_DEVICES);

// Matriz 2 (Jugador 2)
#define DATA_PIN_2 19
#define CLK_PIN_2 16
#define CS_PIN_2 17
MD_MAX72XX mx2(HARDWARE_TYPE, DATA_PIN_2, CLK_PIN_2, CS_PIN_2, MAX_DEVICES);

// Configuración keypads
const byte ROWS = 4, COLS = 4;

// Keypad 1 (directo - Jugador 1)
char keys1[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins1[ROWS] = {13, 12, 14, 27};
byte colPins1[COLS] = {32, 4, 33, 15};
Keypad keypad1 = Keypad(makeKeymap(keys1), rowPins1, colPins1, ROWS, COLS);

// Keypad 2 (I2C - Jugador 2)
char keys2[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins2[ROWS] = {0, 1, 2, 3};
byte colPins2[COLS] = {4, 5, 6, 7};
Keypad_I2C keypad2(makeKeymap(keys2), rowPins2, colPins2, ROWS, COLS, 0x38, PCF8574);

const int botonMaquina = 25;
const int botonMultijugador = 26;

// ========== CONFIGURACIÓN WIFI & WEBSOCKET ========== //
const char* ssid = "ESP32-BARCOS";
const char* password = "12345678";

WebSocketsServer webSocket = WebSocketsServer(81, "", "mqtt");

// ========== CONFIGURACIÓN SUPABASE ========== //
const String supabaseUrl = "https://xinbaubmczexyulddlcd.supabase.co";
const String supabaseKey = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InhpbmJhdWJtY3pleHl1bGRkbGNkIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NTMwNDAwODEsImV4cCI6MjA2ODYxNjA4MX0.4Q8vNB4-pv6cLthnpfJI33H84lP2bSmjd_Z81TehSss";
const String supabaseAuth = "Bearer " + supabaseKey;

// ========== ESTRUCTURAS DEL JUEGO ========== //
struct Jugador {
  String nombre;
  IPAddress ip;
  bool barcos[4][4] = {{false}};
  bool disparos[4][4] = {{false}};
  bool listo = false;
  uint8_t numCliente = 255;
  bool conectado = false;
};

enum ModoJuego { NINGUNO, VS_MAQUINA, MULTIJUGADOR };
enum NivelDificultad { FACIL, MEDIO, DIFICIL };

ModoJuego modo = NINGUNO;
NivelDificultad nivelActual = FACIL;

Jugador jugadores[2];
int jugadorActual = 0;
int turnoActual = 0;
bool juegoIniciado = false;
bool juegoTerminado = false;
bool nivelCompletado = false;
unsigned long ultimoCambioTurno = 0;
unsigned long ultimoDisparo = 0;
unsigned long tiempoMostrarMensaje = 0;
String mensajeMatrix1 = "";
String mensajeMatrix2 = "";
const unsigned long intervaloCambioTurno = 3000;
const unsigned long tiempoMostrarDisparo = 3000;
const unsigned long tiempoMostrarNivel = 3000;
const unsigned long tiempoReinicio = 5000;
unsigned long tiempoFinJuego = 0;
bool mostrarDisparoMaquina = false;
int ultimoDisparoFila = -1;
int ultimoDisparoCol = -1;
bool ultimoDisparoAcierto = false;

// ========== FUNCIONES DEL ZUMBADOR ========== //

void setupBuzzer() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW); // Asegurarse que está silenciado al inicio
}
// ========== FUNCIONES DE SONIDO (LEDC) ========== //

void sonidoInicio() {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(300);
    digitalWrite(BUZZER_PIN, LOW);
}

void sonidoDisparo() {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
}

void sonidoAcierto() {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
}

void sonidoCambioTurno() {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(150);
    digitalWrite(BUZZER_PIN, LOW);
    delay(50);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(150);
    digitalWrite(BUZZER_PIN, LOW);
}

void sonidoVictoria() {
    for(int i = 0; i < 3; i++) {
        digitalWrite(BUZZER_PIN, HIGH);
        delay(100);
        digitalWrite(BUZZER_PIN, LOW);
        delay(50);
        digitalWrite(BUZZER_PIN, HIGH);
        delay(100);
        digitalWrite(BUZZER_PIN, LOW);
        delay(50);
    }
}

void sonidoDerrota() {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(300);
    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(500);
    digitalWrite(BUZZER_PIN, LOW);
}

// ========== PÁGINAS HTML ========== //
const char paginaInicio[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<title>Registro</title><style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body { 
  background: linear-gradient(135deg, #1e3c72 0%, #2a5298 100%);
  color: #fff; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
  display: flex; justify-content: center; align-items: center; 
  min-height: 100vh; padding: 20px;
}
.container {
  background: rgba(0, 0, 0, 0.7); padding: 30px; 
  border-radius: 15px; text-align: center; 
  width: 100%; max-width: 350px;
  box-shadow: 0 10px 25px rgba(0, 0, 0, 0.5);
}
h2 { margin-bottom: 20px; font-size: 1.8rem; }
input {
  padding: 12px 15px; width: 100%; margin-bottom: 20px;
  border-radius: 8px; border: none; font-size: 1rem;
  background: rgba(255, 255, 255, 0.9);
}
button {
  padding: 12px; width: 100%; background: #4CAF50;
  color: #fff; border: none; border-radius: 8px;
  font-size: 1rem; font-weight: bold; cursor: pointer;
  transition: all 0.3s ease; box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
  margin-bottom: 10px;
}
button:hover { background: #45a049; transform: translateY(-2px); }
button:active { transform: translateY(0); }
.historial-btn {
  background: #2196F3;
}
.historial-btn:hover { background: #0b7dda; }
@media (max-width: 400px) {
  .container { padding: 20px; }
  h2 { font-size: 1.5rem; }
}
</style></head>
<body>
<div class="container">
  <h2>Ingresa tu nombre</h2>
  <input type="text" id="nombre" placeholder="Tu nombre" autocomplete="off">
  <button onclick="enviarNombre()">Comenzar</button>
  <button class="historial-btn" onclick="verHistorial()">Ver Historial</button>
</div>
<script>
function enviarNombre() {
  const nombre = document.getElementById('nombre').value.trim();
  if(!nombre) { alert("Por favor ingresa un nombre"); return; }
  
  const btn = document.querySelector('button');
  btn.disabled = true;
  btn.textContent = "Enviando...";
  
  fetch('/nombre', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({nombre: nombre})
  })
  .then(r => {
    if(r.ok) return window.location.href = "/registro_barcos";
    throw new Error('Error en el servidor');
  })
  .catch(e => {
    alert("Error: " + e.message);
    btn.disabled = false;
    btn.textContent = "Comenzar";
  });
}

function verHistorial() {
  window.location.href = "/historial";
}
</script></body></html>
)rawliteral";

const char paginaRegistroBarcos[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<title>Barcos</title><style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
  background: linear-gradient(135deg, #1e3c72 0%, #2a5298 100%);
  color: #fff; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
  padding: 20px; text-align: center;
}
h2 { margin: 15px 0; font-size: 1.8rem; }
#info { font-size: 1.2rem; margin: 10px 0; }
.error {
  color: #ff6b6b; margin: 10px 0; min-height: 20px;
  font-weight: bold;
}
.tablero-container {
  display: flex; justify-content: center;
  margin: 20px 0;
}
table {
  margin: 0 auto; border-collapse: collapse;
  box-shadow: 0 5px 15px rgba(0, 0, 0, 0.3);
}
td {
  width: 60px; height: 60px; border: 2px solid rgba(255, 255, 255, 0.3);
  text-align: center; cursor: pointer; background: rgba(0, 0, 0, 0.3);
  transition: all 0.2s ease;
}
@media (max-width: 400px) {
  td { width: 50px; height: 50px; }
}
td:hover { background: rgba(255, 255, 255, 0.1); }
td.selected { background: #4CAF50; }
button {
  padding: 12px 25px; background: #4CAF50;
  color: #fff; border: none; border-radius: 8px;
  font-size: 1rem; font-weight: bold; cursor: pointer;
  margin-top: 20px; transition: all 0.3s ease;
  box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);
}
button:hover { background: #45a049; transform: translateY(-2px); }
button:active { transform: translateY(0); }
button:disabled {
  background: #cccccc; cursor: not-allowed;
  transform: none;
}
</style></head>
<body>
<h2>Coloca tus 3 barcos</h2>
<p id="info">Barcos: 0/3</p>
<div id="error-msg" class="error"></div>
<div class="tablero-container">
  <table id="matriz"><tbody>
    <tr><td data-pos="0,0"></td><td data-pos="0,1"></td><td data-pos="0,2"></td><td data-pos="0,3"></td></tr>
    <tr><td data-pos="1,0"></td><td data-pos="1,1"></td><td data-pos="1,2"></td><td data-pos="1,3"></td></tr>
    <tr><td data-pos="2,0"></td><td data-pos="2,1"></td><td data-pos="2,2"></td><td data-pos="2,3"></td></tr>
    <tr><td data-pos="3,0"></td><td data-pos="3,1"></td><td data-pos="3,2"></td><td data-pos="3,3"></td></tr>
  </tbody></table>
</div>
<button id="enviarBtn" disabled>Enviar</button>

<script>
const socket = new WebSocket('ws://' + window.location.hostname + ':81/');
let miJugador = null;

socket.onmessage = function(event) {
  const data = JSON.parse(event.data);
  if(data.type === "redirigir") {
    window.location.href = "/juego";
  }
};

document.addEventListener('DOMContentLoaded', () => {
  fetch('/estado_juego')
    .then(r => {
      if(!r.ok) throw new Error('Error de conexión');
      return r.json();
    })
    .then(data => {
      if(data.modo === "NINGUNO") {
        document.getElementById('error-msg').textContent = "⚠️ Debes seleccionar un tipo de juego primero";
        setTimeout(() => window.location.href = "/", 2000);
      }
      miJugador = data.jugador;
    })
    .catch(e => {
      document.getElementById('error-msg').textContent = "Error: " + e.message;
    });
});

const celdas = document.querySelectorAll('#matriz td');
const info = document.getElementById('info');
const enviarBtn = document.getElementById('enviarBtn');
const errorMsg = document.getElementById('error-msg');
let seleccionados = new Set();

celdas.forEach(td => {
  td.addEventListener('click', function() {
    const pos = this.getAttribute('data-pos');
    if(this.classList.contains('selected')) {
      this.classList.remove('selected');
      seleccionados.delete(pos);
    } else if(seleccionados.size < 3) {
      this.classList.add('selected');
      seleccionados.add(pos);
    }
    info.textContent = `Barcos: ${seleccionados.size}/3`;
    enviarBtn.disabled = seleccionados.size !== 3;
  });
});

enviarBtn.addEventListener('click', () => {
  if(seleccionados.size !== 3) {
    errorMsg.textContent = "⚠️ Debes seleccionar exactamente 3 barcos";
    return;
  }
  
  errorMsg.textContent = "";
  enviarBtn.disabled = true;
  enviarBtn.textContent = "Enviando...";
  
  fetch('/posiciones', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ posiciones: Array.from(seleccionados) })
  })
  .then(r => {
    if(!r.ok) return r.text().then(text => { throw new Error(text) });
    return r.json();
  })
  .then(data => {
    if(data.iniciar) {
      // Redirigir automáticamente vía WebSocket
    } else {
      errorMsg.textContent = `⏳ Esperando al otro jugador...`;
    }
  })
  .catch(e => {
    errorMsg.textContent = "⚠️ " + (e.message || "Error al enviar posiciones");
    console.error("Error:", e);
    enviarBtn.disabled = false;
    enviarBtn.textContent = "Enviar";
  });
});
</script></body></html>
)rawliteral";

const char paginaJuego[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<title>Batalla Naval</title><style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
  background: linear-gradient(135deg, #1e3c72 0%, #2a5298 100%);
  color: #fff; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
  padding: 15px; text-align: center;
}
h1 { margin: 10px 0 20px; font-size: 2rem; }
#nivel {
  background: rgba(255, 255, 255, 0.2); padding: 8px 12px;
  border-radius: 20px; display: inline-block; margin-bottom: 10px;
  font-weight: bold;
}
#mensaje-turno {
  padding: 12px; border-radius: 8px; margin: 15px auto;
  max-width: 250px; font-weight: bold; font-size: 1.1rem;
  box-shadow: 0 3px 6px rgba(0, 0, 0, 0.16);
}
.contenedor {
  display: flex; justify-content: center; gap: 20px;
  flex-wrap: wrap; margin: 20px 0;
}
.tablero-container {
  background: rgba(0, 0, 0, 0.3); padding: 15px;
  border-radius: 10px; margin: 10px;
  box-shadow: 0 5px 15px rgba(0, 0, 0, 0.2);
  flex: 1; min-width: 250px; max-width: 300px;
}
h2 { margin-bottom: 10px; font-size: 1.3rem; }
table { margin: 0 auto; border-collapse: collapse; }
td {
  width: 40px; height: 40px; border: 1px solid rgba(255, 255, 255, 0.3);
  transition: all 0.3s ease;
}
@media (max-width: 500px) {
  td { width: 30px; height: 30px; }
}
.agua { background: #00bcd4; }
.barco { background: #4caf50; }
.acierto { background: #f44336; }
.espera { background: #ff9800; }
.ganador { background: #4CAF50; }
.perdedor { background: #f44336; }
.historial-btn {
  background: #2196F3;
  margin: 20px auto;
  display: block;
  max-width: 200px;
}
.historial-btn:hover { background: #0b7dda; }
</style></head>
<body>
<h1>Batalla Naval</h1>
<div id="nivel">Nivel: Fácil</div>
<div id="mensaje-turno" class="espera">Cargando...</div>
<div class="contenedor">
  <div class="tablero-container">
    <h2>Tu Tablero</h2>
    <div id="tablero-propio"></div>
  </div>
  <div class="tablero-container">
    <h2>Tablero Enemigo</h2>
    <div id="tablero-enemigo"></div>
  </div>
</div>
<script>
const socket = new WebSocket('ws://' + window.location.hostname + ':81/');
let miJugador = null;
let miNivel = "Fácil";

socket.onmessage = function(event) {
  const data = JSON.parse(event.data);
  if(data.type === "estado") {
    actualizarInterfaz(data.estado);
  } else if(data.type === "disparo") {
    actualizarDisparo(data.disparo);
  } else if(data.type === "redirigir") {
    window.location.href = "/";
  } else if(data.type === "nivel") {
    document.getElementById('nivel').textContent = "Nivel: " + data.nivel;
    miNivel = data.nivel;
  }
};

function actualizarInterfaz(d) {
  document.getElementById('tablero-propio').innerHTML = crearTablero(d, true);
  document.getElementById('tablero-enemigo').innerHTML = crearTablero(d, false);
  
  const mensaje = document.getElementById('mensaje-turno');
  if(d.juegoTerminado) {
    mensaje.textContent = d.ganador === miJugador ? '¡Ganaste!' : '¡Perdiste!';
    mensaje.className = d.ganador === miJugador ? 'ganador' : 'perdedor';
  } else {
    mensaje.textContent = d.miTurno ? '¡Tu turno!' : 'Esperando turno...';
    mensaje.className = d.miTurno ? 'ganador' : 'espera';
  }
}

function actualizarDisparo(disparo) {
  const tablero = disparo.jugador === miJugador ? 'enemigo' : 'propio';
  const celda = document.querySelector(`#tablero-${tablero} td[data-fila="${disparo.fila}"][data-col="${disparo.col}"]`);
  if(celda) {
    celda.className = disparo.acierto ? 'acierto' : 'agua';
  }
}

function crearTablero(d, esPropio) {
  let html = '<table>';
  for(let i = 0; i < 4; i++) {
    html += '<tr>';
    for(let j = 0; j < 4; j++) {
      let clase = '';
      if(esPropio) {
        if(d.barcosPropios[i][j] && d.disparosEnemigo[i][j]) clase = 'acierto';
        else if(d.barcosPropios[i][j]) clase = 'barco';
        else if(d.disparosEnemigo[i][j]) clase = 'agua';
      } else if(d.disparosPropios[i][j]) {
        clase = d.aciertosPropios[i][j] ? 'acierto' : 'agua';
      }
      html += `<td class="${clase}" data-fila="${i}" data-col="${j}"></td>`;
    }
    html += '</tr>';
  }
  return html + '</table>';
}

function verHistorial() {
  window.location.href = "/historial";
}

fetch('/estado_juego')
  .then(r => r.json())
  .then(d => {
    miJugador = d.jugador;
    actualizarInterfaz(d);
    if(d.nivel) {
      document.getElementById('nivel').textContent = "Nivel: " + d.nivel;
      miNivel = d.nivel;
    }
  });
</script></body></html>
)rawliteral";

const char paginaHistorial[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Historial - Batalla Naval</title>
<style>
* { box-sizing: border-box; margin: 0; padding: 0; }
body { 
  background: linear-gradient(135deg, #1e3c72 0%, #2a5298 100%);
  color: #fff; font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
  padding: 20px;
}
.container {
  max-width: 800px; margin: 0 auto;
}
h1 { text-align: center; margin: 20px 0; }
table {
  width: 100%; border-collapse: collapse;
  margin: 20px 0; box-shadow: 0 5px 15px rgba(0,0,0,0.2);
}
th, td { padding: 12px 15px; text-align: left; border-bottom: 1px solid rgba(255,255,255,0.1); }
th { background: rgba(0,0,0,0.3); }
tr:hover { background: rgba(255,255,255,0.05); }
.winner { color: #4CAF50; font-weight: bold; }
.actions { text-align: center; margin: 20px 0; }
.btn {
  padding: 10px 20px; background: #4CAF50;
  color: #fff; border: none; border-radius: 5px;
  text-decoration: none; display: inline-block;
  margin: 0 10px; cursor: pointer;
}
.btn.historial {
  background: #2196F3;
}
</style>
</head>
<body>
<div class="container">
  <h1>Historial de Partidas</h1>
  
  <table id="partidas">
    <thead>
      <tr>
        <th>Fecha</th>
        <th>Jugador 1</th>
        <th>vs</th>
        <th>Jugador 2</th>
        <th>Ganador</th>
        <th>Modo</th>
      </tr>
    </thead>
    <tbody></tbody>
  </table>
  
  <div class="actions">
    <a href="/" class="btn historial">Inicio</a>
  </div>
</div>

<script>
fetch('/historial_json')
  .then(r => r.json())
  .then(partidas => {
    const tbody = document.querySelector('#partidas tbody');
    
    partidas.forEach(p => {
      const tr = document.createElement('tr');
      
      const fecha = new Date(p.creado_en).toLocaleString();
      
      tr.innerHTML = `
        <td>${fecha}</td>
        <td>${p.nombre_jugador1}</td>
        <td>vs</td>
        <td>${p.nombre_jugador2}</td>
        <td class="winner">${p.nombre_ganador}</td>
        <td>${p.modo_juego}${p.nivel ? ' ('+p.nivel+')' : ''}</td>
      `;
      
      tbody.appendChild(tr);
    });
  });
</script>
</body></html>
)rawliteral";

void setupI2C() {
  Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);
  Wire.setTimeOut(I2C_TIMEOUT_MS);
}

bool checkI2CDevice(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

void resetI2CBus() {
  Wire.end();
  delay(50);
  Wire.begin(I2C_SDA, I2C_SCL, I2C_FREQ);
}

void reiniciarDisparos() {
  for(int i = 0; i < 2; i++) {
    memset(jugadores[i].disparos, false, sizeof(jugadores[i].disparos));
  }
  mx1.clear();
  mx2.clear();
}

void configurarBarcosMaquina() {
  memset(jugadores[1].barcos, false, sizeof(jugadores[1].barcos));
  
  int barcosColocados = 0;
  while(barcosColocados < 3) {
    int fila = random(0, 4);
    int col = random(0, 4);
    if(!jugadores[1].barcos[fila][col]) {
      jugadores[1].barcos[fila][col] = true;
      barcosColocados++;
    }
  }
}

void dibujarCuadro(MD_MAX72XX &mx, uint8_t fila, uint8_t col, bool esAcierto) {
  uint8_t x = 7 - (fila * 2), y = col * 2;
  mx.control(MD_MAX72XX::INTENSITY, esAcierto ? 15 : 5);
  for(uint8_t dy = 0; dy < 2; dy++) {
    for(uint8_t dx = 0; dx < 2; dx++) {
      mx.setPoint(y + dy, x - dx, true);
    }
  }
}

void mostrarDisparoUnico(MD_MAX72XX &mx, int fila, int col, bool acierto) {
  mx.clear();
  dibujarCuadro(mx, fila, col, acierto);
}

void mostrarMensajeMatrix(MD_MAX72XX &mx, String msg) {
  mx.clear();
  
  if(msg == "FACIL" || msg == "N1") {
    mx.setColumn(0, 0b11111111);
    mx.setColumn(1, 0b10000001);
    mx.setColumn(2, 0b10011001);
    mx.setColumn(3, 0b10011001);
    mx.setColumn(4, 0b10011001);
    mx.setColumn(5, 0b10011001);
    mx.setColumn(6, 0b10000001);
    mx.setColumn(7, 0b11111111);
  } else if(msg == "MEDIO" || msg == "N2") {
    mx.setColumn(0, 0b11111111);
    mx.setColumn(1, 0b10000001);
    mx.setColumn(2, 0b10100101);
    mx.setColumn(3, 0b10100101);
    mx.setColumn(4, 0b10100101);
    mx.setColumn(5, 0b10100101);
    mx.setColumn(6, 0b10000001);
    mx.setColumn(7, 0b11111111);
  } else if(msg == "DIFICIL" || msg == "N3") {
    mx.setColumn(0, 0b11111111);
    mx.setColumn(1, 0b10000001);
    mx.setColumn(2, 0b10101001);
    mx.setColumn(3, 0b10111001);
    mx.setColumn(4, 0b10111001);
    mx.setColumn(5, 0b10101001);
    mx.setColumn(6, 0b10000001);
    mx.setColumn(7, 0b11111111);
  }
}

void actualizarMatrizLED(MD_MAX72XX &mx, int jugador) {
  if(jugador < 0 || jugador > 1) return;
  
  mx.clear();
  mx.control(MD_MAX72XX::INTENSITY, 5);
  
  if(mostrarDisparoMaquina && millis() - ultimoDisparo < intervaloCambioTurno) {
    mostrarDisparoUnico(mx, ultimoDisparoFila, ultimoDisparoCol, ultimoDisparoAcierto);
    return;
  }
  
  int oponente = 1 - jugador;
  
  for(int i = 0; i < 4; i++) {
    for(int j = 0; j < 4; j++) {
      if(jugadores[jugador].disparos[i][j]) {
        bool acierto = jugadores[oponente].barcos[i][j];
        dibujarCuadro(mx, i, j, acierto);
      }
    }
  }
}

void enviarEstadoJuego(int jugadorIndex) {
  if(jugadorIndex < 0 || jugadorIndex > 1 || jugadores[jugadorIndex].numCliente == 255) return;

  StaticJsonDocument<512> doc;
  doc["type"] = "estado";
  
  JsonObject estado = doc.createNestedObject("estado");
  estado["jugador"] = jugadorIndex;
  estado["miTurno"] = (turnoActual == jugadorIndex);
  estado["juegoTerminado"] = juegoTerminado;
  if(juegoTerminado) estado["ganador"] = turnoActual;

  if(modo == VS_MAQUINA) {
    switch(nivelActual) {
      case FACIL: estado["nivel"] = "Fácil"; break;
      case MEDIO: estado["nivel"] = "Medio"; break;
      case DIFICIL: estado["nivel"] = "Difícil"; break;
    }
  }

  JsonArray barcosPropios = estado.createNestedArray("barcosPropios");
  JsonArray disparosEnemigo = estado.createNestedArray("disparosEnemigo");
  JsonArray disparosPropios = estado.createNestedArray("disparosPropios");
  JsonArray aciertosPropios = estado.createNestedArray("aciertosPropios");
  
  for(int i = 0; i < 4; i++) {
    JsonArray filaBP = barcosPropios.createNestedArray();
    JsonArray filaDE = disparosEnemigo.createNestedArray();
    JsonArray filaDP = disparosPropios.createNestedArray();
    JsonArray filaAP = aciertosPropios.createNestedArray();
    
    for(int j = 0; j < 4; j++) {
      filaBP.add(jugadores[jugadorIndex].barcos[i][j]);
      filaDE.add(jugadores[1-jugadorIndex].disparos[i][j]);
      filaDP.add(jugadores[jugadorIndex].disparos[i][j]);
      filaAP.add(jugadores[1-jugadorIndex].barcos[i][j] && jugadores[jugadorIndex].disparos[i][j]);
    }
  }
  
  String json;
  serializeJson(doc, json);
  webSocket.sendTXT(jugadores[jugadorIndex].numCliente, json);
}

void notificarDisparo(int jugadorDisparando, int fila, int col, bool acierto) {
  StaticJsonDocument<128> doc;
  doc["type"] = "disparo";
  
  JsonObject disparo = doc.createNestedObject("disparo");
  disparo["jugador"] = jugadorDisparando;
  disparo["fila"] = fila;
  disparo["col"] = col;
  disparo["acierto"] = acierto;
  
  String json;
  serializeJson(doc, json);
  
  for(int i = 0; i < 2; i++) {
    if(jugadores[i].numCliente != 255) {
      webSocket.sendTXT(jugadores[i].numCliente, json);
    }
  }
}

void notificarNivel() {
  StaticJsonDocument<128> doc;
  doc["type"] = "nivel";
  
  switch(nivelActual) {
    case FACIL: doc["nivel"] = "Fácil"; break;
    case MEDIO: doc["nivel"] = "Medio"; break;
    case DIFICIL: doc["nivel"] = "Difícil"; break;
  }
  
  String json;
  serializeJson(doc, json);
  
  for(int i = 0; i < 2; i++) {
    if(jugadores[i].numCliente != 255) {
      webSocket.sendTXT(jugadores[i].numCliente, json);
    }
  }
}

void avanzarNivel() {
  if(nivelActual == FACIL) {
    nivelActual = MEDIO;
    mostrarMensajeMatrix(mx1, "N2");
    sonidoCambioTurno();
  } else if(nivelActual == MEDIO) {
    nivelActual = DIFICIL;
    mostrarMensajeMatrix(mx1, "N3");
    sonidoCambioTurno();
  } else {
    juegoTerminado = true;
    tiempoFinJuego = millis();
    sonidoVictoria();
    return;
  }
  
  reiniciarDisparos();
  configurarBarcosMaquina();
  turnoActual = 0;
  ultimoCambioTurno = millis();
  notificarNivel();
  
  for(int i = 0; i < 2; i++) {
    enviarEstadoJuego(i);
  }
}

void procesarKeypad(Keypad &keypad, int jugador) {
  static bool esperandoCambioTurno = false;
  static unsigned long tiempoUltimoDisparo = 0;

  if (esperandoCambioTurno && (millis() - tiempoUltimoDisparo >= intervaloCambioTurno)) {
    turnoActual = 1 - turnoActual;
    esperandoCambioTurno = false;
    ultimoCambioTurno = millis();
    sonidoCambioTurno();
    
    for (int i = 0; i < 2; i++) {
      enviarEstadoJuego(i);
    }
    return;
  }

  char key = keypad.getKey();
  if (!key || !juegoIniciado || juegoTerminado || turnoActual != jugador || esperandoCambioTurno) {
    return;
  }

  uint8_t fila = (strchr("123A456B789C*0#D", key) - "123A456B789C*0#D") / 4;
  uint8_t col = (strchr("123A456B789C*0#D", key) - "123A456B789C*0#D") % 4;

  if (jugadores[jugador].disparos[fila][col]) {
    return;
  }

  sonidoDisparo();
  
  jugadores[jugador].disparos[fila][col] = true;
  bool acierto = jugadores[1 - jugador].barcos[fila][col];

  if(acierto) sonidoAcierto();

  ultimoDisparo = millis();
  actualizarMatrizLED(jugador == 0 ? mx1 : mx2, jugador);
  notificarDisparo(jugador, fila, col, acierto);

  bool victoria = true;
  for (int i = 0; i < 4 && victoria; i++) {
    for (int j = 0; j < 4 && victoria; j++) {
      if (jugadores[1 - jugador].barcos[i][j] && !jugadores[jugador].disparos[i][j]) {
        victoria = false;
      }
    }
  }

  if (victoria) {
    if (modo == VS_MAQUINA) {
      avanzarNivel();
    } else {
      juegoTerminado = true;
      tiempoFinJuego = millis();
      sonidoVictoria();
    }
    for (int i = 0; i < 2; i++) {
      enviarEstadoJuego(i);
    }
  } else if (!acierto) {
    esperandoCambioTurno = true;
    tiempoUltimoDisparo = millis();
  }
}

void manejarDisparoMaquina() {
  int fila, col;
  bool acierto = false;
  
  int probabilidad = random(0, 100);
  int probabilidadAcierto = 0;
  
  switch(nivelActual) {
    case FACIL: probabilidadAcierto = 10; break;
    case MEDIO: probabilidadAcierto = 20; break;
    case DIFICIL: probabilidadAcierto = 30; break;
  }
  
  if(probabilidad < probabilidadAcierto) {
    for(int i = 0; i < 4; i++) {
      for(int j = 0; j < 4; j++) {
        if(jugadores[0].barcos[i][j] && !jugadores[1].disparos[i][j]) {
          fila = i;
          col = j;
          acierto = true;
          goto disparar;
        }
      }
    }
  }
  
  do {
    fila = random(0, 4);
    col = random(0, 4);
  } while(jugadores[1].disparos[fila][col]);
  
  acierto = jugadores[0].barcos[fila][col];
  
disparar:
  sonidoDisparo();
  if(acierto) sonidoAcierto();
  
  jugadores[1].disparos[fila][col] = true;
  ultimoDisparoFila = fila;
  ultimoDisparoCol = col;
  ultimoDisparoAcierto = acierto;
  mostrarDisparoMaquina = true;
  
  ultimoDisparo = millis();
  tiempoMostrarMensaje = millis() + 7000;
  mostrarDisparoUnico(mx1, fila, col, acierto);
  notificarDisparo(1, fila, col, acierto);

  bool victoria = true;
  for(int i = 0; i < 4 && victoria; i++) {
    for(int j = 0; j < 4 && victoria; j++) {
      if(jugadores[0].barcos[i][j] && !jugadores[1].disparos[i][j]) {
        victoria = false;
      }
    }
  }

  if(victoria) {
    juegoTerminado = true;
    tiempoFinJuego = millis();
    sonidoDerrota();
    for(int i = 0; i < 2; i++) {
      enviarEstadoJuego(i);
    }
  } else if(!acierto) {
    turnoActual = 0;
    ultimoCambioTurno = millis();
    sonidoCambioTurno();
    for(int i = 0; i < 2; i++) {
      enviarEstadoJuego(i);
    }
  }
}

// ========== WEBSOCKET ========== //
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Desconectado!\n", num);
      for(int i = 0; i < 2; i++) {
        if(jugadores[i].numCliente == num) {
          jugadores[i].numCliente = 255;
          jugadores[i].conectado = false;
        }
      }
      break;
      
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[%u] Conectado desde %s\n", num, ip.toString().c_str());
      
      for(int i = 0; i < 2; i++) {
        if(jugadores[i].ip == ip) {
          jugadores[i].numCliente = num;
          jugadores[i].conectado = true;
          Serial.printf("Asociado jugador %d con cliente %u\n", i, num);
          enviarEstadoJuego(i);
          return;
        }
      }
      
      if(modo == VS_MAQUINA && jugadorActual >= 1) {
        Serial.println("Intento de conexión rechazado: Modo VS Máquina solo permite 1 jugador");
        webSocket.disconnect(num);
        return;
      }
      
      if(modo == MULTIJUGADOR && jugadorActual >= 2) {
        Serial.println("Intento de conexión rechazado: Límite de jugadores alcanzado");
        webSocket.disconnect(num);
        return;
      }
      
      if(modo == NINGUNO) {
        Serial.println("Intento de conexión rechazado: Modo no seleccionado");
        webSocket.disconnect(num);
        return;
      }
      
      int jugadorIndex = jugadorActual++;
      jugadores[jugadorIndex].ip = ip;
      jugadores[jugadorIndex].numCliente = num;
      jugadores[jugadorIndex].conectado = true;
      Serial.printf("Nuevo jugador %d asignado al cliente %u\n", jugadorIndex, num);
    } break;
      
    case WStype_TEXT:
      break;
  }
}

void redirigirAJuego() {
  StaticJsonDocument<32> doc;
  doc["type"] = "redirigir";
  
  String json;
  serializeJson(doc, json);
  
  for(int i = 0; i < 2; i++) {
    if(jugadores[i].numCliente != 255) {
      webSocket.sendTXT(jugadores[i].numCliente, json);
    }
  }
}

void redirigirAInicio() {
  StaticJsonDocument<32> doc;
  doc["type"] = "redirigir";
  
  String json;
  serializeJson(doc, json);
  webSocket.broadcastTXT(json);
}

// ========== SUPABASE ========== //
bool guardarPartidaEnSupabase(unsigned long duracion) {
  if(modo == NINGUNO) {
    Serial.println("No se guarda partida sin modo definido");
    return false;
  }
  
  DynamicJsonDocument doc(1024);
  doc["nombre_jugador1"] = jugadores[0].nombre;
  doc["nombre_jugador2"] = modo == VS_MAQUINA ? "Máquina" : jugadores[1].nombre;
  doc["nombre_ganador"] = jugadores[turnoActual].nombre;
  doc["modo_juego"] = modo == VS_MAQUINA ? "VS_MAQUINA" : "MULTIJUGADOR";
  doc["duracion_segundos"] = duracion / 1000;
  
  if(modo == VS_MAQUINA) {
    switch(nivelActual) {
      case FACIL: doc["nivel"] = "Fácil"; break;
      case MEDIO: doc["nivel"] = "Medio"; break;
      case DIFICIL: doc["nivel"] = "Difícil"; break;
    }
  }

  String json;
  serializeJson(doc, json);

  HTTPClient http;
  http.begin(supabaseUrl + "/rest/v1/partidas");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", supabaseAuth);
  http.addHeader("Prefer", "return=minimal");

  int httpCode = http.POST(json);
  bool success = (httpCode == HTTP_CODE_CREATED);
  if(!success) {
    Serial.printf("Error al guardar partida: %d - %s\n", httpCode, http.errorToString(httpCode).c_str());
  }
  http.end();
  return success;
}

String obtenerHistorialSupabase() {
  HTTPClient http;
  http.begin(supabaseUrl + "/rest/v1/partidas?select=creado_en,nombre_jugador1,nombre_jugador2,nombre_ganador,modo_juego,duracion_segundos,nivel&order=creado_en.desc");
  http.addHeader("apikey", supabaseKey);
  http.addHeader("Authorization", supabaseAuth);

  String payload = "[]";
  int httpCode = http.GET();
  if(httpCode == HTTP_CODE_OK) {
    payload = http.getString();
  } else {
    Serial.printf("Error al obtener historial: %d\n", httpCode);
  }
  http.end();
  return payload;
}

// ========== HTTP HANDLERS ========== //
void manejarNombre() {
  if(!server.hasArg("plain")) {
    server.send(400, "text/plain", "Datos no recibidos");
    return;
  }

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if(error) {
    server.send(400, "text/plain", "Error en formato JSON");
    return;
  }
  
  String nombre = doc["nombre"] | "";
  if(nombre.isEmpty()) {
    server.send(400, "text/plain", "Nombre vacío");
    return;
  }

  IPAddress ip = server.client().remoteIP();
  int jugadorIndex = -1;
  
  for(int i = 0; i < 2; i++) {
    if(jugadores[i].ip == ip) {
      jugadorIndex = i;
      break;
    }
  }
  
  if(jugadorIndex == -1) {
    if(modo == VS_MAQUINA && jugadorActual >= 1) {
      server.send(403, "text/plain", "Solo un jugador permitido en modo VS Máquina");
      return;
    } else if(modo == MULTIJUGADOR && jugadorActual >= 2) {
      server.send(403, "text/plain", "Límite de jugadores alcanzado");
      return;
    }
    
    jugadorIndex = jugadorActual++;
    jugadores[jugadorIndex].ip = ip;
  }
  
  jugadores[jugadorIndex].nombre = nombre;
  server.send(200, "text/plain", "OK");
}

void manejarPosiciones() {
  if(!server.hasArg("plain")) {
    server.send(400, "text/plain", "Datos no recibidos");
    return;
  }

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));
  if(error) {
    server.send(400, "text/plain", "Error en formato JSON");
    return;
  }

  if(modo == NINGUNO) {
    server.send(400, "text/plain", "Selecciona un modo de juego primero");
    return;
  }

  JsonArray posiciones = doc["posiciones"];
  if(posiciones.size() != 3) {
    server.send(400, "text/plain", "Debes seleccionar exactamente 3 posiciones");
    return;
  }

  IPAddress ip = server.client().remoteIP();
  int jugadorIndex = -1;
  
  for(int i = 0; i < 2; i++) {
    if(jugadores[i].ip == ip) {
      jugadorIndex = i;
      break;
    }
  }

  if(jugadorIndex == -1) {
    server.send(403, "text/plain", "Registra tu nombre primero");
    return;
  }

  memset(jugadores[jugadorIndex].barcos, false, 16);
  
  for(JsonVariant v : posiciones) {
    String pos = v.as<String>();
    int coma = pos.indexOf(',');
    if(coma == -1) continue;
    
    int fila = pos.substring(0, coma).toInt();
    int col = pos.substring(coma + 1).toInt();
    
    if(fila >= 0 && fila < 4 && col >= 0 && col < 4) {
      jugadores[jugadorIndex].barcos[fila][col] = true;
    }
  }

  jugadores[jugadorIndex].listo = true;
  bool todosListos = true;
  int requeridos = (modo == VS_MAQUINA) ? 1 : 2;
  
  for(int i = 0; i < requeridos; i++) {
    if(!jugadores[i].listo) {
      todosListos = false;
      break;
    }
  }

  StaticJsonDocument<128> respuesta;
  respuesta["status"] = "ok";
  respuesta["iniciar"] = todosListos;
  respuesta["jugador"] = jugadorIndex;
  
  String respuestaStr;
  serializeJson(respuesta, respuestaStr);
  server.send(200, "application/json", respuestaStr);

  if(todosListos) {
    juegoIniciado = true;
    turnoActual = 0;
    ultimoCambioTurno = millis();
    redirigirAJuego();
    sonidoInicio();
    
    if(modo == VS_MAQUINA) {
      randomSeed(analogRead(0));
      configurarBarcosMaquina();
      jugadores[1].listo = true;
      jugadores[1].nombre = "Máquina";
      
      mensajeMatrix1 = "N1";
      mostrarMensajeMatrix(mx1, "N1");
      tiempoMostrarMensaje = millis();
      notificarNivel();
    }
  }
}

void manejarEstadoJuego() {
  IPAddress ip = server.client().remoteIP();
  int jugadorIndex = -1;
  
  for(int i = 0; i < 2; i++) {
    if(jugadores[i].ip == ip) {
      jugadorIndex = i;
      break;
    }
  }
  
  if(jugadorIndex == -1) {
    server.send(403, "text/plain", "Registra tu nombre primero");
    return;
  }
  
  StaticJsonDocument<512> doc;
  doc["jugador"] = jugadorIndex;
  doc["miTurno"] = (turnoActual == jugadorIndex);
  doc["juegoTerminado"] = juegoTerminado;
  if(juegoTerminado) doc["ganador"] = turnoActual;
  
  switch(modo) {
    case NINGUNO: doc["modo"] = "NINGUNO"; break;
    case VS_MAQUINA: 
      doc["modo"] = "VS_MAQUINA"; 
      switch(nivelActual) {
        case FACIL: doc["nivel"] = "Fácil"; break;
        case MEDIO: doc["nivel"] = "Medio"; break;
        case DIFICIL: doc["nivel"] = "Difícil"; break;
      }
      break;
    case MULTIJUGADOR: doc["modo"] = "MULTIJUGADOR"; break;
  }

  JsonArray barcosPropios = doc.createNestedArray("barcosPropios");
  JsonArray disparosEnemigo = doc.createNestedArray("disparosEnemigo");
  JsonArray disparosPropios = doc.createNestedArray("disparosPropios");
  JsonArray aciertosPropios = doc.createNestedArray("aciertosPropios");
  
  for(int i = 0; i < 4; i++) {
    JsonArray filaBP = barcosPropios.createNestedArray();
    JsonArray filaDE = disparosEnemigo.createNestedArray();
    JsonArray filaDP = disparosPropios.createNestedArray();
    JsonArray filaAP = aciertosPropios.createNestedArray();
    
    for(int j = 0; j < 4; j++) {
      filaBP.add(jugadores[jugadorIndex].barcos[i][j]);
      filaDE.add(jugadores[1-jugadorIndex].disparos[i][j]);
      filaDP.add(jugadores[jugadorIndex].disparos[i][j]);
      filaAP.add(jugadores[1-jugadorIndex].barcos[i][j] && jugadores[jugadorIndex].disparos[i][j]);
    }
  }
  
  String respuesta;
  serializeJson(doc, respuesta);
  server.send(200, "application/json", respuesta);
}

void manejarHistorial() {
  server.send_P(200, "text/html", paginaHistorial);
}

void manejarHistorialJson() {
  String historial = obtenerHistorialSupabase();
  server.send(200, "application/json", historial);
}

// ========== MAIN FUNCTIONS ========== //
void reiniciarJuego() {
  modo = NINGUNO;
  nivelActual = FACIL;
  jugadorActual = turnoActual = 0;
  juegoIniciado = juegoTerminado = nivelCompletado = false;
  tiempoFinJuego = 0;
  mostrarDisparoMaquina = false;
  mensajeMatrix1 = "";
  mensajeMatrix2 = "";
  
  for(int i = 0; i < 2; i++) {
    jugadores[i] = Jugador();
  }
  
  mx1.clear();
  mx2.clear();
  digitalWrite(BUZZER_PIN, LOW);
}

void setup() {
  Serial.begin(115200);

  pinMode(botonMaquina, INPUT_PULLUP);
  pinMode(botonMultijugador, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);

  mx1.begin();
  mx1.clear();
  mx1.control(MD_MAX72XX::SHUTDOWN, 0);
  
  mx2.begin();
  mx2.clear();
  mx2.control(MD_MAX72XX::INTENSITY, 8);

  setupI2C();
  setupBuzzer();

  bool inicializado = false;
  for (int i = 0; i < I2C_RETRIES; i++) {
    Serial.println("Intentando inicializar keypad I2C...");
    keypad2.begin();
    delay(100);

    Wire.beginTransmission(0x38);
    if (Wire.endTransmission() == 0) {
      Serial.println("Keypad I2C detectado correctamente.");
      inicializado = true;
      break;
    }
  }

  if (!inicializado) {
    Serial.println("Error crítico: No se pudo detectar el keypad I2C en la dirección 0x38");
  }

  EEPROM.begin(512);
  intentoconexion("ESP32-BARCOS", "12345678");

  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", paginaInicio);
  });
  server.on("/registro_barcos", HTTP_GET, []() {
    server.send_P(200, "text/html", paginaRegistroBarcos);
  });
  server.on("/juego", HTTP_GET, []() {
    server.send_P(200, "text/html", paginaJuego);
  });
  server.on("/historial", HTTP_GET, manejarHistorial);
  server.on("/historial_json", HTTP_GET, manejarHistorialJson);
  server.on("/nombre", HTTP_POST, manejarNombre);
  server.on("/posiciones", HTTP_POST, manejarPosiciones);
  server.on("/estado_juego", HTTP_GET, manejarEstadoJuego);

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  server.begin();

  sonidoInicio();
}

void loop() {
  static unsigned long lastI2CCheck = 0;
  static unsigned long lastButtonCheck = 0;
  
  loopAP();
  server.handleClient();
  webSocket.loop();

  if(millis() - lastI2CCheck > 10000) {
    lastI2CCheck = millis();
    if(!checkI2CDevice(0x38)) {
      Serial.println("Reiniciando bus I2C...");
      resetI2CBus();
    }
  }

  if(millis() - lastButtonCheck > 200) {
    lastButtonCheck = millis();
    
    if(digitalRead(botonMaquina) == LOW) {
      reiniciarJuego();
      modo = VS_MAQUINA;
      nivelActual = FACIL;
      Serial.println("Modo VS Máquina seleccionado");
      sonidoCambioTurno();
      delay(500);
    } else if(digitalRead(botonMultijugador) == LOW) {
      reiniciarJuego();
      modo = MULTIJUGADOR;
      Serial.println("Modo Multijugador seleccionado");
      sonidoCambioTurno();
      delay(500);
    }
  }

  if(juegoIniciado && !juegoTerminado) {
    procesarKeypad(keypad1, 0);
    
    if(modo == MULTIJUGADOR) {
      procesarKeypad(keypad2, 1);
    }
  }

  if(juegoIniciado && !juegoTerminado && modo == VS_MAQUINA && turnoActual == 1 && !mostrarDisparoMaquina) {
    if(millis() - ultimoCambioTurno > intervaloCambioTurno) {
      manejarDisparoMaquina();
    }
  }

  if(mostrarDisparoMaquina) {
    if(millis() - ultimoDisparo >= intervaloCambioTurno) {
      mostrarDisparoMaquina = false;
      actualizarMatrizLED(mx1, 0);
      if(modo == MULTIJUGADOR) {
        actualizarMatrizLED(mx2, 1);
      }
    }
  }

  if(millis() - tiempoMostrarMensaje < tiempoMostrarNivel && (mensajeMatrix1 != "" || mensajeMatrix2 != "")) {
    return;
  } else if((mensajeMatrix1 != "" || mensajeMatrix2 != "") && millis() - tiempoMostrarMensaje >= tiempoMostrarNivel) {
    mensajeMatrix1 = "";
    mensajeMatrix2 = "";
    actualizarMatrizLED(mx1, 0);
    if(modo == MULTIJUGADOR) {
      actualizarMatrizLED(mx2, 1);
    }
  }

  if(juegoIniciado && !juegoTerminado) {
    actualizarMatrizLED(mx1, 0);
    if(modo == MULTIJUGADOR) {
      actualizarMatrizLED(mx2, 1);
    }
  }

  if(juegoTerminado && millis() - tiempoFinJuego > tiempoReinicio && tiempoFinJuego != 0) {
    unsigned long duracionPartida = millis() - tiempoFinJuego; 
    guardarPartidaEnSupabase(duracionPartida);
    redirigirAInicio();
    reiniciarJuego();
  }
}