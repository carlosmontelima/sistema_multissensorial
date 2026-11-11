# Sistema Multissensorial de Sinalização e Acessibilidade para Portas de Banheiro

Sistema embarcado com **ESP32** e **MQTT** para sinalização acessível de portas de banheiro, projetado para oferecer retorno simultâneo em múltiplos canais sensoriais para pessoas cegas, com baixa visão, surdas ou surdocegas:

1. **Retorno Físico (Simulação da Tranca da Porta):** Micro Servo Motor **SG90** que atua como tranca física automatizada (`90°` ao trancar / `0°` ao destrancar) mediante aproximação de Tag/Cartão no leitor **RFID RC522** (com *debounce* de 2 segundos e desligamento automático de PWM após o giro para estabilidade total).
2. **Retorno Tátil (Vibração):** Módulo Motor de Vibração DC **MV50** (`1 pulso forte de 600ms` confirmando o fechamento; `2 pulsos curtos de 150ms` confirmando a abertura).
3. **Retorno Sonoro (Buzzer):** Módulo **Buzzer Ativo 5V** (`1 bipe longo de 500ms` ao trancar; `2 bipes rápidos de 150ms` ao destrancar).
4. **Retorno Visual e Monitoramento IoT (MQTT):**
   - **Tela da Porta ([`dashboard-web/porta.html`](./dashboard-web/porta.html)):** Interface em tela cheia preenchida em **Verde `rgb(25, 255, 87)` (`LIVRE / PORTA ABERTA`)** ou **Vermelho `rgb(255, 25, 25)` (`OCUPADO / PORTA FECHADA`)** com cadeado branco centralizado e alerta visual de emergência.
   - **Painel de Monitoramento ([`dashboard-web/dashboard.html`](./dashboard-web/dashboard.html)):** Dashboard com estatísticas de usos no dia, tempo médio de ocupação, cronômetro em tempo real, alerta de emergência configurável, gráfico de usos por horário e histórico de dias anteriores.

---

## Pinagem Validada no ESP32 (30 Pinos)

| Componente | Pino do Módulo | Destino no ESP32 / Protoboard |
| :--- | :--- | :--- |
| **RFID RC522** (`3.3V`) | `SDA` / `SCK` / `MOSI` / `MISO` / `RST` | `D5` / `D18` / `D23` / `D19` / `D22` |
| **RFID RC522** (`3.3V`) | `3.3V` / `GND` | Pino `3V3` direto no ESP32 / Linha `GND` (`-`) |
| **Servo Motor SG90** (`5V`) | `Sinal` (Laranja) / `VCC` / `GND` | `D13` / Linha `5V` (`VIN`) / Linha `GND` (`-`) |
| **Motor de Vibração MV50** (`5V`) | `IN` (`SIG`) / `VCC` / `GND` | `D27` / Linha `5V` (`VIN`) / Linha `GND` (`-`) |
| **Buzzer Ativo 5V** | `VCC` / `I/O` / `GND` | Linha `5V` (`VIN`) / Linha `GND` (`-`) / Pino **`D26`** |

*(Para mais detalhes passo a passo da protoboard, consulte **[`docs/GUIA_DE_MONTAGEM.md`](./docs/GUIA_DE_MONTAGEM.md)**).*

---

## Estrutura de Arquivos do Projeto

```text
SISTEMA MULTISSENSORIAL/
├── .gitignore                                     # Arquivos ignorados pelo Git
├── run.bat                                        # Abre a Dashboard e inicia o simulador Python
├── README.md                                      # Documentação principal do projeto
├── docs/
│   ├── GUIA_DE_MONTAGEM.md                        # Pinagem validada e montagem na Protoboard de 400 pontos
│   └── CONEXAO_ESP32.md                           # Como gravar o código no ESP32 e conectar via Wi-Fi/MQTT
├── firmware/
│   └── esp32_porta_acessivel/
│       └── esp32_porta_acessivel.ino              # Firmware completo do ESP32 (C++ / Arduino IDE)
├── node-red/
│   └── flows.json                                 # Fluxo pronto para importação no Node-RED
├── dashboard-web/
│   ├── index.html                                 # Tela inicial de seleção (Tela da Porta vs Painel de Monitoramento)
│   ├── porta.html                                 # Tela exclusiva da porta (Fundo verde/vermelho + Cadeado Branco)
│   ├── dashboard.html                             # Painel administrativo (Estatísticas, Gráfico, Histórico e Controle)
│   ├── style.css                                  # Folha de estilos e animações da interface
│   └── app.js                                     # Lógica MQTT over WebSockets, Buzzer Web Audio, Gráfico e Sincronização
└── simulator/
    ├── run.bat                                    # Sobe o servidor web local, abre a dashboard e roda o simulador
    ├── simulador_esp32.py                         # Simulador de terminal que emula o ESP32 via MQTT
    └── requirements.txt                           # Dependência paho-mqtt
```

---

## Como Executar

### 1. Com o Protótipo Físico (ESP32 + RFID + Servo + Vibração + Buzzer)
1. Monte o circuito na protoboard seguindo **[`docs/GUIA_DE_MONTAGEM.md`](./docs/GUIA_DE_MONTAGEM.md)**.
2. Configure o `WIFI_SSID` e `WIFI_PASSWORD` no arquivo **[`firmware/esp32_porta_acessivel/esp32_porta_acessivel.ino`](./firmware/esp32_porta_acessivel/esp32_porta_acessivel.ino)** e carregue para o ESP32 pela Arduino IDE (veja **[`docs/CONEXAO_ESP32.md`](./docs/CONEXAO_ESP32.md)**).
3. Abra o **[`dashboard-web/dashboard.html`](./dashboard-web/dashboard.html)** e clique em **"Abrir Tela da Porta em Nova Aba"** ([`porta.html`](./dashboard-web/porta.html)).
4. Aproxime a Tag RFID do leitor RC522 para trancar (`90°`) e destrancar (`0°`) a porta em tempo real.

### 2. Com o Simulador (`run.bat`)
Dê dois cliques em **[`run.bat`](./run.bat)** para abrir automaticamente o **Painel de Monitoramento** (`http://localhost:8080/dashboard.html`) e o terminal do **`simulador_esp32.py`**.
