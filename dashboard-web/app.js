/**
 * Sistema de Monitoramento e Sinalizacao de Porta (ESP32 + MQTT)
 * Sincronizacao em tempo real entre dashboard.html e porta.html via:
 * 1. MQTT over WebSockets
 * 2. BroadcastChannel API
 * 3. localStorage
 */

const STORAGE_STATS_KEY = 'porta_acessivel_stats_v2';
const STORAGE_LIVE_KEY = 'porta_acessivel_live_v2';
const BROADCAST_CHANNEL_NAME = 'porta_acessivel_sync_channel';

const TOPIC_ESTADO = 'uninassau/acessibilidade/banheiro01/estado';
const TOPIC_COMANDO = 'uninassau/acessibilidade/banheiro01/comando';
const DEBOUNCE_MS = 2000;

const HORARIOS_LABELS = [
  '06h', '07h', '08h', '09h', '10h', '11h', '12h', '13h', '14h',
  '15h', '16h', '17h', '18h', '19h', '20h', '21h', '22h'
];

const pageType = document.body.getAttribute('data-page') || 'dashboard';

let broadcastChannel = null;
try {
  if ('BroadcastChannel' in window) {
    broadcastChannel = new BroadcastChannel(BROADCAST_CHANNEL_NAME);
  }
} catch (e) {
  broadcastChannel = null;
}

const state = {
  portaFechada: false,
  inicioOcupacaoMs: null,
  ultimoAcionamentoMs: 0,
  buzzerAtivado: true,
  testeRapido30s: true,
  limiteMinutos: 15,
  brokerUrl: 'wss://broker.hivemq.com:8884/mqtt',
  emEmergencia: false,
  alertaContabilizadoNoCiclo: false,
  ultimoEventoId: 0,
  mqttClient: null,
  mqttConectado: false,
  chartInstance: null,
  historico: null
};

// ============================================================================
// 1. UTILITARIOS DE DATA E FORMATACAO
// ============================================================================

function obterDataIsoHoje() {
  const d = new Date();
  const ano = d.getFullYear();
  const mes = String(d.getMonth() + 1).padStart(2, '0');
  const dia = String(d.getDate()).padStart(2, '0');
  return `${ano}-${mes}-${dia}`;
}

function formatarDataBr(iso) {
  if (!iso || typeof iso !== 'string') return '';
  const p = iso.split('-');
  if (p.length !== 3) return iso;
  return `${p[2]}/${p[1]}/${p[0]}`;
}

function formatarDuracao(segundosTotais) {
  const s = Math.max(0, Math.round(Number(segundosTotais) || 0));
  const min = String(Math.floor(s / 60)).padStart(2, '0');
  const seg = String(s % 60).padStart(2, '0');
  return `${min}m ${seg}s`;
}

function formatarCronometro(segundosTotais) {
  const s = Math.max(0, Math.floor(Number(segundosTotais) || 0));
  const min = String(Math.floor(s / 60)).padStart(2, '0');
  const seg = String(s % 60).padStart(2, '0');
  return `${min}:${seg}`;
}

function obterHoraLabelAtual() {
  const h = new Date().getHours();
  const label = `${String(h).padStart(2, '0')}h`;
  return HORARIOS_LABELS.includes(label) ? label : '12h';
}

// ============================================================================
// 2. ARMAZENAMENTO DE ESTATISTICAS E ESTADO COMPARTILHADO
// ============================================================================

function criarDiaVazio(iso) {
  const porHora = {};
  HORARIOS_LABELS.forEach((h) => {
    porHora[h] = 0;
  });
  return {
    data: iso,
    usos: 0,
    somaDuracaoSeg: 0,
    ultimaDuracaoSeg: 0,
    alertas: 0,
    porHora
  };
}

function carregarEstatisticas() {
  const hojeIso = obterDataIsoHoje();
  try {
    const raw = localStorage.getItem(STORAGE_STATS_KEY);
    if (raw) {
      const parsed = JSON.parse(raw);
      if (!parsed.dias) parsed.dias = {};
      if (!parsed.dias[hojeIso]) {
        parsed.dias[hojeIso] = criarDiaVazio(hojeIso);
      }
      state.historico = parsed;
      return;
    }
  } catch (e) {}

  state.historico = { dias: { [hojeIso]: criarDiaVazio(hojeIso) } };
  salvarEstatisticas();
}

function salvarEstatisticas() {
  try {
    localStorage.setItem(STORAGE_STATS_KEY, JSON.stringify(state.historico));
  } catch (e) {}
}

function obterDiaHoje() {
  const hojeIso = obterDataIsoHoje();
  if (!state.historico.dias[hojeIso]) {
    state.historico.dias[hojeIso] = criarDiaVazio(hojeIso);
  }
  return state.historico.dias[hojeIso];
}

function salvarEstadoAoVivo() {
  const payload = {
    portaFechada: state.portaFechada,
    inicioOcupacaoMs: state.inicioOcupacaoMs,
    buzzerAtivado: state.buzzerAtivado,
    testeRapido30s: state.testeRapido30s,
    limiteMinutos: state.limiteMinutos,
    brokerUrl: state.brokerUrl,
    emEmergencia: state.emEmergencia,
    eventoId: state.ultimoEventoId,
    updatedAt: Date.now()
  };

  try {
    localStorage.setItem(STORAGE_LIVE_KEY, JSON.stringify(payload));
  } catch (e) {}

  if (broadcastChannel) {
    try {
      broadcastChannel.postMessage({ type: 'SYNC_STATE', payload });
    } catch (e) {}
  }
}

function lerEstadoAoVivo() {
  try {
    const raw = localStorage.getItem(STORAGE_LIVE_KEY);
    if (!raw) return null;
    return JSON.parse(raw);
  } catch (e) {
    return null;
  }
}

// ============================================================================
// 3. SOM DO BUZZER (WEB AUDIO API)
// ============================================================================

let audioCtx = null;

function emitirBipeUnico(duracaoMs, frequenciaHz = 880) {
  if (!state.buzzerAtivado) return;
  try {
    const AudioContextClass = window.AudioContext || window.webkitAudioContext;
    if (!AudioContextClass) return;
    if (!audioCtx) audioCtx = new AudioContextClass();
    if (audioCtx.state === 'suspended') {
      audioCtx.resume();
    }

    const osc = audioCtx.createOscillator();
    const gain = audioCtx.createGain();
    osc.type = 'square';
    osc.frequency.setValueAtTime(frequenciaHz, audioCtx.currentTime);

    gain.gain.setValueAtTime(0.08, audioCtx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.001, audioCtx.currentTime + duracaoMs / 1000);

    osc.connect(gain);
    gain.connect(audioCtx.destination);

    osc.start();
    osc.stop(audioCtx.currentTime + duracaoMs / 1000);
  } catch (e) {}
}

/**
 * Toca o padrao sonoro do Buzzer Ativo 5V conforme a especificacao do hardware:
 * - Ao trancar (FECHADA): 1 bipe longo de 500ms
 * - Ao destrancar (ABERTA): 2 bipes rapidos de 150ms (com intervalo de 150ms)
 */
function tocarPadraoBuzzer(portaFechada) {
  if (!state.buzzerAtivado) return;

  if (portaFechada) {
    emitirBipeUnico(500, 780);
  } else {
    emitirBipeUnico(150, 1046);
    setTimeout(() => {
      emitirBipeUnico(150, 1046);
    }, 300);
  }
}

function tocarBuzzerEmergencia() {
  if (!state.buzzerAtivado) return;
  emitirBipeUnico(300, 520);
  setTimeout(() => emitirBipeUnico(300, 520), 400);
}

// ============================================================================
// 4. MUDANCA DE ESTADO E CONTABILIZACAO
// ============================================================================

function obterLimiteEmergenciaSegundos() {
  if (state.testeRapido30s) return 30;
  return Math.max(1, Number(state.limiteMinutos) || 15) * 60;
}

function processarTransicaoPorta(novoFechada, duracaoExternaSeg = null, registrarEstatisticas = true) {
  const mudou = state.portaFechada !== novoFechada;
  state.portaFechada = novoFechada;

  if (novoFechada) {
    if (mudou || !state.inicioOcupacaoMs) {
      state.inicioOcupacaoMs = Date.now();
      state.emEmergencia = false;
      state.alertaContabilizadoNoCiclo = false;
    }
  } else {
    let duracao = duracaoExternaSeg;
    if ((duracao === null || duracao <= 0) && state.inicioOcupacaoMs) {
      duracao = Math.max(1, Math.round((Date.now() - state.inicioOcupacaoMs) / 1000));
    }
    state.inicioOcupacaoMs = null;
    state.emEmergencia = false;
    state.alertaContabilizadoNoCiclo = false;

    if (mudou && registrarEstatisticas) {
      const duracaoFinal = duracao && duracao > 0 ? duracao : 1;
      const diaHoje = obterDiaHoje();
      diaHoje.usos += 1;
      diaHoje.somaDuracaoSeg += duracaoFinal;
      diaHoje.ultimaDuracaoSeg = duracaoFinal;
      const horaLabel = obterHoraLabelAtual();
      diaHoje.porHora[horaLabel] = (diaHoje.porHora[horaLabel] || 0) + 1;
      salvarEstatisticas();
    }
  }

  if (mudou) {
    state.ultimoEventoId = Date.now();
    tocarPadraoBuzzer(novoFechada);
  }

  atualizarInterfaceCompleta();
}

// ============================================================================
// 5. RENDERIZACAO DA PAGINA DA PORTA (porta.html)
// ============================================================================

function renderizarPaginaPorta() {
  const viewport = document.getElementById('portaViewport');
  const svgOpen = document.getElementById('svgLockOpen');
  const svgClosed = document.getElementById('svgLockClosed');
  const primaryText = document.getElementById('portaPrimaryText');
  const secondaryText = document.getElementById('portaSecondaryText');
  const emergencyStrip = document.getElementById('portaEmergencyStrip');

  if (!viewport) return;

  if (state.portaFechada) {
    viewport.classList.remove('state-open');
    viewport.classList.add('state-closed');
    svgOpen.classList.add('hidden');
    svgClosed.classList.remove('hidden');

    primaryText.textContent = 'OCUPADO';
    secondaryText.textContent = 'PORTA FECHADA';

    if (state.emEmergencia) {
      viewport.classList.add('state-emergency');
      emergencyStrip.classList.remove('hidden');
    } else {
      viewport.classList.remove('state-emergency');
      emergencyStrip.classList.add('hidden');
    }
  } else {
    viewport.classList.remove('state-closed', 'state-emergency');
    viewport.classList.add('state-open');
    svgClosed.classList.add('hidden');
    svgOpen.classList.remove('hidden');
    emergencyStrip.classList.add('hidden');

    primaryText.textContent = 'LIVRE';
    secondaryText.textContent = 'PORTA ABERTA';
  }
}

// ============================================================================
// 6. RENDERIZACAO DO PAINEL DE MONITORAMENTO (dashboard.html)
// ============================================================================

function renderizarPaginaDashboard() {
  const kpiDoorState = document.getElementById('kpiDoorState');
  if (!kpiDoorState) return;

  const diaHoje = obterDiaHoje();
  const hojeIso = obterDataIsoHoje();

  // Estado da Porta
  kpiDoorState.textContent = state.portaFechada ? 'FECHADA' : 'ABERTA';
  kpiDoorState.className = 'kpi-value ' + (state.portaFechada ? 'state-closed-text' : 'state-open-text');

  // Usos no Dia
  document.getElementById('kpiUsesToday').textContent = String(diaHoje.usos);
  document.getElementById('kpiDateToday').textContent = formatarDataBr(hojeIso);

  // Tempo Medio
  const mediaSeg = diaHoje.usos > 0 ? Math.round(diaHoje.somaDuracaoSeg / diaHoje.usos) : 0;
  document.getElementById('kpiAvgTime').textContent = formatarDuracao(mediaSeg);
  document.getElementById('kpiLastDuration').textContent =
    diaHoje.ultimaDuracaoSeg > 0
      ? `Ultimo uso: ${formatarDuracao(diaHoje.ultimaDuracaoSeg)}`
      : 'Ultimo uso: --';

  // Status de Seguranca
  const kpiSafety = document.getElementById('kpiSafetyStatus');
  const dashEmergencyBox = document.getElementById('dashEmergencyBox');
  if (state.emEmergencia && state.portaFechada) {
    kpiSafety.textContent = 'EMERGENCIA';
    kpiSafety.className = 'kpi-value state-closed-text';
    dashEmergencyBox.classList.remove('hidden');
  } else {
    kpiSafety.textContent = 'NORMAL';
    kpiSafety.className = 'kpi-value state-open-text';
    dashEmergencyBox.classList.add('hidden');
  }
  document.getElementById('kpiAlertsCount').textContent = `${diaHoje.alertas} alerta(s) hoje`;

  // Sincroniza controles com o estado
  const chkBuzzer = document.getElementById('chkBuzzerEnabled');
  const chkTest30s = document.getElementById('chkTest30s');
  const inputMin = document.getElementById('inputMaxMinutes');
  if (chkBuzzer) chkBuzzer.checked = state.buzzerAtivado;
  if (chkTest30s) chkTest30s.checked = state.testeRapido30s;
  if (inputMin && document.activeElement !== inputMin) {
    inputMin.value = String(state.limiteMinutos);
  }

  atualizarTabelaHistorico();
  atualizarGraficoHorarios();
}

function atualizarTabelaHistorico() {
  const select = document.getElementById('historyDaySelect');
  const tbody = document.getElementById('historyTableBody');
  if (!select || !tbody) return;

  const selecionadoAntes = select.value || 'all';
  const datas = Object.keys(state.historico.dias).sort().reverse();

  select.innerHTML = '<option value="all">Todos os dias</option>';
  datas.forEach((iso) => {
    const opt = document.createElement('option');
    opt.value = iso;
    opt.textContent = iso === obterDataIsoHoje() ? `${formatarDataBr(iso)} (Hoje)` : formatarDataBr(iso);
    select.appendChild(opt);
  });

  if (datas.includes(selecionadoAntes) || selecionadoAntes === 'all') {
    select.value = selecionadoAntes;
  }

  tbody.innerHTML = '';
  const filtro = select.value === 'all' ? datas : [select.value];

  filtro.forEach((iso) => {
    const d = state.historico.dias[iso];
    if (!d) return;
    const media = d.usos > 0 ? Math.round(d.somaDuracaoSeg / d.usos) : 0;
    const tr = document.createElement('tr');
    tr.innerHTML = `
      <td>${formatarDataBr(iso)}</td>
      <td>${d.usos}</td>
      <td>${formatarDuracao(media)}</td>
      <td>${d.alertas || 0}</td>
    `;
    tbody.appendChild(tr);
  });
}

function atualizarGraficoHorarios() {
  const canvas = document.getElementById('hourlyUsageChart');
  if (!canvas || typeof window.Chart === 'undefined') return;

  const diaHoje = obterDiaHoje();
  const valores = HORARIOS_LABELS.map((h) => diaHoje.porHora[h] || 0);

  const chartBox = canvas.closest('.data-box');
  if (chartBox) {
    chartBox.classList.remove('chart-updated');
    void chartBox.offsetWidth;
    chartBox.classList.add('chart-updated');
  }

  if (!state.chartInstance) {
    state.chartInstance = new window.Chart(canvas, {
      type: 'bar',
      data: {
        labels: HORARIOS_LABELS,
        datasets: [
          {
            label: 'Usos no Horario',
            data: valores,
            backgroundColor: '#2563eb',
            hoverBackgroundColor: '#1d4ed8',
            borderRadius: 6,
            borderSkipped: false
          }
        ]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        animation: {
          duration: 650,
          easing: 'easeOutQuart'
        },
        plugins: {
          legend: { display: false }
        },
        scales: {
          x: {
            ticks: { color: '#475569', font: { weight: '600' } },
            grid: { display: false }
          },
          y: {
            beginAtZero: true,
            ticks: { color: '#475569', stepSize: 1, font: { weight: '600' } },
            grid: { color: '#f1f5f9' }
          }
        }
      }
    });
  } else {
    state.chartInstance.data.datasets[0].data = valores;
    state.chartInstance.update();
  }
}

function atualizarInterfaceCompleta() {
  if (pageType === 'porta') {
    renderizarPaginaPorta();
  } else {
    renderizarPaginaDashboard();
  }
}

// ============================================================================
// 7. CRONOMETRO DE 1 SEGUNDO E VERIFICACAO DE EMERGENCIA
// ============================================================================

function tickCronometro() {
  const limiteSeg = obterLimiteEmergenciaSegundos();
  let decorridoSeg = 0;

  if (state.portaFechada && state.inicioOcupacaoMs) {
    decorridoSeg = Math.max(0, Math.floor((Date.now() - state.inicioOcupacaoMs) / 1000));
    if (decorridoSeg >= limiteSeg) {
      if (!state.emEmergencia) {
        state.emEmergencia = true;
        if (pageType === 'dashboard' && !state.alertaContabilizadoNoCiclo) {
          state.alertaContabilizadoNoCiclo = true;
          const diaHoje = obterDiaHoje();
          diaHoje.alertas += 1;
          salvarEstatisticas();
          salvarEstadoAoVivo();
        }
        tocarBuzzerEmergencia();
        atualizarInterfaceCompleta();
      }
    }
  }

  if (pageType === 'dashboard') {
    const lblCron = document.getElementById('kpiCronometroLabel');
    if (lblCron) {
      lblCron.textContent = state.portaFechada
        ? `Tempo trancada: ${formatarCronometro(decorridoSeg)}`
        : 'Cronometro: 00:00';
    }
    if (state.emEmergencia && state.portaFechada) {
      const txtEmerg = document.getElementById('dashEmergencyText');
      if (txtEmerg) {
        txtEmerg.textContent = `Tempo trancada: ${formatarCronometro(decorridoSeg)} (Limite configurado: ${limiteSeg}s).`;
      }
    }
  } else if (pageType === 'porta') {
    renderizarPaginaPorta();
  }
}

// ============================================================================
// 8. CONEXAO MQTT E SINCRONIZACAO ENTRE ABAS
// ============================================================================

function atualizarBadgeMqtt(conectado, modoLocal = false) {
  const badge = document.getElementById('mqttStatusBadge');
  if (!badge) return;

  if (modoLocal) {
    badge.textContent = 'Local';
    badge.className = 'mqtt-badge disconnected';
  } else if (conectado) {
    badge.textContent = 'Conectado';
    badge.className = 'mqtt-badge connected';
  } else {
    badge.textContent = 'Desconectado';
    badge.className = 'mqtt-badge disconnected';
  }
}

function conectarMqtt(urlBroker) {
  state.brokerUrl = urlBroker;

  if (state.mqttClient) {
    try {
      state.mqttClient.end(true);
    } catch (e) {}
    state.mqttClient = null;
  }

  if (urlBroker === 'local' || typeof window.mqtt === 'undefined') {
    state.mqttConectado = false;
    atualizarBadgeMqtt(false, true);
    return;
  }

  atualizarBadgeMqtt(false, false);

  const clientId = `PortaAcessivel_${pageType}_${Math.random().toString(16).substring(2, 9)}`;
  const client = window.mqtt.connect(urlBroker, {
    clientId,
    clean: true,
    connectTimeout: 5000,
    reconnectPeriod: 4000
  });

  state.mqttClient = client;

  client.on('connect', () => {
    state.mqttConectado = true;
    atualizarBadgeMqtt(true, false);
    client.subscribe(TOPIC_ESTADO, { qos: 1 });
  });

  client.on('message', (topic, messageBuffer) => {
    if (topic !== TOPIC_ESTADO) return;
    const raw = messageBuffer.toString();
    try {
      const dados = JSON.parse(raw);
      const fechada = dados.estado === 'FECHADA' || dados.ocupado === true;
      const duracao = Number(dados.duracao_ultimo_uso_seg) || null;
      processarTransicaoPorta(fechada, duracao, pageType === 'dashboard');
      if (pageType === 'dashboard') {
        salvarEstadoAoVivo();
      }
    } catch (e) {
      const txt = raw.trim().toUpperCase();
      if (txt === 'FECHADA' || txt === 'ABERTA') {
        processarTransicaoPorta(txt === 'FECHADA', null, pageType === 'dashboard');
      }
    }
  });

  client.on('offline', () => {
    state.mqttConectado = false;
    atualizarBadgeMqtt(false, false);
  });

  client.on('error', () => {
    state.mqttConectado = false;
    atualizarBadgeMqtt(false, false);
  });
}

function aplicarEstadoRecebidoDeOutraAba(live) {
  if (!live) return;
  const mudouEstadoPorta = state.portaFechada !== Boolean(live.portaFechada);

  state.portaFechada = Boolean(live.portaFechada);
  state.inicioOcupacaoMs = live.inicioOcupacaoMs || null;
  state.buzzerAtivado = live.buzzerAtivado !== undefined ? Boolean(live.buzzerAtivado) : true;
  state.testeRapido30s = live.testeRapido30s !== undefined ? Boolean(live.testeRapido30s) : true;
  state.limiteMinutos = Number(live.limiteMinutos) || 15;
  state.emEmergencia = Boolean(live.emEmergencia);

  if (mudouEstadoPorta && live.eventoId !== state.ultimoEventoId) {
    state.ultimoEventoId = live.eventoId || Date.now();
    tocarPadraoBuzzer(state.portaFechada);
  }

  carregarEstatisticas();
  atualizarInterfaceCompleta();
}

// ============================================================================
// 9. ACOES DO PAINEL DE MONITORAMENTO (SIMULAR, DADOS, RESET)
// ============================================================================

function acionarSimulacaoTagRfid(ignorarDebounce = false) {
  const agora = Date.now();
  if (!ignorarDebounce && agora - state.ultimoAcionamentoMs < DEBOUNCE_MS) {
    return;
  }
  state.ultimoAcionamentoMs = agora;

  const btnSim = document.getElementById('btnSimulateRfid');
  if (btnSim && !ignorarDebounce) {
    btnSim.disabled = true;
    setTimeout(() => {
      btnSim.disabled = false;
    }, DEBOUNCE_MS);
  }

  const novoFechada = !state.portaFechada;
  let duracaoSeg = 0;
  if (!novoFechada && state.inicioOcupacaoMs) {
    duracaoSeg = Math.max(1, Math.round((agora - state.inicioOcupacaoMs) / 1000));
  }

  processarTransicaoPorta(novoFechada, duracaoSeg, true);
  salvarEstadoAoVivo();

  if (state.mqttClient && state.mqttConectado) {
    const payload = {
      estado: novoFechada ? 'FECHADA' : 'ABERTA',
      ocupado: novoFechada,
      uid_tag: 'SIMULADO',
      duracao_ultimo_uso_seg: duracaoSeg,
      origem: 'PAINEL_WEB',
      timestamp: Math.floor(agora / 1000)
    };
    state.mqttClient.publish(TOPIC_ESTADO, JSON.stringify(payload), { qos: 1, retain: true });
    state.mqttClient.publish(TOPIC_COMANDO, 'TOGGLE', { qos: 1 });
  }
}

function carregarDadosExemplo() {
  const hoje = new Date();
  const dias = {};
  const passados = [
    { offset: 4, usos: 28, media: 140, alertas: 0 },
    { offset: 3, usos: 35, media: 155, alertas: 1 },
    { offset: 2, usos: 22, media: 125, alertas: 0 },
    { offset: 1, usos: 41, media: 162, alertas: 0 }
  ];

  passados.forEach((item) => {
    const d = new Date(hoje);
    d.setDate(hoje.getDate() - item.offset);
    const iso = `${d.getFullYear()}-${String(d.getMonth() + 1).padStart(2, '0')}-${String(d.getDate()).padStart(2, '0')}`;
    const reg = criarDiaVazio(iso);
    reg.usos = item.usos;
    reg.somaDuracaoSeg = item.usos * item.media;
    reg.ultimaDuracaoSeg = item.media;
    reg.alertas = item.alertas;
    dias[iso] = reg;
  });

  const hojeIso = obterDataIsoHoje();
  const regHoje = criarDiaVazio(hojeIso);
  const dist = {
    '08h': 2, '09h': 4, '10h': 5, '11h': 3, '12h': 6,
    '14h': 4, '15h': 3, '18h': 5, '19h': 7, '20h': 4
  };
  let totalHoje = 0;
  Object.entries(dist).forEach(([h, v]) => {
    regHoje.porHora[h] = v;
    totalHoje += v;
  });
  regHoje.usos = totalHoje;
  regHoje.somaDuracaoSeg = totalHoje * 135;
  regHoje.ultimaDuracaoSeg = 120;
  dias[hojeIso] = regHoje;

  state.historico = { dias };
  salvarEstatisticas();
  atualizarInterfaceCompleta();
}

function zerarTodosOsDados() {
  const hojeIso = obterDataIsoHoje();
  state.historico = { dias: { [hojeIso]: criarDiaVazio(hojeIso) } };
  salvarEstatisticas();
  atualizarInterfaceCompleta();
}

// ============================================================================
// 10. INICIALIZACAO
// ============================================================================

window.addEventListener('DOMContentLoaded', () => {
  carregarEstatisticas();

  const liveSalvo = lerEstadoAoVivo();
  if (liveSalvo) {
    state.portaFechada = Boolean(liveSalvo.portaFechada);
    state.inicioOcupacaoMs = liveSalvo.inicioOcupacaoMs || null;
    state.buzzerAtivado = liveSalvo.buzzerAtivado !== undefined ? Boolean(liveSalvo.buzzerAtivado) : true;
    state.testeRapido30s = liveSalvo.testeRapido30s !== undefined ? Boolean(liveSalvo.testeRapido30s) : true;
    state.limiteMinutos = Number(liveSalvo.limiteMinutos) || 15;
    state.brokerUrl = liveSalvo.brokerUrl || state.brokerUrl;
    state.emEmergencia = Boolean(liveSalvo.emEmergencia);
    state.ultimoEventoId = liveSalvo.eventoId || 0;
  } else {
    salvarEstadoAoVivo();
  }

  atualizarInterfaceCompleta();
  conectarMqtt(state.brokerUrl);

  setInterval(tickCronometro, 1000);

  if (broadcastChannel) {
    broadcastChannel.onmessage = (event) => {
      if (event.data && event.data.type === 'SYNC_STATE') {
        aplicarEstadoRecebidoDeOutraAba(event.data.payload);
      }
    };
  }

  window.addEventListener('storage', (event) => {
    if (event.key === STORAGE_LIVE_KEY && event.newValue) {
      try {
        aplicarEstadoRecebidoDeOutraAba(JSON.parse(event.newValue));
      } catch (e) {}
    } else if (event.key === STORAGE_STATS_KEY) {
      carregarEstatisticas();
      atualizarInterfaceCompleta();
    }
  });

  setInterval(() => {
    const atual = lerEstadoAoVivo();
    if (atual && (atual.portaFechada !== state.portaFechada || atual.emEmergencia !== state.emEmergencia)) {
      aplicarEstadoRecebidoDeOutraAba(atual);
    }
  }, 350);

  if (pageType === 'dashboard') {
    document.getElementById('btnSimulateRfid').addEventListener('click', () => {
      acionarSimulacaoTagRfid(false);
    });

    document.getElementById('btnEmergencyUnlock').addEventListener('click', () => {
      if (state.portaFechada) {
        acionarSimulacaoTagRfid(true);
      }
    });

    document.getElementById('btnLoadDemoData').addEventListener('click', () => {
      carregarDadosExemplo();
    });

    document.getElementById('btnResetData').addEventListener('click', () => {
      zerarTodosOsDados();
    });

    document.getElementById('chkBuzzerEnabled').addEventListener('change', (e) => {
      state.buzzerAtivado = e.target.checked;
      salvarEstadoAoVivo();
    });

    document.getElementById('chkTest30s').addEventListener('change', (e) => {
      state.testeRapido30s = e.target.checked;
      salvarEstadoAoVivo();
      tickCronometro();
    });

    document.getElementById('inputMaxMinutes').addEventListener('change', (e) => {
      state.limiteMinutos = Math.max(1, Number(e.target.value) || 15);
      salvarEstadoAoVivo();
      tickCronometro();
    });

    const selectBroker = document.getElementById('mqttBrokerSelect');
    selectBroker.value = state.brokerUrl;
    selectBroker.addEventListener('change', (e) => {
      conectarMqtt(e.target.value);
      salvarEstadoAoVivo();
    });

    document.getElementById('historyDaySelect').addEventListener('change', () => {
      atualizarTabelaHistorico();
    });
  }
});
