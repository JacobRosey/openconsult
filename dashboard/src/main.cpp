#include "dashboard/src/dashboard_json.h"

#include "openconsult/src/common.h"
#include "openconsult/src/consult_interface.h"
#include "openconsult/src/log_recorder.h"
#include "openconsult/src/log_replay.h"
#include "openconsult/src/serial.h"

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/flags/usage_config.h"
#include "httplib.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace openconsult;

#define APP_NAME "openconsult_dashboard"
#define APP_VERSION "0.1.0"
#define APP_DESCRIPTION "Browser dashboard for reading from a Consult device."
#define APP_USAGE "usage: " APP_NAME " [--help] [--version] [--host address] [--port port]\n"\
              "           [--log path] [--replay] [--replay_wrap]\n"\
              "           [--stream_frames count] [--open_browser] device"

ABSL_FLAG(std::string, host, "127.0.0.1",
          "Address to bind the dashboard HTTP server to.");
ABSL_FLAG(int, port, 8080,
          "Port to bind the dashboard HTTP server to.");
ABSL_FLAG(std::string, log, "",
          "Path to log all Consult transactions to. This log may be subsequently "
          "'replayed' using the --replay flag.");
ABSL_FLAG(bool, replay, false,
          "Interpret the passed device as a log to replay transactions from.");
ABSL_FLAG(bool, replay_wrap, false,
          "When replaying a log, wrap at the end of the log.");
ABSL_FLAG(int, stream_frames, 0,
          "Number of engine parameter frames to stream. A value of 0 streams "
          "until the program is interrupted.");
ABSL_FLAG(bool, open_browser, false,
          "Open the dashboard URL in the default browser after the server starts.");

namespace {

const char kDashboardHtml[] = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>OpenConsult Dashboard</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #101114;
      --panel: #1b1d22;
      --panel-strong: #242831;
      --text: #f4f5f7;
      --muted: #9da5b4;
      --line: #343946;
      --accent: #4fd1a5;
      --warn: #f7b955;
      --danger: #ff6b6b;
    }

    * {
      box-sizing: border-box;
    }

    body {
      margin: 0;
      min-height: 100vh;
      background: var(--bg);
      color: var(--text);
      font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    }

    main {
      width: min(1180px, calc(100vw - 32px));
      margin: 0 auto;
      padding: 24px 0;
    }

    header {
      display: flex;
      justify-content: space-between;
      align-items: flex-end;
      gap: 16px;
      margin-bottom: 20px;
    }

    h1 {
      margin: 0;
      font-size: 28px;
      font-weight: 760;
    }

    .status {
      display: flex;
      align-items: center;
      gap: 10px;
      color: var(--muted);
      font-size: 14px;
      white-space: nowrap;
    }

    .dot {
      width: 10px;
      height: 10px;
      border-radius: 50%;
      background: var(--warn);
    }

    .dot.live {
      background: var(--accent);
    }

    .dot.error {
      background: var(--danger);
    }

    .grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
      gap: 12px;
    }

    .tile {
      min-height: 176px;
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 8px;
      padding: 16px;
      display: grid;
      grid-template-rows: auto 1fr 42px;
      gap: 10px;
    }

    .label {
      color: var(--muted);
      font-size: 13px;
      line-height: 1.35;
      min-height: 36px;
    }

    .value {
      align-self: end;
      font-variant-numeric: tabular-nums;
      font-size: clamp(34px, 8vw, 56px);
      line-height: 0.95;
      font-weight: 780;
      overflow-wrap: anywhere;
    }

    canvas {
      width: 100%;
      height: 42px;
      background: var(--panel-strong);
      border-radius: 6px;
    }

    .empty {
      min-height: 320px;
      display: grid;
      place-items: center;
      color: var(--muted);
      border: 1px solid var(--line);
      border-radius: 8px;
      background: var(--panel);
    }

    @media (max-width: 680px) {
      main {
        width: min(100vw - 20px, 1180px);
        padding: 14px 0;
      }

      header {
        align-items: flex-start;
        flex-direction: column;
      }

      h1 {
        font-size: 24px;
      }
    }
  </style>
</head>
<body>
  <main>
    <header>
      <h1>OpenConsult</h1>
      <div class="status"><span id="dot" class="dot"></span><span id="status">Connecting</span></div>
    </header>
    <section id="grid" class="grid" aria-live="polite">
      <div class="empty">Waiting for engine data</div>
    </section>
  </main>
  <script>
    const grid = document.getElementById('grid');
    const statusText = document.getElementById('status');
    const dot = document.getElementById('dot');
    const tiles = new Map();
    const history = new Map();
    const maxPoints = 80;

    function setStatus(text, mode) {
      statusText.textContent = text;
      dot.className = 'dot' + (mode ? ' ' + mode : '');
    }

    function ensureTile(id, parameter) {
      if (tiles.has(id)) return tiles.get(id);
      if (grid.querySelector('.empty')) grid.innerHTML = '';

      const tile = document.createElement('article');
      tile.className = 'tile';

      const label = document.createElement('div');
      label.className = 'label';
      label.textContent = parameter.name;

      const value = document.createElement('div');
      value.className = 'value';
      value.textContent = '--';

      const canvas = document.createElement('canvas');
      canvas.width = 360;
      canvas.height = 84;

      tile.append(label, value, canvas);
      grid.append(tile);

      const created = { value, canvas };
      tiles.set(id, created);
      history.set(id, []);
      return created;
    }

    function drawSparkline(canvas, values) {
      const ctx = canvas.getContext('2d');
      const width = canvas.width;
      const height = canvas.height;
      ctx.clearRect(0, 0, width, height);
      if (values.length < 2) return;

      let min = Math.min(...values);
      let max = Math.max(...values);
      if (min === max) {
        min -= 1;
        max += 1;
      }

      ctx.lineWidth = 4;
      ctx.lineJoin = 'round';
      ctx.lineCap = 'round';
      ctx.strokeStyle = '#4fd1a5';
      ctx.beginPath();

      values.forEach((value, index) => {
        const x = (index / (values.length - 1)) * (width - 12) + 6;
        const y = height - 8 - ((value - min) / (max - min)) * (height - 16);
        if (index === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      });

      ctx.stroke();
    }

    function renderFrame(frame) {
      const date = new Date(frame.timestamp_ms);
      setStatus('Live - updated ' + date.toLocaleTimeString(), 'live');

      Object.entries(frame.parameters).forEach(([id, parameter]) => {
        const tile = ensureTile(id, parameter);
        const value = Number(parameter.value);
        tile.value.textContent = Number.isFinite(value) ? value.toFixed(2) : '--';

        const points = history.get(id);
        points.push(value);
        while (points.length > maxPoints) points.shift();
        drawSparkline(tile.canvas, points);
      });
    }

    const events = new EventSource('/events');
    events.onopen = () => setStatus('Connected', 'live');
    events.onmessage = (event) => renderFrame(JSON.parse(event.data));
    events.addEventListener('error', (event) => {
      setStatus(event.data || 'Stream error', 'error');
      events.close();
    });
    events.onerror = () => setStatus('Disconnected', 'error');
  </script>
</body>
</html>)HTML";

void reportUsageError(const std::string& error) {
    std::cerr << APP_USAGE << "\n";
    std::cerr << "ERROR: " << error << "\n";
    std::exit(2);
}

std::string jsonString(const std::string& value) {
    std::stringstream stream;
    stream << '"';
    for (char c : value) {
        switch (c) {
            case '"':
                stream << "\\\"";
                break;
            case '\\':
                stream << "\\\\";
                break;
            case '\n':
                stream << "\\n";
                break;
            case '\r':
                stream << "\\r";
                break;
            case '\t':
                stream << "\\t";
                break;
            default:
                stream << c;
                break;
        }
    }
    stream << '"';
    return stream.str();
}

std::chrono::milliseconds nowMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch());
}

bool isSafeBrowserUrl(const std::string& url) {
    for (char c : url) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) ||
              c == ':' || c == '/' || c == '.' || c == '-' ||
              c == '_' || c == '[' || c == ']')) {
            return false;
        }
    }
    return true;
}

std::string browserHost(const std::string& host) {
    if (host == "0.0.0.0" || host == "::") {
        return "127.0.0.1";
    }
    return host;
}

bool openBrowser(const std::string& url) {
    if (!isSafeBrowserUrl(url)) {
        std::cerr << "Refusing to open browser for unsafe URL: " << url << "\n";
        return false;
    }

#if defined(_WIN32)
    std::string command = "start \"\" \"" + url + "\"";
#elif defined(__APPLE__)
    std::string command = "open \"" + url + "\"";
#else
    std::string command = "xdg-open \"" + url + "\"";
#endif
    return std::system(command.c_str()) == 0;
}

class EventHub {
public:
    void publishData(const std::string& json) {
        std::lock_guard<std::mutex> lock(mutex);
        latest_event = serverSentEvent("", json);
        ++sequence;
        cv.notify_all();
    }

    void publishTerminalEvent(const std::string& event_name, const std::string& json) {
        std::lock_guard<std::mutex> lock(mutex);
        terminal_event = serverSentEvent(event_name, json);
        stopped = true;
        cv.notify_all();
    }

    bool waitForNext(uint64_t& cursor, bool& sent_terminal_event, std::string& out) {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&]() {
            return sequence > cursor || stopped;
        });

        if (sequence > cursor) {
            cursor = sequence;
            out = latest_event;
            return true;
        }

        if (stopped && !sent_terminal_event && !terminal_event.empty()) {
            sent_terminal_event = true;
            out = terminal_event;
            return true;
        }

        return false;
    }

private:
    static std::string serverSentEvent(const std::string& event_name,
                                       const std::string& data) {
        std::stringstream stream;
        if (!event_name.empty()) {
            stream << "event: " << event_name << "\n";
        }

        std::size_t line_start = 0;
        while (line_start <= data.size()) {
            std::size_t line_end = data.find('\n', line_start);
            if (line_end == std::string::npos) {
                stream << "data: " << data.substr(line_start) << "\n";
                break;
            }
            stream << "data: " << data.substr(line_start, line_end - line_start) << "\n";
            line_start = line_end + 1;
        }
        stream << "\n";
        return stream.str();
    }

    std::mutex mutex;
    std::condition_variable cv;
    uint64_t sequence = 0;
    bool stopped = false;
    std::string latest_event;
    std::string terminal_event;
};

std::unique_ptr<ByteInterface> openDevice(
        const std::string& device_id,
        bool replay,
        bool wrap,
        const std::string& log_path,
        std::ifstream& replay_file,
        std::ofstream& log_file) {
    if (replay) {
        replay_file = std::ifstream(device_id, std::ios_base::in);
        if (!replay_file.good()) {
            reportUsageError(cmn::pformat("Failed to open %s", device_id.c_str()));
        }
        return std::unique_ptr<ByteInterface>(new LogReplay(replay_file, wrap));
    }

    std::unique_ptr<ByteInterface> device(new SerialPort(device_id, 9600));
    if (!log_path.empty()) {
        log_file = std::ofstream(log_path, std::ios_base::out);
        if (!log_file.good()) {
            reportUsageError(cmn::pformat("Failed to open %s", log_path.c_str()));
        }
        device = std::unique_ptr<ByteInterface>(new LogRecorder(std::move(device), log_file));
    }
    return device;
}

void streamEngineData(const std::string& device_id,
                      bool replay,
                      bool wrap,
                      const std::string& log_path,
                      EventHub& hub,
                      std::atomic<bool>& done,
                      httplib::Server& server,
                      int stream_frames) {
    try {
        std::ifstream replay_file;
        std::ofstream log_file;
        auto device = openDevice(device_id, replay, wrap, log_path, replay_file, log_file);
        ConsultInterface consult(std::move(device));
        auto stream = consult.streamEngineParameters(dashboard::commonDashboardParameters());
        for (int i = 0; stream_frames == 0 || i < stream_frames; ++i) {
            auto parameters = stream.getFrame();
            hub.publishData(dashboard::engineParametersFrameToJSON(parameters, nowMilliseconds()));
        }

        hub.publishTerminalEvent("done", "{\"message\":\"Stream completed\"}");
    } catch (const std::exception& error) {
        hub.publishTerminalEvent(
            "error",
            std::string("{\"message\":") + jsonString(error.what()) + "}");
    }
    done = true;
    server.stop();
}

}

int main(int argc, char** argv) {
    absl::FlagsUsageConfig flag_config;
    flag_config.version_string = [](){ return APP_NAME " " APP_VERSION "\n"; };
    absl::SetFlagsUsageConfig(flag_config);
    absl::SetProgramUsageMessage(APP_DESCRIPTION "\n" APP_USAGE);

    auto positional_args = absl::ParseCommandLine(argc, argv);
    std::string host = absl::GetFlag(FLAGS_host);
    int port = absl::GetFlag(FLAGS_port);
    std::string log_path = absl::GetFlag(FLAGS_log);
    bool replay = absl::GetFlag(FLAGS_replay);
    bool wrap = absl::GetFlag(FLAGS_replay_wrap);
    int stream_frames = absl::GetFlag(FLAGS_stream_frames);
    bool open_browser = absl::GetFlag(FLAGS_open_browser);

    if (positional_args.size() < 2) {
        reportUsageError("The following arguments are required: device");
    } else if (positional_args.size() > 2) {
        reportUsageError("Too many positional arguments supplied");
    } else if (port <= 0 || port > 65535) {
        reportUsageError("--port must be between 1 and 65535");
    } else if (stream_frames < 0) {
        reportUsageError("--stream_frames must be 0 or greater");
    }

    EventHub hub;
    std::atomic<bool> done(false);
    httplib::Server server;

    server.Get("/", [](const httplib::Request&, httplib::Response& response) {
        response.set_content(kDashboardHtml, "text/html; charset=utf-8");
    });

    server.Get("/api/metadata", [](const httplib::Request&, httplib::Response& response) {
        response.status = 409;
        response.set_content(
            "{\"error\":\"Metadata reads are unavailable while live streaming is active\"}",
            "application/json");
    });

    server.Get("/api/faults", [](const httplib::Request&, httplib::Response& response) {
        response.status = 409;
        response.set_content(
            "{\"error\":\"Fault reads are unavailable while live streaming is active\"}",
            "application/json");
    });

    server.Get("/events", [&](const httplib::Request&, httplib::Response& response) {
        response.set_header("Cache-Control", "no-cache");
        response.set_header("Connection", "keep-alive");
        auto cursor = std::make_shared<uint64_t>(0);
        auto sent_terminal_event = std::make_shared<bool>(false);
        response.set_chunked_content_provider(
            "text/event-stream",
            [&, cursor, sent_terminal_event](size_t, httplib::DataSink& sink) {
                std::string event;
                if (!hub.waitForNext(*cursor, *sent_terminal_event, event)) {
                    return false;
                }
                return sink.write(event.data(), event.size());
            });
    });

    int bind_result = server.bind_to_port(host.c_str(), port);
    if (bind_result < 0) {
        std::cerr << "Failed to listen on " << host << ":" << port << "\n";
        return 1;
    }

    std::atomic<bool> listen_failed(false);
    std::thread listener([&]() {
        if (!server.listen_after_bind()) {
            listen_failed = true;
            if (!done) {
                std::cerr << "Failed to listen on " << host << ":" << port << "\n";
            }
        }
    });

    while (!server.is_running() && !listen_failed) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (listen_failed) {
        if (listener.joinable()) {
            listener.join();
        }
        return 1;
    }

    std::string dashboard_url = "http://" + browserHost(host) + ":" + std::to_string(port);
    std::cout << "OpenConsult dashboard listening on " << dashboard_url << "\n";
    if (open_browser && !openBrowser(dashboard_url)) {
        std::cerr << "Failed to open browser for " << dashboard_url << "\n";
    }

    std::string device_id = positional_args[1];
    std::thread streamer(streamEngineData,
                         device_id,
                         replay,
                         wrap,
                         log_path,
                         std::ref(hub),
                         std::ref(done),
                         std::ref(server),
                         stream_frames);

    if (streamer.joinable()) {
        streamer.join();
    }
    if (listener.joinable()) {
        listener.join();
    }

    return done ? 0 : 1;
}
