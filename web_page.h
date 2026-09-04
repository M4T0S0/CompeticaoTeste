#pragma once
#include <Arduino.h>

// =====================================================================
// WEB_PAGE.H — Página única do juiz (HTML/CSS/JS puro, sem build)
//
// Porta de web/src/pages/*.jsx + components/ParticipantsPanel.jsx para
// uma única página vanilla, servida pelo próprio ESP32 (ver
// web_server.cpp: GET "/"). Sem SSE (o ESP32 não teria como manter
// muitas conexões abertas) — a página faz polling a cada 1.5s enquanto
// estiver na tela de operação da competição, chamando os mesmos GETs
// que os eventos SSE disparavam na versão com servidor.
//
// A lógica de derivação de fase (select/locked/waiting_start/running/
// validate/day_complete/finished) replica EXATAMENTE a ordem de
// prioridade de CompeticaoDetalhe.jsx — ver comentário em derivePhase().
//
// Deliberadamente sem CDN/fonte externa: o dispositivo precisa funcionar
// só com o próprio Access Point, sem internet — tudo (fonte, ícone,
// estilo) é embutido neste arquivo.
// =====================================================================

const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>RaceTrack UTFPR</title>
<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'%3E%3Ctext y='.9em' font-size='90'%3E%E2%8F%B1%EF%B8%8F%3C/text%3E%3C/svg%3E">
<style>
:root {
  --bg:#07080c; --bg-elev:#0c0f16; --panel:#12151f; --panel-hover:#171b28;
  --border:#1f2431; --border-strong:#2d3446;
  --text:#f2f4f9; --text-dim:#99a2b8; --text-faint:#5b6478;
  --primary:#ffc700; --primary-strong:#e6b400; --primary-soft:rgba(255,199,0,.15);
  --success:#34d399; --success-soft:rgba(52,211,153,.14);
  --danger:#ff5c7a; --danger-soft:rgba(255,92,122,.14);
  --warning:#ffb545; --warning-soft:rgba(255,181,69,.14);
  --gold:#ffd166; --silver:#ccd6e6; --bronze:#e2a97a;
  --radius:18px; --radius-sm:12px;
  --shadow:0 14px 34px -16px rgba(0,0,0,.7);
  --shadow-sm:0 8px 18px -10px rgba(0,0,0,.55);
}
* { box-sizing: border-box; }
html { -webkit-tap-highlight-color: transparent; }
body {
  margin:0; min-height:100vh; display:flex; flex-direction:column;
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Inter, Roboto, "Helvetica Neue", Arial, sans-serif;
  -webkit-font-smoothing:antialiased; -moz-osx-font-smoothing:grayscale;
  letter-spacing:-.005em;
  background:
    radial-gradient(1100px 560px at 12% -12%, rgba(109,139,255,.11), transparent 60%),
    radial-gradient(900px 480px at 100% 0%, rgba(52,211,153,.05), transparent 55%),
    var(--bg);
  color:var(--text);
}
a { color:var(--primary); text-decoration:none; }
a:hover { text-decoration:underline; }

.topbar {
  position:sticky; top:0; z-index:10;
  display:flex; align-items:center; gap:10px;
  padding:16px 20px; background:rgba(7,8,12,.72); backdrop-filter:blur(14px) saturate(160%);
  border-bottom:1px solid var(--border);
}
.topbar .logo { font-size:1.5rem; line-height:1; }
.topbar .brand { font-weight:700; font-size:1rem; letter-spacing:.01em; }
.topbar .brand small { display:block; font-weight:400; font-size:.72rem; color:var(--text-faint); margin-top:1px; }
/* Marca clicável, volta pro início - sem sublinhado no hover porque
   cobre logo+texto juntos, não é um link de frase comum. */
.topbar-home { display:flex; align-items:center; gap:10px; color:inherit; transition:opacity .15s; }
.topbar-home:hover { text-decoration:none; opacity:.8; }

.tabs-bar {
  display:flex; gap:2px; margin-bottom:16px; background:var(--bg-elev);
  padding:4px; border-radius:999px; border:1px solid var(--border);
}
.tab-btn {
  flex:1; border:none; background:transparent; color:var(--text-dim);
  border-radius:999px; padding:11px 14px; font-size:.85rem; font-weight:700; cursor:pointer;
  transition:background .15s, color .15s;
}
.tab-btn:hover { color:var(--text); }
.tab-btn.tab-active { background:linear-gradient(180deg, var(--primary), var(--primary-strong)); color:#111; box-shadow:0 6px 18px -8px rgba(230,180,0,.5); }

main#app { flex:1; width:100%; max-width:720px; margin:0 auto; padding:24px 16px 36px; }

footer.app-footer {
  text-align:center; padding:24px 16px 30px; font-size:.78rem; color:var(--text-faint);
  border-top:1px solid var(--border);
}
footer.app-footer .sep { margin:0 8px; opacity:.5; }
footer.app-footer strong { color:var(--text-dim); font-weight:600; }

h1 {
  font-size:1.6rem; font-weight:800; margin:2px 0 20px; letter-spacing:-.02em;
  background:linear-gradient(180deg, var(--text), var(--text-dim));
  -webkit-background-clip:text; background-clip:text; color:transparent;
}
h2 { font-size:.76rem; margin:0 0 16px; color:var(--text-dim); text-transform:uppercase; letter-spacing:.07em; font-weight:800; display:flex; align-items:center; gap:8px; }
.muted { color:var(--text-dim); }

.panel {
  position:relative; overflow:hidden;
  background:linear-gradient(180deg, var(--panel), var(--bg-elev));
  border:1px solid var(--border); border-radius:var(--radius);
  padding:22px; margin-bottom:16px; box-shadow:var(--shadow);
}
/* Filete sutil no topo do card, efeito "vidro" discreto. */
.panel::before {
  content:''; position:absolute; top:0; left:0; right:0; height:1px;
  background:linear-gradient(90deg, transparent, rgba(255,255,255,.10), transparent);
}

.tiles { display:flex; flex-direction:column; gap:12px; margin-top:6px; }
.tile {
  display:flex; align-items:center; gap:14px; padding:20px;
  background:linear-gradient(180deg, var(--panel), var(--bg-elev));
  border:1px solid var(--border); border-radius:var(--radius);
  color:var(--text); font-weight:600; box-shadow:var(--shadow-sm);
  transition:border-color .15s, transform .15s, box-shadow .15s;
}
.tile:hover { text-decoration:none; border-color:var(--border-strong); transform:translateY(-2px); box-shadow:var(--shadow); }
.tile .tile-icon {
  width:44px; height:44px; border-radius:13px; display:flex; align-items:center; justify-content:center;
  background:var(--bg); color:var(--primary); font-size:1.3rem; flex-shrink:0;
  border:1px solid var(--border-strong);
}
.tile:hover .tile-icon { background:var(--primary); color:#111; }
.tile .tile-sub { font-weight:400; font-size:.8rem; color:var(--text-dim); margin-top:2px; }

.btn-primary, .btn-danger, .btn-ghost {
  border:none; border-radius:999px; padding:15px 22px; font-size:.98rem; font-weight:700;
  letter-spacing:-.005em; cursor:pointer; width:100%; margin-top:10px;
  transition:filter .15s, transform .05s, box-shadow .15s;
}
.btn-primary, .btn-danger { color:#fff; }
.btn-primary { background:linear-gradient(180deg, var(--primary), var(--primary-strong)); color:#111; box-shadow:0 10px 26px -10px rgba(230,180,0,.5); }
.btn-danger { background:linear-gradient(180deg, #ff7d95, var(--danger)); box-shadow:0 10px 26px -10px rgba(255,92,122,.6); }
.btn-ghost { background:var(--panel-hover); color:var(--text); border:1px solid var(--border-strong); }
.btn-primary:hover, .btn-danger:hover, .btn-ghost:hover { filter:brightness(1.1); }
.btn-primary:active, .btn-danger:active, .btn-ghost:active { transform:scale(.98); }
.btn-primary:disabled, .btn-danger:disabled { opacity:.5; }

/* Feedback visual enquanto uma ação (validar/armar/liberar/etc) está em
   andamento — sem isso, um clique que demora um pouco (rede lenta, ESP32
   ocupado) parece não ter feito nada, e a tentação é clicar de novo (que
   o "busy" em JS silenciosamente ignora, reforçando a impressão de bug). */
body.busy .btn-primary, body.busy .btn-danger, body.busy .btn-ghost, body.busy .participant-btn {
  opacity:.5; pointer-events:none;
}

.participant-list { display:flex; flex-wrap:wrap; gap:10px; }
.participant-btn {
  display:flex; align-items:center; gap:9px;
  background:var(--panel-hover); color:var(--text); border:1px solid var(--border);
  border-radius:999px; padding:8px 16px 8px 8px; cursor:pointer; font-size:.92rem;
  transition:border-color .15s, transform .05s, background .15s;
}
.participant-btn:hover { border-color:var(--primary); background:var(--primary-soft); }
.participant-btn:active { transform:scale(.97); }
.avatar {
  width:27px; height:27px; border-radius:50%; flex-shrink:0;
  display:flex; align-items:center; justify-content:center;
  background:linear-gradient(135deg, rgba(109,139,255,.28), rgba(109,139,255,.08));
  color:var(--primary); font-size:.7rem; font-weight:800;
  border:1px solid rgba(109,139,255,.3);
}
.participant-btn .p-meta { color:var(--text-faint); font-size:.8rem; }

.time-display {
  font-size:3.4rem; font-weight:800; font-variant-numeric:tabular-nums; letter-spacing:-.03em;
  text-align:center; margin:18px 0; color:var(--text);
  text-shadow:0 0 46px rgba(109,139,255,.28);
}

table { width:100%; border-collapse:collapse; }
td { padding:12px 8px; border-bottom:1px solid var(--border); }
td:last-child { text-align:right; font-variant-numeric:tabular-nums; font-weight:700; }
tr:last-child td { border-bottom:none; }
.pos { font-weight:800; }
.pos-1 { color:var(--gold); } .pos-2 { color:var(--silver); } .pos-3 { color:var(--bronze); }

.runs-list { list-style:none; padding:0; margin:0; }
.runs-list li { display:flex; justify-content:space-between; align-items:center; padding:12px 0; border-bottom:1px solid var(--border); gap:10px; }
.runs-list li:last-child { border-bottom:none; }
.runs-list .r-time { font-variant-numeric:tabular-nums; font-weight:700; white-space:nowrap; }

.badge { font-size:.7rem; padding:5px 12px; border-radius:999px; background:var(--panel-hover); color:var(--text-dim); white-space:nowrap; font-weight:700; letter-spacing:.01em; }
.badge.live { color:var(--success); background:var(--success-soft); display:inline-flex; align-items:center; gap:6px; }
.live-dot { width:6px; height:6px; border-radius:50%; background:var(--success); animation:pulse 1.6s ease-in-out infinite; box-shadow:0 0 0 3px var(--success-soft); }
@keyframes pulse { 0%,100% { opacity:1; } 50% { opacity:.25; } }
.status-valid { background:var(--success-soft); color:var(--success); }
.status-invalid { background:var(--danger-soft); color:var(--danger); }
.att-valid { color:var(--success); }
.att-invalid { color:var(--danger); }
.att-dnf { color:var(--text-dim); }
.att-pending { color:var(--text-dim); }
.att-best { font-weight:800; }
.ranking-table-wrap { overflow-x:auto; }
.ranking-table-wrap table { width:auto; min-width:100%; }
.ranking-table-wrap th { text-align:center; padding:8px; color:var(--text-dim); font-size:.72rem; text-transform:uppercase; letter-spacing:.05em; border-bottom:1px solid var(--border-strong); white-space:nowrap; }
.ranking-table-wrap th:nth-child(2), .ranking-table-wrap td:nth-child(2) { text-align:left; }
.ranking-table-wrap td { padding:10px 8px; text-align:center; white-space:nowrap; }
.ranking-table-wrap td:last-child { text-align:center; font-weight:400; }
.ranking-tv { padding:24px; }
.ranking-tv h1 { font-size:2rem; margin-bottom:20px; }
.ranking-tv table { width:100%; font-size:1.3rem; border-collapse:collapse; }
.ranking-tv th, .ranking-tv td { padding:10px 14px; text-align:center; border-bottom:1px solid rgba(255,255,255,.08); }
.ranking-tv td:nth-child(2) { text-align:left; }
.status-pending { background:var(--warning-soft); color:var(--warning); }

label { display:block; margin-bottom:14px; font-size:.82rem; color:var(--text-dim); font-weight:600; }
input {
  width:100%; padding:13px 15px; margin-top:6px; border-radius:var(--radius-sm);
  border:1px solid var(--border-strong); background:var(--bg-elev); color:var(--text); font-size:1rem;
  transition:border-color .15s, box-shadow .15s;
}
input:focus { outline:none; border-color:var(--primary); box-shadow:0 0 0 3px var(--primary-soft); }
.link-btn { background:none; border:none; color:var(--danger); cursor:pointer; font-size:.82rem; font-weight:600; }
.back { display:inline-flex; align-items:center; gap:4px; margin-bottom:16px; font-size:.88rem; font-weight:600; color:var(--text-dim); transition:color .15s; }
.back:hover { color:var(--primary); }
details summary { cursor:pointer; color:var(--text-dim); font-size:.88rem; font-weight:600; }
</style>
</head>
<body>
<header class="topbar">
<a class="topbar-home" href="#/">
    <img src="data:image/png;base64,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" alt="UTFPR" style="height:34px;width:auto;">
    <div class="brand">RaceTrack UTFPR<small>painel do juiz</small></div>
  </a>
  <a href="#/config" style="margin-left:auto; opacity:.55; font-size:1.2rem;" title="Configurações">⚙️</a>
</header>
<main id="app"><p class="muted">Carregando...</p></main>
<footer class="app-footer">
  Desenvolvido por : <strong>Matheus Leineker Stanula</strong>
</footer>
<script>
const API = '/api/v1';
let busy = false;
let pollTimer = null;
let ctx = {};

let participantFilterText = '';
let lastPendingList = [];

function renderParticipantButtons(list, filterText) {
  const f = (filterText || '').toLowerCase();
  const filtered = (f ? list.filter(x => x.name.toLowerCase().includes(f)) : list).slice().sort((a, b) => a.name.localeCompare(b.name));
  if (filtered.length === 0) return '<p class="muted">Nenhum robô encontrado.</p>';
  return filtered.map(x => `<button class="participant-btn" onclick="doArm(${x.participant_id}, ${x.attempts_done + 1})">` +
    `<span class="avatar">${initials(x.name)}</span>${escapeHtml(x.name)}${x.team_name ? ` <span class="p-meta">(${escapeHtml(x.team_name)})</span>` : ''}</button>`).join('');
}

window.filterParticipants = (value) => {
  participantFilterText = value;
  const box = document.getElementById('participant-list-box');
  if (box) box.innerHTML = renderParticipantButtons(lastPendingList, participantFilterText);
};

const STATUS_LABELS = { pending_validation:'Aguardando validação', valid:'Válida', invalid:'Inválida', dnf:'DNF' };
const STATUS_CLASS = { pending_validation:'status-pending', valid:'status-valid', invalid:'status-invalid', dnf:'status-invalid' };

function escapeHtml(s) {
  return String(s == null ? '' : s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
}

function initials(name) {
  const parts = String(name || '').trim().split(/\s+/).filter(Boolean);
  if (parts.length === 0) return '?';
  if (parts.length === 1) return parts[0][0].toUpperCase();
  return (parts[0][0] + parts[parts.length - 1][0]).toUpperCase();
}

function formatElapsed(us) {
  if (us === null || us === undefined) return '--:--.---';
  let totalMs = Math.floor(us / 1000);
  const ms = totalMs % 1000; totalMs = Math.floor(totalMs / 1000);
  const s = totalMs % 60; totalMs = Math.floor(totalMs / 60);
  const m = totalMs;
  const pad = (n, l) => String(n).padStart(l, '0');
  return pad(m, 2) + ':' + pad(s, 2) + '.' + pad(ms, 3);
}

function medal(pos) {
  if (pos === 1) return '&#129351;';
  if (pos === 2) return '&#129352;';
  if (pos === 3) return '&#129353;';
  return '#' + pos;
}
function posClass(pos) { return pos >= 1 && pos <= 3 ? 'pos pos-' + pos : 'pos'; }


function renderRankingTable(rows) {
  if (rows.length === 0) return '<p class="muted">Nenhum resultado ainda.</p>';
  const maxAttempts = rows.reduce((m, r) => Math.max(m, r.attempts.length), 0);
  const head = Array.from({ length: maxAttempts }, (_, i) => `<th>T${i + 1}</th>`).join('');
  const body = rows.map(r => {
    const cells = Array.from({ length: maxAttempts }, (_, i) => r.attempts[i]).map(a => {
      if (!a) return '<td>&mdash;</td>';
      const cls = a.status === 'valid' ? 'att-valid'
        : a.status === 'dnf' ? 'att-dnf'
        : a.status === 'pending_validation' ? 'att-pending'
        : 'att-invalid';
      const label = a.status === 'dnf' ? 'DNF' : formatElapsed(a.elapsed_us);
      return `<td class="${cls}${a.is_best ? ' att-best' : ''}">${label}</td>`;
    }).join('');
    return `<tr><td class="${posClass(r.position)}">${medal(r.position)}</td><td>${escapeHtml(r.participant_name)}${r.team_name ? ` <span class="p-meta">(${escapeHtml(r.team_name)})</span>` : ''}</td>${cells}</tr>`;
  }).join('');
  return `<div class="ranking-table-wrap"><table><thead><tr><th>Pos</th><th>Robô</th>${head}</tr></thead><tbody>${body}</tbody></table></div>`;
}

async function api(method, path, body) {
  const opts = { method };
  if (body !== undefined) {
    opts.headers = { 'Content-Type': 'application/json' };
    opts.body = JSON.stringify(body);
  }
  const res = await fetch(API + path, opts);
  if (res.status === 204) return null;
  let data = null;
  try { data = await res.json(); } catch (e) {}
  if (!res.ok) throw new Error((data && data.detail) || ('Erro HTTP ' + res.status));
  return data;
}

async function withBusy(fn) {
  if (busy) return;
  busy = true;
  document.body.classList.add('busy'); // feedback visual imediato (ver CSS "body.busy")
  try { await fn(); } catch (e) { alert(e.message); } finally { busy = false; document.body.classList.remove('busy'); }
}

function stopPolling() {
  if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
  if (rankingTvTimer) { clearInterval(rankingTvTimer); rankingTvTimer = null; }
}

let rankingTvTimer = null;

async function renderRankingTv(root, competitionId) {
  root.innerHTML = '<p class="muted">Carregando...</p>';
  const draw = async () => {
    let competition, ranking;
    try {
      [competition, ranking] = await Promise.all([
        api('GET', '/competitions/' + competitionId),
        api('GET', '/competitions/' + competitionId + '/ranking'),
      ]);
    } catch (e) { return; }

    root.innerHTML = `<div class="ranking-tv">
      <h1>${escapeHtml(competition.name)} &mdash; Ranking</h1>
      ${renderRankingTable(ranking.ranking)}
    </div>`;
  };
  await draw();
  rankingTvTimer = setInterval(draw, 3000);
}

// ------------------- Roteamento -------------------

window.addEventListener('hashchange', render);
window.addEventListener('DOMContentLoaded', render);

function render() {
  stopPolling();
  const root = document.getElementById('app');
  const hash = location.hash || '#/';
  const mDetail = hash.match(/^#\/competitions\/(\d+)$/);
  const mRankingTv = hash.match(/^#\/competitions\/(\d+)\/ranking-tv$/);
  const mParticipants = hash.match(/^#\/competitions\/(\d+)\/participants$/);
  const mEditCompetition = hash.match(/^#\/competitions\/(\d+)\/edit$/);
  const mNew = hash.match(/^#\/competitions\/new$/);
  const mList = hash.match(/^#\/competitions$/);
  const mHistory = hash.match(/^#\/history$/);
  const mConfig = hash.match(/^#\/config$/);

  if (mRankingTv) return renderRankingTv(root, parseInt(mRankingTv[1], 10));
  if (mParticipants) return renderNewCompetitionParticipants(root, parseInt(mParticipants[1], 10));
  if (mEditCompetition) return renderEditCompetition(root, parseInt(mEditCompetition[1], 10));
  if (mNew) return renderNewCompetition(root);
  if (mDetail) return renderCompetitionDetail(root, parseInt(mDetail[1], 10));
  if (mList) return renderCompetitionsList(root);
  if (mHistory) return renderHistoryList(root);
  if (mConfig) return renderConfig(root);
  return renderHome(root);
}

// ------------------- Configurações (modo Online / nuvem, Fase 18) -------------------

async function renderConfig(root) {
  root.innerHTML = '<a class="back" href="#/">&larr; Início</a><p class="muted">Carregando...</p>';
  let cfg, judgeAuth;
  try {
    [cfg, judgeAuth] = await Promise.all([api('GET', '/cloud-config'), api('GET', '/judge-auth')]);
  } catch (e) { root.innerHTML += '<p>' + escapeHtml(e.message) + '</p>'; return; }

  root.innerHTML = `
    <a class="back" href="#/">&larr; Início</a>
    <h1>Configurações</h1>
    <p class="muted" id="online-status-line" style="margin-top:-12px;">
      Modo Online: <strong style="color:${cfg.online_active ? 'var(--success)' : 'var(--text-dim)'}">${cfg.online_active ? 'ativo' : 'inativo'}</strong>
    </p>
    <form id="cloud-config-form" class="panel">
      <label>URL da nuvem<input name="base_url" placeholder="http://192.168.15.23:8001" value="${escapeHtml(cfg.base_url)}"></label>
      <label>Token do dispositivo<input name="device_token" value="${escapeHtml(cfg.device_token)}"></label>
      <button class="btn-primary" type="submit">Salvar</button>
      <button class="btn-ghost" type="button" id="test-conn-btn">Testar conexão</button>
      <p id="test-result" class="muted" style="margin-top:10px;"></p>
    </form>
    <p class="muted" style="font-size:.8rem;">Preencha os dois campos e salve para ativar o Modo Online. Deixe
      em branco para voltar ao Modo Offline — o cronômetro continua funcionando normalmente sem nuvem ou
      sem internet, isto aqui só espelha os resultados, nunca é obrigatório.</p>

    <div class="panel">
      <h2>⬇️ Exportar dados</h2>
      <p class="muted" style="margin-top:-6px;">Os dados ficam salvos na memória interna do ESP32 o tempo todo.
        Use o botão abaixo para baixar uma cópia completa (equipes, participantes, competições e corridas)
        pro computador.</p>
      <button class="btn-ghost" type="button" onclick="window.open(API + '/export', '_blank')">Baixar dados</button>
    </div>

    <div class="panel">
      <h2>🔒 Segurança desta página</h2>
      <p class="muted" style="margin-top:-6px;">
        Protege esta página do juiz (e a de configurações) contra qualquer pessoa que descubra o endereço do
        ESP32 ou entre na rede Wi-Fi dele. Status: <strong style="color:${judgeAuth.enabled ? 'var(--success)' : 'var(--text-dim)'}">${judgeAuth.enabled ? 'protegida' : 'sem senha'}</strong>
      </p>
      <form id="judge-auth-form">
        <label>Nova senha (usuário fixo: <strong>${escapeHtml(judgeAuth.username)}</strong>)<input type="password" name="password" placeholder="${judgeAuth.enabled ? 'Deixe em branco pra manter a atual' : 'Deixe em branco para não exigir senha'}"></label>
        <button class="btn-primary" type="submit">Salvar senha</button>
        ${judgeAuth.enabled ? '<button class="btn-ghost" type="button" id="judge-auth-remove-btn">Remover senha</button>' : ''}
      </form>
      <p class="muted" style="font-size:.8rem; margin-top:10px;">O navegador vai pedir usuário e senha na
        próxima vez que abrir esta página. O Dashboard Público/Tempo Real da nuvem continuam livres pra
        qualquer participante ver, mesmo com isto ativado — a senha protege só o painel de operação.</p>
    </div>
  `;

  document.getElementById('judge-auth-form').addEventListener('submit', (e) => {
    e.preventDefault();
    withBusy(async () => {
      const password = new FormData(e.target).get('password').trim();
      if (!password) { alert('Digite uma senha para salvar (ou use "Remover senha" pra desativar).'); return; }
      await api('POST', '/judge-auth', { password });
      alert('Senha salva. A próxima requisição vai pedir login.');
      renderConfig(root);
    });
  });

  const removeBtn = document.getElementById('judge-auth-remove-btn');
  if (removeBtn) {
    removeBtn.addEventListener('click', () => {
      if (!confirm('Remover a senha? A página do juiz voltará a ficar acessível sem login.')) return;
      withBusy(async () => {
        await api('POST', '/judge-auth', { password: '' });
        renderConfig(root);
      });
    });
  }

  function readForm() {
    const fd = new FormData(document.getElementById('cloud-config-form'));
    return {
      base_url: fd.get('base_url').trim(),
      device_token: fd.get('device_token').trim(),
    };
  }

  document.getElementById('cloud-config-form').addEventListener('submit', (e) => {
    e.preventDefault();
    withBusy(async () => {
      const saved = await api('POST', '/cloud-config', readForm());
      const p = document.getElementById('test-result');
      p.textContent = 'Configuração salva.';
      p.style.color = '';
      document.getElementById('online-status-line').innerHTML =
        `Modo Online: <strong style="color:${saved.online_active ? 'var(--success)' : 'var(--text-dim)'}">${saved.online_active ? 'ativo' : 'inativo'}</strong>`;
    });
  });

  document.getElementById('test-conn-btn').addEventListener('click', () => {
    withBusy(async () => {
      await api('POST', '/cloud-config', readForm()); // testa sempre o que está no formulário, mesmo se ainda não salvo
      const result = await api('POST', '/cloud-config/test');
      const p = document.getElementById('test-result');
      p.textContent = result.message;
      p.style.color = result.ok ? 'var(--success)' : 'var(--danger)';
    });
  });
}

function renderHome(root) {
  root.innerHTML = `
    <div style="text-align:center; margin-bottom:22px;">
      <h1 style="margin-bottom:2px;">RaceTrack UTFPR</h1>
      <p class="muted" style="margin-top:0;">Cronometragem de robôs seguidores de linha</p>
    </div>
    <div class="tiles">
      <a class="tile" href="#/competitions/new">
        <span class="tile-icon">➕</span>
        <span>Nova competição</span>
      </a>
      <a class="tile" href="#/competitions">
        <span class="tile-icon">📋</span>
        <span>Abrir competição</span>
      </a>
      <a class="tile" href="#/history">
        <span class="tile-icon">📜</span>
        <span>Histórico</span>
      </a>
    </div>
  `;
}

async function renderCompetitionsList(root) {
  root.innerHTML = '<a class="back" href="#/">&larr; Início</a><p class="muted">Carregando...</p>';
  let list;
  try { list = await api('GET', '/competitions'); } catch (e) { root.innerHTML += '<p>' + escapeHtml(e.message) + '</p>'; return; }
  // Só competições ainda não encerradas - as encerradas ficam no
  // Histórico (ver renderHistoryList), pra não dar a impressão de que
  // dá pra continuar operando uma competição que já terminou.
  list = list.filter(c => c.status !== 'finished');
  list.sort((a, b) => b.id - a.id);

  root.innerHTML = `
    <a class="back" href="#/">&larr; Início</a>
    <h1>Competições</h1>
    <div class="panel">
      ${list.length === 0 ? '<p class="muted">Nenhuma competição em andamento.</p>' : '<ul class="runs-list">' +
        list.map(c => `<li><a href="#/competitions/${c.id}">${escapeHtml(c.name)}</a>` +
          `<span class="badge status-valid">Dia ${c.current_day}/${c.num_days}</span></li>`).join('') +
        '</ul>'}
    </div>
  `;
}

async function renderHistoryList(root) {
  root.innerHTML = '<a class="back" href="#/">&larr; Início</a><p class="muted">Carregando...</p>';
  let list;
  try { list = await api('GET', '/competitions'); } catch (e) { root.innerHTML += '<p>' + escapeHtml(e.message) + '</p>'; return; }
  list = list.filter(c => c.status === 'finished');
  list.sort((a, b) => b.id - a.id);

  root.innerHTML = `
    <a class="back" href="#/">&larr; Início</a>
    <h1>Histórico</h1>
    <div class="panel">
      ${list.length === 0 ? '<p class="muted">Nenhuma competição encerrada ainda.</p>' : '<ul class="runs-list">' +
        list.map(c => `<li><a href="#/competitions/${c.id}">${escapeHtml(c.name)}</a>` +
          `<span class="badge status-invalid">Encerrada</span></li>`).join('') +
        '</ul>'}
    </div>
  `;
}

// ------------------- Nova competição (wizard 2 passos) -------------------

async function renderNewCompetition(root) {
  root.innerHTML = `
    <a class="back" href="#/">&larr; Início</a>
    <h1>Nova competição</h1>
    <form id="new-comp-form" class="panel">
      <label>Nome<input required name="name" autofocus placeholder="Ex.: Desafio de Verão 2026"></label>
      <label>Dias<input type="number" min="1" value="1" name="num_days"></label>
      <label>Tomadas por dia<input type="number" min="1" value="1" name="attempts_per_day"></label>
      <button class="btn-primary" type="submit">Criar competição</button>
    </form>
  `;
  document.getElementById('new-comp-form').addEventListener('submit', (e) => {
    e.preventDefault();
    withBusy(async () => {
      const fd = new FormData(e.target);
      const comp = await api('POST', '/competitions', {
        name: fd.get('name'),
        num_days: parseInt(fd.get('num_days'), 10) || 1,
        attempts_per_day: parseInt(fd.get('attempts_per_day'), 10) || 1,
      });
      renderNewCompetitionParticipants(root, comp.id);
    });
  });
}

let participantsLinkTeamId = '';
let participantsLinkSearch = '';

async function renderNewCompetitionParticipants(root, competitionId) {
  const [linked, all, teams] = await Promise.all([
    api('GET', '/competitions/' + competitionId + '/participants'),
    api('GET', '/participants'),
    api('GET', '/teams'),
  ]);
  const linkedIds = new Set(linked.map(p => p.id));
  const unlinked = all.filter(p => !linkedIds.has(p.id));
  const teamNameById = new Map(teams.map(t => [t.id, t.name]));
  const teamOptions = teams.map(t => `<option value="${t.id}" ${String(t.id) === String(participantsLinkTeamId) ? 'selected' : ''}>${escapeHtml(t.name)}</option>`).join('');

  root.innerHTML = `
    <h1>Participantes</h1>
    <form id="new-team-form" class="panel">
      <h2>Criar equipe</h2>
      <label>Nome da equipe<input id="new-team-name" placeholder="Ex.: Equipe Falcão"></label>
      <button type="submit" class="btn-primary">Criar equipe</button>
    </form>
    <form id="add-part-form" class="panel">
      <h2>Cadastrar robô</h2>
      <label>Equipe
        <select name="team_id" id="team-select" required ${teams.length === 0 ? 'disabled' : ''}>
          <option value="" disabled ${teams.length === 0 ? 'selected' : ''}>Selecione a equipe</option>
          ${teams.map(t => `<option value="${t.id}">${escapeHtml(t.name)}</option>`).join('')}
        </select>
      </label>
      <label>Nome do robô<input required name="name" placeholder="Nome do robô"></label>
      <button class="btn-primary" type="submit">Cadastrar robô</button>
    </form>
    <div class="panel">
      <h2>Vincular robô já cadastrado</h2>
      <label>Equipe
        <select id="link-team-filter">
          <option value="" ${participantsLinkTeamId ? '' : 'selected'} disabled>Selecione a equipe</option>
          ${teamOptions}
        </select>
      </label>
      <input id="link-search" type="text" placeholder="Buscar por nome..." value="${escapeHtml(participantsLinkSearch)}" style="margin-top:8px;">
      <div id="link-team-list" class="participant-list" style="margin-top:14px;"></div>
    </div>
    <div class="panel">
      <h2>Vinculados (${linked.length})</h2>
      ${linked.length === 0 ? '<p class="muted">Nenhum participante vinculado ainda.</p>' : '<ul class="runs-list">' +
        [...linked].sort((a, b) => a.name.localeCompare(b.name)).map(p => `<li><span style="display:flex;align-items:center;gap:10px;"><span class="avatar">${initials(p.name)}</span>${escapeHtml(p.name)} <span class="p-meta">${escapeHtml(teamNameById.get(p.team_id) || '?')}</span></span>` +
          `<button class="link-btn" onclick="if(confirm('Remover este robô da competição?')) unlinkExisting(${competitionId}, ${p.id})">remover</button></li>`).join('') +
        '</ul>'}
    </div>
    <button class="btn-primary" onclick="location.hash='#/competitions/${competitionId}'">Concluir &rarr;</button>
  `;

  document.getElementById('new-team-form').addEventListener('submit', (e) => {
    e.preventDefault();
    withBusy(async () => {
      const name = document.getElementById('new-team-name').value.trim();
      if (!name) return;
      await api('POST', '/teams', { name });
      renderNewCompetitionParticipants(root, competitionId);
    });
  });

  document.getElementById('add-part-form').addEventListener('submit', (e) => {
    e.preventDefault();
    withBusy(async () => {
      const fd = new FormData(e.target);
      const p = await api('POST', '/participants', { name: fd.get('name'), team_id: parseInt(fd.get('team_id'), 10) });
      await api('POST', '/competitions/' + competitionId + '/participants', { participant_id: p.id });
      renderNewCompetitionParticipants(root, competitionId);
    });
  });

  function renderLinkList() {
    let list = unlinked;
    if (participantsLinkTeamId) list = list.filter(p => String(p.team_id) === String(participantsLinkTeamId));
    if (participantsLinkSearch) list = list.filter(p => p.name.toLowerCase().includes(participantsLinkSearch.toLowerCase()));
    const box = document.getElementById('link-team-list');
    box.innerHTML = list.length === 0 ? '<p class="muted">Nenhum robô encontrado.</p>' :
      [...list].sort((a, b) => a.name.localeCompare(b.name)).map(p => `<button class="participant-btn" onclick="linkExisting(${competitionId}, ${p.id})">` +
        `<span class="avatar">${initials(p.name)}</span>${escapeHtml(p.name)} <span class="p-meta">${escapeHtml(teamNameById.get(p.team_id) || '?')}</span></button>`).join('');
  }
  document.getElementById('link-team-filter').addEventListener('change', (e) => {
    participantsLinkTeamId = e.target.value;
    renderLinkList();
  });
  document.getElementById('link-search').addEventListener('input', (e) => {
    participantsLinkSearch = e.target.value;
    renderLinkList();
  });
  renderLinkList();
}

async function renderEditCompetition(root, id) {
  root.innerHTML = '<p class="muted">Carregando...</p>';
  let competition;
  try { competition = await api('GET', '/competitions/' + id); } catch (e) { root.innerHTML = '<p>' + escapeHtml(e.message) + '</p>'; return; }

  root.innerHTML = `
    <a class="back" href="#/competitions/${id}">&larr; ${escapeHtml(competition.name)}</a>
    <h1>Editar competição</h1>
    <form id="edit-comp-form" class="panel">
      <label>Dias<input type="number" min="1" name="num_days" value="${competition.num_days}"></label>
      <label>Tomadas por dia<input type="number" min="1" name="attempts_per_day" value="${competition.attempts_per_day}"></label>
      <button class="btn-primary" type="submit">Salvar</button>
    </form>
    <p class="muted" style="font-size:.8rem;">Só é possível editar antes de qualquer corrida ser registrada nesta competição.</p>
  `;
  document.getElementById('edit-comp-form').addEventListener('submit', (e) => {
    e.preventDefault();
    withBusy(async () => {
      const fd = new FormData(e.target);
      await api('PATCH', '/competitions/' + id, {
        num_days: parseInt(fd.get('num_days'), 10) || 1,
        attempts_per_day: parseInt(fd.get('attempts_per_day'), 10) || 1,
      });
      location.hash = '#/competitions/' + id;
    });
  });
}

window.linkExisting = (competitionId, participantId) => withBusy(async () => {
  await api('POST', '/competitions/' + competitionId + '/participants', { participant_id: participantId });
  renderNewCompetitionParticipants(document.getElementById('app'), competitionId);
});
window.unlinkExisting = (competitionId, participantId) => withBusy(async () => {
  await api('DELETE', '/competitions/' + competitionId + '/participants/' + participantId);
  renderNewCompetitionParticipants(document.getElementById('app'), competitionId);
});

// ------------------- Tela de operação (detalhe da competição) -------------------

async function renderCompetitionDetail(root, id) {
  root.innerHTML = '<p class="muted">Carregando...</p>';
  activeDetailTab = 'ranking'; // reseta ao entrar numa competição (evita herdar aba de uma visita anterior)
  const ok = await refreshDetail(root, id);
  // Pula o tick do polling enquanto uma ação (doValidate/doArm/etc, via
  // withBusy) estiver em andamento — sem isso, a resposta do polling
  // periódico podia chegar DEPOIS da resposta da ação (o ESP32 atende
  // uma requisição por vez, então a ordem de chegada não é garantida) e
  // sobrescrever a tela já atualizada com o estado antigo, dando a
  // impressão de que o clique não fez nada (era preciso clicar de novo).
  if (ok) pollTimer = setInterval(() => {
    if (!busy && document.activeElement?.id !== 'participant-search') refreshDetail(root, id);
  }, 1500);
}

// Guarda de sequência: se duas chamadas a refreshDetail estiverem em voo
// ao mesmo tempo (ex.: o polling disparou bem no instante em que uma
// ação também chamou refreshDetail), só a resposta da chamada MAIS
// RECENTE pode atualizar a tela — descarta qualquer resposta atrasada de
// uma chamada mais antiga, nunca deixa o mais velho sobrescrever o mais
// novo. Reforça a proteção do "if (!busy)" acima para qualquer overlap
// que escape dela.
let refreshSeq = 0;

async function refreshDetail(root, id) {
  const mySeq = ++refreshSeq;
  let competition, dayStatus, armState, ranking, runs;
  try {
    [competition, dayStatus, armState, ranking, runs] = await Promise.all([
      api('GET', '/competitions/' + id),
      api('GET', '/competitions/' + id + '/day-status'),
      api('GET', '/arm'),
      api('GET', '/competitions/' + id + '/ranking'),
      api('GET', '/competitions/' + id + '/runs'),
    ]);
  } catch (e) {
    if (mySeq !== refreshSeq) return false; // resposta atrasada - descarta
    root.innerHTML = '<a class="back" href="#/">&larr; Início</a><p>' + escapeHtml(e.message) + '</p>';
    return false;
  }
  if (mySeq !== refreshSeq) return false; // resposta atrasada - descarta
  ctx = { competitionId: id, currentDay: dayStatus.current_day };
  root.innerHTML = renderDetailHtml(competition, dayStatus, armState, ranking, runs);
  applyDetailTab(); // reaplica a aba ativa (o innerHTML acima sempre volta pro estado "padrão" do HTML)
  return true;
}

function derivePhase(competition, dayStatus, armState, runs) {
  if (competition.status === 'finished') return { phase: 'finished' };
  const pendingRun = runs.find(r => r.status === 'pending_validation');
  if (pendingRun) return { phase: 'validate', pendingRun };
  if (dayStatus.day_complete) return { phase: 'day_complete' };
  if (armState.armed && !armState.released) return { phase: 'locked' };
  if (armState.armed && armState.released && !armState.started) return { phase: 'waiting_start' };
  if (armState.armed && armState.released && armState.started) return { phase: 'running' };
  return { phase: 'select' };
}

function renderPhasePanel(competition, dayStatus, armState, derived) {
  const p = derived.phase;

  if (p === 'finished') {
    return `<div class="panel"><h2>🏁 Encerrada</h2><p>Competição encerrada. Veja o ranking final abaixo.</p></div>`;
  }

  if (p === 'validate') {
    const r = derived.pendingRun;
    return `<div class="panel">
      <h2>✅ Validar resultado</h2>
      <p>${escapeHtml(r.participant_name)} &mdash; Dia ${r.day}, Tomada ${r.attempt}</p>
      <p class="time-display">${formatElapsed(r.elapsed_us)}</p>
      <button class="btn-primary" onclick="doValidate(${r.id}, 'valid')">Válida</button>
      <button class="btn-danger" onclick="doValidate(${r.id}, 'invalid')">Inválida</button>
      <button class="btn-ghost" onclick="doValidate(${r.id}, 'retry')">Repetir tomada</button>
    </div>`;
  }

  if (p === 'day_complete') {
    const label = dayStatus.is_last_day ? 'Encerrar competição' : ('Iniciar Dia ' + (dayStatus.current_day + 1));
    return `<div class="panel">
      <h2>📅 Dia ${dayStatus.current_day} concluído</h2>
      <button class="btn-primary" onclick="doAdvanceDay(${dayStatus.is_last_day})">${label} &rarr;</button>
    </div>`;
  }

  if (p === 'locked') {
    return `<div class="panel">
      <h2>🔒 Corrida travada</h2>
      <p><span class="avatar" style="margin-right:8px;">${initials(armState.participant_name)}</span>${escapeHtml(armState.participant_name)} &mdash; Dia ${armState.day}, Tomada ${armState.attempt}</p>
      <button class="btn-primary" onclick="doRelease()">Liberar corrida</button>
    </div>`;
  }

  if (p === 'waiting_start') {
    return `<div class="panel">
      <h2><span class="live-dot"></span> Aguardando o competidor</h2>
      <p>${escapeHtml(armState.participant_name)} &mdash; liberado, pode iniciar a qualquer momento.</p>
    </div>`;
  }

  if (p === 'running') {
    return `<div class="panel">
      <h2><span class="live-dot"></span> Corrida em andamento</h2>
      <p>${escapeHtml(armState.participant_name)}</p>
      <button class="btn-ghost" onclick="doRetry()">Repetir tomada</button>
      <button class="btn-danger" onclick="doAbort()">Abortar corrida</button>
    </div>`;
  }

  // select
  const pending = dayStatus.participants.filter(x => !x.done);
  const done = dayStatus.participants.filter(x => x.done);
  if (dayStatus.participants.length === 0) {
    return `<div class="panel"><h2>🎯 Selecionar participante</h2><p class="muted">Nenhum participante vinculado a esta competição.</p></div>`;
  }
  lastPendingList = pending;
  return `<div class="panel">
    <h2>🎯 Selecionar participante</h2>
    <input id="participant-search" type="text" placeholder="Buscar por nome..." value="${escapeHtml(participantFilterText)}" oninput="filterParticipants(this.value)" style="margin-bottom:10px; width:100%;">
    <div class="participant-list" id="participant-list-box">
      ${renderParticipantButtons(pending, participantFilterText)}
    </div>
    ${done.length ? `<details style="margin-top:14px;"><summary>Já concluíram o dia</summary>
      <ul class="runs-list">${[...done].sort((a, b) => a.name.localeCompare(b.name)).map(x => `<li><span style="display:flex;align-items:center;gap:10px;"><span class="avatar">${initials(x.name)}</span>${escapeHtml(x.name)}</span></li>`).join('')}</ul>
    </details>` : ''}
  </div>`;
}

function renderDetailHtml(competition, dayStatus, armState, ranking, runs) {
  const derived = derivePhase(competition, dayStatus, armState, runs);
  const runsSorted = runs.slice().reverse();
  const backHref = competition.status === 'finished' ? '#/history' : '#/competitions';
  const backLabel = competition.status === 'finished' ? 'Histórico' : 'Competições';

  return `
    <a class="back" href="${backHref}">&larr; ${backLabel}</a>
    <div style="display:flex; justify-content:space-between; align-items:center; gap:10px; flex-wrap:wrap;">
      <h1 style="margin:0;">${escapeHtml(competition.name)}</h1>
      ${competition.status !== 'finished' ? `<div style="display:flex; gap:8px;">
        <a class="btn-ghost" style="width:auto; margin-top:0;" href="#/competitions/${competition.id}/participants">Gerenciar participantes</a>
        <a class="btn-ghost" style="width:auto; margin-top:0;" href="#/competitions/${competition.id}/edit">Editar competição</a>
      </div>` : ''}
    </div>
    ${competition.status !== 'finished' ? `<p class="muted" style="margin-top:-12px;">Dia ${dayStatus.current_day} de ${dayStatus.num_days}</p>` : ''}

    ${renderPhasePanel(competition, dayStatus, armState, derived)}

    <div class="tabs-bar">
      <button class="tab-btn" id="tab-btn-ranking" onclick="switchDetailTab('ranking')">🏆 Ranking</button>
      <button class="tab-btn" id="tab-btn-runs" onclick="switchDetailTab('runs')">📜 Corridas Recentes</button>
    </div>

    <div id="tab-ranking" class="panel">
      <h2>🏆 Ranking
        <button class="btn-ghost" style="float:right;" onclick="window.open('#/competitions/${competition.id}/ranking-tv','_blank')">🖥️ Tela cheia</button>
      </h2>
      ${renderRankingTable(ranking.ranking)}
    </div>

    <div id="tab-runs" class="panel">
      <h2>📜 Corridas Recentes</h2>
      ${runsSorted.length === 0 ? '<p class="muted">Nenhuma corrida ainda.</p>' : `<ul class="runs-list">` +
        runsSorted.map(r => `<li><span>${escapeHtml(r.participant_name)} &mdash; Dia ${r.day}/T${r.attempt}</span>` +
          `<span class="r-time">${formatElapsed(r.elapsed_us)}</span>` +
          `<span class="badge ${STATUS_CLASS[r.status] || ''}">${STATUS_LABELS[r.status] || r.status}</span></li>`).join('') + `</ul>`}
    </div>
  `;
}

// Preserva a aba ativa entre atualizações (o polling reescreve o HTML
// inteiro a cada 1.5s - sem isso, quem estivesse na aba "Corridas
// Recentes" veria a tela voltar sozinha pra "Ranking" a cada atualização).
let activeDetailTab = 'ranking';

function applyDetailTab() {
  const rankingPanel = document.getElementById('tab-ranking');
  const runsPanel = document.getElementById('tab-runs');
  const rankingBtn = document.getElementById('tab-btn-ranking');
  const runsBtn = document.getElementById('tab-btn-runs');
  if (!rankingPanel || !runsPanel || !rankingBtn || !runsBtn) return; // fora da tela de detalhe (ex.: erro de carregamento)
  rankingPanel.style.display = activeDetailTab === 'ranking' ? '' : 'none';
  runsPanel.style.display = activeDetailTab === 'runs' ? '' : 'none';
  rankingBtn.classList.toggle('tab-active', activeDetailTab === 'ranking');
  runsBtn.classList.toggle('tab-active', activeDetailTab === 'runs');
}

window.switchDetailTab = (tab) => {
  activeDetailTab = tab;
  applyDetailTab();
};

window.doArm = (participantId, attempt) => withBusy(async () => {
  await api('POST', '/arm', { competition_id: ctx.competitionId, participant_id: participantId, day: ctx.currentDay, attempt });
  await refreshDetail(document.getElementById('app'), ctx.competitionId);
});
window.doRelease = () => withBusy(async () => {
  await api('POST', '/arm/release');
  await refreshDetail(document.getElementById('app'), ctx.competitionId);
});
window.doAbort = () => withBusy(async () => {
  await api('POST', '/arm/abort');
  await refreshDetail(document.getElementById('app'), ctx.competitionId);
});
window.doRetry = () => withBusy(async () => {
  await api('POST', '/arm/retry');
  await refreshDetail(document.getElementById('app'), ctx.competitionId);
});
window.doValidate = (runId, status) => withBusy(async () => {
  await api('PATCH', '/runs/' + runId + '/validation', { status });
  await refreshDetail(document.getElementById('app'), ctx.competitionId);
});
window.doAdvanceDay = (isLastDay) => {
  const msg = isLastDay
    ? 'Encerrar esta competição? Não será possível registrar mais corridas depois disso.'
    : 'Avançar para o próximo dia? Não será possível voltar a registrar corridas do dia atual depois disso.';
  if (!confirm(msg)) return;
  withBusy(async () => {
    await api('POST', '/competitions/' + ctx.competitionId + '/advance-day');
    await refreshDetail(document.getElementById('app'), ctx.competitionId);
  });
};
</script>
</body>
</html>
)HTMLPAGE";
