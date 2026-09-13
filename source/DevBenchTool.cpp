#include "PCH.h"

#include "DevBenchTool.h"

#include "DevBench/DevBenchAPI.h"
#include "Settings.h"
#include "Unbinder.h"
#include "utils/Logger.h"
#include "utils/Strings.h"

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

		// The value of a top-level JSON member, as text: a quoted string (unescaped for \" and \\)
		// or a bare number/word. Empty when absent.
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

		// Runs a_fn on the game's main thread and waits (the handler runs on DevBench's own
		// thread; the ControlMap is only touched on the main thread).
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
			if (op == "rows")
			{
				const int ctx = ContextArg(args);
				if (ctx == -2) { a_write(a_sink, R"({"ok":false,"op":"rows","error":"unknown context"})"); return; }
				a_write(a_sink, unbinder::RowsJson(ctx).c_str());
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
				if (!ran) { a_write(a_sink, std::format(R"({{"ok":false,"op":"{}","error":"main thread did not run the task in time"}})", op).c_str()); return; }
				a_write(a_sink, std::format(R"({{"ok":{},"op":"{}","context":"{}","event":"{}","device":"{}","why":"{}"}})", ok ? "true" : "false", op,
											unbinder::ContextName(ctx), EscapeJson(event), unbinder::DeviceName(dev), EscapeJson(why)).c_str());
				return;
			}
			if (op == "apply")
			{
				const bool ran = RunOnMainThread([]() { unbinder::ApplyAll("tool"); });
				a_write(a_sink, std::format(R"({{"ok":{},"op":"apply"}})", ran ? "true" : "false").c_str());
				return;
			}
			if (op == "reload")
			{
				bool ok = false;
				const bool ran = RunOnMainThread([&]() { unbinder::RestoreAll("tool reload"); ok = settings::Reload(); unbinder::ApplyAll("tool reload"); });
				a_write(a_sink, std::format(R"({{"ok":{},"op":"reload"}})", (ran && ok) ? "true" : "false").c_str());
				return;
			}
			if (op == "save")
			{
				bool ok = false;
				const bool ran = RunOnMainThread([&]() { ok = settings::Save(); });
				a_write(a_sink, std::format(R"({{"ok":{},"op":"save"}})", (ran && ok) ? "true" : "false").c_str());
				return;
			}
			if (op == "restore")
			{
				const bool ran = RunOnMainThread([]() { unbinder::ClearAll(); settings::RestoreDefaults(); settings::Save(); });
				a_write(a_sink, std::format(R"({{"ok":{},"op":"restore"}})", ran ? "true" : "false").c_str());
				return;
			}
			if (op == "enable" || op == "disable")
			{
				const bool on = op == "enable";
				const bool ran = RunOnMainThread([on]() {
					settings::general::enabled = on;
					if (on) { unbinder::ApplyAll("tool enable"); } else { unbinder::RestoreAll("tool disable"); }
					settings::Save();
				});
				a_write(a_sink, std::format(R"({{"ok":{},"op":"{}"}})", ran ? "true" : "false", op).c_str());
				return;
			}
			if (op == "strings")
			{
				a_write(a_sink, std::format(R"({{"ok":true,"op":"strings","strings":{}}})", strings::StatusJson()).c_str());
				return;
			}

			const std::string json = std::format(
				"{{\"ok\":true,\"op\":\"state\","
				"\"settings\":{{\"enabled\":{},\"logLevel\":{},\"iniPath\":\"{}\"}},{}}}",
				settings::general::enabled ? "true" : "false", settings::debug::logLevel, EscapeJson(settings::GetIniPath()), unbinder::StateJson());
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
			"\"description\":\"Unbind Vanilla Controls live state and driver. op=state (default): settings, the list of unbound "
			"controls with their remembered keys, last apply. op=dump [context]: every mapping of the live ControlMap (event, key, modifier, "
			"index, remappable, linked, flags) per context and device. op=rows [context]: the page's snapshot. op=unbind / op=rebind with "
			"context (name or index), event, device (keyboard|mouse|gamepad): the same calls the page's switches make, saved. op=apply re-applies "
			"the list; op=reload restores, re-reads the INI and applies; op=save; op=restore gives every key back and empties the list; "
			"op=enable / op=disable; op=strings reports the active language.\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{\"op\":{\"type\":\"string\"},\"context\":{\"type\":\"string\"},\"event\":{\"type\":\"string\"},\"device\":{\"type\":\"string\"}}},"
			"\"readOnly\":false"
			"}";

		if (devBench->RegisterTool("uvc.control", descriptor, &ControlTool, nullptr))
		{
			logger::info("Registered \"uvc.control\" with DevBench (build {})", devBench->GetBuildNumber());
			registered = true;
		}
	}
}
