/*
 * Web page for device selection and voice effects.
 * Minimal single-threaded HTTP server: pick ALSA capture/playback,
 * start/stop audio, choose presets and adjust effects live.
 */

#include "web.h"

#include "engine.h"
#include "presets.h"
#include "settings.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <string>

namespace {

const char kIndexHtml[] = R"HTML(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>UVC Voice Changer</title>
<style>
body{font-family:system-ui,sans-serif;max-width:560px;margin:1.5em auto;padding:0 1em;background:#111;color:#eee}
h1{font-size:1.3em;margin-bottom:.2em}
p.sub{margin-top:0;color:#aaa}
label{display:block;margin-top:1em;font-weight:600}
select,button{font-size:1em;width:100%;padding:.6em;margin-top:.3em;border-radius:6px}
label.adv{font-weight:normal;color:#aaa;font-size:.9em;margin-top:1.2em}
label.adv input{width:auto;margin-right:.4em}
.row{display:flex;gap:.5em;margin-top:1.2em}
.row button{flex:1}
#status{margin-top:1.2em;padding:.8em;border-radius:6px;background:#222;white-space:pre-wrap}
.run{border-left:6px solid #2a2}
.idle{border-left:6px solid #666}
.err{border-left:6px solid #c33}
h2{font-size:1.1em;margin-top:2em;border-top:1px solid #333;padding-top:1em}
fieldset{border:1px solid #333;border-radius:6px;margin-top:1em;padding:.5em .8em}
legend{font-weight:600;padding:0 .3em}
legend input,label.on input{width:auto;margin-right:.4em}
label.on{font-weight:600}
.knob{display:grid;grid-template-columns:8.5em 1fr 3.5em;align-items:center;gap:.5em;margin:.4em 0}
.knob input{width:100%}
.val{text-align:right;color:#aaa;font-variant-numeric:tabular-nums}
fieldset.off .knob{opacity:.4}
label.opt{font-weight:normal;margin-top:.9em}
label.opt input{width:auto;margin-right:.4em}
#startupmsg{color:#aaa;font-size:.9em;margin-top:.2em}
#fxmsg{margin-top:.6em;min-height:1.2em;color:#aaa;font-size:.9em}
#fxmsg.err{color:#f66;border:0}
#active{margin-top:.6em;padding:.8em;border-radius:6px;background:#1d2b1d;border-left:6px solid #2a2;font-size:1.3em;font-weight:700}
#active.custom{background:#2b2615;border-left-color:#c90}
#active small{display:block;font-size:.65em;font-weight:normal;color:#aaa}
.voices{display:grid;grid-template-columns:1fr 1fr;gap:.5em;margin-top:.8em}
.voices button{padding:1em .5em;font-size:1.1em;background:#222;color:#eee;border:2px solid #444;margin:0}
.voices button.active{background:#2a5a2a;border-color:#6d6;color:#fff;font-weight:700}
.ab{display:flex;gap:.5em;margin-top:.3em}
.ab button{flex:1;background:#222;color:#eee;border:2px solid #444}
.ab button.active{border-color:#6d6;font-weight:700}
details{margin-top:2em;border-top:1px solid #333;padding-top:1em}
summary{cursor:pointer;font-weight:600;color:#aaa}
h3{font-size:1em;margin:1.5em 0 .2em;color:#aaa}
.knob.sub{grid-template-columns:8.5em 1fr;color:#aaa}
.knob.sub input{width:auto;justify-self:start}
</style>
</head>
<body>
<h1>UVC Voice Changer</h1>
<p class="sub">Stereo &middot; 48 kHz &middot; 16-bit &middot; 2 ch</p>

<div id="active">Loading&hellip;</div>
<div class="voices" id="voices"></div>
<label for="more">More voices</label>
<select id="more"></select>
<label class="opt"><input type="checkbox" id="startup">Load this voice at startup</label>
<div id="startupmsg"></div>

<h2>Audio</h2>
<label for="input">Input device</label>
<select id="input"></select>
<label for="output">Output device</label>
<select id="output"></select>
<div class="row">
<button id="refresh">Refresh devices</button>
<button id="start">Start audio</button>
<button id="stop">Stop audio</button>
</div>
<div id="status" class="idle">Loading&hellip;</div>
<label class="opt"><input type="checkbox" id="autostart">Start audio automatically (at boot, and after the USB audio reconnects)</label>
<label class="adv"><input type="checkbox" id="all">Show all ALSA device names (troubleshooting)</label>

<details id="advanced">
<summary>Advanced: DSP tuning (not needed for normal use)</summary>
<h3>A/B compare</h3>
<div class="ab">
<button id="ab0" data-mode="0">Bypass</button>
<button id="ab1" data-mode="1">Classic DSP</button>
<button id="ab2" data-mode="2">Character DSP</button>
</div>
<label for="preset">Preset</label>
<select id="preset"></select>
<div class="row">
<button id="save">Save</button>
<button id="saveas">Save as new&hellip;</button>
<button id="reset">Restore defaults</button>
</div>
<div id="fxmsg"></div>
<label class="on"><input type="checkbox" id="enabled">Effects on</label>
<div class="knob"><span>Volume dB</span><input type="range" id="volume_db" aria-label="Volume dB" min="-24" max="12" step="0.5"><span class="val" id="volume_db_v"></span></div>
<div id="fx"></div>
</details>
<script>
const $ = id => document.getElementById(id);

function show(text, cls) {
  const e = $('status');
  e.textContent = text;
  e.className = cls;
}

function fill(sel, items, want) {
  const keep = sel.value || want;
  sel.innerHTML = '';
  for (const d of items) {
    const o = document.createElement('option');
    o.value = d.id;
    o.textContent = d.label;
    sel.appendChild(o);
  }
  if ([...sel.options].some(o => o.value === keep)) sel.value = keep;
}

function render(s) {
  for (const id of ['input', 'output', 'refresh', 'start']) $(id).disabled = s.running;
  $('stop').disabled = !s.running;
  if (s.running) {
    show('Running: ' + s.input + ' \u2192 ' + s.output + '\nperiods = ' + s.periods, 'run');
  } else if (s.error) {
    show((s.status ? s.status + '\n' : '') + s.error, 'err');
  } else {
    show(s.status || 'Stopped. Pick devices, then Start audio.', 'idle');
  }
}

async function api(path, opts) {
  const r = await fetch(path, opts);
  return r.json();
}

async function refresh() {
  const s = await api('/api/status');
  const d = await api('/api/devices' + ($('all').checked ? '?all=1' : ''));
  fill($('input'), d.capture || [], s.input);
  fill($('output'), d.playback || [], s.output);
  render(s);
  if (d.error) {
    show(d.error, 'err');
  } else if (!s.running && (!d.capture.length || !d.playback.length)) {
    show('No USB audio ' + (!d.capture.length ? 'input' : 'output') +
         ' found. Plug it in, then tap Refresh devices.', 'err');
  }
}

async function post(path, body) {
  render(await api(path, {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: body
  }));
}

$('refresh').onclick = refresh;
$('all').onchange = refresh;
$('start').onclick = () => {
  show('Starting\u2026', 'idle');
  post('/api/start', 'input=' + encodeURIComponent($('input').value) +
                     '&output=' + encodeURIComponent($('output').value));
};
$('stop').onclick = () => post('/api/stop', '');

setInterval(async () => {
  try {
    render(await api('/api/status'));
  } catch (e) {
    show('Lost connection to the Pi', 'err');
  }
}, 1000);

const FX = [
  {title: 'Pitch', on: 'pitch_on', knobs: [
    ['pitch_semitones', 'Semitones', -12, 12, 0.5]]},
  {title: 'High-pass (cut bass)', on: 'hp_on', knobs: [
    ['hp_freq', 'Frequency Hz', 20, 2000, 10],
    ['hp_cascade', 'Steepness', 1, 4, 1]]},
  {title: 'Low-pass (cut treble)', on: 'lp_on', knobs: [
    ['lp_freq', 'Frequency Hz', 1000, 12000, 100],
    ['lp_cascade', 'Steepness', 1, 4, 1]]},
  {title: 'Presence peak', on: 'peak_on', knobs: [
    ['peak_freq', 'Frequency Hz', 200, 8000, 50],
    ['peak_q', 'Width (Q)', 0.3, 8, 0.1],
    ['peak_gain_db', 'Boost dB', -12, 18, 0.5]]},
  {title: 'Ring modulator (robot buzz)', on: 'ring_on', knobs: [
    ['ring_freq', 'Frequency Hz', 5, 1000, 5],
    ['ring_mix', 'Mix', 0, 1, 0.05]]},
  {title: 'Metal / cavity / echo', on: 'comb_on', knobs: [
    ['comb_ms', 'Delay ms', 0.2, 300, 0.1],
    ['comb_feedback', 'Feedback', 0, 0.9, 0.05],
    ['comb_damp', 'Damping', 0, 0.9, 0.05],
    ['comb_mix', 'Mix', 0, 1, 0.05]]},
  {title: 'Clipping (grit)', on: 'clip_on', knobs: [
    ['clip_factor', 'Amount', 1, 10, 1]]},
  {title: 'Character stages (master)', on: 'character_on', knobs: [
    ['char_mix', 'Wet mix', 0, 1, 0.05]]},
  {title: 'Vocal character (formant approximation)', on: 'vc_on', knobs: [
    ['formant_scale', 'Size scale', 0.6, 1.6, 0.01],
    ['formant_db', 'Formant dB', -12, 12, 0.5],
    ['formant_q', 'Formant Q', 0.5, 10, 0.1],
    ['nasal_db', 'Nasal dB', -12, 12, 0.5],
    ['tilt_db', 'Tilt dB', -12, 12, 0.5]]},
  {title: 'Compressor', on: 'comp_on', knobs: [
    ['comp_threshold_db', 'Threshold dB', -60, 0, 1],
    ['comp_ratio', 'Ratio', 1, 20, 0.5],
    ['comp_attack_ms', 'Attack ms', 0.5, 100, 0.5],
    ['comp_release_ms', 'Release ms', 5, 1000, 5],
    ['comp_makeup_db', 'Makeup dB', -12, 24, 0.5]]},
  {title: 'Resonators (wet)', on: 'res_on', knobs: [1, 2, 3, 4].flatMap(i => [
    ['res' + i + '_on', 'Band ' + i + ' on', 'bool'],
    ['res' + i + '_freq', 'Band ' + i + ' Hz', 50, 10000, 10],
    ['res' + i + '_q', 'Band ' + i + ' Q', 0.3, 20, 0.1],
    ['res' + i + '_gain_db', 'Band ' + i + ' dB', -18, 18, 0.5]])},
  {title: 'Saturation (wet)', on: 'sat_on', knobs: [
    ['sat_drive_db', 'Drive dB', 0, 36, 0.5],
    ['sat_bias', 'Asymmetry', -0.5, 0.5, 0.01],
    ['sat_mix', 'Mix', 0, 1, 0.05],
    ['sat_out_db', 'Output dB', -24, 12, 0.5]]},
  {title: 'Helmet / comms (wet)', on: 'helmet_on', knobs: [
    ['helmet_low_hz', 'Low cut Hz', 50, 2000, 10],
    ['helmet_high_hz', 'High cut Hz', 1000, 12000, 50],
    ['helmet_reflect_ms', 'Reflection ms', 0.1, 29, 0.1],
    ['helmet_reflect_fb', 'Refl. feedback', 0, 0.9, 0.05],
    ['helmet_reflect_mix', 'Refl. mix', 0, 1, 0.05],
    ['helmet_am_hz', 'AM Hz', 0.5, 500, 0.5],
    ['helmet_am_depth', 'AM depth', 0, 1, 0.01]]},
  {title: 'Final EQ', on: 'feq_on', knobs: [
    ['feq_low_hz', 'Low shelf Hz', 40, 1000, 10],
    ['feq_low_db', 'Low dB', -12, 12, 0.5],
    ['feq_high_hz', 'High shelf Hz', 1000, 16000, 100],
    ['feq_high_db', 'High dB', -12, 12, 0.5]]},
  {title: 'Limiter (safety)', on: 'lim_on', knobs: [
    ['lim_ceiling_db', 'Ceiling dB', -12, 0, 0.5],
    ['lim_release_ms', 'Release ms', 5, 1000, 5]]},
];
const KEYS = ['enabled', 'volume_db'];
let sendTimer = null;

function buildFx() {
  for (const s of FX) {
    const box = document.createElement('fieldset');
    box.id = s.on + '_box';
    const legend = document.createElement('legend');
    const cb = document.createElement('input');
    cb.type = 'checkbox';
    cb.id = s.on;
    cb.onchange = fxChanged;
    cb.setAttribute('aria-label', s.title);
    legend.append(cb, s.title);
    box.appendChild(legend);
    KEYS.push(s.on);
    for (const [key, label, min, max, step] of s.knobs) {
      const row = document.createElement('div');
      row.className = 'knob';
      const name = document.createElement('span');
      name.textContent = label;
      if (min === 'bool') {
        row.className = 'knob sub';
        const c = document.createElement('input');
        c.type = 'checkbox';
        c.id = key;
        c.setAttribute('aria-label', s.title + ' ' + label);
        c.onchange = fxChanged;
        row.append(name, c);
        box.appendChild(row);
        KEYS.push(key);
        continue;
      }
      const r = document.createElement('input');
      Object.assign(r, {type: 'range', id: key, min: min, max: max, step: step});
      r.setAttribute('aria-label', s.title + ' ' + label);
      r.oninput = fxChanged;
      const v = document.createElement('span');
      v.className = 'val';
      v.id = key + '_v';
      row.append(name, r, v);
      box.appendChild(row);
      KEYS.push(key);
    }
    $('fx').appendChild(box);
  }
  $('enabled').onchange = fxChanged;
  $('volume_db').oninput = fxChanged;
}

function updateLabels() {
  for (const k of KEYS) {
    const el = $(k);
    if (el.type === 'range') $(k + '_v').textContent = el.value;
  }
  for (const s of FX) $(s.on + '_box').className = $(s.on).checked ? '' : 'off';
}

let presets = [];
let base = '';

function presetById(id) {
  return presets.find(p => p.id === id);
}

function customLabel() {
  const p = presetById(base);
  return p ? 'Custom (from ' + p.name + ')' : 'Custom';
}

function updatePresetButtons() {
  const p = presetById(base);
  $('save').disabled = !p;
  $('save').textContent = p ? 'Save to ' + p.name : 'Save';
  $('reset').disabled = !p;
  $('reset').textContent = p && !p.builtin ? 'Delete preset' : 'Restore defaults';
}

function showFx(d) {
  presets = d.presets;
  base = d.base;
  const sel = $('preset');
  sel.innerHTML = '';
  for (const p of presets.concat([{id: 'custom', name: customLabel()}])) {
    const o = document.createElement('option');
    o.value = p.id;
    o.textContent = p.name + (p.modified ? ' (edited)' : '');
    sel.appendChild(o);
  }
  sel.value = d.fx.preset;
  for (const k of KEYS) {
    const el = $(k);
    if (el.type === 'checkbox') el.checked = !!d.fx[k];
    else el.value = d.fx[k];
  }
  mode = d.mode;
  renderVoices();
  updateAb();
  updateLabels();
  updatePresetButtons();
  updateStartup();
  $('fxmsg').textContent = d.error || d.message || '';
  $('fxmsg').className = d.error ? 'err' : '';
}

let mode = 2;
const MODE_NAMES = ['Bypass', 'Classic DSP', 'Character DSP'];

function renderVoices() {
  const cur = $('preset').value;
  const box = $('voices');
  box.innerHTML = '';
  for (const p of presets.filter(p => p.voice)) {
    const b = document.createElement('button');
    b.textContent = p.name;
    b.dataset.id = p.id;
    b.onclick = () => selectVoice(p.id);
    box.appendChild(b);
  }
  const more = $('more');
  more.innerHTML = '';
  more.appendChild(new Option('Choose\u2026', ''));
  for (const p of presets.filter(p => !p.voice)) more.appendChild(new Option(p.name, p.id));
  more.value = presets.some(p => !p.voice && p.id === cur) ? cur : '';
  updateActive();
}

function updateActive() {
  const cur = $('preset').value;
  const p = presetById(cur);
  const a = $('active');
  a.className = p ? '' : 'custom';
  a.textContent = p ? p.name : customLabel();
  const note = [];
  if (!p) note.push('Tuned by hand in Advanced; save it to keep it.');
  if (mode !== 2) note.push('A/B compare is set to ' + MODE_NAMES[mode] + ' (Advanced).');
  if (note.length) {
    const s = document.createElement('small');
    s.textContent = note.join(' ');
    a.appendChild(s);
  }
  for (const b of $('voices').children) {
    b.className = b.dataset.id === cur ? 'active' : '';
    b.setAttribute('aria-pressed', b.dataset.id === cur ? 'true' : 'false');
  }
}

function selectVoice(id) {
  clearTimeout(sendTimer);
  sendTimer = null;
  $('preset').value = id;
  updateActive();
  presetPost('/api/fx', 'preset=' + encodeURIComponent(id));
}

function updateAb() {
  for (let i = 0; i < 3; i++) $('ab' + i).className = i === mode ? 'active' : '';
}

for (let i = 0; i < 3; i++) {
  $('ab' + i).onclick = async () => {
    showFx(await api('/api/mode', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: 'mode=' + i
    }));
  };
}

$('more').onchange = () => {
  if ($('more').value) selectVoice($('more').value);
};

async function presetPost(path, body) {
  showFx(await api(path, {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: body
  }));
  showSettings(await api('/api/settings'));
}

let settings = null;

function updateStartup() {
  if (!settings) return;
  const cur = $('preset').value;
  $('startup').disabled = !cur || cur === 'custom';
  $('startup').checked = cur === settings.startup_preset;
  $('startupmsg').textContent = 'Loads at startup: ' + settings.startup_name;
}

function showSettings(s) {
  settings = s;
  $('autostart').checked = s.autostart;
  updateStartup();
}

async function postSettings(body) {
  const s = await api('/api/settings', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: body
  });
  showSettings(s);
  return s;
}

$('autostart').onchange = async () => {
  const s = await postSettings('autostart=' + ($('autostart').checked ? 1 : 0) +
                               '&input=' + encodeURIComponent($('input').value) +
                               '&output=' + encodeURIComponent($('output').value));
  if (s.error) show(s.error, 'err');
};

$('startup').onchange = async () => {
  const id = $('startup').checked ? $('preset').value : 'clean';
  const s = await postSettings('startup_preset=' + encodeURIComponent(id));
  if (s.error) {
    $('fxmsg').textContent = s.error;
    $('fxmsg').className = 'err';
  }
};

$('save').onclick = async () => {
  const p = presetById(base);
  if (p && confirm('Overwrite "' + p.name + '" with the current settings?')) {
    await flushFx();
    presetPost('/api/preset/save', 'id=' + encodeURIComponent(p.id));
  }
};

$('saveas').onclick = async () => {
  const name = (prompt('Name for the new preset:') || '').trim();
  if (!name) return;
  const existing = presets.find(p => p.name.toLowerCase() === name.toLowerCase());
  if (existing && !confirm('"' + existing.name + '" already exists. Overwrite it?')) return;
  await flushFx();
  presetPost('/api/preset/save', 'name=' + encodeURIComponent(name));
};

$('reset').onclick = () => {
  const p = presetById(base);
  if (!p) return;
  const q = p.builtin
    ? 'Restore "' + p.name + '" to its original settings? Your saved changes to it will be lost.'
    : 'Delete "' + p.name + '"?';
  if (confirm(q)) presetPost('/api/preset/reset', 'id=' + encodeURIComponent(p.id));
};

function fxBody() {
  return KEYS.map(k => {
    const el = $(k);
    return k + '=' + (el.type === 'checkbox' ? (el.checked ? 1 : 0) : encodeURIComponent(el.value));
  }).join('&');
}

function fxChanged() {
  updateLabels();
  $('preset').value = 'custom';
  const custom = $('preset').querySelector('option[value="custom"]');
  if (custom) custom.textContent = customLabel();
  $('fxmsg').textContent = '';
  updateStartup();
  updateActive();
  clearTimeout(sendTimer);
  sendTimer = setTimeout(sendFx, 80);
}

function sendFx() {
  sendTimer = null;
  return api('/api/fx', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: fxBody()
  });
}

async function flushFx() {
  if (sendTimer) {
    clearTimeout(sendTimer);
    await sendFx();
  }
}

$('preset').onchange = () => {
  if ($('preset').value === 'custom') return;
  presetPost('/api/fx', 'preset=' + encodeURIComponent($('preset').value));
};

buildFx();
api('/api/fx').then(showFx);
api('/api/settings').then(showSettings);
refresh();
</script>
</body>
</html>
)HTML";

const size_t kMaxHeaderBytes = 16384;
const size_t kMaxBodyBytes = 8192;

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    return out;
}

std::string device_list_json(const AlsaDeviceList& list) {
    std::string out = "[";
    for (int i = 0; i < list.count; i++) {
        if (i) {
            out += ",";
        }
        out += "{\"id\":\"" + json_escape(list.items[i].id) + "\",\"label\":\"" +
               json_escape(list.items[i].label) + "\"}";
    }
    out += "]";
    return out;
}

bool list_contains(const AlsaDeviceList& list, const std::string& id) {
    for (int i = 0; i < list.count; i++) {
        if (id == list.items[i].id) {
            return true;
        }
    }
    return false;
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string url_decode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') {
            out += ' ';
        } else if (s[i] == '%' && i + 2 < s.size() && hex_value(s[i + 1]) >= 0 &&
                   hex_value(s[i + 2]) >= 0) {
            out += (char)(hex_value(s[i + 1]) * 16 + hex_value(s[i + 2]));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

bool form_find(const std::string& body, const char* key, std::string* value) {
    const std::string want = std::string(key) + "=";
    size_t pos = 0;
    while (pos <= body.size()) {
        size_t end = body.find('&', pos);
        if (end == std::string::npos) {
            end = body.size();
        }
        if (body.compare(pos, want.size(), want) == 0) {
            *value = url_decode(body.substr(pos + want.size(), end - pos - want.size()));
            return true;
        }
        pos = end + 1;
    }
    return false;
}

std::string form_value(const std::string& body, const char* key) {
    std::string value;
    form_find(body, key, &value);
    return value;
}

std::string fx_json(const FxParams& fx) {
    int count = 0;
    const FxField* fields = fx_fields(&count);
    std::string out = "{\"preset\":\"" + json_escape(fx.preset) + "\"";
    for (int i = 0; i < count; i++) {
        const char* base = (const char*)&fx + fields[i].offset;
        char val[32];
        switch (fields[i].kind) {
            case kFxBool:
                snprintf(val, sizeof(val), "%s", *(const bool*)base ? "true" : "false");
                break;
            case kFxInt:
                snprintf(val, sizeof(val), "%d", *(const int*)base);
                break;
            case kFxFloat:
                snprintf(val, sizeof(val), "%g", *(const float*)base);
                break;
        }
        out += std::string(",\"") + fields[i].key + "\":" + val;
    }
    return out + "}";
}

/* Apply any fields present in a form body; missing fields keep their value. */
void fx_from_form(const std::string& body, FxParams* fx) {
    int count = 0;
    const FxField* fields = fx_fields(&count);
    for (int i = 0; i < count; i++) {
        std::string v;
        if (!form_find(body, fields[i].key, &v)) {
            continue;
        }
        char* base = (char*)fx + fields[i].offset;
        switch (fields[i].kind) {
            case kFxBool:
                *(bool*)base = v == "1" || v == "true" || v == "on";
                break;
            case kFxInt:
                *(int*)base = atoi(v.c_str());
                break;
            case kFxFloat:
                *(float*)base = strtof(v.c_str(), nullptr);
                break;
        }
    }
    fx_clamp(fx);
}

struct Request {
    std::string method;
    std::string path;
    std::string query;
    std::string body;
};

bool read_request(int fd, Request* req) {
    std::string buf;
    char tmp[2048];
    size_t header_end;
    while ((header_end = buf.find("\r\n\r\n")) == std::string::npos) {
        if (buf.size() > kMaxHeaderBytes) {
            return false;
        }
        ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) {
            return false;
        }
        buf.append(tmp, (size_t)n);
    }

    std::string head = buf.substr(0, header_end);
    size_t sp1 = head.find(' ');
    size_t sp2 = sp1 == std::string::npos ? std::string::npos : head.find(' ', sp1 + 1);
    if (sp2 == std::string::npos) {
        return false;
    }
    req->method = head.substr(0, sp1);
    req->path = head.substr(sp1 + 1, sp2 - sp1 - 1);
    size_t q = req->path.find('?');
    if (q != std::string::npos) {
        req->query = req->path.substr(q + 1);
        req->path.resize(q);
    }

    std::string lower = head;
    for (char& c : lower) {
        c = (char)tolower((unsigned char)c);
    }
    size_t content_length = 0;
    size_t cl = lower.find("\r\ncontent-length:");
    if (cl != std::string::npos) {
        content_length = strtoul(lower.c_str() + cl + 17, nullptr, 10);
    }
    if (content_length > kMaxBodyBytes) {
        return false;
    }

    req->body = buf.substr(header_end + 4);
    while (req->body.size() < content_length) {
        ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) {
            return false;
        }
        req->body.append(tmp, (size_t)n);
    }
    req->body.resize(content_length);
    return true;
}

void send_all(int fd, const char* data, size_t len) {
    while (len > 0) {
        ssize_t n = send(fd, data, len, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR) {
                continue;
            }
            return;
        }
        data += n;
        len -= (size_t)n;
    }
}

void send_response(int fd, int code, const char* reason, const char* type, const std::string& body) {
    char head[256];
    int n = snprintf(head,
                     sizeof(head),
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Cache-Control: no-store\r\n"
                     "Connection: close\r\n\r\n",
                     code,
                     reason,
                     type,
                     body.size());
    send_all(fd, head, (size_t)n);
    send_all(fd, body.data(), body.size());
}

class WebServer {
public:
    explicit WebServer(const EngineConfig& defaults)
        : cfg_(defaults),
          store_(defaults.presets_path),
          settings_path_(settings_path_for(defaults.presets_path)) {
        cfg_.rate = 48000;
        cfg_.channels = 2;
        settings_.load(settings_path_);
        if (!settings_.input.empty() && !settings_.output.empty()) {
            snprintf(cfg_.input_dev, sizeof(cfg_.input_dev), "%s", settings_.input.c_str());
            snprintf(cfg_.output_dev, sizeof(cfg_.output_dev), "%s", settings_.output.c_str());
        }
        FxParams fx;
        if (store_.get(cfg_.preset, &fx)) {
            engine_.set_fx(fx);
            base_ = cfg_.preset;
        }
        if (settings_.autostart) {
            fprintf(stderr, "Autostart on: starting audio with %s -> %s\n", cfg_.input_dev, cfg_.output_dev);
        }
    }

    /* Called about twice a second: autostart / restart audio if enabled. */
    void tick() {
        if (!settings_.autostart || user_stopped_ || engine_.running()) {
            return;
        }
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec < next_try_) {
            return;
        }
        if (engine_.start(cfg_)) {
            fprintf(stderr, "Audio started automatically.\n");
            retry_sec_ = 2;
            return;
        }
        next_try_ = now.tv_sec + retry_sec_;
        if (retry_sec_ < 30) {
            retry_sec_ *= 2;
        }
    }

    void handle(int fd) {
        Request req;
        if (!read_request(fd, &req)) {
            send_response(fd, 400, "Bad Request", "text/plain", "Bad request\n");
            return;
        }
        const char* json = "application/json";
        if (req.method == "GET" && (req.path == "/" || req.path == "/index.html")) {
            send_response(fd, 200, "OK", "text/html; charset=utf-8", kIndexHtml);
        } else if (req.method == "GET" && req.path == "/api/devices") {
            send_response(fd, 200, "OK", json, devices_json(form_value(req.query, "all") == "1"));
        } else if (req.method == "GET" && req.path == "/api/status") {
            send_response(fd, 200, "OK", json, status_json(""));
        } else if (req.method == "POST" && req.path == "/api/start") {
            send_response(fd, 200, "OK", json, start(req.body));
        } else if (req.method == "POST" && req.path == "/api/stop") {
            user_stopped_ = true;
            engine_.stop();
            send_response(fd, 200, "OK", json, status_json(""));
        } else if (req.method == "GET" && req.path == "/api/settings") {
            send_response(fd, 200, "OK", json, settings_json(""));
        } else if (req.method == "POST" && req.path == "/api/settings") {
            send_response(fd, 200, "OK", json, update_settings(req.body));
        } else if (req.method == "GET" && req.path == "/api/fx") {
            send_response(fd, 200, "OK", json, fx_response());
        } else if (req.method == "POST" && req.path == "/api/fx") {
            update_fx(req.body);
            send_response(fd, 200, "OK", json, fx_response());
        } else if (req.method == "POST" && req.path == "/api/mode") {
            engine_.set_mode(atoi(form_value(req.body, "mode").c_str()));
            send_response(fd, 200, "OK", json, fx_response());
        } else if (req.method == "POST" && req.path == "/api/preset/save") {
            send_response(fd, 200, "OK", json, save_preset(req.body));
        } else if (req.method == "POST" && req.path == "/api/preset/reset") {
            send_response(fd, 200, "OK", json, reset_preset(req.body));
        } else {
            send_response(fd, 404, "Not Found", "text/plain", "Not found\n");
        }
    }

private:
    std::string devices_json(bool all) {
        AlsaDeviceList capture = {};
        AlsaDeviceList playback = {};
        std::string out;
        const int rc_in = all ? alsa_enumerate_all_capture(&capture) : alsa_enumerate_capture(&capture);
        const int rc_out = all ? alsa_enumerate_all_playback(&playback) : alsa_enumerate_playback(&playback);
        if (rc_in < 0 || rc_out < 0) {
            out = "{\"capture\":[],\"playback\":[],\"error\":\"" + json_escape(alsa_last_error()) + "\"}";
        } else {
            out = "{\"capture\":" + device_list_json(capture) + ",\"playback\":" +
                  device_list_json(playback) + "}";
        }
        alsa_device_list_free(&capture);
        alsa_device_list_free(&playback);
        return out;
    }

    std::string status_json(const std::string& override_error) {
        const std::string error = override_error.empty() ? engine_.last_error() : override_error;
        char periods[32];
        snprintf(periods, sizeof(periods), "%llu", engine_.blocks());
        return std::string("{\"running\":") + (engine_.running() ? "true" : "false") +
               ",\"input\":\"" + json_escape(cfg_.input_dev) + "\",\"output\":\"" +
               json_escape(cfg_.output_dev) + "\",\"status\":\"" + json_escape(engine_.status()) +
               "\",\"error\":\"" + json_escape(error) + "\",\"periods\":" + periods + "}";
    }

    std::string fx_response(const std::string& error = "", const std::string& message = "") {
        std::string out = "{\"presets\":[";
        bool first = true;
        for (const PresetInfo& p : store_.list()) {
            if (!first) {
                out += ",";
            }
            first = false;
            out += "{\"id\":\"" + json_escape(p.id) + "\",\"name\":\"" + json_escape(p.name) +
                   "\",\"builtin\":" + (p.builtin ? "true" : "false") +
                   ",\"modified\":" + (p.modified ? "true" : "false") +
                   ",\"voice\":" + (p.voice ? "true" : "false") + "}";
        }
        return out + "],\"mode\":" + std::to_string(engine_.mode()) + ",\"base\":\"" + json_escape(base_) +
               "\",\"error\":\"" + json_escape(error) +
               "\",\"message\":\"" + json_escape(message) + "\",\"fx\":" + fx_json(engine_.fx()) + "}";
    }

    void update_fx(const std::string& body) {
        FxParams fx = engine_.fx();
        std::string preset;
        if (form_find(body, "preset", &preset)) {
            if (!store_.get(preset, &fx)) {
                return;
            }
            base_ = preset;
        } else {
            fx_from_form(body, &fx);
            snprintf(fx.preset, sizeof(fx.preset), "%s", "custom");
        }
        engine_.set_fx(fx);
    }

    std::string preset_name(const std::string& id) {
        for (const PresetInfo& p : store_.list()) {
            if (p.id == id) {
                return p.name;
            }
        }
        return id;
    }

    /* Make the saved preset the active one, keeping the current sound. */
    void select_saved(const std::string& id) {
        FxParams fx = engine_.fx();
        snprintf(fx.preset, sizeof(fx.preset), "%s", id.c_str());
        engine_.set_fx(fx);
        base_ = id;
    }

    std::string save_preset(const std::string& body) {
        std::string err;
        std::string id;
        std::string name;
        if (form_find(body, "id", &id)) {
            if (!store_.save(id, engine_.fx(), &err)) {
                return fx_response(err);
            }
        } else if (form_find(body, "name", &name)) {
            id = store_.save_as(name, engine_.fx(), &err);
            if (id.empty()) {
                return fx_response(err);
            }
        } else {
            return fx_response("Nothing to save.");
        }
        select_saved(id);
        return fx_response("", "Saved \"" + preset_name(id) + "\".");
    }

    std::string reset_preset(const std::string& body) {
        const std::string id = form_value(body, "id");
        const std::string name = preset_name(id);
        const bool builtin = store_.is_builtin(id);
        std::string err;
        if (!store_.reset(id, &err)) {
            return fx_response(err);
        }
        if (builtin) {
            FxParams fx;
            store_.get(id, &fx);
            engine_.set_fx(fx);
            base_ = id;
            return fx_response("", "Restored \"" + name + "\" to its original settings.");
        }
        if (base_ == id) {
            FxParams fx = engine_.fx();
            snprintf(fx.preset, sizeof(fx.preset), "%s", "custom");
            engine_.set_fx(fx);
            base_.clear();
        }
        if (settings_.startup_preset == id) {
            settings_.startup_preset.clear();
            std::string save_err;
            if (!settings_.save(settings_path_, &save_err)) {
                fprintf(stderr, "%s\n", save_err.c_str());
            }
        }
        return fx_response("", "Deleted \"" + name + "\".");
    }

    static bool device_known(const std::string& id, bool capture) {
        AlsaDeviceList list = {};
        bool known = false;
        if ((capture ? alsa_enumerate_capture(&list) : alsa_enumerate_playback(&list)) == 0) {
            known = list_contains(list, id);
        }
        alsa_device_list_free(&list);
        if (!known &&
            (capture ? alsa_enumerate_all_capture(&list) : alsa_enumerate_all_playback(&list)) == 0) {
            known = list_contains(list, id);
        }
        alsa_device_list_free(&list);
        return known;
    }

    std::string start(const std::string& body) {
        const std::string in = form_value(body, "input");
        const std::string out = form_value(body, "output");
        if (in.empty() || out.empty()) {
            return status_json("Select both an input and an output device.");
        }

        if (!device_known(in, true) || !device_known(out, false)) {
            return status_json("Device not found. Refresh devices and try again.");
        }

        snprintf(cfg_.input_dev, sizeof(cfg_.input_dev), "%s", in.c_str());
        snprintf(cfg_.output_dev, sizeof(cfg_.output_dev), "%s", out.c_str());
        user_stopped_ = false;
        if (!engine_.start(cfg_)) {
            return status_json(engine_.last_error().empty() ? "Failed to start audio engine" : "");
        }
        if (settings_.input != in || settings_.output != out) {
            settings_.input = in;
            settings_.output = out;
            std::string err;
            if (!settings_.save(settings_path_, &err)) {
                fprintf(stderr, "%s\n", err.c_str());
            }
        }
        return status_json("");
    }

    std::string settings_json(const std::string& error) {
        const std::string startup = settings_.startup_preset.empty() ? "clean" : settings_.startup_preset;
        return std::string("{\"autostart\":") + (settings_.autostart ? "true" : "false") +
               ",\"input\":\"" + json_escape(settings_.input) + "\",\"output\":\"" +
               json_escape(settings_.output) + "\",\"startup_preset\":\"" + json_escape(startup) +
               "\",\"startup_name\":\"" + json_escape(preset_name(startup)) + "\",\"error\":\"" +
               json_escape(error) + "\"}";
    }

    std::string update_settings(const std::string& body) {
        AppSettings next = settings_;
        std::string v;
        if (form_find(body, "autostart", &v)) {
            next.autostart = v == "1" || v == "true";
            std::string in;
            std::string out;
            if (next.autostart && form_find(body, "input", &in) && form_find(body, "output", &out)) {
                if (!device_known(in, true) || !device_known(out, false)) {
                    return settings_json("Device not found. Refresh devices and try again.");
                }
                next.input = in;
                next.output = out;
            }
            if (next.autostart && (next.input.empty() || next.output.empty())) {
                return settings_json("Select an input and output device first.");
            }
        }
        if (form_find(body, "startup_preset", &v)) {
            FxParams tmp;
            if (!store_.get(v, &tmp)) {
                return settings_json("Unknown preset: " + v);
            }
            next.startup_preset = v;
        }
        std::string err;
        if (!next.save(settings_path_, &err)) {
            return settings_json(err);
        }
        settings_ = next;
        if (!engine_.running() && !settings_.input.empty() && !settings_.output.empty()) {
            snprintf(cfg_.input_dev, sizeof(cfg_.input_dev), "%s", settings_.input.c_str());
            snprintf(cfg_.output_dev, sizeof(cfg_.output_dev), "%s", settings_.output.c_str());
        }
        retry_sec_ = 2;
        next_try_ = 0;
        return settings_json("");
    }

    EngineConfig cfg_;
    PresetStore store_;
    std::string settings_path_;
    AppSettings settings_;
    std::string base_; /* preset the current settings came from */
    bool user_stopped_ = false; /* Stop pressed: no autostart retries until Start */
    time_t next_try_ = 0;
    int retry_sec_ = 2;
    PassEngine engine_;
};

}  // namespace

int run_web(const EngineConfig* defaults, volatile sig_atomic_t* keep_running) {
    const int port = defaults->web_port;
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        perror("socket");
        return 1;
    }
    int one = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(lfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Cannot listen on port %d: %s\n", port, strerror(errno));
        if (errno == EADDRINUSE) {
            fprintf(stderr,
                    "Is the boot service already running? Stop it first:\n"
                    "  sudo systemctl stop uvc-voicechanger\n");
        }
        close(lfd);
        return 1;
    }
    if (listen(lfd, 8) < 0) {
        perror("listen");
        close(lfd);
        return 1;
    }

    char host[256] = "raspberrypi";
    gethostname(host, sizeof(host) - 1);
    fprintf(stderr,
            "UVC Linux audio engine — web config\n"
            "  open http://%s.local:%d/ from a phone or computer on the same network\n"
            "  presets file: %s\n"
            "  Ctrl+C to quit\n",
            host,
            port,
            defaults->presets_path);

    WebServer server(*defaults);
    while (*keep_running) {
        server.tick();
        struct pollfd p = {lfd, POLLIN, 0};
        int r = poll(&p, 1, 500);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            break;
        }
        if (r == 0) {
            continue;
        }
        int cfd = accept(lfd, nullptr, nullptr);
        if (cfd < 0) {
            continue;
        }
        struct timeval tv = {2, 0};
        setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        server.handle(cfd);
        close(cfd);
    }

    close(lfd);
    fprintf(stderr, "Stopped.\n");
    return 0;
}
