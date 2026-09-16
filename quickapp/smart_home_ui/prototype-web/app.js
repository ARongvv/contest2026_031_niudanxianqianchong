const state = {
  currentPage: 'home',
  lightsOn: true,
  curtainOpen: true,
  activeDeviceId: null,
  activeRoom: '全部',
  modelBackend: 'DeepSeek',
  activeSettingsSection: 'model',
  revision: 12,
  scenes: [
    { name: '回家模式', icon: 'home', desc: '温暖灯光 · 新风开启', color: '#c78855' },
    { name: '观影模式', icon: 'television-classic', desc: '调暗灯光 · 合上窗帘', color: '#6b73bd' },
    { name: '睡眠模式', icon: 'moon', desc: '关闭照明 · 安静守护', color: '#7c77aa' },
    { name: '离家模式', icon: 'home-shield', desc: '关闭设备 · 安防布防', color: '#4f9e82' }
  ],
  devices: [
    { id: 'living-light', name: '客厅主灯', room: '客厅', type: 'light', icon: 'bulb', on: true, detail: '亮度 70% · 暖白光', brightness: 70, color: '暖白光' },
    { id: 'ac', name: '客厅空调', room: '客厅', type: 'air', icon: 'air-conditioning', on: true, detail: '26°C · 舒适模式', temperature: 26, mode: '舒适', fan: '自动' },
    { id: 'curtain', name: '客厅窗帘', room: '客厅', type: 'curtain', icon: 'blinds', on: true, detail: '已打开 100%', position: 100 },
    { id: 'speaker', name: '家庭音响', room: '卧室', type: 'media', icon: 'boombox', on: false, detail: '待机中', volume: 38 },
    { id: 'bedside-light', name: '床头灯', room: '卧室', type: 'light', icon: 'lamp', on: true, detail: '亮度 35% · 柔光', brightness: 35, color: '柔光' },
    { id: 'kitchen-light', name: '厨房主灯', room: '厨房', type: 'light', icon: 'lamp-2', on: true, detail: '亮度 85% · 自然光', brightness: 85, color: '自然光' },
    { id: 'bath-fan', name: '卫生间排风', room: '卫生间', type: 'air', icon: 'propeller', on: false, detail: '已关闭 · 自动除湿', mode: '自动', fan: '低风' },
    { id: 'camera', name: '摄像头 G3', room: '客厅', type: 'safe', icon: 'camera', on: true, detail: '在线 · AI 守护已开启', battery: null }
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
  const visibleDevices = state.activeRoom === '全部'
    ? state.devices
    : state.devices.filter((device) => device.room === state.activeRoom);
  $('#device-grid').innerHTML = visibleDevices.map((device) => `
    <button class="device-card" data-open-device="${device.id}">
      <img class="device-icon ${device.type}" src="${iconPath(device.icon)}" alt="" />
      <b>${device.name}</b><small>${device.detail}</small>
      <i class="toggle ${device.on ? 'on' : ''}" aria-hidden="true"></i>
    </button>`).join('');
  const quickLight = $('#quick-light-text');
  const lightSummary = $('#light-summary');
  const curtainLabel = $('#curtain-label');
  if (quickLight) quickLight.textContent = state.lightsOn ? '3 组已开启' : '全部已关闭';
  if (lightSummary) lightSummary.textContent = state.lightsOn ? '照明 3 组已开启' : '照明已全部关闭';
  if (curtainLabel) curtainLabel.textContent = state.curtainOpen ? '已打开' : '已关闭';
  $$('.filter').forEach((node) => node.classList.toggle('active', node.dataset.room === state.activeRoom));
  $$('[data-device-id="living-light"] .toggle').forEach((node) => node.classList.toggle('on', state.lightsOn));
  $$('[data-device-id="curtain"] .toggle').forEach((node) => node.classList.toggle('on', state.curtainOpen));
}

function adjustTemperature(delta) {
  const temperature = $('#ac-temperature');
  const current = Number(temperature.textContent);
  const next = Math.max(16, Math.min(30, current + delta));
  temperature.textContent = next;
  const ac = state.devices.find((device) => device.id === 'ac');
  ac.temperature = next;
  ac.detail = `${next}°C · ${ac.mode}模式`;
  state.revision += 1;
  if (state.activeDeviceId === 'ac') renderDeviceDetail();
  showToast(`空调设定为 ${next}°C · 等待真实设备确认`);
}

function deviceById(id) {
  return state.devices.find((device) => device.id === id);
}

function detailOptions(items, selected, property) {
  return `<div class="detail-options">${items.map((item) => `<button class="detail-option ${item === selected ? 'active' : ''}" data-action="set-device-option" data-property="${property}" data-value="${item}">${item}</button>`).join('')}</div>`;
}

function renderDeviceDetail() {
  const device = deviceById(state.activeDeviceId);
  if (!device) return;
  $('#device-sheet-title').innerHTML = `<b>${device.name}</b><small>${device.room} · ${device.on ? '在线' : '已关闭'}</small>`;
  let primary = '';
  let controls = '';

  if (device.id === 'ac') {
    primary = `<div class="device-primary"><div><p>当前设定温度</p><strong>${device.temperature}°C</strong></div><div class="detail-stepper"><button data-action="detail-temperature" data-delta="-1">−</button><button data-action="detail-temperature" data-delta="1">＋</button></div></div>`;
    controls = `<section class="detail-section"><h3>运行模式</h3>${detailOptions(['舒适', '制冷', '除湿', '送风'], device.mode, 'mode')}</section><section class="detail-section"><h3>风速</h3>${detailOptions(['自动', '低风', '中风', '高风'], device.fan, 'fan')}</section><div class="detail-row"><span>摆风</span><span>上下自动 ›</span></div><div class="detail-row"><span>睡眠定时</span><span>未设置 ›</span></div>`;
  } else if (device.type === 'air') {
    primary = `<div class="device-primary"><div><p>运行状态</p><strong>${device.on ? '运行中' : '已关闭'}</strong></div><button class="detail-switch" data-action="toggle-device" data-device-id="${device.id}"><i class="toggle ${device.on ? 'on' : ''}"></i>${device.on ? '已开启' : '已关闭'}</button></div>`;
    controls = `<section class="detail-section"><h3>运行模式</h3>${detailOptions(['自动', '换气', '除湿', '强劲'], device.mode, 'mode')}</section><section class="detail-section"><h3>风速</h3>${detailOptions(['低风', '中风', '高风'], device.fan, 'fan')}</section><div class="detail-row"><span>定时关闭</span><span>未设置 ›</span></div>`;
  } else if (device.type === 'light') {
    primary = `<div class="device-primary"><div><p>亮度</p><strong>${device.brightness}%</strong></div><button class="detail-switch" data-action="toggle-device" data-device-id="${device.id}"><i class="toggle ${device.on ? 'on' : ''}"></i>${device.on ? '已开启' : '已关闭'}</button></div>`;
    controls = `<section class="detail-section"><h3>亮度调节</h3><input class="detail-slider" type="range" min="1" max="100" value="${device.brightness}" data-property="brightness" /></section><section class="detail-section"><h3>灯光效果</h3>${detailOptions(['暖白光', '自然光', '冷白光', '夜灯'], device.color, 'color')}</section><div class="detail-row"><span>延时关灯</span><span>未设置 ›</span></div>`;
  } else if (device.id === 'curtain') {
    primary = `<div class="device-primary"><div><p>开合度</p><strong>${device.position}%</strong></div><button class="detail-switch" data-action="toggle-device" data-device-id="curtain"><i class="toggle ${device.on ? 'on' : ''}"></i>${device.on ? '已打开' : '已关闭'}</button></div>`;
    controls = `<section class="detail-section"><h3>精确开合</h3><input class="detail-slider" type="range" min="0" max="100" value="${device.position}" data-property="position" /></section><section class="detail-section"><h3>快捷操作</h3>${detailOptions(['全开', '半开', '关闭'], device.position === 100 ? '全开' : device.position === 0 ? '关闭' : '半开', 'curtainPreset')}</section><div class="detail-row"><span>日出日落联动</span><span>未设置 ›</span></div>`;
  } else if (device.id === 'camera') {
    primary = `<div class="camera-preview-placeholder"><b>客厅 · 实时预览</b><small>Native Monitor / CameraPreview 保留帧缓冲</small><button data-action="native-preview">全屏预览</button></div>`;
    controls = `<section class="detail-section"><h3>安防能力</h3>${detailOptions(['AI 守护', '隐私模式', '移动侦测'], 'AI 守护', 'cameraMode')}</section><div class="detail-row"><span>录像与回放</span><span>最近 7 天 ›</span></div><div class="detail-row"><span>画面叠加</span><span>显示 AI 事件 ›</span></div>`;
  } else if (device.id === 'door') {
    primary = `<div class="device-primary"><div><p>门锁状态</p><strong>${device.on ? '已上锁' : '已解锁'}</strong></div><button data-action="toggle-device" data-device-id="door">${device.on ? '临时解锁' : '立即上锁'}</button></div>`;
    controls = `<div class="detail-row"><span>剩余电量</span><span>${device.battery}%</span></div><div class="detail-row"><span>临时密码</span><span>管理 ›</span></div><div class="detail-row"><span>开锁记录</span><span>今天 2 条 ›</span></div>`;
  } else {
    primary = `<div class="device-primary"><div><p>家庭音响</p><strong>${device.on ? '正在播放' : '待机中'}</strong></div><button data-action="toggle-device" data-device-id="speaker">${device.on ? '暂停播放' : '开始播放'}</button></div>`;
    controls = `<section class="detail-section"><h3>音量</h3><input class="detail-slider" type="range" min="0" max="100" value="${device.volume}" data-property="volume" /></section><div class="detail-row"><span>播放队列</span><span>Last Dance ›</span></div><div class="detail-row"><span>定时关闭</span><span>未设置 ›</span></div>`;
  }
  $('#device-sheet-body').innerHTML = `${primary}${controls}<button class="outline-button" data-action="open-device-settings">设备设置</button>`;
}

function openDevice(id) {
  if (!deviceById(id)) return;
  state.activeDeviceId = id;
  renderDeviceDetail();
  $('#device-sheet').classList.add('open');
  $('#device-sheet').setAttribute('aria-hidden', 'false');
}

function closeDevice() {
  $('#device-sheet').classList.remove('open');
  $('#device-sheet').setAttribute('aria-hidden', 'true');
  state.activeDeviceId = null;
}

function updateModelSummary() {
  const summary = $('#settings-model-summary');
  if (summary) summary.textContent = `${state.modelBackend} · 就绪`;
}

function selectSettingsSection(section) {
  state.activeSettingsSection = section;
  $$('.settings-category').forEach((node) => node.classList.toggle('active', node.dataset.section === section));
  $$('[data-settings-group]').forEach((node) => node.classList.toggle('active', node.dataset.settingsGroup === section));
}

function switchPage(page) {
  state.currentPage = page;
  $$('.page').forEach((node) => node.classList.toggle('active', node.dataset.page === page));
  $$('.nav-item').forEach((node) => node.classList.toggle('active', node.dataset.nav === page));
  $('#agent-sheet').classList.remove('open');
  closeDevice();
}

function toggleDevice(id) {
  if (id === 'living-light') {
    state.lightsOn = !state.lightsOn;
    state.devices.filter((device) => device.type === 'light').forEach((device) => { device.on = state.lightsOn; });
    showToast(state.lightsOn ? '全屋灯光已打开 · 等待真实状态确认' : '全屋灯光已关闭 · 等待真实状态确认');
  } else if (id === 'curtain') {
    state.curtainOpen = !state.curtainOpen;
    state.devices.find((device) => device.id === 'curtain').detail = state.curtainOpen ? '已打开 100%' : '已关闭';
    showToast(state.curtainOpen ? '窗帘已打开' : '窗帘已关闭');
  } else {
    const device = deviceById(id);
    if (device) {
      device.on = !device.on;
      if (id === 'door') device.detail = device.on ? '已上锁 · 电量 82%' : '已解锁 · 请注意安全';
      showToast(`${device.name}${device.on ? '已开启' : '已关闭'}`);
    }
  }
  state.revision += 1;
  renderDevices();
  if (state.activeDeviceId === id) renderDeviceDetail();
}

function runScene(name) {
  if (name === '观影模式') { state.lightsOn = true; state.curtainOpen = false; }
  if (name === '睡眠模式' || name === '离家模式') { state.lightsOn = false; }
  if (name === '回家模式') { state.lightsOn = true; state.curtainOpen = true; }
  state.devices.find((device) => device.id === 'living-light').on = state.lightsOn;
  state.devices.filter((device) => device.type === 'light').forEach((device) => { device.on = state.lightsOn; });
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
  const roomFilter = event.target.closest('[data-room]');
  const action = event.target.closest('[data-action]');
  if (nav) switchPage(nav.dataset.nav);
  if (scene) runScene(scene.dataset.scene);
  if (roomFilter) { state.activeRoom = roomFilter.dataset.room; renderDevices(); return; }
  if (action?.dataset.action === 'ac-down') { adjustTemperature(-1); return; }
  if (action?.dataset.action === 'ac-up') { adjustTemperature(1); return; }
  if (action?.dataset.action === 'detail-temperature') { adjustTemperature(Number(action.dataset.delta)); return; }
  if (action?.dataset.action === 'toggle-device') { toggleDevice(action.dataset.deviceId); return; }
  if (action?.dataset.action === 'close-device') { closeDevice(); return; }
  if (action?.dataset.action === 'favorite-device') { showToast('已加入常用设备'); return; }
  if (action?.dataset.action === 'native-preview') { showToast('真机将从 QuickApp 页面交接至 Native Monitor'); return; }
  if (action?.dataset.action === 'open-device-settings') { showToast('设备设置将由原生服务提供参数页'); return; }
  if (action?.dataset.action === 'select-settings-section') {
    selectSettingsSection(action.dataset.section);
    return;
  }
  if (action?.dataset.action === 'test-model') { showToast(`${state.modelBackend} 连接测试成功（网页演示）`); return; }
  if (action?.dataset.action === 'reload-skills') { showToast('Skills 已重新加载：5 个有效，0 个跳过（网页演示）'); return; }
  if (action?.dataset.action === 'open-policy') { showToast('高风险控制由原生 tool guard 和管理员确认策略保护'); return; }
  if (action?.dataset.action === 'open-diagnostics') { showToast('诊断页将展示原生服务采集的只读健康数据'); return; }
  if (action?.dataset.action === 'set-device-option') {
    const device = deviceById(state.activeDeviceId);
    if (!device) return;
    const { property, value } = action.dataset;
    if (property === 'curtainPreset') {
      device.position = value === '全开' ? 100 : value === '关闭' ? 0 : 50;
      device.on = device.position > 0;
      state.curtainOpen = device.on;
      device.detail = device.position === 100 ? '已打开 100%' : `已打开 ${device.position}%`;
    } else {
      device[property] = value;
      if (property === 'mode' && device.id === 'ac') device.detail = `${device.temperature}°C · ${value}模式`;
      if (property === 'color' && device.type === 'light') device.detail = `亮度 ${device.brightness}% · ${value}`;
    }
    state.revision += 1;
    renderDevices();
    renderDeviceDetail();
    showToast(`${device.name}已更新 · 等待真实状态确认`);
    return;
  }
  const openDeviceButton = event.target.closest('[data-open-device]');
  if (openDeviceButton) { openDevice(openDeviceButton.dataset.openDevice); return; }
  if (action?.dataset.action === 'open-agent') { $('#agent-sheet').classList.add('open'); $('#agent-sheet').setAttribute('aria-hidden', 'false'); }
  if (action?.dataset.action === 'close-agent') { $('#agent-sheet').classList.remove('open'); $('#agent-sheet').setAttribute('aria-hidden', 'true'); }
  if (action?.dataset.action === 'monitor') showToast('真机阶段将页面级交接至 Native Monitor');
  if (action?.dataset.action === 'trigger-alert') showToast('AI 提醒：客厅检测到人员活动（网页演示）');
  const prompt = event.target.closest('[data-prompt]');
  if (prompt) askAgent(prompt.dataset.prompt);
});

document.addEventListener('input', (event) => {
  const control = event.target.closest('[data-property]');
  const device = deviceById(state.activeDeviceId);
  if (!control || !device) return;
  const value = Number(control.value);
  if (control.dataset.property === 'brightness') {
    device.brightness = value;
    device.detail = `亮度 ${value}% · ${device.color}`;
  } else if (control.dataset.property === 'position') {
    device.position = value;
    device.on = value > 0;
    state.curtainOpen = device.on;
    device.detail = value === 0 ? '已关闭' : `已打开 ${value}%`;
  } else if (control.dataset.property === 'volume') {
    device.volume = value;
    device.detail = device.on ? `正在播放 · 音量 ${value}%` : `待机中 · 音量 ${value}%`;
  } else return;
  state.revision += 1;
  renderDevices();
  renderDeviceDetail();
});

document.addEventListener('change', (event) => {
  if (event.target.id !== 'model-backend') return;
  state.modelBackend = event.target.value;
  const model = $('#model-name');
  if (model) {
    const options = state.modelBackend === 'Qwen'
      ? ['qwen-turbo', 'qwen-plus']
      : state.modelBackend === 'Custom'
        ? ['custom-model']
        : ['deepseek-v4-flash', 'deepseek-chat'];
    model.innerHTML = options.map((name) => `<option>${name}</option>`).join('');
  }
  updateModelSummary();
  showToast(`已选择 ${state.modelBackend}；模型配置将在下一次对话生效`);
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
updateModelSummary();
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
