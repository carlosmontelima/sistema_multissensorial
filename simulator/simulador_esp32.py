#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
======================================================================================
PROJETO DE EXTENSÃO — UNINASSAU (CAMPUS GRAÇAS - RECIFE/PE)
SIMULADOR VIRTUAL DO ESP32 + RFID RC522 + SERVO SG90 + VIBRAÇÃO MV50 + BUZZER 5V
======================================================================================
Este script finge ser o ESP32 físico conectado ao Broker MQTT, permitindo testar
toda a Dashboard Web e o Node-RED antes das peças físicas chegarem!

Como executar:
    pip install paho-mqtt
    python simulador_esp32.py
======================================================================================
"""

import json
import random
import sys
import time
from datetime import datetime

# Tenta importar winsound (nativo do Windows) para tocar os bipes reais no PC
try:
    import winsound
    HAS_WINSOUND = True
except ImportError:
    HAS_WINSOUND = False

# Tenta importar paho-mqtt
try:
    import paho.mqtt.client as mqtt
    HAS_MQTT = True
except ImportError:
    HAS_MQTT = False

# ======================================================================================
# CONFIGURAÇÕES PADRÃO (IDÊNTICAS AO FIRMWARE DO ESP32)
# ======================================================================================
BROKER_PADRAO = "broker.hivemq.com"   # Sincroniza direto com dashboard-web/index.html
BROKER_LOCAL  = "localhost"           # Para Mosquitto instalado localmente
MQTT_PORT     = 1883

TOPIC_ESTADO  = "uninassau/acessibilidade/banheiro01/estado"
TOPIC_COMANDO = "uninassau/acessibilidade/banheiro01/comando"

DEBOUNCE_SEGUNDOS = 2.0

TAGS_EXEMPLO = [
    "A1:B2:C3:D4",
    "E5:9A:11:08",
    "7C:44:F0:2B",
    "04:8D:55:9E",
]


class SimuladorESP32PortaAcessivel:
    def __init__(self, broker_host: str = BROKER_PADRAO):
        self.broker_host = broker_host
        self.porta_fechada = False
        self.inicio_ocupacao_ts = 0.0
        self.ultimo_acionamento_ts = 0.0
        self.usos_sessao = 0
        self.conectado_mqtt = False
        self.client = None

    # ----------------------------------------------------------------------------------
    # SIMULAÇÃO FÍSICA DOS ATUADORES (SERVO SG90, VIBRAÇÃO MV50 E BUZZER 5V)
    # ----------------------------------------------------------------------------------
    def tocar_bipe_pc(self, frequencia_hz: int, duracao_ms: int):
        if HAS_WINSOUND:
            try:
                winsound.Beep(frequencia_hz, duracao_ms)
                return
            except Exception:
                pass
        time.sleep(duracao_ms / 1000.0)

    def simular_atuadores_trancar(self):
        print("\n  ⚙️  [SERVO SG90 - GPIO 13]     Girando para 90° (TRANCA FÍSICA ACIONADA)")
        print("  📳  [VIBRAÇÃO MV50 - GPIO 27]  Acionando 1 pulso forte de 600ms...")
        print("  🔊  [BUZZER ATIVO - GPIO 26]   Emitindo 1 bipe longo de 500ms...")
        self.tocar_bipe_pc(780, 500)
        time.sleep(0.1)  # Completa os 600ms da vibração

    def simular_atuadores_destrancar(self):
        print("\n  ⚙️  [SERVO SG90 - GPIO 13]     Retornando para 0° (PORTA DESTRANCADA)")
        print("  📳  [VIBRAÇÃO MV50 - GPIO 27]  Acionando 2 pulsos curtos de 150ms...")
        print("  🔊  [BUZZER ATIVO - GPIO 26]   Emitindo 2 bipes rápidos de 150ms...")
        for i in range(2):
            self.tocar_bipe_pc(1046, 150)
            if i == 0:
                time.sleep(0.15)

    # ----------------------------------------------------------------------------------
    # LÓGICA DE ALTERNÂNCIA (TOGGLE) COM DEBOUNCE DE 2 SEGUNDOS
    # ----------------------------------------------------------------------------------
    def passar_tag_rfid(self, uid_tag: str = None, origem: str = "SIMULADOR_PYTHON"):
        agora = time.time()
        tempo_desde_ultimo = agora - self.ultimo_acionamento_ts

        if tempo_desde_ultimo < DEBOUNCE_SEGUNDOS:
            restante = DEBOUNCE_SEGUNDOS - tempo_desde_ultimo
            print(f"\n⏳ [DEBOUNCE RFID] Leitura ignorada! Aguarde {restante:.1f}s para evitar leitura duplicada.")
            return

        self.ultimo_acionamento_ts = agora
        if not uid_tag:
            uid_tag = random.choice(TAGS_EXEMPLO)

        self.porta_fechada = not self.porta_fechada
        horario_str = datetime.now().strftime("%H:%M:%S")

        print("\n" + "=" * 72)
        print(f"🏷️  [{horario_str}] TAG RFID DETECTADA NO RC522! UID: {uid_tag}")

        duracao_segundos = 0
        if self.porta_fechada:
            # Transição para FECHADA / OCUPADO
            self.inicio_ocupacao_ts = agora
            self.simular_atuadores_trancar()
            print("\n🔴 NOVO ESTADO DA PORTA: FECHADA / OCUPADO")
        else:
            # Transição para ABERTA / LIVRE
            if self.inicio_ocupacao_ts > 0:
                duracao_segundos = max(1, int(round(agora - self.inicio_ocupacao_ts)))
            self.inicio_ocupacao_ts = 0.0
            self.usos_sessao += 1
            self.simular_atuadores_destrancar()
            print(f"\n🟢 NOVO ESTADO DA PORTA: ABERTA / LIVRE (Permaneceu fechada por {duracao_segundos}s)")

        self.publicar_estado_mqtt(uid_tag, duracao_segundos, origem)
        print("=" * 72)

    # ----------------------------------------------------------------------------------
    # COMUNICAÇÃO MQTT (ENVIO E RECEBIMENTO)
    # ----------------------------------------------------------------------------------
    def publicar_estado_mqtt(self, uid_tag: str, duracao_segundos: int, origem: str):
        payload = {
            "estado": "FECHADA" if self.porta_fechada else "ABERTA",
            "ocupado": self.porta_fechada,
            "uid_tag": uid_tag,
            "duracao_ultimo_uso_seg": duracao_segundos,
            "usos_sessao": self.usos_sessao,
            "servo_angulo": 90 if self.porta_fechada else 0,
            "origem": origem,
            "timestamp": int(time.time()),
        }
        json_str = json.dumps(payload, ensure_ascii=False)

        print(f"\n📡 [MQTT TX -> {TOPIC_ESTADO}]")
        print(f"   Payload: {json_str}")

        if self.client and self.conectado_mqtt:
            self.client.publish(TOPIC_ESTADO, json_str, qos=1, retain=True)
            print("   ✅ Publicado com sucesso no Broker MQTT!")
        else:
            print("   ⚠️  Modo Offline (Sem conexão com Broker MQTT no momento).")

    def _on_connect(self, client, userdata, flags, rc, *args):
        if rc == 0:
            self.conectado_mqtt = True
            client.subscribe(TOPIC_COMANDO, qos=1)
        else:
            self.conectado_mqtt = False

    def _on_disconnect(self, client, userdata, *args):
        self.conectado_mqtt = False

    def _on_message(self, client, userdata, msg):
        try:
            comando = msg.payload.decode("utf-8").strip().upper()
            print(f"\n📥 [MQTT RX <- {msg.topic}] Comando recebido da Dashboard: {comando}")
            if "TOGGLE" in comando:
                self.passar_tag_rfid(uid_tag="REMOTO:WEB", origem="DASHBOARD_REMOTO")
        except Exception as e:
            print(f"Erro ao processar mensagem recebida: {e}")

    def iniciar_conexao_mqtt(self):
        if not HAS_MQTT:
            print("\n⚠️  Biblioteca 'paho-mqtt' não encontrada.")
            print("   Para enviar mensagens via rede MQTT, instale com: pip install paho-mqtt")
            print("   Continuando em modo de simulação local no terminal...\n")
            return

        if self.client:
            try:
                self.client.loop_stop()
                self.client.disconnect()
            except Exception:
                pass

        print(f"\n🔄 Conectando ao Broker MQTT ({self.broker_host}:{MQTT_PORT})...")
        try:
            # Compatível tanto com paho-mqtt 1.x quanto 2.x
            if hasattr(mqtt, "CallbackAPIVersion"):
                self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION1)
            else:
                self.client = mqtt.Client()

            self.client.on_connect = self._on_connect
            self.client.on_disconnect = self._on_disconnect
            self.client.on_message = self._on_message

            self.client.connect(self.broker_host, MQTT_PORT, keepalive=60)
            self.client.loop_start()
            time.sleep(1.0)

            if self.conectado_mqtt:
                print(f"✅ Conectado ao Broker MQTT: {self.broker_host}:{MQTT_PORT}")
                print(f"   Publicando em: {TOPIC_ESTADO}")
            else:
                print(f"⚠️  Ainda conectando a {self.broker_host}...")
        except Exception as e:
            self.conectado_mqtt = False
            print(f"⚠️  Não foi possível conectar a {self.broker_host}: {e}")

    # ----------------------------------------------------------------------------------
    # MODO AUTOMÁTICO DE DEMONSTRAÇÃO
    # ----------------------------------------------------------------------------------
    def executar_ciclo_automatico(self, ciclos: int = 3):
        print(f"\n🤖 Iniciando simulação automática de {ciclos} ciclo(s) de uso do banheiro...")
        print("   (Pressione Ctrl+C a qualquer momento para interromper o modo automático)\n")
        try:
            for c in range(1, ciclos + 1):
                print(f"\n--- [Ciclo {c}/{ciclos}] Usuário aproximando Tag RFID para ENTRAR ---")
                if self.porta_fechada:
                    self.passar_tag_rfid()
                    time.sleep(2.5)

                self.passar_tag_rfid()  # Tranca a porta
                tempo_ocupado = random.randint(4, 8)
                for s in range(tempo_ocupado, 0, -1):
                    sys.stdout.write(f"\r   ⏳ Banheiro ocupado... destrancando em {s}s ")
                    sys.stdout.flush()
                    time.sleep(1.0)
                print()

                print(f"--- [Ciclo {c}/{ciclos}] Usuário aproximando Tag RFID para SAIR ---")
                self.passar_tag_rfid()  # Destranca a porta
                if c < ciclos:
                    time.sleep(2.5)
            print("\n✅ Simulação automática concluída!")
        except KeyboardInterrupt:
            print("\n⏹️  Simulação automática interrompida pelo usuário.")


def exibir_cabecalho(sim: SimuladorESP32PortaAcessivel):
    estado_txt = "🔴 FECHADA (OCUPADO)" if sim.porta_fechada else "🟢 ABERTA (LIVRE)"
    status_mqtt = f"🟢 Conectado ({sim.broker_host})" if sim.conectado_mqtt else f"⚪ Offline ({sim.broker_host})"
    print("\n" + "=" * 72)
    print("  🚻 SIMULADOR ESP32 — SISTEMA MULTISSENSORIAL DE PORTA (UNINASSAU)")
    print("=" * 72)
    print(f"  Estado Atual da Porta : {estado_txt}")
    print(f"  Servo SG90 (GPIO 13)  : {'90° (Trancado)' if sim.porta_fechada else '0° (Destrancado)'}")
    print(f"  Usos nesta Sessão     : {sim.usos_sessao}")
    print(f"  Status Broker MQTT    : {status_mqtt}")
    print("-" * 72)
    print("  Comandos disponíveis:")
    print("    [ENTER] -> 🏷️  Simular aproximação de Tag RFID (Trancar / Destrancar)")
    print("    [A]     -> 🤖 Rodar 3 ciclos automáticos (Testar gráficos da Dashboard)")
    print("    [E]     -> 🚨 Simular Emergência (Trancar e contar 32s para disparar alerta)")
    print("    [B]     -> 🌐 Trocar Broker (broker.hivemq.com <-> localhost Mosquitto)")
    print("    [Q]     -> ❌ Sair do simulador")
    print("=" * 72)


def main():
    sim = SimuladorESP32PortaAcessivel(broker_host=BROKER_PADRAO)
    sim.iniciar_conexao_mqtt()

    while True:
        exibir_cabecalho(sim)
        try:
            opcao = input("\n👉 Pressione [ENTER] para passar a Tag RFID ou digite uma opção: ").strip().upper()
        except (KeyboardInterrupt, EOFError):
            print("\nEncerrando simulador...")
            break

        if opcao == "":
            sim.passar_tag_rfid()
        elif opcao == "A":
            sim.executar_ciclo_automatico(ciclos=3)
        elif opcao == "E":
            print("\n🚨 [MODO TESTE DE EMERGÊNCIA]")
            if not sim.porta_fechada:
                sim.passar_tag_rfid(uid_tag="TAG:TESTE-EMERGENCIA")
            print("Aguardando 32 segundos com a porta trancada para disparar o alerta na Dashboard...")
            try:
                for seg in range(1, 33):
                    sys.stdout.write(f"\r   ⏱️  Tempo trancada: {seg:02d}s / 30s ")
                    sys.stdout.flush()
                    time.sleep(1.0)
                print("\n🚨 30 segundos atingidos! Verifique o Alerta de Emergência na Dashboard Web!")
            except KeyboardInterrupt:
                print("\nContagem interrompida.")
        elif opcao == "B":
            sim.broker_host = BROKER_LOCAL if sim.broker_host == BROKER_PADRAO else BROKER_PADRAO
            sim.iniciar_conexao_mqtt()
        elif opcao == "Q":
            print("\n👋 Encerrando Simulador ESP32. Bons testes e ótima apresentação na UNINASSAU!")
            break
        else:
            print("⚠️  Opção inválida. Pressione apenas [ENTER] para passar a Tag RFID.")


if __name__ == "__main__":
    main()
