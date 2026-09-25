/*
 * Web page for device selection and voice effects.
 * Minimal single-threaded HTTP server: pick ALSA capture/playback,
 * start/stop audio, choose presets and adjust effects live.
 */

#include "web.h"

#include "engine.h"

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
</style>
</head>
<body>
<h1>UVC Voice Changer</h1>
<p class="sub">Stereo &middot; 48 kHz &middot; 16-bit &middot; 2 ch</p>
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
<label class="adv"><input type="checkbox" id="all">Show all ALSA device names (troubleshooting)</label>

<h2>Voice</h2>
<label for="preset">Preset</label>
<select id="preset"></select>
<label class="on"><input type="checkbox" id="enabled">Effects on</label>
<div class="knob"><span>Volume dB</span><input type="range" id="volume_db" aria-label="Volume dB" min="-24" max="12" step="0.5"><span class="val" id="volume_db_v"></span></div>
<div id="fx"></div>
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
  {title: 'Metal / echo', on: 'comb_on', knobs: [
    ['comb_ms', 'Delay ms', 1, 300, 1],
    ['comb_feedback', 'Feedback', 0, 0.9, 0.05],
    ['comb_mix', 'Mix', 0, 1, 0.05]]},
  {title: 'Clipping (grit)', on: 'clip_on', knobs: [
    ['clip_factor', 'Amount', 1, 10, 1]]},
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

function showFx(d) {
  const sel = $('preset');
  if (!sel.options.length) {
    for (const p of d.presets.concat([{id: 'custom', name: 'Custom'}])) {
      const o = document.createElement('option');
      o.value = p.id;
      o.textContent = p.name;
      sel.appendChild(o);
    }
  }
  sel.value = d.fx.preset;
  for (const k of KEYS) {
    const el = $(k);
    if (el.type === 'checkbox') el.checked = !!d.fx[k];
    else el.value = d.fx[k];
  }
  updateLabels();
}

function fxBody() {
  return KEYS.map(k => {
    const el = $(k);
    return k + '=' + (el.type === 'checkbox' ? (el.checked ? 1 : 0) : encodeURIComponent(el.value));
  }).join('&');
}

function fxChanged() {
  updateLabels();
  $('preset').value = 'custom';
  clearTimeout(sendTimer);
  sendTimer = setTimeout(() => api('/api/fx', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: fxBody()
  }), 80);
}

$('preset').onchange = async () => {
  if ($('preset').value === 'custom') return;
  showFx(await api('/api/fx', {
    method: 'POST',
    headers: {'Content-Type': 'application/x-www-form-urlencoded'},
    body: 'preset=' + encodeURIComponent($('preset').value)
  }));
};

buildFx();
api('/api/fx').then(showFx);
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
    explicit WebServer(const EngineConfig& defaults) : cfg_(defaults) {
        cfg_.rate = 48000;
        cfg_.channels = 2;
        FxParams fx;
        if (fx_apply_preset(cfg_.preset, &fx)) {
            engine_.set_fx(fx);
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
            engine_.stop();
            send_response(fd, 200, "OK", json, status_json(""));
        } else if (req.method == "GET" && req.path == "/api/fx") {
            send_response(fd, 200, "OK", json, fx_response());
        } else if (req.method == "POST" && req.path == "/api/fx") {
            update_fx(req.body);
            send_response(fd, 200, "OK", json, fx_response());
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

    std::string fx_response() {
        int count = 0;
        const FxPresetInfo* presets = fx_presets(&count);
        std::string out = "{\"presets\":[";
        for (int i = 0; i < count; i++) {
            if (i) {
                out += ",";
            }
            out += std::string("{\"id\":\"") + presets[i].id + "\",\"name\":\"" +
                   json_escape(presets[i].name) + "\"}";
        }
        return out + "],\"fx\":" + fx_json(engine_.fx()) + "}";
    }

    void update_fx(const std::string& body) {
        FxParams fx = engine_.fx();
        std::string preset;
        if (form_find(body, "preset", &preset)) {
            fx_apply_preset(preset.c_str(), &fx);
        } else {
            fx_from_form(body, &fx);
            snprintf(fx.preset, sizeof(fx.preset), "%s", "custom");
        }
        engine_.set_fx(fx);
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
        if (!engine_.start(cfg_) && engine_.last_error().empty()) {
            return status_json("Failed to start audio engine");
        }
        return status_json("");
    }

    EngineConfig cfg_;
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
            "UVC Linux audio engine — Milestone 1 web config\n"
            "  open http://%s.local:%d/ from a phone or computer on the same network\n"
            "  Ctrl+C to quit\n",
            host,
            port);

    WebServer server(*defaults);
    while (*keep_running) {
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
