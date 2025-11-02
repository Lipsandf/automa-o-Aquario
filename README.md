# 🐠 Automação Completa de Aquário Marinho com ESP32

## Visão Geral do Projeto

Este projeto oferece uma solução robusta e multifuncional para o gerenciamento de um aquário marinho, utilizando um módulo ESP32. Ele combina controle local (Web Server), automação por tempo e temperatura (Termostato e Timers) e integração com assistentes de voz (Sinric Pro para Alexa/Google Home), além de controle físico via controle remoto infravermelho (IR).

O firmware gerencia um painel de 8 relés e lê a temperatura de um sensor DS18B20.

### Funcionalidades Principais

* **Controle Web (Local):** Interface responsiva acessível pelo navegador para controlar todas as 8 tomadas, configurar timers e termostato.
* **Controle por Infravermelho (IR):** Utiliza um sensor TSOP1838 e um controle remoto IR comum para acionamento rápido de relés e funções mestras.
* **Integração com Assistente de Voz (Sinric Pro):** Controla remotamente as Tomadas 6 e 7 e define a Temperatura Alvo do Termostato via Alexa/Google Home.
* **Termostato Inteligente:** Controla relés de aquecimento e resfriamento com histerese (deadband) configurável, baseado na leitura do DS18B20.
* **Timers Agendados:** Permite programar horários de LIGA/DESLIGA diários para cada tomada individualmente, suportando agendamento noturno (passando pela meia-noite).

## 🛠️ Hardware Necessário

| Componente | Função | Conexão (Pinos GPIO) |
| :--- | :--- | :--- |
| **ESP32 Dev Module** | Microcontrolador Principal | - |
| **Módulo Relé 8 Canais** | Chaveamento das Tomadas | GPIO 25, 26, 27, 32, 33, 18, 19, 23 |
| **Sensor DS18B20** | Leitura de Temperatura | GPIO 4 |
| **Receptor IR (TSOP1838)** | Controle Remoto | **GPIO 15** |

## ⚙️ Configuração do Software

### 1. Bibliotecas

Instale as seguintes bibliotecas através do Gerenciador de Bibliotecas da IDE do Arduino:

* `WiFi` (Integrada)
* `WebServer` (Integrada)
* `ESPmDNS` (Integrada)
* `ArduinoOTA` (Integrada)
* `OneWire` (Por Paul Stoffregen)
* `DallasTemperature` (Por Miles Burton)
* `NTPClient` (Por Fabrice Weinberg)
* `Preferences` (Integrada)
* `SinricPro` (Por SinricPro)
* `IRremote` (Por Armin Joachimsmeyer)

### 2. Credenciais

Antes de carregar o código, você deve atualizar as credenciais na **Seção 2: CONFIGURAÇÕES GLOBAIS**:

```cpp
// --- CREDENCIAIS DE REDE ---
#define WIFI_SSID 			"Seu_WiFi_SSID"
#define WIFI_PASS 			"Sua_Senha_WiFi"
#define OTA_PASSWORD 		"Sua_Senha_OTA"

// --- CREDENCIAIS SINRIC PRO (Obtidas no portal Sinric Pro) ---
#define APP_KEY 			"SEU-APP-KEY"
#define APP_SECRET 			"SEU-APP-SECRET"
#define DEVICE_ID_TEMP 		"ID-DO-SEU-TERMOSTATO"
#define DEVICE_ID_RELE_6 	"ID-TOMADA-6"
#define DEVICE_ID_RELE_7 	"ID-TOMADA-7"
