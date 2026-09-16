const state = {
  currentPage: 'home',
  lightsOn: true,
  curtainOpen: true,
  revision: 12,
  scenes: [
    { name: '回家模式', icon: 'home', desc: '温暖灯光 · 新风开启', color: '#c78855' },
    { name: '观影模式', icon: 'television-classic', desc: '调暗灯光 · 合上窗帘', color: '#6b73bd' },
    { name: '睡眠模式', icon: 'moon', desc: '关闭照明 · 安静守护', color: '#7c77aa' },
    { name: '离家模式', icon: 'home-shield', desc: '关闭设备 · 安防布防', color: '#4f9e82' }
  ],
  devices: [
    { id: 'living-light', name: '客厅主灯', type: 'light', icon: 'bulb', on: true, detail: '亮度 70% · 暖白光' },
    { id: 'desk-light', name: '阅读灯', type: 'light', icon: 'lamp', on: true, detail: '亮度 35% · 柔光' },
    { id: 'ac', name: '客厅空调', type: 'air', icon: 'air-conditioning', on: true, detail: '26°C · 舒适模式' },
    { id: 'curtain', name: '客厅窗帘', type: 'curtain', icon: 'blinds', on: true, detail: '已打开 100%' },
    { id: 'speaker', name: '家庭音响', type: 'media', icon: 'boombox', on: false, detail: '待机中' },
    { id: 'door', name: '入户门锁', type: 'safe', icon: 'lock', on: true, detail: '已上锁 · 电量 82%' }
  ]
};

const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => [...document.querySelectorAll(selector)];
const iconPath = (name) => `assets/icons/${name}.png`;
let toastTimer;

function showToast(message) {
  const toast = $('#toast');
  toast.textContent = message;
  toast.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => toast.classList.remove('show'), 2200);
}

function renderScenes() {
  const sceneMarkup = state.scenes.slice(0, 1).map((scene) => `
    <button class="scene-card" data-scene="${scene.name}" style="--scene-color:${scene.color}">
      <img class="scene-icon" src="${iconPath(scene.icon)}" alt="" /><b>${scene.name}</b><small>${scene.desc}</small>
    </button>`).join('');
  $('#home-scenes').innerHTML = sceneMarkup;
  $('#scene-page-grid').innerHTML = state.scenes.map((scene) => `
    <button class="scene-page-card" data-scene="${scene.name}" style="--scene-color:${scene.color}">
      <img class="scene-page-icon" src="${iconPath(scene.icon)}" alt="" /><b>${scene.name}</b><small>${scene.desc}</small>
    </button>`).join('');
}

function renderDevices() {
  $('#device-grid').innerHTML = state.devices.map((device) => `
    <button class="device-card" data-device="${device.id}">
      <img class="device-icon ${device.type}" src="${iconPath(device.icon)}" alt="" />
      <b>${device.name}</b><small>${device.detail}</small>
      <i class="toggle ${device.on ? 'on' : ''}"></i>
    </button>`).join('');
  const quickLight = $('#quick-light-text');
  const lightSummary = $('#light-summary');
  const curtainLabel = $('#curtain-label');
  if (quickLight) quickLight.textContent = state.lightsOn ? '3 组已开启' : '全部已关闭';
  if (lightSummary) lightSummary.textContent = state.lightsOn ? '照明 3 组已开启' : '照明已全部关闭';
  if (curtainLabel) curtainLabel.textContent = state.curtainOpen ? '已打开' : '已关闭';
  $$('[data-device="living-light"] .toggle').forEach((node) => node.classList.toggle('on', state.lightsOn));
  $$('[data-device="curtain"] .toggle').forEach((node) => node.classList.toggle('on', state.curtainOpen));
}

function adjustTemperature(delta) {
  const temperature = $('#ac-temperature');
  const current = Number(temperature.textContent);
  const next = Math.max(16, Math.min(30, current + delta));
  temperature.textContent = next;
  state.devices.find((device) => device.id === 'ac').detail = `${next}°C · 舒适模式`;
  state.revision += 1;
  showToast(`空调设定为 ${next}°C · 等待真实设备确认`);
}

function switchPage(page) {
  state.currentPage = page;
  $$('.page').forEach((node) => node.classList.toggle('active', node.dataset.page === page));
  $$('.nav-item').forEach((node) => node.classList.toggle('active', node.dataset.nav === page));
  $('#agent-sheet').classList.remove('open');
}

function toggleDevice(id) {
  if (id === 'living-light') {
    state.lightsOn = !state.lightsOn;
    state.devices.filter((device) => ['living-light', 'desk-light'].includes(device.id)).forEach((device) => { device.on = state.lightsOn; });
    showToast(state.lightsOn ? '全屋灯光已打开 · 等待真实状态确认' : '全屋灯光已关闭 · 等待真实状态确认');
  } else if (id === 'curtain') {
    state.curtainOpen = !state.curtainOpen;
    state.devices.find((device) => device.id === 'curtain').detail = state.curtainOpen ? '已打开 100%' : '已关闭';
    showToast(state.curtainOpen ? '窗帘已打开' : '窗帘已关闭');
  } else {
    const device = state.devices.find((item) => item.id === id);
    if (device) {
      device.on = !device.on;
      showToast(`${device.name}${device.on ? '已开启' : '已关闭'}`);
    }
  }
  state.revision += 1;
  renderDevices();
}

function runScene(name) {
  if (name === '观影模式') { state.lightsOn = true; state.curtainOpen = false; }
  if (name === '睡眠模式' || name === '离家模式') { state.lightsOn = false; }
  if (name === '回家模式') { state.lightsOn = true; state.curtainOpen = true; }
  state.devices.find((device) => device.id === 'living-light').on = state.lightsOn;
  state.devices.find((device) => device.id === 'desk-light').on = state.lightsOn;
  state.devices.find((device) => device.id === 'curtain').detail = state.curtainOpen ? '已打开 100%' : '已关闭';
  state.revision += 1;
  renderDevices();
  showToast(`“${name}”已执行 · revision ${state.revision}`);
}

function askAgent(prompt) {
  const text = prompt.trim();
  if (!text) return;
  const answer = $('#agent-answer');
  answer.textContent = '正在通过 system.smarthome 查询家庭状态…';
  setTimeout(() => {
    if (text.includes('观影')) { runScene('观影模式'); answer.textContent = '已执行观影模式：客厅灯光已调暗，窗帘已关闭。'; }
    else if (text.includes('异常')) answer.textContent = '当前门窗均已关闭，客厅摄像头在线，未发现需要处理的异常。';
    else if (text.includes('客厅') || text.includes('怎么样')) answer.textContent = `客厅温度 26°C，${state.lightsOn ? '主灯已开启' : '灯光已关闭'}，窗帘${state.curtainOpen ? '已打开' : '已关闭'}。`;
    else answer.textContent = '这是网页原型中的异步 Agent 占位回复。真机将由 cAGENT 经 Native Feature 返回结果。';
  }, 520);
}

document.addEventListener('click', (event) => {
  const nav = event.target.closest('[data-nav]');
  const scene = event.target.closest('[data-scene]');
  const device = event.target.closest('[data-device]');
  const action = event.target.closest('[data-action]');
  if (nav) switchPage(nav.dataset.nav);
  if (scene) runScene(scene.dataset.scene);
  if (action?.dataset.action === 'ac-down') { adjustTemperature(-1); return; }
  if (action?.dataset.action === 'ac-up') { adjustTemperature(1); return; }
  if (device) toggleDevice(device.dataset.device);
  if (action?.dataset.action === 'open-agent') { $('#agent-sheet').classList.add('open'); $('#agent-sheet').setAttribute('aria-hidden', 'false'); }
  if (action?.dataset.action === 'close-agent') { $('#agent-sheet').classList.remove('open'); $('#agent-sheet').setAttribute('aria-hidden', 'true'); }
  if (action?.dataset.action === 'monitor') showToast('真机阶段将页面级交接至 Native Monitor');
  if (action?.dataset.action === 'trigger-alert') showToast('AI 提醒：客厅检测到人员活动（网页演示）');
  const prompt = event.target.closest('[data-prompt]');
  if (prompt) askAgent(prompt.dataset.prompt);
});

$('#agent-form').addEventListener('submit', (event) => { event.preventDefault(); askAgent($('#agent-input').value); $('#agent-input').value = ''; });

function updateClock() {
  const now = new Date();
  const clock = now.toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit', hour12: false });
  const date = now.toLocaleDateString('zh-CN', { month: 'long', day: 'numeric', weekday: 'long' });
  $('#clock').textContent = clock;
  $('#top-clock').textContent = clock;
  $('#screensaver-clock').textContent = clock;
  $('#date-label').textContent = date;
  $('#screensaver-date').textContent = date;
  const hour = now.getHours();
  const period = $('#period');
  if (period) period.textContent = hour < 12 ? '上午' : hour < 18 ? '下午' : '晚上';
}

renderScenes();
renderDevices();
updateClock();
setInterval(updateClock, 30000);

let swipeStartY = null;
function enterHome() {
  const screensaver = $('#screensaver');
  if (!screensaver.classList.contains('hidden')) {
    screensaver.classList.add('hidden');
    showToast('欢迎回家');
  }
}

$('#screensaver').addEventListener('pointerdown', (event) => { swipeStartY = event.clientY; });
$('#screensaver').addEventListener('pointerup', (event) => {
  if (swipeStartY !== null && swipeStartY - event.clientY >= 46) enterHome();
  swipeStartY = null;
});
$('#screensaver').addEventListener('pointercancel', () => { swipeStartY = null; });
window.addEventListener('keydown', (event) => {
  if (event.key === 'ArrowUp' || event.key === ' ') enterHome();
});
