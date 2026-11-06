#include "PCH.h"

#include "layout/LayoutDevBenchTool.h"

#include "DevBench/DevBenchAPI.h"
#include "layout/Layout.h"
#include "layout/LayoutSettings.h"
#include "Settings.h"
#include "utils/Logger.h"

#include <format>
#include <string>
#include <string_view>

// DevBench tool "uvcl.control" (rules 31 and 64): the graphic's live state, what each box holds, and opening and closing it with no key
// pressed. A test still presses the real X. English only: only a tool reads this (rule 66).
namespace LayoutDevBenchTool
{
	namespace
	{
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
				const auto close = a_json.find('"', pos + 1);
				return close == std::string_view::npos ? std::string{} : std::string(a_json.substr(pos + 1, close - pos - 1));
			}
			std::string out;
			while (pos < a_json.size() && a_json[pos] != ',' && a_json[pos] != '}' && a_json[pos] != ' ') { out += a_json[pos++]; }
			return out;
		}

		void ControlTool(void*, const char* a_args, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			const std::string_view args = a_args ? a_args : "";
			const std::string op = Get(args, "op");
			std::string ok = "true";
			if (op == "open") { layout::RequestOpen(); }
			else if (op == "close") { layout::RequestClose(); }
			else if (op == "context")
			{
				try { layout::RequestContext(std::stoi(Get(args, "value"))); }
				catch (...) { ok = "false,\"error\":\"value is a whole number\""; }
			}
			else if (op == "faces")
			{
				layoutsettings::Faces faces{};
				if (layoutsettings::ParseFaces(Get(args, "value"), faces))
				{
					layoutsettings::faces = faces;
					// one line for both: the Controls sheet reads the same GamepadFaces:Controls (UVC 1.4.0)
					switch (faces)
					{
					case layoutsettings::Faces::kXbox: settings::controls::gamepadFaces = settings::controls::Faces::kXbox; break;
					case layoutsettings::Faces::kPlayStation: settings::controls::gamepadFaces = settings::controls::Faces::kPlayStation; break;
					case layoutsettings::Faces::kSteam: settings::controls::gamepadFaces = settings::controls::Faces::kSteam; break;
					default: settings::controls::gamepadFaces = settings::controls::Faces::kAuto; break;
					}
					if (!layoutsettings::Save()) { ok = "false,\"error\":\"the INI could not be written\""; }
				}
				else { ok = "false,\"error\":\"value is Auto, PlayStation, Xbox or Steam\""; }
			}
			else if (op == "hint")
			{
				try { layout::SetHintPosition(std::stod(Get(args, "right")), std::stod(Get(args, "y"))); }
				catch (...) { ok = "false,\"error\":\"right and y are numbers, in the 1280 by 720 design\""; }
			}
			else if (!op.empty() && op != "state") { ok = "false,\"error\":\"op is state, open, close, context, faces or hint\""; }

			const std::string out = std::format(R"({{"ok":{},"op":"{}",{}}})", ok, op.empty() ? "state" : op, layout::StateJson());
			a_write(a_sink, out.c_str());
		}
	}

	void Init(bool a_lastAttempt)
	{
		static bool registered = false;
		if (registered) { return; }
		DevBenchAPI::IDevBenchInterface001* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench)
		{
			if (a_lastAttempt) { logger::info("DevBench not detected; skipping the \"uvcl.control\" tool"); }
			return;
		}
		constexpr const char* descriptor =
			"{"
			"\"description\":\"Unbind Vanilla Controls controller graphic (formerly UVC Controller Layout): live state and driver. op=state (default): faces, whether UVC's API is there, the System page's path and state, whether the input wrap is in, whether the graphic is open, the hint, rows read at the last open, scale and origin, font, and the lines of every box. op=open / op=close (next journal frame; opens only on the Controls page). op=context value=N: move N contexts, wrapping round. op=faces value=Auto|PlayStation|Xbox|Steam writes GamepadFaces:Controls in UnbindControls.ini (the sheet follows it too); takes effect at the next open. op=hint right=N y=N moves the hint for this session, in the 1280 by 720 design.\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{\"op\":{\"type\":\"string\"},\"value\":{\"type\":\"string\"},\"right\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"}}},"
			"\"readOnly\":false"
			"}";
		if (devBench->RegisterTool("uvcl.control", descriptor, &ControlTool, nullptr))
		{
			logger::info("registered \"uvcl.control\" with DevBench (build {})", devBench->GetBuildNumber());
			registered = true;
		}
	}
}
