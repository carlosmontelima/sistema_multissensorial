/*
 * ======================================================================================
 * PROJETO DE EXTENSÃO — UNINASSAU (CAMPUS GRAÇAS - RECIFE/PE)
 * SISTEMA MULTISSENSORIAL DE SINALIZAÇÃO E ACESSIBILIDADE PARA PORTAS DE BANHEIRO
 * ======================================================================================
 *
 * Microcontrolador: ESP32 DOIT DevKit V1 (30 Pinos)
 *
 * --------------------------------------------------------------------------------------
 * 📚 BIBLIOTECAS NECESSÁRIAS NA ARDUINO IDE (Menu: Sketch -> Incluir Biblioteca -> Gerenciar Bibliotecas):
 * --------------------------------------------------------------------------------------
 * 1. MFRC522         (por GithubCommunity)      -> Leitura do módulo RFID RC522 via SPI
 * 2. ESP32Servo      (por Kevin Harrington)     -> Controle PWM do Micro Servo Motor SG90 no ESP32
 * 3. PubSubClient    (por Nick O'Leary)         -> Cliente MQTT para comunicação IoT
 * 4. ArduinoJson     (por Benoit Blanchon v6/v7)-> Formatação das mensagens JSON no MQTT
 *
 * *Bibliotecas nativas já inclusas no pacote ESP32:* <SPI.h>, <WiFi.h>
 *
 * --------------------------------------------------------------------------------------
 * ⚙️ CONFIGURAÇÃO DA PLACA NA ARDUINO IDE:
 * --------------------------------------------------------------------------------------
 * 1. Em Arquivo -> Preferências -> URLs Adicionais para Gerenciadores de Placas, adicione:
 *    https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
 * 2. Em Ferramentas -> Placa -> Gerenciador de Placas, instale "esp32 by Espressif Systems".
 * 3. Selecione a placa: "DOIT ESP32 DEVKIT V1" (ou "ESP32 Dev Module").
 * 4. Velocidade da Serial (Monitor Serial): 115200 baud.
 *
 * --------------------------------------------------------------------------------------
 * 🔌 RESUMO DA PINAGEM (Conforme docs/GUIA_DE_MONTAGEM.md):
 * --------------------------------------------------------------------------------------
 * - RFID RC522 (3.3V): SDA(SS)=GPIO 5 | SCK=GPIO 18 | MOSI=GPIO 23 | MISO=GPIO 19 | RST=GPIO 22
 * - Servo SG90 (5V):   Sinal=GPIO 13
 * - Vibração MV50(5V): IN/SIG=GPIO 27
 * - Buzzer Ativo (5V): I/O=GPIO 26
 * ======================================================================================
 */

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <MFRC522.h>
#include <ESP32Servo.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ======================================================================================
// 1. CONFIGURAÇÕES DE REDE WI-FI E BROKER MQTT
// ======================================================================================
const char* WIFI_SSID     = "SEU_WIFI_AQUI";       // Nome da rede Wi-Fi (ou Hotspot do celular)
const char* WIFI_PASSWORD = "SUA_SENHA_WIFI_AQUI"; // Senha do Wi-Fi

// Broker MQTT:
// - Para usar com a Dashboard Web sem instalar nada no PC, mantenha "broker.hivemq.com"
// - Para usar com Mosquitto instalado localmente no notebook, troque pelo IP do PC (ex: "192.168.1.100")
const char* MQTT_BROKER   = "broker.hivemq.com";
const int   MQTT_PORT     = 1883;
const char* MQTT_CLIENT_ID = "ESP32_PortaAcessivel_Uninassau_01";

// Tópicos MQTT (Arquitetura ESP <-> MQTT <-> Computador)
const char* TOPIC_ESTADO  = "uninassau/acessibilidade/banheiro01/estado";
const char* TOPIC_COMANDO = "uninassau/acessibilidade/banheiro01/comando";

// ======================================================================================
// 2. DEFINIÇÃO DOS PINOS DO HARDWARE (ESP32 30 PINOS)
// ======================================================================================
// Pinos do Leitor RFID RC522 (Interface VSPI padrão)
#define PIN_RFID_SS    5   // Pino SDA / SS do RC522 -> GPIO 5 (D5)
#define PIN_RFID_RST   22  // Pino RST do RC522      -> GPIO 22 (D22)
// SCK = GPIO 18 | MISO = GPIO 19 | MOSI = GPIO 23 (definidos automaticamente pelo SPI.begin())

// Pinos dos Atuadores Multissensoriais
#define PIN_SERVO      13  // Fio Laranja do Servo SG90        -> GPIO 13 (D13)
#define PIN_VIBRACAO   27  // Pino IN do Motor Vibração MV50   -> GPIO 27 (D27)
#define PIN_BUZZER     26  // Pino I/O do Buzzer Ativo 5V      -> GPIO 26 (D26)
#define PIN_LED_STATUS 2   // LED Azul interno do ESP32 (indica porta trancada)

// Configuração de polaridade do Buzzer (mude para false se o seu módulo for Active-Low)
const bool BUZZER_ACTIVE_HIGH = true;

// Ângulos do Servo Motor SG90 (Tranca Física)
const int ANGULO_DESTRANCADO = 0;   // 0°  = Porta ABERTA / LIVRE
const int ANGULO_TRANCADO    = 90;  // 90° = Porta FECHADA / OCUPADO

// Tempo de Debounce do RFID (2 segundos para evitar leituras duplicadas acidentais)
const unsigned long DEBOUNCE_RFID_MS = 2000;

// ======================================================================================
// 3. OBJETOS E VARIÁVEIS DE ESTADO GLOBAIS
// ======================================================================================
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
Servo servoTranca;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

bool portaFechada = false;                  // false = ABERTA (Livre), true = FECHADA (Ocupado)
unsigned long ultimoAcionamentoMs = 0;      // Controle de debounce de 2s
unsigned long inicioOcupacaoMs = 0;         // Marca o millis() em que a porta foi trancada
unsigned long ultimaTentativaWifiMs = 0;    // Reconexão Wi-Fi não-bloqueante
unsigned long ultimaTentativaMqttMs = 0;    // Reconexão MQTT não-bloqueante
unsigned int contadorUsosSessao = 0;        // Quantidade de usos desde que o ESP32 ligou
String ultimaTagUid = "NENHUMA";

// ======================================================================================
// 4. FUNÇÕES AUXILIARES DE ACIONAMENTO DOS ATUADORES (TÁTIL, SONORO E FÍSICO)
// ======================================================================================

/**
 * Liga ou desliga o Buzzer respeitando se o módulo é Active-High ou Active-Low.
 */
void definirEstadoBuzzer(bool ligado) {
  if (BUZZER_ACTIVE_HIGH) {
    digitalWrite(PIN_BUZZER, ligado ? HIGH : LOW);
  } else {
    digitalWrite(PIN_BUZZER, ligado ? LOW : HIGH);
  }
}

/**
 * Executa o retorno multissensorial simultâneo ao TRANCAR a porta (Estado = FECHADA):
 * - Servo Motor gira para 90°
 * - Motor de Vibração aciona 1 pulso forte de 600ms
 * - Buzzer Ativo emite 1 bipe longo de 500ms
 */
void acionarFeedbackTrancar() {
  Serial.println(F("🔒 [ATUADORES] Trancando porta: Servo -> 90° | Vibracao -> 600ms | Buzzer -> 500ms"));

  // 1. Aciona tranca física (Servo para 90 graus) e LED interno do ESP32
  servoTranca.write(ANGULO_TRANCADO);
  digitalWrite(PIN_LED_STATUS, HIGH);

  // 2. Inicia simultaneamente Vibração (600ms) e Buzzer (500ms)
  digitalWrite(PIN_VIBRACAO, HIGH);
  definirEstadoBuzzer(true);

  delay(500); // Aguarda os 500ms do Buzzer
  definirEstadoBuzzer(false); // Desliga o Buzzer em 500ms

  delay(100); // Aguarda os 100ms restantes para completar os 600ms da Vibração
  digitalWrite(PIN_VIBRACAO, LOW);
}

/**
 * Executa o retorno multissensorial simultâneo ao DESTRANCAR a porta (Estado = ABERTA):
 * - Servo Motor retorna para 0°
 * - Motor de Vibração aciona 2 pulsos curtos de 150ms
 * - Buzzer Ativo emite 2 bipes rápidos de 150ms
 */
void acionarFeedbackDestrancar() {
  Serial.println(F("🔓 [ATUADORES] Destrancando porta: Servo -> 0° | Vibracao -> 2x 150ms | Buzzer -> 2x 150ms"));

  // 1. Retorna tranca física (Servo para 0 graus) e apaga LED interno
  servoTranca.write(ANGULO_DESTRANCADO);
  digitalWrite(PIN_LED_STATUS, LOW);

  // 2. Executa os 2 pulsos curtos simultâneos (Vibração + Buzzer de 150ms)
  for (int i = 0; i < 2; i++) {
    digitalWrite(PIN_VIBRACAO, HIGH);
    definirEstadoBuzzer(true);
    delay(150);

    digitalWrite(PIN_VIBRACAO, LOW);
    definirEstadoBuzzer(false);

    if (i == 0) {
      delay(150); // Intervalo de 150ms entre o 1º e o 2º pulso
    }
  }
}

// ======================================================================================
// 5. PUBLICAÇÃO E RECEBIMENTO MQTT (JSON)
// ======================================================================================

/**
 * Publica o estado atual da porta no Broker MQTT em formato JSON,
 * além de enviar a string pura caso algum nó simples precise ler diretamente.
 */
void publicarEstadoMqtt(unsigned long duracaoSegundos, const char* origemAcionamento) {
  StaticJsonDocument<256> doc;

  doc["estado"] = portaFechada ? "FECHADA" : "ABERTA";
  doc["ocupado"] = portaFechada;
  doc["uid_tag"] = ultimaTagUid;
  doc["duracao_ultimo_uso_seg"] = duracaoSegundos;
  doc["usos_sessao"] = contadorUsosSessao;
  doc["servo_angulo"] = portaFechada ? ANGULO_TRANCADO : ANGULO_DESTRANCADO;
  doc["origem"] = origemAcionamento;
  doc["uptime_seg"] = millis() / 1000;

  char bufferJson[256];
  serializeJson(doc, bufferJson);

  Serial.print(F("📡 [MQTT TX] Publicando em "));
  Serial.print(TOPIC_ESTADO);
  Serial.print(F(": "));
  Serial.println(bufferJson);

  if (mqttClient.connected()) {
    // Publica com retain = true para que a Dashboard receba o estado atual assim que abrir
    mqttClient.publish(TOPIC_ESTADO, bufferJson, true);
  } else {
    Serial.println(F("⚠️ [MQTT] Broker desconectado no momento. O funcionamento fisico continua normal."));
  }
}

/**
 * Alterna o estado da porta (Toggle) ou define um estado específico,
 * acionando todos os atuadores e publicando no MQTT.
 */
void alternarEstadoPorta(String uidTag, const char* origem) {
  portaFechada = !portaFechada;
  ultimaTagUid = uidTag;

  if (portaFechada) {
    // Transição: ABERTA -> FECHADA (Trancar)
    inicioOcupacaoMs = millis();
    acionarFeedbackTrancar();
    publicarEstadoMqtt(0, origem);

    Serial.println(F("🔴 ESTADO ATUAL: FECHADA / OCUPADO"));
  } else {
    // Transição: FECHADA -> ABERTA (Destrancar)
    unsigned long duracaoSegundos = 0;
    if (inicioOcupacaoMs > 0) {
      duracaoSegundos = (millis() - inicioOcupacaoMs) / 1000;
      if (duracaoSegundos == 0) duracaoSegundos = 1; // Mínimo de 1s para registro
    }
    contadorUsosSessao++;

    acionarFeedbackDestrancar();
    publicarEstadoMqtt(duracaoSegundos, origem);

    Serial.print(F("🟢 ESTADO ATUAL: ABERTA / LIVRE (Tempo ocupada: "));
    Serial.print(duracaoSegundos);
    Serial.println(F(" segundos)"));
  }
  Serial.println(F("------------------------------------------------------------------"));
}

/**
 * Callback executado quando o ESP32 recebe um comando vindo do Computador/Dashboard via MQTT.
 */
void callbackMqtt(char* topic, byte* payload, unsigned int length) {
  String mensagem = "";
  for (unsigned int i = 0; i < length; i++) {
    mensagem += (char)payload[i];
  }
  mensagem.trim();

  Serial.print(F("📥 [MQTT RX] Comando recebido em "));
  Serial.print(topic);
  Serial.print(F(": "));
  Serial.println(mensagem);

  // Verifica debounce também para comandos remotos
  if (millis() - ultimoAcionamentoMs < DEBOUNCE_RFID_MS) {
    Serial.println(F("⏳ [DEBOUNCE] Aguarde 2 segundos entre acionamentos."));
    return;
  }

  // Aceita tanto comando em texto puro ("TOGGLE", "ABRIR", "FECHAR") quanto JSON {"acao": "TOGGLE"}
  if (mensagem.indexOf("TOGGLE") >= 0 || mensagem.indexOf("SIMULAR_TAG") >= 0) {
    ultimoAcionamentoMs = millis();
    alternarEstadoPorta("REMOTO:DASHBOARD", "DASHBOARD_MQTT");
  } else if (mensagem.indexOf("ABRIR") >= 0 && portaFechada) {
    ultimoAcionamentoMs = millis();
    alternarEstadoPorta("REMOTO:EMERGENCIA", "DASHBOARD_EMERGENCIA");
  } else if (mensagem.indexOf("FECHAR") >= 0 && !portaFechada) {
    ultimoAcionamentoMs = millis();
    alternarEstadoPorta("REMOTO:DASHBOARD", "DASHBOARD_MQTT");
  }
}

// ======================================================================================
// 6. GERENCIAMENTO DE CONEXÃO WI-FI E MQTT (NÃO-BLOQUEANTE)
// ======================================================================================

void gerenciarConexoesRede() {
  // Se o usuário ainda não configurou o SSID real, ignora tentativas repetidas sem travar o RFID
  if (String(WIFI_SSID) == "SEU_WIFI_AQUI") {
    return;
  }

  // 1. Verifica Wi-Fi a cada 10 segundos se desconectado
  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - ultimaTentativaWifiMs > 10000) {
      ultimaTentativaWifiMs = millis();
      Serial.print(F("📶 [WI-FI] Tentando conectar a rede: "));
      Serial.println(WIFI_SSID);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    return;
  }

  // 2. Se Wi-Fi está conectado, verifica conexão com o Broker MQTT a cada 5 segundos
  if (!mqttClient.connected()) {
    if (millis() - ultimaTentativaMqttMs > 5000) {
      ultimaTentativaMqttMs = millis();
      Serial.print(F("🔄 [MQTT] Conectando ao Broker "));
      Serial.print(MQTT_BROKER);
      Serial.print(F("... "));

      // Cria ID único adicionando parte do MAC Address para evitar conflito em brokers públicos
      String clientId = String(MQTT_CLIENT_ID) + "_" + String((uint32_t)ESP.getEfuseMac(), HEX);

      if (mqttClient.connect(clientId.c_str())) {
        Serial.println(F("CONECTADO!"));
        mqttClient.subscribe(TOPIC_COMANDO);
        publicarEstadoMqtt(0, "BOOT_SYNC");
      } else {
        Serial.print(F("Falhou (rc="));
        Serial.print(mqttClient.state());
        Serial.println(F("). Tentando novamente em 5s."));
      }
    }
  } else {
    mqttClient.loop();
  }
}

// ======================================================================================
// 7. LEITURA DO SENSOR RFID RC522
// ======================================================================================

/**
 * Converte os bytes do UID da Tag RFID para uma String hexadecimal (ex: "A1:B2:C3:D4").
 */
String obterUidFormatado() {
  String uidStr = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) {
      uidStr += "0";
    }
    uidStr += String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) {
      uidStr += ":";
    }
  }
  uidStr.toUpperCase();
  return uidStr;
}

void verificarLeituraRfid() {
  // Verifica se há uma nova tag aproximada do leitor RC522
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  unsigned long agoraMs = millis();

  // Aplica o Debounce de 2 segundos para evitar leituras duplicadas acidentais
  if (agoraMs - ultimoAcionamentoMs < DEBOUNCE_RFID_MS) {
    Serial.println(F("⏳ [RFID] Leitura ignorada (Debounce de 2 segundos ativo)."));
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  ultimoAcionamentoMs = agoraMs;
  String uidLido = obterUidFormatado();

  Serial.print(F("🏷️ [RFID] Tag detectada com sucesso! UID: "));
  Serial.println(uidLido);

  // Encerra comunicação criptográfica com a tag atual
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  // Executa a alternância (Toggle) do estado da porta
  alternarEstadoPorta(uidLido, "ESP32_RFID");
}

// ======================================================================================
// 8. SETUP E LOOP PRINCIPAL DO ESP32
// ======================================================================================

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println(F("=================================================================="));
  Serial.println(F("  UNINASSAU — SISTEMA MULTISSENSORIAL DE PORTA DE BANHEIRO (ESP32)"));
  Serial.println(F("=================================================================="));

  // 1. Configura pinos do Motor de Vibração, Buzzer e LED interno
  pinMode(PIN_VIBRACAO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_STATUS, OUTPUT);

  digitalWrite(PIN_VIBRACAO, LOW);
  definirEstadoBuzzer(false);
  digitalWrite(PIN_LED_STATUS, LOW);

  // 2. Inicializa o Micro Servo Motor SG90 na posição inicial (0° = ABERTA)
  ESP32PWM::allocateTimer(0);
  servoTranca.setPeriodHertz(50); // Frequência padrão de 50Hz para servos SG90
  servoTranca.attach(PIN_SERVO, 500, 2400);
  servoTranca.write(ANGULO_DESTRANCADO);
  Serial.println(F("✅ [SERVO SG90] Inicializado na posicao 0° (PORTA ABERTA)."));

  // 3. Inicializa barramento SPI e módulo Leitor RFID RC522
  SPI.begin(); // SCK=18, MISO=19, MOSI=23, SS=5
  rfid.PCD_Init();
  delay(50);
  Serial.print(F("✅ [RFID RC522] Inicializado. Versao do Firmware RC522: 0x"));
  Serial.println(rfid.PCD_ReadRegister(rfid.VersionReg), HEX);

  // 4. Inicializa Wi-Fi e configura cliente MQTT
  WiFi.mode(WIFI_STA);
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(callbackMqtt);

  if (String(WIFI_SSID) != "SEU_WIFI_AQUI") {
    Serial.print(F("📶 [WI-FI] Conectando a "));
    Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  } else {
    Serial.println(F("ℹ️ [WI-FI] SSID padrao detectado. Configure WIFI_SSID para publicar no MQTT."));
  }

  // Bipe curto de inicialização concluída (100ms)
  definirEstadoBuzzer(true);
  delay(100);
  definirEstadoBuzzer(false);

  Serial.println(F("🟢 Sistema pronto! Aproxime uma Tag RFID do leitor RC522 para trancar/destrancar."));
  Serial.println(F("------------------------------------------------------------------"));
}

void loop() {
  // 1. Mantém conexão Wi-Fi e MQTT ativa sem bloquear a leitura da porta
  gerenciarConexoesRede();

  // 2. Verifica continuamente se uma Tag RFID foi aproximada
  verificarLeituraRfid();

  // 3. Permite testar também pelo Monitor Serial da Arduino IDE digitando 't' ou 'T'
  if (Serial.available() > 0) {
    char tecla = Serial.read();
    if (tecla == 't' || tecla == 'T') {
      if (millis() - ultimoAcionamentoMs >= DEBOUNCE_RFID_MS) {
        ultimoAcionamentoMs = millis();
        Serial.println(F("⌨️ [SERIAL] Comando de teste recebido via Monitor Serial!"));
        alternarEstadoPorta("SERIAL:TESTE", "MONITOR_SERIAL");
      }
    }
  }

  delay(20);
}
