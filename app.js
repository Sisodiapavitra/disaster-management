let streamRunning = true;
let serialConnected = false;
const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => [...document.querySelectorAll(selector)];

function formatTime(date = new Date()) { return date.toLocaleTimeString([], { hour12: false }); }
function showToast(message) { const toast = $('#toast'); toast.textContent = message; toast.classList.add('show'); window.clearTimeout(showToast.timer); showToast.timer = window.setTimeout(() => toast.classList.remove('show'), 3200); }
function setText(key, value) { const element = document.querySelector(`[data-reading="${key}"]`); if (element) element.textContent = value; }
function labelFor(state) { return state === 'critical' ? '● CRITICAL' : state === 'watch' ? '● WATCH' : '● NORMAL'; }
function updateNode(node, state, percentage) {
  const card = document.querySelector(`[data-node="${node}"]`); const pill = card.querySelector('[data-state-pill]');
  pill.className = `state-pill ${state}`; pill.textContent = labelFor(state);
  const bar = card.querySelector('[data-bar]'); bar.style.width = `${Math.max(4, percentage)}%`; bar.style.background = state === 'critical' ? 'var(--red)' : state === 'watch' ? 'var(--yellow)' : '';
}
function applyState(state) {
  const { readings, thresholds, states, overall, updated_at, serial } = state;
  serialConnected = Boolean(serial?.connected);
  Object.entries(readings).forEach(([key, value]) => setText(key, typeof value === 'number' && !Number.isInteger(value) ? value.toFixed(['waterDistance', 'forestTemp'].includes(key) ? 1 : 0) : value));
  updateNode('forest', states.forest, Math.min(100, readings.smoke / thresholds.smokeCritical * 100));
  updateNode('air', states.air, Math.min(100, readings.pm25 / thresholds.pmCritical * 100));
  updateNode('flood', states.flood, Math.min(100, Math.max(0, 100 - readings.waterDistance / thresholds.floodWarningDistance * 100)));
  $('#overallOrb').className = `status-orb ${overall === 'normal' ? '' : overall}`;
  $('#overallLabel').textContent = overall === 'critical' ? 'SYSTEM CRITICAL' : overall === 'watch' ? 'SYSTEM WATCH' : 'SYSTEM NORMAL';
  $('#overallTitle').textContent = overall === 'critical' ? 'Immediate response recommended' : overall === 'watch' ? 'Elevated conditions detected' : 'Monitoring 3 active nodes';
  $('#overallSummary').textContent = overall === 'critical' ? 'One or more readings crossed the critical threshold. Verify locally and notify the response team.' : overall === 'watch' ? 'A reading is above the watch threshold. Keep the channel under observation.' : 'All channels are reporting. Threshold rules are evaluating incoming readings.';
  $('#networkStatus').textContent = serial?.connected ? `ESP32 connected (${serial.port})` : serial?.error ? `ESP32 error: ${serial.error}` : streamRunning ? 'Demo stream active' : 'Stream paused';
  const timestamp = new Date(updated_at); $('#lastUpdated').textContent = formatTime(timestamp); $('#decisionTime').textContent = formatTime(timestamp).slice(0, 5);
}
async function api(path, options = {}) { const response = await fetch(path, { headers: { 'Content-Type': 'application/json' }, ...options }); if (!response.ok) { const detail = await response.json().catch(() => ({})); throw new Error(detail.detail || `API error ${response.status}`); } return response.json(); }
async function refreshState() { try { applyState(await api('/api/state')); } catch { $('#networkStatus').textContent = 'Backend offline'; showToast('Start FastAPI with run_server.bat'); } }
async function addDemoTick() { if (!streamRunning || serialConnected) return; try { applyState(await api('/api/demo-tick', { method: 'POST' })); } catch {} }
$$('[data-threshold]').forEach(input => input.addEventListener('change', async () => {
  const payload = Object.fromEntries($$('[data-threshold]').map(field => [field.dataset.threshold, Number(field.value)]));
  try { applyState(await api('/api/thresholds', { method: 'PUT', body: JSON.stringify(payload) })); showToast('Threshold rule updated on server'); } catch (error) { showToast(`Threshold rejected: ${error.message}`); }
}));
$('#pauseButton').addEventListener('click', () => { streamRunning = !streamRunning; $('#pauseButton').textContent = streamRunning ? 'Ⅱ' : '▶'; $('#networkStatus').textContent = streamRunning ? 'Demo stream active' : 'Stream paused'; showToast(streamRunning ? 'Demo telemetry resumed' : 'Demo telemetry paused'); });
$('#clearLog').addEventListener('click', () => { $('#activityList').innerHTML = ''; $('#eventCount').textContent = '00'; showToast('Decision log cleared'); });
$('#serialButton').addEventListener('click', async () => {
  const port = window.prompt('Enter the central ESP32 COM port, for example COM5:');
  if (!port) return;
  try { applyState(await api('/api/serial/connect', { method: 'POST', body: JSON.stringify({ port, baud_rate: 115200 }) })); showToast(`Connecting to ${port} at 115200 baud`); } catch (error) { showToast(`Serial connection failed: ${error.message}`); }
});
refreshState(); setInterval(addDemoTick, 3500); setInterval(refreshState, 1500);
