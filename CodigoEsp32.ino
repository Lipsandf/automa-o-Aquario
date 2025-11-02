// =================================================================
// 					 1. BIBLIOTECAS
// =================================================================
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Arduino.h>
#include <IRremote.hpp>

// --- BIBLIOTECAS PARA TEMPERATURA E TEMPO ---
#include <OneWire.h>
#include <DallasTemperature.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Preferences.h>

// --- NOVAS BIBLIOTECAS PARA SINRIC PRO ---
#include <SinricPro.h>
#include <SinricProSwitch.h>
#include <SinricProThermostat.h>

// =================================================================
// 					 2. CONFIGURAÇÕES GLOBAIS
// =================================================================
const char* host = "aquario";
WebServer server(80);

// --- CREDENCIAIS DE REDE ---
#define WIFI_SSID 			"Lucas Kamargo_EXT"
#define WIFI_PASS 			"98831565"
#define OTA_PASSWORD 		"9940"

// --- NOVAS CREDENCIAIS SINRIC PRO (ATUALIZE ESTES VALORES!) ---
#define APP_KEY 			"26d51a7a-415f-4fea-88e5-09317521e0e1"
#define APP_SECRET 			"c956a710-7fe4-4af0-8043-34e80397097d-c9ec9d3e-553c-40ce-8aaf-ab462141ffc8"
#define DEVICE_ID_TEMP 		"68fd23da5918d860c0aa8b35"
#define DEVICE_ID_RELE_6 	"68fd1ff5ba649e246c12747e"
#define DEVICE_ID_RELE_7 	"68fd2099acd5d3d66b55d4dc"


const char* sinricProRelayIDs[] = {
	"UNUSED", "UNUSED", "UNUSED", "UNUSED",
	"UNUSED", DEVICE_ID_RELE_6, DEVICE_ID_RELE_7, "UNUSED"
};
const int RELES_SINRIC_ATIVOS[] = {5, 6};

// --- CONFIGURAÇÕES DE TEMPO E NTP ---
WiFiUDP ntpUDP;
const long utcOffsetInSeconds = -10800;
NTPClient timeClient(ntpUDP, "a.ntp.br", utcOffsetInSeconds, 60000);

// --- OBJETO PARA SALVAR DADOS ---
Preferences preferences;
// =================================================================
// 					 3. CONFIGURAÇÕES DOS PINOS E COMPONENTES
// =================================================================

// --- Relés (8 Canais) ---
const int NUM_RELES = 8;
const int relePins[NUM_RELES] = {25, 26, 27, 32, 33, 18, 19, 23};
bool releState[NUM_RELES];
String releNames[NUM_RELES];

// --- Sensor DS18B20 ---
const int ONE_WIRE_BUS = 4;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// --- Receptor IR TSOP1838 ---
const int IR_RECV_PIN = 15;

// =================================================================
// 					 4. VARIÁVEIS DE ESTADO E ESTRUTURAS
// =================================================================
float temperaturaC = 0.0;
unsigned long ultimaLeituraTemp = 0;
const long INTERVALO_LEITURA_TEMP = 5000;
unsigned long sinricProReportTimer = 0;
const long SINRIC_PRO_REPORT_INTERVAL = 60000;

// --- ESTRUTURAS PARA TIMER E TERMOSTATO ---
struct TimerConfig {
	bool enabled;
	int onHour;
	int onMinute;
	int offHour;
	int offMinute;
};
TimerConfig timerConfigs[NUM_RELES];

struct ThermostatConfig {
	bool enabled;
	int coolingRelay;
	int heatingRelay;
	float targetTemp;
	float deadband;
};
ThermostatConfig thermoConfig;

// --- PROTÓTIPOS DE FUNÇÕES ---
void setRele(int id, bool state);
void saveRelayStates();
void handleRoot();
void handleToggleRele();
void handleToggleSimple();
void handleMasterToggle();
void handleSaveTimers();
void handleSaveThermostat();
void handleSaveAllNames();
void handleAlimentadorToggle();
void handleNotFound();
void handleSensorData();
void applyTimers();
void applyThermostat();
void loadConfigurations();
void saveConfigurations();
void setupOTA();
bool onPowerState(const String &deviceId, bool &state);
bool onTargetTemperature(const String &deviceId, float &targetTemp);
void handleIR();


// =================================================================
// 					 5. FUNÇÕES DE CONTROLE E LEITURA
// =================================================================

void setRele(int id, bool state) {
	if (id >= 0 && id < NUM_RELES) {
		if (releState[id] != state) {
			releState[id] = state;
			digitalWrite(relePins[id], releState[id] ? LOW : HIGH);
			Serial.printf("Relé %d (%s) -> %s\n", id + 1, releNames[id].c_str(), state ? "LIGADO" : "DESLIGADO");
			saveRelayStates();

			if (id == 5 || id == 6) {
				if (WiFi.isConnected()) {
					SinricProSwitch &mySwitch = SinricPro[sinricProRelayIDs[id]];
					mySwitch.sendPowerStateEvent(state);
				}
			}
		}
	}
}

void saveRelayStates() {
	preferences.begin("config", false);
	preferences.putBytes("relay_states", releState, sizeof(releState));
	preferences.end();
}

void readTemperature() {
	if (millis() - ultimaLeituraTemp >= INTERVALO_LEITURA_TEMP) {
		sensors.requestTemperatures();
		temperaturaC = sensors.getTempCByIndex(0);
		ultimaLeituraTemp = millis();

		if (temperaturaC == DEVICE_DISCONNECTED_C) {
			Serial.println("Erro: DS18B20 desconectado ou falha na leitura!");
		} else {
			Serial.printf("Temperatura DS18B20: %.2f °C\n", temperaturaC);

			if (WiFi.isConnected() && (millis() - sinricProReportTimer >= SINRIC_PRO_REPORT_INTERVAL)) {
				SinricProThermostat &myThermostat = SinricPro[DEVICE_ID_TEMP];
				myThermostat.sendTemperatureEvent(temperaturaC);
				sinricProReportTimer = millis();
			}
		}
	}
}


void handleIR() {
	if (IrReceiver.decode()) {
		unsigned long hexCode = IrReceiver.decodedIRData.decodedRawData;

		if (hexCode != 0xFFFFFFFF && hexCode != 0) {
			
			Serial.println("\n--- COMANDO IR RECEBIDO ---");
			Serial.print("Protocolo: ");
			Serial.println(IrReceiver.getProtocolString());
			Serial.print("Código Hexa: 0x");
			Serial.println(hexCode, HEX);
			
			switch (hexCode) {
				case 0xBA45FF00:
					setRele(0, !releState[0]);
					break;
				case 0xB946FF00:
					setRele(1, !releState[1]);
					break;
				case 0xB847FF00:
					setRele(2, !releState[2]);
					break;
				case 0xBB44FF00:
					setRele(3, !releState[3]);
					break;
				case 0xBF40FF00:
					setRele(4, !releState[4]);
					break;
				case 0xBC43FF00:
					setRele(5, !releState[5]);
					break;
				case 0xF807FF00:
					setRele(6, !releState[6]);
					break;
				case 0xEA15FF00:
					setRele(7, !releState[7]);
					break;
				case 0xE31CFF00:
					handleAlimentadorToggle();
					Serial.println("IR: Função ALIMENTADOR acionada.");
					break;
				case 0xF20DFF00:
					handleMasterToggle();
					Serial.println("IR: Função MASTER TOGGLE acionada.");
					break;
			}
		}

		IrReceiver.resume();
	}
}


// =================================================================
// 					 6. FUNÇÕES WEB (Geração de HTML e Handlers)
// =================================================================

String createHtmlPage() {
	const char* backgroundImageUrl = "https://images.pexels.com/photos/2446439/pexels-photo-2446439.jpeg";
	String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
	html += "<title>Controle Aquário Marinho</title>";
	html += "<style>";
	html += "body{";
	html += "	font-family: Arial, sans-serif;";
	html += "	text-align: center;";
	html += "	background-image: url('" + String(backgroundImageUrl) + "');";
	html += "	background-size: cover;";
	html += "	background-attachment: fixed;";
	html += "	margin: 0;";
	html += "	padding: 0;";
	html += "}";
	html += ".container{";
	html += "	background-color: rgb(173,216,230);";
	html += "	max-width: 600px; margin: 30px auto; padding: 20px;";
	html += "	border: 1px solid #ccc; border-radius: 10px;";
	html += "}";
	html += ".btn{";
	html += "	display: inline-block; padding: 10px; margin: 5px 5px; text-decoration: none; color: white;";
	html += "	border-radius: 5px; width: 150px; text-align: center; line-height: 15px;";
	html += "}";
	html += ".fancy-input {";
	html += "	border-radius: 5px; border: 1px solid #aaa; padding: 5px; background-color: #f5f5f5;";
	html += "}";

	html += ".on{background-color:#4CAF50;}";
	html += ".off{background-color:#f44336;}";
	html += ".master{background-color:#007bff;}";

	html += ".modal { display: none; position: fixed; z-index: 1; left: 0; top: 0; width: 100%; height: 100%; overflow: auto; background-color: rgba(0,0,0,0.4);}";
	html += ".modal-content { background-color: #fefefe; margin: 10% auto; padding: 20px; border: 1px solid #888; width: 80%; max-width: 400px; border-radius: 10px; text-align: left;}";
	html += ".close { color: #aaa; float: right; font-size: 28px; font-weight: bold;}";
	html += ".close:hover, .close:focus { color: #000; text-decoration: none; cursor: pointer;}";

	html += "</style>";
	html += "<script>";
	html += "function fetchData() {";
	html += "	var xhttp = new XMLHttpRequest();";
	html += "	xhttp.onreadystatechange = function() {";
	html += "	  if (this.readyState == 4 && this.status == 200) {";
	html += "	    var data = JSON.parse(this.responseText);";
	html += "	    document.getElementById('temp_value').innerHTML = data.temp.toFixed(2) + ' &deg;C';";
	html += "	    document.getElementById('time_value').innerHTML = data.time;";
	html += "	    for (var i = 0; i < " + String(NUM_RELES) + "; i++) {";
	html += "	      var container = document.getElementById('status_container_' + i);";
	html += "	      var linkBtn = document.getElementById('link_' + i);";
	html += "	      if (container && linkBtn) {";
	html += "	        var status = data.relays[i] ? 'LIGADO' : 'DESLIGADO';";
	html += "	        document.getElementById('display_name_' + i).innerHTML = data.names[i];";
	html += "	        container.childNodes[1].nodeValue = ' ' + status + ' ';";
	html += "	        linkBtn.className = 'btn ' + (data.relays[i] ? 'off' : 'on');";
	html += "	        linkBtn.innerHTML = data.relays[i] ? 'Desligar' : 'Ligar';";
	html += "	        linkBtn.href = '/toggle?id=' + i + '&action=' + (data.relays[i] ? 'off' : 'on');";
	html += "	        document.getElementById('cooling_opt_' + i).innerHTML = data.names[i];";
	html += "	        document.getElementById('heating_opt_' + i).innerHTML = data.names[i];";
	html += "	        document.getElementById('modal_input_' + i).value = data.names[i];";
	html += "	      }";
	html += "	    }";
	html += "	  }";
	html += "	};";
	html += "	xhttp.open('GET', '/data', true);";
	html += "	xhttp.send();";
	html += "}";
	html += "setInterval(fetchData, 5000);";
	html += "window.onload = fetchData;";
	html += "function openModal() {";
	html += "	document.getElementById('renameModal').style.display = 'block';";
	html += "}";
	html += "function closeModal() {";
	html += "	document.getElementById('renameModal').style.display = 'none';";
	html += "}";
	html += "window.onclick = function(event) {";
	html += "	if (event.target == document.getElementById('renameModal')) {";
	html += "		closeModal();";
	html += "	}";
	html += "}";

	html += "</script>";
	html += "</head><body><div class='container' style='margin-top: 10px;'><h1>Aquário Marinho</h1>";
	html += "<h2>Temperatura: <span id='temp_value'>" + String(temperaturaC, 2) + " &deg;C</span></h2>";
	html += "<h3>Hora Atual: <span id='time_value'>" + timeClient.getFormattedTime() + "</span></h3>";
	html += "<div style='display: flex; justify-content: space-around; margin-bottom: 10px;'>";
	html += "<a class='btn master' href='/master_toggle' style='width: 30%;'>LIGAR/DESLIGAR TODAS</a>";
	html += "<a class='btn master' href='/alimentador_toggle' style='width: 30%; background-color: #ff9800;'>ALIMENTADOR</a>";
	html += "<button onclick='openModal()' class='btn master' style='width: 30%;'>EDITAR NOMES</button>";
	html += "</div><hr>";
	html += "<h2>Configuração do Termostato</h2>";
	html += "<div style='text-align: left; margin: 0 auto; max-width: 300px;'>";
	html += "<form id='save_thermostat_form' method='get' action='/save_thermostat'>";
	html += "Ativar: <input type='checkbox' name='enabled' " + String(thermoConfig.enabled ? "checked" : "") + " class='fancy-input'><br>";
	html += "Temp. Alvo (&deg;C): <input type='number' name='targetTemp' step='0.1' value='" + String(thermoConfig.targetTemp, 1) + "' class='fancy-input' style='width: 70px;'><br>";
	html += "Margem de Inércia (&deg;C): <input type='number' name='deadband' step='0.1' value='" + String(thermoConfig.deadband, 1) + "' class='fancy-input' style='width: 70px;'> (Histerese)<br>";
	html += "Relé de Resfriamento: <select name='coolingRelay' class='fancy-input'>";
	for (int i = 0; i < NUM_RELES; i++) {
		html += "<option id='cooling_opt_" + String(i) + "' value='" + String(i) + "' " + String(thermoConfig.coolingRelay == i ? "selected" : "") + ">" + releNames[i] + "</option>";
	}
	html += "</select><br>";

	html += "Relé de Aquecimento: <select name='heatingRelay' class='fancy-input'>";
	for (int i = 0; i < NUM_RELES; i++) {
		html += "<option id='heating_opt_" + String(i) + "' value='" + String(i) + "' " + String(thermoConfig.heatingRelay == i ? "selected" : "") + ">" + releNames[i] + "</option>";
	}
	html += "</select><br>";

	html += "</form>";

	html += "</div>";
	html += "<button onclick=\"document.querySelector('#save_thermostat_form').submit()\" class='btn master' style='width: 250px; margin-top: 10px;'>Salvar Termostato</button><hr>";
	html += "<h2>Controle de Tomadas e Timers</h2>";
	html += "<form method='get' action='/save_timers'>";
	for (int i = 0; i < NUM_RELES; i++) {
		String status = releState[i] ?
		"LIGADO" : "DESLIGADO";
		String style = releState[i] ? "on" : "off";
		String action = releState[i] ? "off" : "on";
		html += "<div style='border: 1px solid #ddd; margin: 10px 0; padding: 10px; text-align: left;'>";
		html += "<div id='status_container_" + String(i) + "' style='display: flex; align-items: center; justify-content: space-between;'>";
		html += "<span><span id='display_name_" + String(i) + "'>" + releNames[i] + ":</span> " "</span>";

		html += "<a id='link_" + String(i) + "' class='btn " + style + "' href='/toggle?id=" + String(i) + "&action=" + action + "'>" + String(releState[i] ? "Desligar" : "Ligar") + "</a>";
		html += "</div>";

		html += "<div style='margin-top: 10px;'>Timer: <input type='checkbox' name='timer_enabled_" + String(i) + "' " + String(timerConfigs[i].enabled ? "checked" : "") + " class='fancy-input' style='margin-right: 5px;'>";
		html += " Liga: <input type='time' name='onTime_" + String(i) + "' value='" + String(timerConfigs[i].onHour < 10 ? "0" : "") + String(timerConfigs[i].onHour) + ":" + String(timerConfigs[i].onMinute < 10 ? "0" : "") + String(timerConfigs[i].onMinute) + "' class='fancy-input' style='width: 80px;'>";
		html += " Desliga: <input type='time' name='offTime_" + String(i) + "' value='" + String(timerConfigs[i].offHour < 10 ? "0" : "") + String(timerConfigs[i].offHour) + ":" + String(timerConfigs[i].offMinute < 10 ? "0" : "") + String(timerConfigs[i].offMinute) + "' class='fancy-input' style='width: 80px;'>";
		html += "</div>";

		html += "</div>";
	}

	html += "<input type='submit' value='Salvar Timers' class='btn master' style='width: 250px; margin-top: 10px;'></form>";
	html += "</div>";

	html += "<footer style='margin-top: 30px; padding: 10px; font-size: 0.9em; color: #333; background-color: rgba(255, 255, 255, 0.4); border-radius: 5px;'>";
	html += "Desenvolvido por Felipe Camargo";
	html += "</footer>";

	html += "<div id='renameModal' class='modal'>";
	html += "<div class='modal-content'>";
	html += "<span class='close' onclick='closeModal()'>&times;</span>";
	html += "<h3 style='text-align: center;'>Editar Nomes das Tomadas</h3>";
	html += "<form method='get' action='/save_all_names' style='text-align: left;'>";

	for (int i = 0; i < NUM_RELES; i++) {
		html += "<label for='name_" + String(i) + "'>Tomada " + String(i + 1) + ":</label><br>";
		html += "<input type='text' id='modal_input_" + String(i) + "' name='name_" + String(i) + "' value='" + releNames[i] + "' class='fancy-input' style='width: 100%; margin-bottom: 10px;'><br>";
	}

	html += "<button type='submit' class='btn on' style='width: 100%;'>Salvar Todos os Nomes</button>";
	html += "</form>";

	html += "</div></div>";

	html += "</body></html>";
	return html;
}


void handleSensorData() {
	timeClient.update();
	String json = "{\"temp\": " + String(temperaturaC, 2) + ",";
	json += "\"time\": \"" + timeClient.getFormattedTime() + "\",";
	json += "\"relays\": [";

	for (int i = 0; i < NUM_RELES; i++) {
		json += releState[i] ?
		"true" : "false";
		if (i < NUM_RELES - 1) json += ",";
	}
	json += "],";
	json += "\"names\": [";
	for (int i = 0; i < NUM_RELES; i++) {
		json += "\"" + releNames[i] + "\"";
		if (i < NUM_RELES - 1) json += ",";
	}
	json += "]}";

	server.send(200, "application/json", json);
}

void handleSaveAllNames() {
	preferences.begin("config", false);
	for (int i = 0; i < NUM_RELES; i++) {
		String argName = "name_" + String(i);
		if (server.hasArg(argName)) {
			String newName = server.arg(argName);
			releNames[i] = newName;

			preferences.putString(("name_" + String(i)).c_str(), newName.c_str());
			Serial.printf("Nome do Relé %d alterado para: %s\n", i + 1, newName.c_str());
		}
	}

	preferences.end();
	server.sendHeader("Location", "/");
	server.send(303);
}

void handleAlimentadorToggle() {
	const int rele_6_index = 5;
	const int rele_7_index = 6;

	bool newState = !releState[rele_6_index];
	
	setRele(rele_6_index, newState);
	setRele(rele_7_index, newState);

	server.sendHeader("Location", "/");
	server.send(303);
}


void handleRoot() {
	server.send(200, "text/html", createHtmlPage());
}

void handleToggleRele() {
	if (server.hasArg("id") && server.hasArg("action")) {
		int id = server.arg("id").toInt();
		bool newState = (server.arg("action") == "on");
		setRele(id, newState);

		server.sendHeader("Location", "/");
		server.send(303);
	} else {
		server.send(400, "text/plain", "Parametros ausentes");
	}
}

void handleToggleSimple() {
	if (server.hasArg("id")) {
		int id = server.arg("id").toInt();
		if (id >= 0 && id < NUM_RELES) {
			bool newState = !releState[id];
			setRele(id, newState);

			server.sendHeader("Location", "/");
			server.send(303);
		} else {
			server.send(404, "text/plain", "ID de rele invalido");
		}

	} else {
		server.send(400, "text/plain", "Parametro 'id' ausente");
	}
}

void handleMasterToggle() {
	bool newState = !releState[0];
	for (int i = 0; i < NUM_RELES; i++) {
		setRele(i, newState);
	}
	server.sendHeader("Location", "/");
	server.send(303);
}

void handleSaveThermostat() {
	thermoConfig.enabled = server.hasArg("enabled");
	thermoConfig.targetTemp = server.arg("targetTemp").toFloat();
	thermoConfig.deadband = server.arg("deadband").toFloat();
	thermoConfig.coolingRelay = server.arg("coolingRelay").toInt();
	thermoConfig.heatingRelay = server.arg("heatingRelay").toInt();
	Serial.printf("Termostato Salvo. Temp: %.1f C, Deadband: %.1f, Resfria: %d, Aquece: %d\n",
				  thermoConfig.targetTemp, thermoConfig.deadband, thermoConfig.coolingRelay + 1, thermoConfig.heatingRelay + 1);
	saveConfigurations();

	SinricProThermostat &myThermostat = SinricPro[DEVICE_ID_TEMP];
	myThermostat.sendTargetTemperatureEvent(thermoConfig.targetTemp);

	server.sendHeader("Location", "/");
	server.send(303);
}

void handleSaveTimers() {
	for (int i = 0; i < NUM_RELES; i++) {
		String timerKey = "timer_enabled_" + String(i);
		String onTimeKey = "onTime_" + String(i);
		String offTimeKey = "offTime_" + String(i);

		timerConfigs[i].enabled = server.hasArg(timerKey);
		if (server.hasArg(onTimeKey)) {
			String onTimeStr = server.arg(onTimeKey);
			timerConfigs[i].onHour = onTimeStr.substring(0, 2).toInt();
			timerConfigs[i].onMinute = onTimeStr.substring(3, 5).toInt();
		}

		if (server.hasArg(offTimeKey)) {
			String offTimeStr = server.arg(offTimeKey);
			timerConfigs[i].offHour = offTimeStr.substring(0, 2).toInt();
			timerConfigs[i].offMinute = offTimeStr.substring(3, 5).toInt();
		}

		Serial.printf("Timer %d Salvo. ON: %02d:%02d, OFF: %02d:%02d, Ativo: %s\n",
					  i + 1, timerConfigs[i].onHour, timerConfigs[i].onMinute,
					  timerConfigs[i].offHour, timerConfigs[i].offMinute,
					  timerConfigs[i].enabled ? "SIM" : "NAO");
	}

	saveConfigurations();
	server.sendHeader("Location", "/");
	server.send(303);
}

void handleNotFound() {
	server.send(404, "text/plain", "Pagina nao encontrada");
}

// =================================================================
// 					 7. LÓGICA DE AGENDAMENTO E TERMOSTATO
// =================================================================

void applyThermostat() {
	if (!thermoConfig.enabled) return;
	float currentTemp = temperaturaC;
	float target = thermoConfig.targetTemp;
	float deadband = thermoConfig.deadband;

	int coolingRelay = thermoConfig.coolingRelay;
	int heatingRelay = thermoConfig.heatingRelay;
	if (coolingRelay == heatingRelay) return;

	if (currentTemp > (target + deadband)) {
		if (!releState[coolingRelay]) {
			setRele(coolingRelay, true);
		}
		if (releState[heatingRelay]) {
			setRele(heatingRelay, false);
		}
	}
	else if (currentTemp <= target) {
		if (releState[coolingRelay]) {
			setRele(coolingRelay, false);
		}
	}

	if (currentTemp < (target - deadband)) {
		if (!releState[heatingRelay]) {
			setRele(heatingRelay, true);
		}
		if (releState[coolingRelay]) {
			setRele(coolingRelay, false);
		}
	}
	else if (currentTemp >= target) {
		if (releState[heatingRelay]) {
			setRele(heatingRelay, false);
		}
	}
}


void applyTimers() {
	timeClient.update();

	int currentHour = timeClient.getHours();
	int currentMinute = timeClient.getMinutes();
	for (int i = 0; i < NUM_RELES; i++) {
		if (!timerConfigs[i].enabled) continue;
		int onTime = timerConfigs[i].onHour * 60 + timerConfigs[i].onMinute;
		int offTime = timerConfigs[i].offHour * 60 + timerConfigs[i].offMinute;
		int currentTime = currentHour * 60 + currentMinute;

		bool shouldBeOn;
		if (onTime < offTime) {
			shouldBeOn = (currentTime >= onTime && currentTime < offTime);
		} else {
			shouldBeOn = (currentTime >= onTime || currentTime < offTime);
		}

		if (shouldBeOn && !releState[i]) {
			setRele(i, true);
		} else if (!shouldBeOn && releState[i]) {
			setRele(i, false);
		}
	}
}

// =================================================================
// 					 8. GERENCIAMENTO DE CONFIGURAÇÃO (FLASH)
// =================================================================

void loadConfigurations() {
	preferences.begin("config", true);
	if (preferences.getBytes("timers", timerConfigs, sizeof(timerConfigs)) == 0) {
		memset(timerConfigs, 0, sizeof(timerConfigs));
	}

	if (preferences.getBytes("thermostat", &thermoConfig, sizeof(thermoConfig)) == 0) {
		thermoConfig.targetTemp = 25.0;
		thermoConfig.deadband = 0.5;
		thermoConfig.coolingRelay = 0;
		thermoConfig.heatingRelay = 1;
	}

	if (preferences.getBytes("relay_states", releState, sizeof(releState)) == 0) {
		memset(releState, 0, sizeof(releState));
	}

	for (int i = 0; i < NUM_RELES; i++) {
		String key = "name_" + String(i);
		String defaultName = "Tomada " + String(i + 1);
		releNames[i] = preferences.getString(key.c_str(), defaultName.c_str());
	}

	preferences.end();
}

void saveConfigurations() {
	preferences.begin("config", false);
	preferences.putBytes("timers", timerConfigs, sizeof(timerConfigs));
	preferences.putBytes("thermostat", &thermoConfig, sizeof(thermoConfig));

	preferences.end();
}

// =================================================================
// 					 9. FUNÇÕES DE CALLBACK SINRIC PRO
// =================================================================

bool onPowerState(const String &deviceId, bool &state) {
	Serial.printf("Comando Sinric Pro - Dispositivo: %s, Estado: %s\r\n", deviceId.c_str(), state ? "ON" : "OFF");
	for (int i = 0; i < sizeof(RELES_SINRIC_ATIVOS) / sizeof(RELES_SINRIC_ATIVOS[0]); i++) {
		int releIndex = RELES_SINRIC_ATIVOS[i];
		if (deviceId.equals(sinricProRelayIDs[releIndex])) {
			timerConfigs[releIndex].enabled = false;
			
			setRele(releIndex, state);
			return true;
		}
	}

	Serial.printf("ERRO: ID do dispositivo Sinric Pro não encontrado ou não está ativo: %s\r\n", deviceId.c_str());
	return false;
}

bool onTargetTemperature(const String &deviceId, float &targetTemp) { 
	Serial.printf("Comando Sinric Pro - Temp. Alvo: %.2f\r\n", targetTemp);
	if (deviceId.equals(DEVICE_ID_TEMP)) {
		thermoConfig.targetTemp = targetTemp;
		thermoConfig.enabled = true;
		saveConfigurations();

		applyThermostat();
		return true;
	}

	return false;
}


// =================================================================
// 					 10. SETUP
// =================================================================
void setup() {
	Serial.begin(115200);
	
	loadConfigurations();
	for (int i = 0; i < NUM_RELES; i++) {
		pinMode(relePins[i], OUTPUT);
		digitalWrite(relePins[i], releState[i] ? LOW : HIGH);
	}

	sensors.begin();
	
	Serial.println("Conectando ao Wi-Fi...");
	WiFi.begin(WIFI_SSID, WIFI_PASS);
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}
	Serial.println("\nWi-Fi conectado!");
	Serial.print("Endereço IP: ");
	Serial.println(WiFi.localIP());

	timeClient.begin();
	timeClient.update();
	Serial.println("Hora NTP sincronizada: " + timeClient.getFormattedTime());

	if (MDNS.begin(host)) {
		Serial.println("mDNS iniciado. Acesse em: " + String(host) + ".local");
	}
	setupOTA();

	server.on("/", handleRoot);
	server.on("/data", handleSensorData);
	server.on("/toggle", handleToggleRele);
	server.on("/toggle_id", handleToggleSimple);
	server.on("/master_toggle", handleMasterToggle);
	server.on("/alimentador_toggle", handleAlimentadorToggle);
	server.on("/save_timers", handleSaveTimers);
	server.on("/save_thermostat", handleSaveThermostat);
	server.on("/save_all_names", handleSaveAllNames);
	server.onNotFound(handleNotFound);
	server.begin();
	Serial.println("Servidor web e Serviços iniciados!");

	SinricPro.onConnected([]() { Serial.println("SinricPro Conectado!"); });
	SinricPro.onDisconnected([]() { Serial.println("SinricPro Desconectado. Tentando reconectar..."); });
	SinricPro.begin(APP_KEY, APP_SECRET);

	for (int releIndex : RELES_SINRIC_ATIVOS) {
		SinricProSwitch &mySwitch = SinricPro[sinricProRelayIDs[releIndex]];
		mySwitch.onPowerState(onPowerState);
		mySwitch.sendPowerStateEvent(releState[releIndex]);
		Serial.printf("Relé %d configurado no Sinric Pro.\n", releIndex + 1);
	}

	SinricProThermostat &myThermostat = SinricPro[DEVICE_ID_TEMP];
	myThermostat.onTargetTemperature(onTargetTemperature);
	myThermostat.sendTargetTemperatureEvent(thermoConfig.targetTemp);
	myThermostat.sendTemperatureEvent(temperaturaC);
	Serial.println("Sinric Pro configurado com 2 Switches (Tomadas 6, 7) e 1 Termostato.");

	IrReceiver.begin(IR_RECV_PIN, ENABLE_LED_FEEDBACK);
	Serial.printf("Receptor IR (TSOP1838) iniciado no GPIO %d.\n", IR_RECV_PIN);
}

// =================================================================
// 					 11. LOOP PRINCIPAL
// =================================================================
void loop() {
	ArduinoOTA.handle();
	server.handleClient();
	SinricPro.handle();

	readTemperature();
	applyTimers();
	applyThermostat();
	handleIR();
	
	delay(200);
}

// =================================================================
// 					 FUNÇÃO OTA
// =================================================================
void setupOTA() {
	ArduinoOTA.setHostname(host);
	ArduinoOTA.setPassword(OTA_PASSWORD);

	ArduinoOTA.onStart([]() {
		Serial.println("OTA iniciado: Novo Sketch em upload.");
	});
	ArduinoOTA.onEnd([]() {
		Serial.println("\nOTA Finalizado! Reiniciando...");
	});
	ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
		Serial.printf("Progresso: %u%%\r", (progress / (total / 100)));
	});
	ArduinoOTA.onError([](ota_error_t error) {
		Serial.printf("ERRO OTA [%u]\n", error);
	});

	ArduinoOTA.begin();
	Serial.println("OTA ativado.");
}