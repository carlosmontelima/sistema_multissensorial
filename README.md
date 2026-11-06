# Sistema Multissensorial de Sinalização e Acessibilidade para Portas de Banheiro

Sistema embarcado com **ESP32** e **MQTT** para sinalização acessível de portas de banheiro, oferecendo retorno físico (tranca com servo motor), tátil (motor de vibração), sonoro (buzzer ativo) e visual (tela de sinalização com cadeado e painel de estatísticas).

---

## Estrutura de Arquivos do Projeto

```text
SISTEMA MULTISSENSORIAL/
├── .gitignore                                     # Arquivos ignorados pelo Git
├── run.bat                                        # Script para abrir a dashboard e iniciar o simulador Python
├── README.md                                      # Visão geral e instruções do projeto
├── docs/
│   ├── GUIA_DE_MONTAGEM.md                        # Pinagem exata e guia de montagem na Protoboard de 400 pontos
│   └── CONEXAO_ESP32.md                           # Passo a passo de como conectar o ESP32 físico ao sistema via Wi-Fi/MQTT
├── firmware/
│   └── esp32_porta_acessivel/
│       └── esp32_porta_acessivel.ino              # Código C++ completo e comentado para o ESP32 (Arduino IDE)
├── node-red/
│   └── flows.json                                 # Fluxo pronto para importação no Node-RED
├── dashboard-web/
│   ├── index.html                                 # Tela inicial de seleção (Tela da Porta vs Painel de Monitoramento)
│   ├── porta.html                                 # Tela exclusiva para a porta (Fundo verde/vermelho + Cadeado Branco)
│   ├── dashboard.html                             # Painel administrativo (Estatísticas, Gráfico, Histórico e Simulação)
│   ├── style.css                                  # Folha de estilos da porta e da dashboard
│   └── app.js                                     # Sincronização em tempo real (MQTT + BroadcastChannel + localStorage)
└── simulator/
    ├── run.bat                                    # Script .bat que sobe o servidor web, abre a dashboard e roda o simulador
    ├── simulador_esp32.py                         # Simulador de terminal que emula o ESP32 enviando/recebendo pacotes MQTT
    └── requirements.txt                           # Dependência paho-mqtt
```

---

## Como Rodar Tudo de Uma Vez (Modo Simulador)

Basta dar dois cliques em **[`run.bat`](./run.bat)** (ou em **[`simulator/run.bat`](./simulator/run.bat)**).

O script automaticamente:
1. Verifica/instala a biblioteca `paho-mqtt`;
2. Inicia um servidor web local na porta `8080`;
3. Abre direto o **Painel de Monitoramento** (`http://localhost:8080/dashboard.html`) no navegador;
4. Inicia o **`simulador_esp32.py`** no terminal (pressione `ENTER` no terminal ou clique em **Simular Tag RFID** no `dashboard.html` para alternar o estado da porta).

---

## Quando o ESP32 Chegar

1. Monte o circuito na protoboard seguindo o **[`docs/GUIA_DE_MONTAGEM.md`](./docs/GUIA_DE_MONTAGEM.md)**.
2. Siga o tutorial em **[`docs/CONEXAO_ESP32.md`](./docs/CONEXAO_ESP32.md)** para gravar o arquivo **[`firmware/esp32_porta_acessivel/esp32_porta_acessivel.ino`](./firmware/esp32_porta_acessivel/esp32_porta_acessivel.ino)** e conectar o ESP32 ao Wi-Fi e ao Broker MQTT.
