/**
 * P2P Node Dashboard — app.js
 * Connects to server.py via WebSocket, processes messages,
 * renders terminal output, peers, and stats.
 */

(function () {
  'use strict';

  /* ── Config ──────────────────────────────────────────────────────────────── */
  const WS_URL = `ws://${location.hostname}:8765`;
  const RECONNECT = 3000;  // ms before reconnect attempt

  /* ── State ───────────────────────────────────────────────────────────────── */
  let ws = null;
  let taskCount = 0;
  let peers = {};         // ip:port → {ip, port, load1, tasks, lastSeen}
  let history = [];
  let historyIdx = -1;
  let pendingOutput = '';       // accumulates until we get a done/exit_code line

  /* ── DOM refs ────────────────────────────────────────────────────────────── */
  const $output = document.getElementById('output');
  const $input = document.getElementById('cmd-input');
  const $sendBtn = document.getElementById('send-btn');
  const $clearBtn = document.getElementById('clear-btn');
  const $statusPill = document.getElementById('status-pill');
  const $statusText = document.getElementById('status-text');
  const $statPeers = document.getElementById('stat-peers');
  const $statTasks = document.getElementById('stat-tasks');
  const $statExit = document.getElementById('stat-exit');
  const $statPort = document.getElementById('stat-port');
  const $peersList = document.getElementById('peers-list');
  const $historyList = document.getElementById('history-list');
  const $uploadBtn = document.getElementById('upload-btn');
  const $fileInput = document.getElementById('file-input');

  /* ── Terminal helpers ────────────────────────────────────────────────────── */
  function appendLine(text, cls = '') {
    const span = document.createElement('span');
    span.className = 'out-line' + (cls ? ' ' + cls : '');
    span.textContent = text;
    $output.appendChild(span);
    $output.appendChild(document.createTextNode('\n'));
    $output.scrollTop = $output.scrollHeight;
  }

  function systemMsg(text) { appendLine(text, 'system'); }
  function successMsg(text) { appendLine(text, 'success'); }
  function clearOutput() { $output.innerHTML = ''; }

  /* ── Peer list renderer ──────────────────────────────────────────────────── */
  function parsePeersOutput(raw) {
    /**
     * Parses lines from `peers` REPL output. We look for lines like:
     *   [1] 192.168.1.5:7777  load=0.42/0.31/0.20  tasks=1
     * and also the plain IP:port pattern as a fallback.
     */
    /* Robust regex to match both "IP PORT LOAD% TASKS" and "IP:PORT load=LOAD% tasks=TASKS" */
    const re = /((?:\d{1,3}\.){3}\d{1,3})[\s:]+(\d+)\s+(?:load=)?\s*([\d.]+)\s*%?\s*(?:tasks=)?\s*(\d+)?/gi;



    let m;
    while ((m = re.exec(raw)) !== null) {
      const key = `${m[1]}:${m[2]}`;
      peers[key] = {
        ip: m[1],
        port: m[2],
        utilization: m[3] ? parseFloat(m[3]) : 0,
        activeTasks: m[4] ? parseInt(m[4]) : 0,
        lastSeen: Date.now(),
      };
    }

    renderPeers();
  }

  function renderPeers() {
    const keys = Object.keys(peers);
    $statPeers.textContent = keys.length;

    if (keys.length === 0) {
      $peersList.innerHTML = '<div class="no-peers">No peers discovered yet</div>';
      return;
    }

    $peersList.innerHTML = '';
    keys.forEach(key => {
      const p = peers[key];
      const loadPct = p.utilization || 0;
      /* Simple score for display: load + 10 * tasks */
      const score = (loadPct + 10 * (p.activeTasks || 0)).toFixed(1);

      const el = document.createElement('div');
      el.className = 'peer-item';
      el.innerHTML = `
        <div class="peer-head">
          <span class="peer-ip">${p.ip}</span>
          <span class="peer-port">:${p.port}</span>
        </div>
        <div class="peer-load-bar">
          <div class="peer-load-fill" style="width:${loadPct}%"></div>
        </div>
        <div class="peer-meta">
          <span>load: ${loadPct.toFixed(1)}%</span>
        </div>`;

      $peersList.appendChild(el);
    });

  }

  /* ── Exit-code parser ────────────────────────────────────────────────────── */
  function tryParseExitCode(line) {
    // "--- exit code: 0 ---"
    const m = line.match(/exit code[:：]\s*(-?\d+)/i);
    if (m) {
      const code = parseInt(m[1]);
      $statExit.textContent = code;
      $statExit.style.color = code === 0 ? 'var(--success)' : 'var(--danger)';
    }
  }

  /* ── History ─────────────────────────────────────────────────────────────── */
  function pushHistory(cmd) {
    if (!cmd || history[0] === cmd) return;
    history.unshift(cmd);
    if (history.length > 30) history.pop();
    historyIdx = -1;
    renderHistory();
  }

  function renderHistory() {
    if (history.length === 0) {
      $historyList.innerHTML = '<div class="no-history">No commands yet</div>';
      return;
    }
    $historyList.innerHTML = '';
    history.slice(0, 12).forEach(cmd => {
      const el = document.createElement('div');
      el.className = 'hist-item';
      el.textContent = cmd;
      el.title = cmd;
      el.addEventListener('click', () => {
        $input.value = cmd;
        $input.focus();
      });
      $historyList.appendChild(el);
    });
  }

  /* ── Send command ────────────────────────────────────────────────────────── */
  function sendCommand(cmd) {
    cmd = cmd.trim();
    if (!cmd || !ws || ws.readyState !== WebSocket.OPEN) return;

    appendLine(`p2p> ${cmd}`, 'success');
    ws.send(JSON.stringify({ type: 'command', cmd }));

    if (cmd === 'peers' || cmd.startsWith('submit ')) {
      if (cmd.startsWith('submit ')) {
        taskCount++;
        $statTasks.textContent = taskCount;
      }
      pushHistory(cmd);
    }

  }

  /* ── WebSocket ───────────────────────────────────────────────────────────── */
  function setStatus(alive, text) {
    $statusPill.className = alive ? 'alive' : 'dead';
    $statusText.textContent = text;
  }

  function connect() {
    setStatus(false, 'Connecting…');
    ws = new WebSocket(WS_URL);

    ws.onopen = () => {
      setStatus(true, 'Connected');
      systemMsg('── Connected to p2p_node bridge ──');
      ws.send(JSON.stringify({ type: 'status' }));
    };

    ws.onclose = () => {
      setStatus(false, 'Disconnected');
      systemMsg('── Connection lost — retrying in 3 s ──');
      setTimeout(connect, RECONNECT);
    };

    ws.onerror = () => {
      setStatus(false, 'Error');
    };

    ws.onmessage = ({ data }) => {
      let msg;
      try { msg = JSON.parse(data); } catch { return; }

      switch (msg.type) {
        case 'output': {
          const line = msg.line;
          appendLine(line, msg.kind === 'stderr' ? 'stderr' : '');
          tryParseExitCode(line);
          // Accumulate for peer parser, but also try parsing immediately if it looks like a peer line
          pendingOutput += line + '\n';
          if (line.match(/\d+\.\d+\.\d+\.\d+[\s:]+\d+/) || line.match(/load[%=\s]/)) {
            parsePeersOutput(line);
            renderPeers();
          }

          break;
        }
        case 'done':
          // After "peers" output, parse peer list
          if (pendingOutput.match(/\d+\.\d+\.\d+\.\d+[\s:]+\d+/)) {
            parsePeersOutput(pendingOutput);
            renderPeers();
          }
          pendingOutput = '';

          break;
        case 'exited':
          setStatus(false, 'p2p_node exited');
          systemMsg('── p2p_node process exited ──');
          break;
        case 'status':
          setStatus(msg.alive, msg.alive ? 'Running' : 'Stopped');
          break;
        case 'error':
          appendLine(msg.line ?? 'Unknown error', 'stderr');
          break;
      }
    };
  }

  /* ── Event listeners ─────────────────────────────────────────────────────── */
  $sendBtn.addEventListener('click', () => {
    sendCommand($input.value);
    $input.value = '';
    $input.focus();
  });

  $input.addEventListener('keydown', e => {
    if (e.key === 'Enter') {
      sendCommand($input.value);
      $input.value = '';
    } else if (e.key === 'ArrowUp') {
      e.preventDefault();
      if (historyIdx < history.length - 1) {
        historyIdx++;
        $input.value = history[historyIdx];
      }
    } else if (e.key === 'ArrowDown') {
      e.preventDefault();
      if (historyIdx > 0) {
        historyIdx--;
        $input.value = history[historyIdx];
      } else {
        historyIdx = -1;
        $input.value = '';
      }
    }
  });

  $clearBtn.addEventListener('click', clearOutput);

  $uploadBtn.addEventListener('click', () => $fileInput.click());

  $fileInput.addEventListener('change', () => {
    const file = $fileInput.files[0];
    if (!file) return;

    const reader = new FileReader();
    reader.onload = (e) => {
      const content = e.target.result;
      if (!ws || ws.readyState !== WebSocket.OPEN) {
        systemMsg('Cannot upload: not connected', 'stderr');
        return;
      }
      systemMsg(`Uploading and submitting ${file.name}...`);
      ws.send(JSON.stringify({
        type: 'file_upload',
        filename: file.name,
        content: content
      }));
      taskCount++;
      $statTasks.textContent = taskCount;
      pushHistory(`submit ${file.name}`);
    };
    reader.readAsText(file);
    $fileInput.value = ''; // Reset for next selection
  });

  document.querySelectorAll('.qa-btn[data-cmd]').forEach(btn => {
    btn.addEventListener('click', () => {
      $input.value = btn.dataset.cmd;
      $input.focus();
    });
  });

  /* ── Init ────────────────────────────────────────────────────────────────── */
  // Try to read port from URL: ?port=7777
  const urlPort = new URLSearchParams(location.search).get('port');
  if (urlPort) $statPort.textContent = urlPort;

  connect();

})();
