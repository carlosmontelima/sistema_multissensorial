# Como Gravar e Conectar o ESP32 com o Sistema (Dashboard Web e Node-RED)

Este guia descreve a configuração na **Arduino IDE** e a comunicação em tempo real entre o protótipo físico no **ESP32** (Leitor RFID RC522, Servo Motor SG90 simulando a tranca da porta, Motor de Vibração MV50 e Buzzer Ativo 5V) e as interfaces Web (`porta.html` e `dashboard.html`).

---

## 1. Arquitetura de Comunicação MQTT

O ESP32 e o computador se comunicam via **Broker MQTT** utilizando dois tópicos:

- **Tópico de Estado (ESP32 publica -> Telas recebem):**
  `uninassau/acessibilidade/banheiro01/estado`
- **Tópico de Comando (Painel publica -> ESP32 recebe):**
  `uninassau/acessibilidade/banheiro01/comando`

Por padrão, tanto o firmware (`esp32_porta_acessivel.ino`) quanto as páginas Web (`porta.html` e `dashboard.html`) apontam para o broker **`broker.hivemq.com`**. Basta que o ESP32 esteja conectado a uma rede Wi-Fi de `2.4 GHz` com internet (como o roteador/Hotspot do celular) para sincronizar automaticamente com o computador.

---

## 2. Configuração e Upload na Arduino IDE

### Passo 2.1. Selecionar a Placa e a Porta
1. Em **Arquivo > Preferências > URLs Adicionais para Gerenciadores de Placas**, adicione:
   ```text
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
2. Em **Ferramentas > Placa > Gerenciador de Placas**, instale **esp32 by Espressif Systems**.
3. Selecione a placa **DOIT ESP32 DEVKIT V1** e a porta USB correspondente (ex: **`COM5`**).

### Passo 2.2. Bibliotecas Utilizadas
Em **Sketch > Incluir Biblioteca > Gerenciar Bibliotecas**, instale:
1. **`MFRC522`** (por *GithubCommunity*)
2. **`ESP32Servo`** (por *Kevin Harrington*)
3. **`PubSubClient`** (por *Nick O'Leary*)
4. **`ArduinoJson`** (por *Benoit Blanchon*)

### Passo 2.3. Configurar Wi-Fi e Carregar
1. Abra [`firmware/esp32_porta_acessivel/esp32_porta_acessivel.ino`](../firmware/esp32_porta_acessivel/esp32_porta_acessivel.ino).
2. Nas linhas **39 e 40**, preencha o nome e a senha do Wi-Fi (`2.4 GHz`):
   ```cpp
   const char* WIFI_SSID     = "NOME_DO_WIFI";
   const char* WIFI_PASSWORD = "SENHA_DO_WIFI";
   ```
3. Clique em **Upload (Carregar)**. Você pode fazer o upload com todos os componentes conectados na protoboard.

---

## 3. Comportamento Validado do Protótipo

A cada leitura válida da Tag RFID no leitor RC522 (com *debounce* de 2 segundos para evitar leituras duplicadas) — ou ao acionar remotamente pelo botão **Simular Tag RFID** na Dashboard / tecla `t` no Monitor Serial (`115200 baud`):

1. **Ao TRANCAR a porta (`Estado = FECHADA / OCUPADO`):**
   - **Tranca Física (Servo SG90):** Gira para **`90°`** e desliga o pulso PWM (`detach`) após `450ms` para manter a posição firme sem tremores.
   - **Retorno Tátil (Motor de Vibração MV50):** Emite **1 pulso forte de `600ms`**.
   - **Retorno Sonoro (Buzzer Ativo 5V):** Emite **1 bipe longo de `500ms`**.
   - **Retorno Visual (MQTT):** A tela [`porta.html`](../dashboard-web/porta.html) fica **Vermelha (`rgb(255, 25, 25)`)** com o cadeado branco fechado (`OCUPADO / PORTA FECHADA`) e o cronômetro é iniciado no [`dashboard.html`](../dashboard-web/dashboard.html).

2. **Ao DESTRANCAR a porta (`Estado = ABERTA / LIVRE`):**
   - **Tranca Física (Servo SG90):** Retorna para **`0°`** e estabiliza.
   - **Retorno Tátil (Motor de Vibração MV50):** Emite **2 pulsos curtos de `150ms`**.
   - **Retorno Sonoro (Buzzer Ativo 5V):** Emite **2 bipes rápidos de `150ms`**.
   - **Retorno Visual e Estatísticas (MQTT):** O ESP32 publica o estado `"ABERTA"` junto com a duração em segundos que a porta permaneceu fechada. A tela [`porta.html`](../dashboard-web/porta.html) volta para **Verde (`rgb(25, 255, 87)`)** com o cadeado branco aberto (`LIVRE / PORTA ABERTA`), e o [`dashboard.html`](../dashboard-web/dashboard.html) incrementa o contador do dia, recalcula o tempo médio e atualiza o gráfico de usos por horário.
