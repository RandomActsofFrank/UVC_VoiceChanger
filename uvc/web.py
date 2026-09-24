"""Local control page for the voice changer."""

from flask import Flask, Response, abort, jsonify, request

from uvc.engine import Engine

app = Flask(__name__)
engine = Engine()

PAGE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Voice Changer</title>
<style>
  body { font-family: sans-serif; margin: 1.5rem; background: #111; color: #eee; }
  h1 { font-size: 1.4rem; margin-bottom: 0.2rem; }
  p { color: #bbb; }
  button, input { font: inherit; }
  button { background: #2a2a2a; color: #eee; border: 1px solid #555; padding: 0.4rem 0.7rem; margin: 0.15rem; }
  button.on { background: #1d4ed8; border-color: #1d4ed8; }
  section { border: 1px solid #333; padding: 0.8rem; margin: 0.8rem 0; }
  label { display: inline-block; margin-right: 1rem; }
  .row { display: flex; flex-wrap: wrap; gap: 0.6rem; align-items: center; }
  #status { font-family: ui-monospace, monospace; }
</style>
</head>
<body>
<h1>Voice Changer</h1>
<p id="status">Starting…</p>
<div class="row">
  <button id="pass" type="button">Pass through</button>
  <button id="record" type="button">Record</button>
  <button id="play" type="button">Play</button>
  <button id="idle" type="button">Stop</button>
  <a href="/download"><button type="button">Download WAV</button></a>
</div>
<div class="row">
  <button type="button" id="preset-flat">Flat</button>
  <button type="button" id="preset-radio">Radio</button>
  <button type="button" id="preset-dalek">Dalek</button>
</div>
<form id="controls">
  <section>
    <label>Mic gain <input name="mic_gain" type="number" min="0" max="11" step="1"></label>
    <label>Amp volume <input name="amp_vol" type="number" min="0" max="10" step="1"></label>
    <label><input name="disable" type="checkbox"> Disable filters</label>
    <label>Pitch <input name="pitch" type="number" min="0.5" max="2" step="0.01"></label>
  </section>
  <section>
    <strong>Low cut</strong>
    <label><input name="high_pass" type="checkbox"> On</label>
    <label>Hz <input name="hp_freq" type="number" min="20" max="12000" step="1"></label>
    <label>Q <input name="hp_q" type="number" min="0.1" max="10" step="0.1"></label>
    <label>Cascade <input name="hp_cas" type="number" min="1" max="8" step="1"></label>
  </section>
  <section>
    <strong>High cut</strong>
    <label><input name="low_pass" type="checkbox"> On</label>
    <label>Hz <input name="lp_freq" type="number" min="20" max="12000" step="1"></label>
    <label>Q <input name="lp_q" type="number" min="0.1" max="10" step="0.1"></label>
    <label>Cascade <input name="lp_cas" type="number" min="1" max="8" step="1"></label>
  </section>
  <section>
    <strong>Band pass</strong>
    <label><input name="band_pass" type="checkbox"> On</label>
    <label>Hz <input name="bp_freq" type="number" min="20" max="12000" step="1"></label>
    <label>Q <input name="bp_q" type="number" min="0.1" max="10" step="0.1"></label>
    <label>Cascade <input name="bp_cas" type="number" min="1" max="8" step="1"></label>
  </section>
  <section>
    <strong>Low shelf</strong>
    <label><input name="low_shelf" type="checkbox"> On</label>
    <label>Hz <input name="ls_freq" type="number" min="20" max="12000" step="1"></label>
    <label>Gain dB <input name="ls_gain" type="number" min="-24" max="24" step="0.5"></label>
  </section>
  <section>
    <strong>High shelf</strong>
    <label><input name="high_shelf" type="checkbox"> On</label>
    <label>Hz <input name="hs_freq" type="number" min="20" max="12000" step="1"></label>
    <label>Gain dB <input name="hs_gain" type="number" min="-24" max="24" step="0.5"></label>
  </section>
  <section>
    <strong>Peak</strong>
    <label><input name="peak" type="checkbox"> On</label>
    <label>Hz <input name="pk_freq" type="number" min="20" max="12000" step="1"></label>
    <label>Q <input name="pk_q" type="number" min="0.1" max="10" step="0.1"></label>
    <label>Gain dB <input name="pk_gain" type="number" min="-24" max="24" step="0.5"></label>
  </section>
  <section>
    <strong>Ring mod</strong>
    <label><input name="ring" type="checkbox"> On</label>
    <label>Hz <input name="sw_freq" type="number" min="1" max="400" step="1"></label>
    <label>Amp <input name="sw_amp" type="number" min="1" max="127" step="1"></label>
  </section>
  <section>
    <strong>Reverb</strong>
    <label><input name="reverb" type="checkbox"> On</label>
    <label>Decay <input name="decay_factor" type="number" min="0" max="20" step="1"></label>
    <strong>Clip</strong>
    <label><input name="clipping" type="checkbox"> On</label>
    <label>Hardness <input name="clip_factor" type="number" min="1" max="10" step="1"></label>
  </section>
</form>
<script>
const form = document.getElementById("controls");
let ready = false;

function fill(settings) {
  ready = false;
  for (const el of form.elements) {
    if (!el.name || !(el.name in settings)) continue;
    if (el.type === "checkbox") el.checked = !!settings[el.name];
    else el.value = settings[el.name];
  }
  ready = true;
}

function payload() {
  const data = {};
  for (const el of form.elements) {
    if (!el.name) continue;
    if (el.type === "checkbox") data[el.name] = el.checked;
    else if (el.type === "number") data[el.name] = Number(el.value);
    else data[el.name] = el.value;
  }
  return data;
}

async function sendSettings() {
  const res = await fetch("/api/settings", {
    method: "POST",
    headers: {"Content-Type": "application/json"},
    body: JSON.stringify(payload())
  });
  return res.json();
}

function show(status) {
  const dev = status.device || {};
  const where = dev.play3 ? dev.name : (dev.name + " (Play 3 not found)");
  const err = dev.error ? " Audio error: " + dev.error : "";
  document.getElementById("status").textContent =
    "Mode " + status.mode +
    " · " + where +
    " · " + status.recorded_seconds.toFixed(1) + "s recorded" +
    err;
  for (const id of ["pass","record","play","idle"]) {
    document.getElementById(id).classList.toggle("on", status.mode === id);
  }
}

form.addEventListener("change", () => { if (ready) sendSettings(); });

for (const name of ["flat","radio","dalek"]) {
  document.getElementById("preset-" + name).onclick = async () => {
    const res = await fetch("/api/preset/" + name, {method: "POST"});
    const status = await res.json();
    fill(status.settings);
    show(status);
  };
}

for (const id of ["pass","record","play","idle"]) {
  document.getElementById(id).onclick = async () => {
    const res = await fetch("/api/mode/" + id, {method: "POST"});
    if (!res.ok) {
      alert("Record something first.");
      return;
    }
    show(await res.json());
  };
}

async function poll() {
  const res = await fetch("/api/status");
  const status = await res.json();
  if (!ready) fill(status.settings);
  show(status);
}
poll();
setInterval(poll, 1000);
</script>
</body>
</html>
"""


@app.get("/")
def index():
    return PAGE


@app.get("/api/status")
def status():
    return jsonify(engine.status())


@app.post("/api/settings")
def settings():
    engine.update_settings(request.get_json(force=True) or {})
    return jsonify(engine.status())


@app.post("/api/preset/<name>")
def preset(name):
    if name not in ("flat", "radio", "dalek"):
        abort(404)
    engine.apply_preset(name)
    return jsonify(engine.status())


@app.post("/api/mode/<mode>")
def mode(mode):
    if mode not in ("idle", "pass", "record", "play"):
        abort(404)
    if not engine.set_mode(mode):
        return jsonify({"error": "Nothing recorded yet"}), 409
    return jsonify(engine.status())


@app.get("/download")
def download():
    wav = engine.render_wav()
    if not wav:
        abort(404)
    return Response(
        wav,
        mimetype="audio/wav",
        headers={"Content-Disposition": "attachment; filename=VoiceChanger.wav"},
    )


def main():
    started = engine.start()
    if started:
        print("Audio: %s" % engine.device["name"])
    else:
        print("Audio failed: %s" % engine.device["error"])
    print("Open http://127.0.0.1:8080")
    app.run(host="0.0.0.0", port=8080, threaded=True, use_reloader=False)
