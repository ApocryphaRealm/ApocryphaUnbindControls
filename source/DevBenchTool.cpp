#include "PCH.h"

#include "DevBenchTool.h"

#include "DevBench/DevBenchAPI.h"
#include "ControlsList.h"
#include "Functions.h"
#include "Settings.h"
#include "SystemMenu.h"
#include "Unbinder.h"
#include "utils/Logger.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace DevBenchTool
{
	namespace
	{
		std::string EscapeJson(std::string_view a_in)
		{
			std::string out;
			out.reserve(a_in.size() + 8);
			for (const char c : a_in)
			{
				switch (c)
				{
				case '\\': out += "\\\\"; break;
				case '"': out += "\\\""; break;
				case '\n': out += "\\n"; break;
				default: out += c; break;
				}
			}
			return out;
		}

		// The value of a top-level JSON member, as text: a quoted string or a bare word. Empty when absent.
		std::string Get(std::string_view a_json, const char* a_name)
		{
			const std::string key = std::format("\"{}\"", a_name);
			auto pos = a_json.find(key);
			if (pos == std::string_view::npos) { return {}; }
			pos = a_json.find(':', pos + key.size());
			if (pos == std::string_view::npos) { return {}; }
			++pos;
			while (pos < a_json.size() && (a_json[pos] == ' ' || a_json[pos] == '\t')) { ++pos; }
			if (pos >= a_json.size()) { return {}; }
			if (a_json[pos] == '"')
			{
				std::string out;
				for (++pos; pos < a_json.size() && a_json[pos] != '"'; ++pos)
				{
					if (a_json[pos] == '\\' && pos + 1 < a_json.size()) { ++pos; }
					out += a_json[pos];
				}
				return out;
			}
			std::string out;
			while (pos < a_json.size() && a_json[pos] != ',' && a_json[pos] != '}' && a_json[pos] != ' ') { out += a_json[pos++]; }
			return out;
		}

		// Runs a_fn on the game's main thread and waits (the handler runs on DevBench's own thread; the
		// ControlMap is only touched on the main thread).
		bool RunOnMainThread(std::function<void()> a_fn, int a_timeoutMs = 4000)
		{
			auto* tasks = SKSE::GetTaskInterface();
			if (!tasks) { return false; }
			auto done = std::make_shared<std::atomic<bool>>(false);
			auto m = std::make_shared<std::mutex>();
			auto cv = std::make_shared<std::condition_variable>();
			tasks->AddTask([=]() {
				a_fn();
				{
					std::scoped_lock l(*m);
					done->store(true);
				}
				cv->notify_all();
			});
			std::unique_lock l(*m);
			return cv->wait_for(l, std::chrono::milliseconds(a_timeoutMs), [&]() { return done->load(); });
		}

		int ContextArg(std::string_view a_args)
		{
			const std::string c = Get(a_args, "context");
			if (c.empty()) { return -1; }
			const int byName = unbinder::ContextIndex(c);
			if (byName >= 0) { return byName; }
			try { return std::stoi(c); } catch (...) { return -2; }
		}

		void ControlTool(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			const std::string_view args = a_argsJson ? a_argsJson : "";
			const std::string op = Get(args, "op");

			if (op == "dump")
			{
				const int ctx = ContextArg(args);
				if (ctx == -2) { a_write(a_sink, R"({"ok":false,"op":"dump","error":"unknown context"})"); return; }
				std::string out;
				if (!RunOnMainThread([&]() { out = unbinder::DumpJson(ctx); })) { a_write(a_sink, R"({"ok":false,"op":"dump","error":"main thread did not run the task in time"})"); return; }
				a_write(a_sink, out.c_str());
				return;
			}
			if (op == "unbind" || op == "rebind")
			{
				const int ctx = ContextArg(args);
				const std::string event = Get(args, "event");
				const int dev = unbinder::DeviceIndex(Get(args, "device"));
				if (ctx < 0 || event.empty() || dev < 0)
				{
					a_write(a_sink, std::format("{{\"ok\":false,\"op\":\"{}\",\"error\":\"need context (name or index), event and device (keyboard, mouse or gamepad)\"}}", op).c_str());
					return;
				}
				bool ok = false;
				std::string why;
				const bool ran = RunOnMainThread([&]() {
					ok = op == "unbind" ? unbinder::Unbind(ctx, event, dev, why) : unbinder::Rebind(ctx, event, dev, why);
					if (ok) { settings::Save(); }
				});
				if (!ran) { a_write(a_sink, std::format("{{\"ok\":false,\"op\":\"{}\",\"error\":\"main thread did not run the task in time\"}}", op).c_str()); return; }
				a_write(a_sink, std::format("{{\"ok\":{},\"op\":\"{}\",\"context\":\"{}\",\"event\":\"{}\",\"device\":\"{}\",\"why\":\"{}\"}}", ok ? "true" : "false", op,
											unbinder::ContextName(ctx), EscapeJson(event), unbinder::DeviceName(dev), EscapeJson(why)).c_str());
				return;
			}
			if (op == "listen")
			{
				int seconds = 20;
				try { const std::string s = Get(args, "seconds"); if (!s.empty()) { seconds = std::stoi(s); } } catch (...) {}
				controlslist::Listen(seconds);
				a_write(a_sink, std::format("{{\"ok\":true,\"op\":\"listen\",\"seconds\":{},\"log\":\"UnbindControls.log, lines starting listen:\"}}", seconds).c_str());
				return;
			}
			if (op == "systemrows")
			{
				std::string out;
				if (!RunOnMainThread([&]() { out = systemmenu::RowsJson(); })) { a_write(a_sink, R"({"ok":false,"op":"systemrows","error":"main thread did not run the task in time"})"); return; }
				a_write(a_sink, out.c_str());
				return;
			}
			if (op == "rows")
			{
				std::string out;
				if (!RunOnMainThread([&]() { out = controlslist::RowsJson(); })) { a_write(a_sink, R"({"ok":false,"op":"rows","error":"main thread did not run the task in time"})"); return; }
				a_write(a_sink, out.c_str());
				return;
			}
			if (op == "own")
			{
				int imported = 0;
				bool removed = false;
				const bool ran = RunOnMainThread([&]() {
					imported = unbinder::ImportLiveRemaps("tool");
					if (imported > 0) { settings::Save(); }
					removed = unbinder::RemoveCustomMap("tool");
				});
				a_write(a_sink, std::format("{{\"ok\":{},\"op\":\"own\",\"imported\":{},\"customMapRemoved\":{},\"path\":\"{}\"}}", ran ? "true" : "false", imported,
											removed ? "true" : "false", EscapeJson(unbinder::CustomMapPath())).c_str());
				return;
			}
			// The driving half of this feature (rule 64): bind, unbind and deliver an extra Controls-page row without
			// opening the journal and pushing the buttons by hand.
			if (op == "function")
			{
				const std::string name = Get(args, "event");
				const std::string deviceText = Get(args, "device");
				const std::string keyText = Get(args, "key");
				std::size_t index = 0;
				if (!functions::IsFunctionRow(name, &index))
				{
					a_write(a_sink, std::format(R"({{"ok":false,"op":"function","error":"no extra row named \"{}\""}})", EscapeJson(name)).c_str());
					return;
				}
				const int device = unbinder::DeviceIndex(deviceText);
				if (device < 0)
				{
					a_write(a_sink, R"({"ok":false,"op":"function","error":"device must be keyboard, mouse or gamepad"})");
					return;
				}
				bool ok = false;
				int files = 0;
				std::string why;
				const bool ran = RunOnMainThread([&]() {
					if (keyText.empty() || keyText == "none")
					{
						ok = functions::Unbind(index, device);
					}
					else
					{
						const std::uint16_t key = unbinder::ParseButton(keyText, device);
						if (key == 0xFF) { why = "not a button name or code"; return; }
						ok = functions::Bind(index, device, key, why);
					}
					if (ok) { settings::Save(); files = functions::Deliver("tool"); }
				});
				a_write(a_sink, std::format(R"({{"ok":{},"op":"function","row":"{}","device":"{}","filesWritten":{},"error":"{}"}})", (ran && ok) ? "true" : "false",
											EscapeJson(name), EscapeJson(deviceText), files, EscapeJson(why)).c_str());
				return;
			}
			if (op == "opencontrols")
			{
				bool ok = false;
				std::string why;
				const bool ran = RunOnMainThread([&]() { ok = controlslist::OpenControlsPanel(why); });
				a_write(a_sink, std::format(R"({{"ok":{},"op":"opencontrols","error":"{}"}})", (ran && ok) ? "true" : "false", EscapeJson(why)).c_str());
				return;
			}
			if (op == "deliver")
			{
				int files = 0;
				const bool ran = RunOnMainThread([&]() { files = functions::Deliver("tool"); });
				a_write(a_sink, std::format(R"({{"ok":{},"op":"deliver","filesWritten":{}}})", ran ? "true" : "false", files).c_str());
				return;
			}
			if (op == "apply")
			{
				const bool ran = RunOnMainThread([]() { unbinder::ApplyAll("tool"); });
				a_write(a_sink, std::format("{{\"ok\":{},\"op\":\"apply\"}}", ran ? "true" : "false").c_str());
				return;
			}
			if (op == "reload")
			{
				bool ok = false;
				const bool ran = RunOnMainThread([&]() { unbinder::RestoreAll("tool reload"); ok = settings::Reload(); unbinder::ApplyAll("tool reload"); });
				a_write(a_sink, std::format("{{\"ok\":{},\"op\":\"reload\"}}", (ran && ok) ? "true" : "false").c_str());
				return;
			}

			const std::string json = std::format(
				"{{\"ok\":true,\"op\":\"state\",\"settings\":{{\"enabled\":{},\"keepRemapsInIni\":{},\"logLevel\":{},\"iniPath\":\"{}\"}},{},{},{},{}}}",
				settings::general::enabled ? "true" : "false", settings::general::keepRemapsInIni ? "true" : "false", settings::debug::logLevel, EscapeJson(settings::GetIniPath()), unbinder::StateJson(), controlslist::StateJson(), systemmenu::StateJson(), functions::StateJson());
			a_write(a_sink, json.c_str());
		}
	}

	void Init(bool a_lastAttempt)
	{
		static bool registered = false;
		if (registered) { return; }

		DevBenchAPI::IDevBenchInterface001* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench)
		{
			if (a_lastAttempt) { logger::info("DevBench not detected; skipping the \"uvc.control\" tool"); }
			else { logger::debug("DevBench not detected yet; will retry at the next message"); }
			return;
		}

		constexpr const char* descriptor =
			"{"
			"\"description\":\"Unbind Vanilla Controls live state and driver. op=state (default): settings, the INI list with the "
			"keys captured this session, last apply. op=dump [context]: every mapping of the live ControlMap (event, key, modifier, "
			"remappable) per context and device. op=unbind / op=rebind with context (name or index), event, device "
			"(keyboard|mouse|gamepad): add to or remove from the list, applied and written to the INI. op=apply re-applies the list; "
			"op=reload gives the keys back, re-reads the INI and applies. op=rows (journal open): every row of the game's Controls list - event, the buttonName and buttonID the game sent, whether this mod draws it blank and why. op=listen [seconds, default 20, 1-120]: log every button event - device, code, the user event the game attached, value, held time - to the mod's log. op=systemrows (journal open): the System page - whether it picks rows by name, every row it knows (canonical), the rows it shows, the [SystemMenu] list and the rows removed this open. op=own: controls with no INI line whose live keys differ from controlmap.txt are written into the INI, and ControlMap_Custom.txt is removed from the game folder; op=state reports customMap, lastOwn and ownLines. op=function with event (the extra row's name), device and key (a button name or code, or \"none\" to unbind): binds an extra [Functions] row and writes its value into the target mod's settings file. op=deliver writes every extra row's value to its target file now. op=opencontrols opens the System page's Controls panel (journal open, System tab) by setting the page's own state, never by driving keys. op=state lists the extra rows with the SKSE input code each one delivers.\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{\"op\":{\"type\":\"string\"},\"context\":{\"type\":\"string\"},\"event\":{\"type\":\"string\"},\"device\":{\"type\":\"string\"},\"key\":{\"type\":\"string\"},\"seconds\":{\"type\":\"string\"}}},"
			"\"readOnly\":false"
			"}";

		if (devBench->RegisterTool("uvc.control", descriptor, &ControlTool, nullptr))
		{
			logger::info("Registered \"uvc.control\" with DevBench (build {})", devBench->GetBuildNumber());
			registered = true;
		}
	}
}
