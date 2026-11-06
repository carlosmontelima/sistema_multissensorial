# Como Conectar o ESP32 Físico com o Sistema (Dashboard Web e Node-RED)

Este documento explica o passo a passo para substituir o simulador pelo **ESP32 físico** assim que os componentes chegarem, fazendo com que ele se comunique diretamente com a **Tela da Porta (`porta.html`)**, com o **Painel de Monitoramento (`dashboard.html`)** e com o **Node-RED**.

---

## 1. Como Funciona a Comunicação

O ESP32 e as telas do computador conversam através de um **Broker MQTT** usando os mesmos tópicos que o simulador já utiliza:

- **Tópico de Estado (ESP32 publica -> Telas recebem):**
  `uninassau/acessibilidade/banheiro01/estado`
- **Tópico de Comando (Painel publica -> ESP32 recebe):**
  `uninassau/acessibilidade/banheiro01/comando`

Por padrão, tanto o código do ESP32 (`esp32_porta_acessivel.ino`) quanto as páginas Web (`porta.html` e `dashboard.html`) já vêm configurados para o broker **`broker.hivemq.com`**. Isso significa que **basta o ESP32 e o computador terem acesso à internet (por exemplo, roteando o Wi-Fi do celular)** para que eles se enxerguem automaticamente, sem precisar configurar IP fixo ou instalar servidor MQTT no notebook.

---

## 2. Passo a Passo na Arduino IDE

### Passo 2.1. Instalar o Suporte à Placa ESP32
1. Abra a **Arduino IDE**.
2. Vá em **Arquivo > Preferências**.
3. No campo **URLs Adicionais para Gerenciadores de Placas**, cole:
   ```text
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
4. Vá em **Ferramentas > Placa > Gerenciador de Placas**, pesquise por **`esp32`** e instale o pacote **esp32 by Espressif Systems**.
5. Em **Ferramentas > Placa**, selecione **DOIT ESP32 DEVKIT V1**.

### Passo 2.2. Instalar as 4 Bibliotecas Necessárias
Vá em **Sketch > Incluir Biblioteca > Gerenciar Bibliotecas** e instale:
1. **`MFRC522`** (por *GithubCommunity*)
2. **`ESP32Servo`** (por *Kevin Harrington*)
3. **`PubSubClient`** (por *Nick O'Leary*)
4. **`ArduinoJson`** (por *Benoit Blanchon*)

### Passo 2.3. Configurar o Wi-Fi no Código do ESP32
1. Abra o arquivo [`firmware/esp32_porta_acessivel/esp32_porta_acessivel.ino`](../firmware/esp32_porta_acessivel/esp32_porta_acessivel.ino).
2. Nas linhas **39 e 40**, coloque o nome e a senha da rede Wi-Fi (dica: na apresentação na faculdade, use o roteador/Hotspot de `2.4 GHz` do celular):
   ```cpp
   const char* WIFI_SSID     = "NOME_DO_SEU_WIFI";
   const char* WIFI_PASSWORD = "SENHA_DO_SEU_WIFI";
   ```
   *(Atenção: o ESP32 conecta apenas em redes Wi-Fi de **2.4 GHz**; não use redes 5 GHz).*

### Passo 2.4. Gravar o Código no ESP32
1. Com o circuito montado conforme o [`docs/GUIA_DE_MONTAGEM.md`](./GUIA_DE_MONTAGEM.md), conecte o ESP32 ao computador via cabo Micro-USB.
2. Selecione a porta COM em **Ferramentas > Porta**.
3. Clique na seta **Carregar (Upload)**.
   - *Observação:* Se aparecer `Connecting........_____` na parte inferior da Arduino IDE, segure o botão **`BOOT`** na placa ESP32 por 2 segundos até o carregamento iniciar.
4. Abra o **Monitor Serial** (`Ctrl + Shift + M`) em **`115200 baud`** para ver as mensagens:
   - `Conectando a rede...`
   - `Conectado ao Broker broker.hivemq.com... CONECTADO!`
   - `Sistema pronto! Aproxime uma Tag RFID do leitor RC522 para trancar/destrancar.`

---

## 3. Abrindo as Telas no Computador

Com o ESP32 ligado na tomada (ou na USB):

1. Abra o [`dashboard-web/dashboard.html`](../dashboard-web/dashboard.html) e o [`dashboard-web/porta.html`](../dashboard-web/porta.html) no navegador.
2. No `dashboard.html`, certifique-se de que o seletor **Broker MQTT** está em **`Broker Publico (HiveMQ WSS)`** e o indicador ao lado mostra **`Conectado`**.
3. Coloque a janela do `porta.html` em tela cheia (`F11`).
4. **Pronto!** Ao aproximar o cartão/chaveiro RFID do leitor RC522 no ESP32:
   - O **Servo SG90** gira na protoboard (`90°` ao fechar / `0°` ao abrir);
   - O **Motor de Vibração MV50** vibra (`600ms` / `2x 150ms`);
   - O **Buzzer Ativo 5V** apita (`500ms` / `2x 150ms`);
   - A tela **`porta.html`** muda instantaneamente entre **LIVRE / PORTA ABERTA** (Verde) e **OCUPADO / PORTA FECHADA** (Vermelho);
   - O painel **`dashboard.html`** contabiliza os usos do dia, o tempo médio, o cronômetro e o gráfico por horário.

---

## 4. Opção Alternativa: Usar Mosquitto Local (100% Sem Internet)

Caso no local da apresentação não haja sinal de internet nem 4G para usar o broker público:

1. Instale o [Eclipse Mosquitto](https://mosquitto.org/download/) no notebook que exibirá a Dashboard.
2. No arquivo `mosquitto.conf`, habilite a porta TCP padrão (`1883` para o ESP32) e a porta WebSockets (`9001` para o navegador):
   ```text
   allow_anonymous true
   listener 1883 0.0.0.0
   listener 9001 0.0.0.0
   protocol websockets
   ```
3. Descubra o IP local do seu notebook no Wi-Fi (rodando `ipconfig` no terminal do Windows, ex: `192.168.43.100`).
4. No código do ESP32 (`esp32_porta_acessivel.ino`, linha 45), troque:
   ```cpp
   const char* MQTT_BROKER = "192.168.43.100";
   ```
5. No `dashboard.html`, mude o seletor **Broker MQTT** para **`Mosquitto Local (ws://localhost:9001)`**.
