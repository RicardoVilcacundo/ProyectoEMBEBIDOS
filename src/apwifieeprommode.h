#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <HTTPClient.h> // Añadido para la comunicación con la API de Telegram
#include <ArduinoJson.h> // Añadido para construir los mensajes JSON de Telegram

#ifdef ARDUINO_ARCH_ESP32
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif
#include <Wire.h>
#include "WiFi.h"

WebServer server(80);

// Define tu Token de Bot de Telegram y tu ID de Chat aquí
// ¡IMPORTANTE! Reemplaza estos valores con tus datos reales.
// Puedes obtener un token de bot de BotFather en Telegram.
// Para obtener tu ID de chat, envía un mensaje a tu bot, luego ve a:
// https://api.telegram.org/botTU_TOKEN_DE_BOT/getUpdates
// Busca "chat":{"id":TU_ID_DE_CHAT,...}
#define TELEGRAM_BOT_TOKEN "7803345353:AAGPcLqbtzB7D6WSnK63IJv-m5ucroXyqq4"
#define TELEGRAM_CHAT_ID "1205115257"

// Variables para la redirección (lógica de turnos anteriores)
bool redirectPending = false;
String redirectUrl = "";

// Función para enviar un mensaje a Telegram
void sendTelegramMessage(String message) {
    // Verifica si las credenciales de Telegram están configuradas
    if (String(TELEGRAM_BOT_TOKEN) == "YOUR_TELEGRAM_BOT_TOKEN" || String(TELEGRAM_CHAT_ID) == "YOUR_TELEGRAM_CHAT_ID") {
        Serial.println("ERROR: TELEGRAM_BOT_TOKEN o TELEGRAM_CHAT_ID no configurados. No se puede enviar el mensaje.");
        return;
    }

    HTTPClient http;
    String telegramApiUrl = "https://api.telegram.org/bot" + String(TELEGRAM_BOT_TOKEN) + "/sendMessage";

    http.begin(telegramApiUrl);
    http.addHeader("Content-Type", "application/json"); // Especifica que el contenido es JSON

    // Crea un documento JSON para el cuerpo de la solicitud
    StaticJsonDocument<200> doc; // Tamaño del documento JSON, ajusta si el mensaje es más grande
    doc["chat_id"] = String(TELEGRAM_CHAT_ID);
    doc["text"] = message;
    doc["parse_mode"] = "HTML"; // Opcional: permite usar formato HTML (negritas, cursivas, etc.)

    String requestBody;
    serializeJson(doc, requestBody); // Convierte el documento JSON a una cadena

    Serial.println("Enviando mensaje a Telegram...");
    Serial.println(requestBody);

    int httpCode = http.POST(requestBody); // Envía la solicitud POST

    // Verifica el código de respuesta HTTP
    if (httpCode > 0) {
        Serial.printf("[HTTP] POST... código: %d\n", httpCode);
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
            String payload = http.getString(); // Obtiene la respuesta del servidor
            Serial.println("Respuesta de Telegram:");
            Serial.println(payload);
        }
    } else {
        Serial.printf("[HTTP] POST... falló, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end(); // Cierra la conexión
}


String leerStringDeEEPROM(int direccion)
{
    String cadena = "";
    char caracter = EEPROM.read(direccion);
    int i = 0;
    while (caracter != '\0' && i < 100)
    {
        cadena += caracter;
        i++;
        caracter = EEPROM.read(direccion + i);
    }
    return cadena;
}

void escribirStringEnEEPROM(int direccion, String cadena)
{
    int longitudCadena = cadena.length();
    for (int i = 0; i < longitudCadena; i++)
    {
        EEPROM.write(direccion + i, cadena[i]);
    }
    EEPROM.write(direccion + longitudCadena, '\0'); // Null-terminated string
    EEPROM.commit();                                // Guardamos los cambios en la memoria EEPROM
}

void handleRoot()
{
    String html = "<html><body>";
    html += "<form method='POST' action='/wifi'>";
    html += "Red Wi-Fi: <input type='text' name='ssid'><br>";
    html += "Contraseña: <input type='password' name='password'><br>";
    html += "<input type='submit' value='Conectar'>";
    html += "</form></body></html>";
    server.send(200, "text/html", html);
}

int posW = 50;
void handleWifi()
{
    String ssid = server.arg("ssid");
    String password = server.arg("password");
    Serial.print("Conectando a la red Wi-Fi ");
    Serial.println(ssid);
    Serial.print("Clave Wi-Fi ");
    Serial.println(password);
    Serial.print("...");
    WiFi.disconnect(); // Desconectar la red Wi-Fi anterior, si se estaba conectado
    WiFi.begin(ssid.c_str(), password.c_str(), 6);

    int cnt = 0;
    while (WiFi.status() != WL_CONNECTED && cnt < 8)
    {
        delay(1000);
        Serial.print(".");
        cnt++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.print("Guardando en memoria eeprom...");
        String varsave = leerStringDeEEPROM(300);
        if (varsave == "a") {
            posW = 0;
            escribirStringEnEEPROM(300, "b");
        }
        else{
            posW=50;
            escribirStringEnEEPROM(300, "a");
        }
        escribirStringEnEEPROM(0 + posW, ssid);
        escribirStringEnEEPROM(100 + posW, password);

        Serial.println("Conexión establecida");
        // *** AÑADIDO: Enviar IP a Telegram después de conectar a una red nueva ***
        String ipMessage = "ESP32 conectado a WiFi. IP: <b>" + WiFi.localIP().toString() + "</b> (Red: " + ssid + ")";
        sendTelegramMessage(ipMessage);

        // Configurar la bandera de redirección y la URL
        redirectPending = true;
        redirectUrl = "http://" + WiFi.localIP().toString() + "/";
        server.send(200, "text/plain", "Conexión establecida. reiniciando ESP32"); // Enviar una respuesta rápida
        delay(3000);
        
        // Reiniciar la ESP32
        ESP.restart();
    }
    else
    {
        Serial.println("Conexión no establecida");
        server.send(200, "text/plain", "Conexión no establecida");
    }
}

bool lastRed()
{ // verifica si una de las 2 redes guardadas en la memoria eeprom se encuentra disponible
    // para conectarse en ese momento
    for (int psW = 0; psW <= 50; psW += 50)
    {
        String usu = leerStringDeEEPROM(0 + psW);
        String cla = leerStringDeEEPROM(100 + psW);
        Serial.println(usu);
        Serial.println(cla);
        WiFi.disconnect();
        WiFi.begin(usu.c_str(), cla.c_str(), 6);
        int cnt = 0;
        while (WiFi.status() != WL_CONNECTED && cnt < 5)
        {
            delay(1000);
            Serial.print(".");
            cnt++;
        }
        if (WiFi.status() == WL_CONNECTED){
            Serial.println("Conectado a Red Wifi");
            String currentIP = WiFi.localIP().toString();
            Serial.println(currentIP);
            // *** AÑADIDO: Enviar IP a Telegram después de conectar a una red guardada ***
            String ipMessage = "ESP32 conectado a red guardada. IP: <b>" + currentIP + "</b> (Red: " + usu + ")";
            sendTelegramMessage(ipMessage);

            // Configurar la bandera de redirección y la URL
            redirectPending = true;
            redirectUrl = "http://" + currentIP + "/";
            break;
        }
    }
    if (WiFi.status() == WL_CONNECTED)
        return true;
    else
        return false;
}

void initAP(const char *apSsid, const char *apPassword)
{ // Nombre de la red Wi-Fi y  Contraseña creada por el ESP32
    Serial.begin(115200);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSsid, apPassword);

    server.on("/", handleRoot);
    server.on("/wifi", handleWifi);

    server.begin();
    Serial.println("Servidor web iniciado");
}

void loopAP()
{
    server.handleClient();
    // Manejar la redirección si está pendiente (lógica de turnos anteriores)
    if (redirectPending) {
        server.sendHeader("Location", redirectUrl);
        server.send(302, "text/plain", ""); // Enviar 302 Found para la redirección
        redirectPending = false; // Restablecer la bandera
    }
}

void intentoconexion(const char *apname, const char *appassword)
{
    Serial.begin(115200);
    EEPROM.begin(512);
    Serial.println("ingreso a intentoconexion");

    // Intentar conectar a redes guardadas primero
    bool connectedToSaved = lastRed();

    if (!connectedToSaved)
    {                                  
        Serial.println("Conectarse desde su celular a la red creada");
        Serial.println("en el navegador colocar la ip:");
        Serial.println("192.168.4.1");
        initAP(apname, appassword); // nombre de wifi a generarse y contrasena
    }

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED) // mientras no se encuentre conectado a una red
    {
        loopAP(); // genera una red wifi para que se configure desde la app movil
        // Lógica de tiempo de espera para el modo AP y reinicio (lógica de turnos anteriores)
    }
    // Si llegamos aquí, WiFi está conectado.
    // Asegurarse de que la redirección ocurra si no fue manejada ya por handleWifi o lastRed
    if (WiFi.status() == WL_CONNECTED && !redirectPending) {
        redirectPending = true;
        redirectUrl = "http://" + WiFi.localIP().toString() + "/";
        // *** AÑADIDO: Enviar IP a Telegram si la conexión fue establecida de otra forma (por si acaso) ***
        // Aunque handleWifi y lastRed ya lo hacen, esto es una capa de seguridad.
        String ipMessage = "ESP32 conectado al iniciar. IP: <b>" + WiFi.localIP().toString() + "</b>";
        sendTelegramMessage(ipMessage);
    }
}