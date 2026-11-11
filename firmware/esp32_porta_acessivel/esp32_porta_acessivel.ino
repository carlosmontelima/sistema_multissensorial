/*
 * ======================================================================================
 * SISTEMA MULTISSENSORIAL DE SINALIZAÇÃO E ACESSIBILIDADE PARA PORTAS DE BANHEIRO
 * ======================================================================================
 *
 * Microcontrolador: ESP32 DOIT DevKit V1 (30 Pinos)
 *
 * --------------------------------------------------------------------------------------
 * BIBLIOTECAS NECESSÁRIAS NA ARDUINO IDE (Sketch -> Incluir Biblioteca -> Gerenciar Bibliotecas):
 * --------------------------------------------------------------------------------------
 * 1. MFRC522         (por GithubCommunity)      -> Leitura do módulo RFID RC522 via SPI
 * 2. ESP32Servo      (por Kevin Harrington)     -> Controle PWM do Micro Servo Motor SG90
 * 3. PubSubClient    (por Nick O'Leary)         -> Cliente MQTT para comunicação IoT
 * 4. ArduinoJson     (por Benoit Blanchon v6/v7)-> Formatação das mensagens JSON no MQTT
 *
 * --------------------------------------------------------------------------------------
 * RESUMO DA PINAGEM VALIDADA EM BANCADA (Conforme docs/GUIA_DE_MONTAGEM.md):
 * --------------------------------------------------------------------------------------
 * - RFID RC522 (3.3V direto no 3V3):
 *     SDA(SS)=GPIO 5 | SCK=GPIO 18 | MOSI=GPIO 23 | MISO=GPIO 19 | RST=GPIO 22 | GND=GND
 * - Servo SG90 (Simulação da Tranca Física - 5V no VIN):
 *     Sinal (Laranja)=GPIO 13 | VCC (Vermelho)=5V(VIN) | GND (Marrom)=GND
 * - Motor de Vibração MV50 (Retorno Tátil - 5V no VIN):
 *     IN/SIG=GPIO 27 | VCC=5V(VIN) | GND=GND
 * - Módulo Buzzer Ativo 5V (Retorno Sonoro - Chaveamento diferencial 5V/3.3V):
 *     VCC=5V(VIN) | I/O=GND | GND=GPIO 26 (LOW=5V apita / HIGH=1.7V mudo)
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
const char* WIFI_SSID      = "SEU_WIFI_AQUI";       // Nome da rede Wi-Fi 2.4GHz
const char* WIFI_PASSWORD  = "SUA_SENHA_WIFI_AQUI"; // Senha do Wi-Fi

const char* MQTT_BROKER    = "broker.hivemq.com";
const int   MQTT_PORT      = 1883;
const char* MQTT_CLIENT_ID = "ESP32_PortaAcessivel_01";

// Tópicos MQTT (ESP32 <-> MQTT <-> Computador)
const char* TOPIC_ESTADO   = "uninassau/acessibilidade/banheiro01/estado";
const char* TOPIC_COMANDO  = "uninassau/acessibilidade/banheiro01/comando";

// ======================================================================================
// 2. DEFINIÇÃO DOS PINOS DO HARDWARE (ESP32 30 PINOS)
// ======================================================================================
#define PIN_RFID_SS    5   // Pino SDA / SS do RC522 -> GPIO 5 (D5)
#define PIN_RFID_RST   22  // Pino RST do RC522      -> GPIO 22 (D22)
// SCK = GPIO 18 | MISO = GPIO 19 | MOSI = GPIO 23 (definidos pelo SPI.begin())

#define PIN_SERVO      13  // Fio Laranja do Servo SG90 (Tranca Física) -> GPIO 13 (D13)
#define PIN_VIBRACAO   27  // Pino IN do Motor Vibração MV50            -> GPIO 27 (D27)
#define PIN_BUZZER     26  // Pino GND do Módulo Buzzer Ativo 5V        -> GPIO 26 (D26)
#define PIN_LED_STATUS 2   // LED Azul interno do ESP32 (aceso = porta trancada)

// Ângulos do Servo Motor SG90 (Simulação da Tranca Física da Porta)
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
unsigned int contadorUsosSessao = 0;        // Quantidade de usos na sessão
String ultimaTagUid = "NENHUMA";

// ======================================================================================
// 4. FUNÇÕES DE ACIONAMENTO DOS ATUADORES (TRANCA SERVO, VIBRAÇÃO E BUZZER)
// ======================================================================================

/**
 * Liga ou desliga o Buzzer 5V no ESP32 (3.3V):
 * Com VCC no 5V, I/O no GND e GND do Buzzer no D26:
 * - Quando D26 = LOW (0V)   -> O Buzzer recebe 5V completos (5V - 0V = 5V) e APITA!
 * - Quando D26 = HIGH (3.3V)-> Sobram apenas 1.7V no Buzzer (5V - 3.3V = 1.7V) e fica MUDO!
 */
void definirEstadoBuzzer(bool ligado) {
  pinMode(PIN_BUZZER, OUTPUT);
  if (ligado) {
    digitalWrite(PIN_BUZZER, LOW);  // 0V -> Libera os 5V para apitar
  } else {
    digitalWrite(PIN_BUZZER, HIGH); // 3.3V -> Derruba a tensão para 1.7V (Silêncio total)
  }
}

/**
 * Move o Servo SG90 (tranca física) para o ângulo exato (0° ou 90°) e desliga o pulso PWM
 * (detach) após concluir o movimento para evitar tremores ou desvios de posição.
 */
void moverServoEstavel(int angulo) {
  servoTranca.write(angulo);
  if (!servoTranca.attached()) {
    servoTranca.attach(PIN_SERVO, 500, 2400);
  }
  servoTranca.write(angulo);
  delay(450); // Tempo para o braço do SG90 completar o giro físico
  servoTranca.detach();
  digitalWrite(PIN_SERVO, LOW);
}

/**
 * Executa o retorno multissensorial simultâneo ao TRANCAR a porta (Estado = FECHADA):
 * - Servo Motor gira para 90° (Tranca fechada)
 * - Motor de Vibração aciona 1 pulso forte de 600ms
 * - Buzzer Ativo emite 1 bipe longo de 500ms
 */
void acionarFeedbackTrancar() {
  Serial.println(F("[ATUADORES] Trancando porta: Servo -> 90° | Vibracao -> 600ms | Buzzer -> 500ms"));

  digitalWrite(PIN_LED_STATUS, HIGH);
  digitalWrite(PIN_VIBRACAO, HIGH);
  definirEstadoBuzzer(true);

  moverServoEstavel(ANGULO_TRANCADO); // 450ms movendo o servo para 90°

  delay(50); // Completa os 500ms do Buzzer (450 + 50 = 500ms)
  definirEstadoBuzzer(false);

  delay(100); // Completa os 600ms da Vibração (500 + 100 = 600ms)
  digitalWrite(PIN_VIBRACAO, LOW);
}

/**
 * Executa o retorno multissensorial simultâneo ao DESTRANCAR a porta (Estado = ABERTA):
 * - Servo Motor retorna para 0° (Tranca aberta)
 * - Motor de Vibração aciona 2 pulsos curtos de 150ms
 * - Buzzer Ativo emite 2 bipes rápidos de 150ms
 */
void acionarFeedbackDestrancar() {
  Serial.println(F("[ATUADORES] Destrancando porta: Servo -> 0° | Vibracao -> 2x 150ms | Buzzer -> 2x 150ms"));

  digitalWrite(PIN_LED_STATUS, LOW);
  moverServoEstavel(ANGULO_DESTRANCADO);

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

  Serial.print(F("[MQTT TX] Publicando em "));
  Serial.print(TOPIC_ESTADO);
  Serial.print(F(": "));
  Serial.println(bufferJson);

  if (mqttClient.connected()) {
    mqttClient.publish(TOPIC_ESTADO, bufferJson, true);
  } else {
    Serial.println(F("[MQTT] Broker desconectado no momento. Funcionamento fisico normal."));
  }
}

void alternarEstadoPorta(String uidTag, const char* origem) {
  portaFechada = !portaFechada;
  ultimaTagUid = uidTag;

  if (portaFechada) {
    inicioOcupacaoMs = millis();
    acionarFeedbackTrancar();
    publicarEstadoMqtt(0, origem);
    Serial.println(F("ESTADO ATUAL: FECHADA / OCUPADO"));
  } else {
    unsigned long duracaoSegundos = 0;
    if (inicioOcupacaoMs > 0) {
      duracaoSegundos = (millis() - inicioOcupacaoMs) / 1000;
      if (duracaoSegundos == 0) duracaoSegundos = 1;
    }
    contadorUsosSessao++;

    acionarFeedbackDestrancar();
    publicarEstadoMqtt(duracaoSegundos, origem);

    Serial.print(F("ESTADO ATUAL: ABERTA / LIVRE (Tempo ocupada: "));
    Serial.print(duracaoSegundos);
    Serial.println(F(" segundos)"));
  }
  Serial.println(F("------------------------------------------------------------------"));
}

void callbackMqtt(char* topic, byte* payload, unsigned int length) {
  String mensagem = "";
  for (unsigned int i = 0; i < length; i++) {
    mensagem += (char)payload[i];
  }
  mensagem.trim();

  Serial.print(F("[MQTT RX] Comando recebido em "));
  Serial.print(topic);
  Serial.print(F(": "));
  Serial.println(mensagem);

  if (millis() - ultimoAcionamentoMs < DEBOUNCE_RFID_MS) {
    return;
  }

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
  if (String(WIFI_SSID) == "SEU_WIFI_AQUI") {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (millis() - ultimaTentativaWifiMs > 10000) {
      ultimaTentativaWifiMs = millis();
      Serial.print(F("[WI-FI] Conectando a rede: "));
      Serial.println(WIFI_SSID);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    return;
  }

  if (!mqttClient.connected()) {
    if (millis() - ultimaTentativaMqttMs > 5000) {
      ultimaTentativaMqttMs = millis();
      Serial.print(F("[MQTT] Conectando ao Broker "));
      Serial.print(MQTT_BROKER);
      Serial.print(F("... "));

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
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  unsigned long agoraMs = millis();

  if (agoraMs - ultimoAcionamentoMs < DEBOUNCE_RFID_MS) {
    Serial.println(F("[RFID] Leitura ignorada (Debounce de 2 segundos ativo)."));
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  ultimoAcionamentoMs = agoraMs;
  String uidLido = obterUidFormatado();

  Serial.print(F("[RFID] Tag detectada com sucesso! UID: "));
  Serial.println(uidLido);

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

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
  Serial.println(F("  SISTEMA MULTISSENSORIAL DE PORTA DE BANHEIRO (ESP32 + MQTT)"));
  Serial.println(F("=================================================================="));

  pinMode(PIN_VIBRACAO, OUTPUT);
  pinMode(PIN_LED_STATUS, OUTPUT);

  digitalWrite(PIN_VIBRACAO, LOW);
  definirEstadoBuzzer(false);
  digitalWrite(PIN_LED_STATUS, LOW);

  ESP32PWM::allocateTimer(0);
  servoTranca.setPeriodHertz(50);
  moverServoEstavel(ANGULO_DESTRANCADO);
  Serial.println(F("[SERVO SG90] Inicializado na posicao 0° (PORTA ABERTA)."));

  SPI.begin();
  rfid.PCD_Init();
  delay(50);
  Serial.print(F("[RFID RC522] Inicializado. Versao do Firmware RC522: 0x"));
  Serial.println(rfid.PCD_ReadRegister(rfid.VersionReg), HEX);

  WiFi.mode(WIFI_STA);
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(callbackMqtt);

  if (String(WIFI_SSID) != "SEU_WIFI_AQUI") {
    Serial.print(F("[WI-FI] Conectando a "));
    Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  } else {
    Serial.println(F("[WI-FI] SSID padrao detectado. Configure WIFI_SSID para publicar no MQTT."));
  }

  definirEstadoBuzzer(true);
  delay(100);
  definirEstadoBuzzer(false);

  Serial.println(F("Sistema pronto! Aproxime uma Tag RFID do leitor RC522 para trancar/destrancar."));
  Serial.println(F("------------------------------------------------------------------"));
}

void loop() {
  gerenciarConexoesRede();
  verificarLeituraRfid();

  if (Serial.available() > 0) {
    char tecla = Serial.read();
    if (tecla == 't' || tecla == 'T') {
      if (millis() - ultimoAcionamentoMs >= DEBOUNCE_RFID_MS) {
        ultimoAcionamentoMs = millis();
        Serial.println(F("[SERIAL] Comando de teste recebido via Monitor Serial!"));
        alternarEstadoPorta("SERIAL:TESTE", "MONITOR_SERIAL");
      }
    }
  }

  delay(20);
}
