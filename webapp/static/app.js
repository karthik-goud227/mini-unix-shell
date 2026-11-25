const sampleScript = `# A small MiniUnix script
VALUE=before
for ITEM in alpha beta
do
    printf 'loop=%s\\n' "$ITEM"
done
if [ "$VALUE" = before ]
then
    echo condition=true
fi
`;

const examples = {
  hello: "printf 'Hello from MiniUnix!\\n'\n",
  pipeline: "printf 'apple\\nbanana\\n' | grep banana | wc -l\n",
  redirect: "printf 'Saved by msh\\n' > /tmp/miniunix-lab-output.txt; cat < /tmp/miniunix-lab-output.txt\n",
  job: "sleep 30\n",
  jobs: "jobs\n",
};

const terminalElement = document.getElementById('terminal');
const stage = document.getElementById('terminal-stage');
const badge = document.getElementById('live-badge');
const liveLabel = document.getElementById('live-label');
const cwdLabel = document.getElementById('working-directory');
const restartButton = document.getElementById('restart-shell');
const editor = document.getElementById('script-editor');
const toast = document.getElementById('toast');
let toastTimer;
let cursor = 0;
let connected = false;
let inputQueue = Promise.resolve();

editor.value = localStorage.getItem('miniunix-script') || sampleScript;
editor.addEventListener('input', () => localStorage.setItem('miniunix-script', editor.value));

function notify(message) {
  toast.textContent = message;
  toast.hidden = false;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { toast.hidden = true; }, 3400);
}

async function request(path, data) {
  const response = await fetch(path, data === undefined ? {} : {
    method: 'POST',
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(data),
  });
  const result = await response.json();
  if (!response.ok) throw new Error(result.error || `Request failed (${response.status})`);
  return result;
}

function setLive(alive) {
  badge.classList.toggle('offline', !alive);
  liveLabel.textContent = alive ? 'Shell running' : 'Shell exited';
  restartButton.hidden = alive;
  document.getElementById('run-script').disabled = !alive;
}

const terminal = new Terminal({
  cursorBlink: true,
  fontFamily: 'Consolas, Menlo, "DejaVu Sans Mono", monospace',
  fontSize: 13,
  lineHeight: 1.28,
  scrollback: 3000,
  convertEol: false,
  theme: {
    background: '#101b2a', foreground: '#e7f0f8', cursor: '#7be1bd',
    selectionBackground: '#3f709380', black: '#1d2a3c', red: '#ff7d86',
    green: '#7be1bd', yellow: '#efc776', blue: '#7eb7f6',
    magenta: '#c7a6f4', cyan: '#78d8e4', white: '#e7f0f8',
  },
});
const fitAddon = new FitAddon.FitAddon();
terminal.loadAddon(fitAddon);
terminal.open(terminalElement);

function sendInput(data) {
  inputQueue = inputQueue.then(() => request('/api/input', {data})).catch(err => notify(err.message));
  return inputQueue;
}
terminal.onData(sendInput);

let resizeTimer;
function fitTerminal() {
  clearTimeout(resizeTimer);
  resizeTimer = setTimeout(async () => {
    fitAddon.fit();
    try { await request('/api/resize', {cols: terminal.cols, rows: terminal.rows}); }
    catch (err) { /* The shell may have exited. */ }
  }, 80);
}
new ResizeObserver(fitTerminal).observe(stage);
fitTerminal();

function decodeBase64(encoded) {
  const text = atob(encoded);
  const bytes = new Uint8Array(text.length);
  for (let i = 0; i < text.length; i++) bytes[i] = text.charCodeAt(i);
  return bytes;
}

async function outputLoop() {
  try {
    const result = await request(`/api/output?cursor=${cursor}`);
    if (result.truncated) terminal.write('\r\n[Earlier output was trimmed]\r\n');
    if (result.data) terminal.write(decodeBase64(result.data));
    cursor = result.cursor;
    setLive(result.alive);
    connected = true;
  } catch (err) {
    if (connected) notify('Connection to the local shell was lost.');
    connected = false;
    badge.classList.add('offline');
    liveLabel.textContent = 'Disconnected';
  } finally {
    setTimeout(outputLoop, connected ? 90 : 900);
  }
}
outputLoop();

async function refreshSession() {
  try {
    const result = await request('/api/session');
    cwdLabel.textContent = result.cwd || 'Project directory';
    cwdLabel.title = result.cwd || '';
  } catch (err) { /* Output loop displays connection state. */ }
}
refreshSession();
setInterval(refreshSession, 4000);

document.querySelectorAll('[data-example]').forEach(button => button.addEventListener('click', async () => {
  const key = button.dataset.example;
  if (key === 'job') notify('Press Ctrl+Z to stop the sleep, then choose Show jobs.');
  await sendInput(examples[key]);
  terminal.focus();
}));
document.getElementById('send-interrupt').addEventListener('click', () => { sendInput('\x03'); terminal.focus(); });
document.getElementById('send-suspend').addEventListener('click', () => { sendInput('\x1a'); terminal.focus(); });
document.getElementById('clear-terminal').addEventListener('click', () => { terminal.clear(); terminal.focus(); });
restartButton.addEventListener('click', async () => {
  try {
    terminal.write('\r\n[Starting a new shell]\r\n');
    await request('/api/restart', {});
    setLive(true);
    fitTerminal();
    terminal.focus();
  } catch (err) { notify(err.message); }
});
document.getElementById('run-script').addEventListener('click', async () => {
  try {
    const result = await request('/api/script', {source: editor.value});
    notify(`${result.filename} sent to the shell`);
    terminal.focus();
  } catch (err) { notify(err.message); }
});
document.getElementById('reset-script').addEventListener('click', () => {
  editor.value = sampleScript;
  localStorage.setItem('miniunix-script', sampleScript);
  notify('Example script restored.');
});
document.getElementById('load-script').addEventListener('change', async event => {
  const file = event.target.files[0];
  if (!file) return;
  if (file.size > 65536) { notify('Choose a script smaller than 64 KB.'); return; }
  editor.value = await file.text();
  localStorage.setItem('miniunix-script', editor.value);
  notify(`${file.name} loaded into the editor`);
  event.target.value = '';
});
