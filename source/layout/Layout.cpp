// Unbind Vanilla Controls, the controller graphic (was UVC Controller Layout until 1.4.0) - own code, GPL-3.0-or-later.
//
// A controller drawn on the game's Controls page (Journal, System, Controls), a pointer from each button to a box, and in each box what
// that button does now, in Unbind Vanilla Controls. No movie is replaced and none is shipped: everything is drawn into the journal's
// own movie from this DLL, as Movement Speed Slider's row and Unbind Vanilla Controls' sheet are.
//
// - After each journal frame this finds the System page (the same paths as the other two mods). Once UVC has wrapped the page's
//   handleInput (its member _uvcHandleInput) this wraps it again, so this mod sees input first. While the graphic is up it takes every
//   input, so nothing reaches the sheet or the journal beneath it.
// - On the Controls page (SystemPage.INPUT_MAPPING_STATE) with no remap under way, X (the page's own test for that button:
//   navEquivalent gamepad_X, or code 88 on the keyboard, which the page uses only on the save list) asks for the graphic. The clips are
//   made on the next frame, not inside the input call.
// - Curves: nothing here uses curveTo. Dragon's Eye Minimap found that it never renders in that movie while moveTo, lineTo and
//   beginFill do (source/CompassRing.cpp there), so every round shape is straight segments.
// - Coordinates are a 1280 by 720 design, scaled to fit Stage.visibleRect and centred, so a journal's reskin or an ultrawide screen does
//   not move anything: nothing is measured from the journal, and the art is under _root, above all of it.
// - One context at a time (the owner, 2026-11-04, like The Witcher 3's controls screen): its name at the top, LB and RB (or D-pad left and
//   right, or a click on the name) step through the contexts UVC has something bound in, wrapping round, and the panel is drawn again.
// - Which pad: FacesNow. An XInput pad does not say what it is, so Auto goes by Steam's own files first (SteamConfig.cpp), then what the game does say.
// - Fonts: the name the journal's own Controls rows use, whatever that journal calls it. The face buttons are drawn, so no font needs
//   them.
#include "PCH.h"

#include "layout/Layout.h"

#include "layout/PadTables.h"
#include "layout/LayoutRows.h"
#include "layout/SteamConfig.h"
#include "layout/LayoutSettings.h"
#include "layout/LayoutStrings.h"
#include "utils/Logger.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <format>
#include <mutex>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

namespace layout
{
	namespace
	{
		constexpr std::array kPages = {
			"_root.QuestJournalFader.Menu_mc.SystemFader.Page_mc",
			"_root.Menu_mc.SystemFader.Page_mc",
			"_level0.QuestJournalFader.Menu_mc.SystemFader.Page_mc",
		};
		constexpr int kInputMappingState = 6;  // SystemPage.INPUT_MAPPING_STATE, in every journal read (vanilla, Norden UI)
		constexpr const char* kUvcWrapMember = "_uvcHandleInput";
		constexpr const char* kOurWrapMember = "_uvclHandleInput";
		constexpr const char* kPanelName = "uvclPanel";
		constexpr const char* kHintName = "uvclHint";
		constexpr const char* kPictureName = "uvclPicture";  // the dark sheet and the pad's picture, under the panel; kept while LB and RB rebuild the panel
		constexpr int kPanelDepth = 30400;
		constexpr int kHintDepth = 30399;
		constexpr int kPictureDepth = 30398;

		constexpr double kDesignW = 1280.0;
		constexpr double kDesignH = 720.0;
		constexpr double kCx = 640.0;
		constexpr double kBoxW = 300.0;
		constexpr double kNameBottom = 62.0;  // the name at the top, which a click steps
		constexpr double kLeftBoxX = 12.0;
		constexpr double kRightBoxX = kDesignW - 12.0 - kBoxW;
		constexpr double kColTop = 66.0;
		constexpr double kColBottom = 676.0;
		constexpr double kLineH = 17.0;
		constexpr double kBoxPad = 9.0;
		constexpr double kMinGap = 5.0;
		constexpr double kMaxGap = 16.0;
		constexpr double kStub = 16.0;
		constexpr std::size_t kLinesInBox = 6;  // a sixth becomes "+N more" only when the column would not fit otherwise: see PlaceColumn

		constexpr std::uint32_t kPartFill = 0x3A3D44;
		constexpr std::uint32_t kPointer = 0xD6D8DE;
		constexpr std::uint32_t kBoxFill = 0x0A0A0C;
		constexpr std::uint32_t kBoxEdge = 0xEBEBEE;
		constexpr std::uint32_t kDot = 0xF5F5F8;
		constexpr std::uint32_t kDim = 0xAAACB2;
		constexpr std::uint32_t kText = 0xFFFFFF;

		struct Pt
		{
			double x;
			double y;
		};

		// The game's own say: PlayStation when it says a PlayStation pad is in use (SystemPage.iPlatform: CONTROLLER_ORBIS 3,
		// CONTROLLER_PROSPERO 5), or when the button prompts it draws are PlayStation's (the name gamepad.txt gives A: a UI mod's counts,
		// since those are the prompts on the player's screen); otherwise Xbox.
		layoutsettings::Faces FacesFromGame(const RE::GFxValue& a_page)
		{
			RE::GFxValue value;
			if (a_page.IsObject() && a_page.GetMember("iPlatform", &value) && value.IsNumber())
			{
				const int platform = static_cast<int>(value.GetNumber());
				logger::debug("layout: iPlatform {}", platform);
				if (platform == 3 || platform == 5) { return layoutsettings::Faces::kPlayStation; }
			}
			if (auto* devices = RE::BSInputDeviceManager::GetSingleton())
			{
				RE::BSFixedString name;
				if (devices->GetButtonNameFromID(RE::INPUT_DEVICE::kGamepad, 0x1000, name) && name.c_str())
				{
					logger::debug("layout: the game's name for A: {}", name.c_str());
					if (std::string_view(name.c_str()).starts_with("PS")) { return layoutsettings::Faces::kPlayStation; }
				}
			}
			return layoutsettings::Faces::kXbox;
		}

		std::string g_facesFrom;  // main thread: what chose the pad, for the log and the tool

		// The pad to draw. A hand-picked one always wins. Auto: the pad Steam's log says is connected; the type in the game's active Steam
		// Input config; what the game itself says.
		layoutsettings::Faces FacesNow(const RE::GFxValue& a_page)
		{
			const auto wanted = layoutsettings::faces.load();
			if (wanted != layoutsettings::Faces::kAuto)
			{
				g_facesFrom = "the setting";
				return wanted;
			}
			const auto& steam = steamconfig::Current();
			if (steam.logPad)
			{
				// the log can't tell Valve's two pads apart, and only one has a picture
				if (*steam.logPad != layoutsettings::Faces::kSteam || !steam.configRead || steam.configType == "controller_triton")
				{
					g_facesFrom = "Steam's log";
					return *steam.logPad;
				}
			}
			if (steam.configRead)
			{
				if (const auto pad = steamconfig::PadOfType(steam.configType))
				{
					g_facesFrom = "the Steam Input config";
					return *pad;
				}
			}
			g_facesFrom = "the game";
			return FacesFromGame(a_page);
		}

		const padtables::Pad& PadOf(layoutsettings::Faces a_f)
		{
			switch (a_f)
			{
			case layoutsettings::Faces::kPlayStation: return padtables::kPlayStation;
			case layoutsettings::Faces::kSteam: return padtables::kSteam;
			default: return padtables::kXbox;
			}
		}

		// Where a box's leader goes. On the Steam picture a button can come from another control by the player's config: the leader follows
		// it. A button the config says nothing clear about stays on its own control.
		padtables::Anchor AnchorOf(int a_slot, layoutsettings::Faces a_f)
		{
			int part = a_slot;
			if (a_f == layoutsettings::Faces::kSteam && a_slot >= 0 && a_slot < rows::kButtonCount)
			{
				const auto& senders = steamconfig::Current().senders[static_cast<std::size_t>(a_slot)];
				if (!senders.empty() && std::find(senders.begin(), senders.end(), a_slot) == senders.end()) { part = senders.front(); }
			}
			if (part >= 100) { return padtables::kParts[static_cast<std::size_t>(part - 100)]; }
			return PadOf(a_f).at[static_cast<std::size_t>(part)];
		}

		// A pad's own trackpad entry, which the game has no row for: what Steam Input makes it do, or that the game doesn't bind it. It never
		// invents a binding: the game's control map and its button tables have no touchpad at all (read 2026-11-05: Interface\Controls\PC
		// controlmap.txt, gamepad.txt, gamepadorbis.txt, and no such word in SkyrimSE.exe).
		std::vector<rows::Line> TrackpadEntry(int a_slot, layoutsettings::Faces a_f)
		{
			const auto& steam = steamconfig::Current();
			const auto fits = steam.configRead && steamconfig::PadOfType(steam.configType) == a_f;
			std::vector<rows::Line> out;
			const auto add = [&](const std::string& a_text) { out.push_back({ a_text, false }); };
			const std::string notBound = TR("UVCL_PadNotBound", "Not bound by the game");
			if (a_slot == padtables::kTouchpad)
			{
				add(TR("UVCL_Touchpad", "Touchpad"));
				if (!fits || (!steam.left.read && !steam.right.read))
				{
					add(notBound);
					return out;
				}
				// Steam Input sees a PlayStation touchpad as two halves
				const auto half = [&](const std::string& a_name, const steamconfig::TrackpadLines& a_pad)
				{
					std::string joined;
					for (const auto& line : a_pad.lines) { joined += (joined.empty() ? "" : ", ") + line; }
					add(std::format("{}: {}", a_name, joined.empty() ? TR("UVCL_PadNothing", "Nothing bound") : joined));
				};
				half(TR("UVCL_LeftHalf", "Left half"), steam.left);
				half(TR("UVCL_RightHalf", "Right half"), steam.right);
				return out;
			}
			const bool left = a_slot == padtables::kLeftPad;
			add(left ? TR("UVCL_LeftTrackpad", "Left trackpad") : TR("UVCL_RightTrackpad", "Right trackpad"));
			const auto& pad = left ? steam.left : steam.right;
			if (!fits || !pad.read)
			{
				add(notBound);
				return out;
			}
			for (const auto& line : pad.lines) { add(line); }
			return out;
		}

		const char* TrackpadId(int a_slot)
		{
			if (a_slot == padtables::kTouchpad) { return "touchpad"; }
			return a_slot == padtables::kLeftPad ? "leftTrackpad" : "rightTrackpad";
		}

		// ---- drawing: straight segments only (see the head of this file) ----

		void Draw2(RE::GFxValue& a_clip, const char* a_op, double a_x, double a_y)
		{
			std::array<RE::GFxValue, 2> args{ RE::GFxValue(a_x), RE::GFxValue(a_y) };
			(void)a_clip.Invoke(a_op, nullptr, args.data(), args.size());
		}

		void Stroke(RE::GFxValue& a_clip, double a_width, std::uint32_t a_rgb, double a_alpha = 100.0)
		{
			std::array<RE::GFxValue, 3> args{ RE::GFxValue(a_width), RE::GFxValue(static_cast<double>(a_rgb)), RE::GFxValue(a_alpha) };
			(void)a_clip.Invoke("lineStyle", nullptr, args.data(), args.size());
		}

		void NoStroke(RE::GFxValue& a_clip) { (void)a_clip.Invoke("lineStyle"); }

		void Polygon(RE::GFxValue& a_clip, const std::vector<Pt>& a_pts, std::uint32_t a_rgb, double a_alpha = 100.0)
		{
			if (a_pts.size() < 3) { return; }
			std::array<RE::GFxValue, 2> fill{ RE::GFxValue(static_cast<double>(a_rgb)), RE::GFxValue(a_alpha) };
			(void)a_clip.Invoke("beginFill", nullptr, fill.data(), fill.size());
			Draw2(a_clip, "moveTo", a_pts[0].x, a_pts[0].y);
			for (std::size_t i = 1; i < a_pts.size(); ++i) { Draw2(a_clip, "lineTo", a_pts[i].x, a_pts[i].y); }
			Draw2(a_clip, "lineTo", a_pts[0].x, a_pts[0].y);
			(void)a_clip.Invoke("endFill");
		}

		void OpenLine(RE::GFxValue& a_clip, const std::vector<Pt>& a_pts)
		{
			if (a_pts.size() < 2) { return; }
			Draw2(a_clip, "moveTo", a_pts[0].x, a_pts[0].y);
			for (std::size_t i = 1; i < a_pts.size(); ++i) { Draw2(a_clip, "lineTo", a_pts[i].x, a_pts[i].y); }
		}

		std::vector<Pt> Round(Pt a_c, double a_r, int a_segments = 28)
		{
			std::vector<Pt> pts;
			pts.reserve(static_cast<std::size_t>(a_segments));
			for (int i = 0; i < a_segments; ++i)
			{
				const double a = 2.0 * std::numbers::pi * i / a_segments;
				pts.push_back({ a_c.x + std::cos(a) * a_r, a_c.y + std::sin(a) * a_r });
			}
			return pts;
		}

		std::vector<Pt> Rect(double a_x, double a_y, double a_w, double a_h)
		{
			return { { a_x, a_y }, { a_x + a_w, a_y }, { a_x + a_w, a_y + a_h }, { a_x, a_y + a_h } };
		}

		// ---- text ----

		struct TextStyle
		{
			double size = 15.0;
			std::uint32_t colour = kText;
			const char* align = "left";
			bool shrink = true;
		};

		struct Canvas
		{
			RE::GFxValue clip;
			std::string font;
			int nextDepth = 100;
			int fields = 0;
		};

		// Returns the field, undefined when the movie would not make one.
		RE::GFxValue Text(Canvas& a_c, double a_x, double a_y, double a_w, double a_h, const std::string& a_text, const TextStyle& a_s)
		{
			const std::string name = std::format("t{}", a_c.nextDepth);
			std::array<RE::GFxValue, 6> args{ RE::GFxValue(name.c_str()), RE::GFxValue(static_cast<double>(a_c.nextDepth++)), RE::GFxValue(a_x), RE::GFxValue(a_y), RE::GFxValue(a_w), RE::GFxValue(a_h) };
			(void)a_c.clip.Invoke("createTextField", nullptr, args.data(), args.size());
			RE::GFxValue field;
			if (!a_c.clip.GetMember(name.c_str(), &field) || !field.IsDisplayObject())
			{
				logger::warn("layout: a text field could not be made");
				return {};
			}
			field.SetMember("embedFonts", RE::GFxValue(true));
			field.SetMember("selectable", RE::GFxValue(false));
			field.SetMember("multiline", RE::GFxValue(false));
			field.SetMember("wordWrap", RE::GFxValue(false));
			if (a_s.shrink) { field.SetMember("textAutoSize", RE::GFxValue("shrink")); }
			RE::GFxValue format;
			if (field.Invoke("getTextFormat", &format, nullptr, 0) && format.IsObject())
			{
				format.SetMember("font", RE::GFxValue(a_c.font.c_str()));
				format.SetMember("size", RE::GFxValue(a_s.size));
				format.SetMember("color", RE::GFxValue(static_cast<double>(a_s.colour)));
				format.SetMember("align", RE::GFxValue(a_s.align));
				(void)field.Invoke("setNewTextFormat", nullptr, &format, 1);
				field.SetMember("text", RE::GFxValue(a_text.c_str()));
				(void)field.Invoke("setTextFormat", nullptr, &format, 1);
			}
			else
			{
				field.SetMember("text", RE::GFxValue(a_text.c_str()));
			}
			++a_c.fields;
			return field;
		}

		// ---- the buttons ----

		void FaceGlyph(Canvas& a_c, rows::Button a_b, layoutsettings::Faces a_f, Pt a_at, double a_r)
		{
			auto& clip = a_c.clip;
			NoStroke(clip);
			Polygon(clip, Round(a_at, a_r), kPartFill);
			if (a_f == layoutsettings::Faces::kPlayStation)
			{
				const double s = a_r * 0.52;
				switch (a_b)
				{
				case rows::kY:
					Stroke(clip, 2.0, 0x6FD3B8);
					Draw2(clip, "moveTo", a_at.x, a_at.y - s * 1.1);
					Draw2(clip, "lineTo", a_at.x + s, a_at.y + s * 0.75);
					Draw2(clip, "lineTo", a_at.x - s, a_at.y + s * 0.75);
					Draw2(clip, "lineTo", a_at.x, a_at.y - s * 1.1);
					break;
				case rows::kB:
				{
					Stroke(clip, 2.0, 0xE66A6A);
					auto circle = Round(a_at, s * 1.05, 20);
					circle.push_back(circle.front());
					OpenLine(clip, circle);
					break;
				}
				case rows::kA:
					Stroke(clip, 2.0, 0x7FB0E6);
					OpenLine(clip, { { a_at.x - s, a_at.y - s }, { a_at.x + s, a_at.y + s } });
					OpenLine(clip, { { a_at.x - s, a_at.y + s }, { a_at.x + s, a_at.y - s } });
					break;
				default:
					Stroke(clip, 2.0, 0xD9A0D0);
					OpenLine(clip, { { a_at.x - s, a_at.y - s }, { a_at.x + s, a_at.y - s }, { a_at.x + s, a_at.y + s }, { a_at.x - s, a_at.y + s }, { a_at.x - s, a_at.y - s } });
					break;
				}
				NoStroke(clip);
				return;
			}
			std::uint32_t colour = 0x5FB760;
			const char* letter = "A";
			if (a_b == rows::kB) { colour = 0xD9534F; letter = "B"; }
			if (a_b == rows::kX) { colour = 0x5B8DD9; letter = "X"; }
			if (a_b == rows::kY) { colour = 0xE3C04E; letter = "Y"; }
			if (a_f == layoutsettings::Faces::kSteam) { colour = kText; }  // its letters are white on the pad
			Stroke(clip, 2.0, colour);
			auto ring = Round(a_at, a_r - 1.0, 20);
			ring.push_back(ring.front());
			OpenLine(clip, ring);
			NoStroke(clip);
			const double size = a_r * 1.2;
			Text(a_c, a_at.x - a_r, a_at.y - size * 0.68, a_r * 2.0, size * 1.4, letter, { size, colour, "center", false });
		}

		// One arm of the D-pad, its point outward; lit picks which arm stands out in a box's small picture.
		void DpadArrow(RE::GFxValue& a_clip, Pt a_at, rows::Button a_arm, double a_s, std::uint32_t a_rgb)
		{
			std::vector<Pt> pts;
			switch (a_arm)
			{
			case rows::kDUp: pts = { { a_at.x, a_at.y - a_s }, { a_at.x + a_s, a_at.y + a_s * 0.7 }, { a_at.x - a_s, a_at.y + a_s * 0.7 } }; break;
			case rows::kDDown: pts = { { a_at.x, a_at.y + a_s }, { a_at.x + a_s, a_at.y - a_s * 0.7 }, { a_at.x - a_s, a_at.y - a_s * 0.7 } }; break;
			case rows::kDLeft: pts = { { a_at.x - a_s, a_at.y }, { a_at.x + a_s * 0.7, a_at.y - a_s }, { a_at.x + a_s * 0.7, a_at.y + a_s } }; break;
			default: pts = { { a_at.x + a_s, a_at.y }, { a_at.x - a_s * 0.7, a_at.y - a_s }, { a_at.x - a_s * 0.7, a_at.y + a_s } }; break;
			}
			NoStroke(a_clip);
			Polygon(a_clip, pts, a_rgb);
		}

		// A button's short name in a pill: triggers, bumpers, Back and Start, and a stick in a box.
		void Pill(Canvas& a_c, Pt a_at, double a_w, double a_h, const std::string& a_name, double a_size)
		{
			NoStroke(a_c.clip);
			Polygon(a_c.clip, Rect(a_at.x - a_w * 0.5, a_at.y - a_h * 0.5, a_w, a_h), kPartFill);
			Text(a_c, a_at.x - a_w * 0.5, a_at.y - a_size * 0.72, a_w, a_size * 1.5, a_name, { a_size, kText, "center", true });
		}

		void BoxGlyph(Canvas& a_c, rows::Button a_b, layoutsettings::Faces a_f, Pt a_at)
		{
			switch (a_b)
			{
			case rows::kA:
			case rows::kB:
			case rows::kX:
			case rows::kY:
				FaceGlyph(a_c, a_b, a_f, a_at, 15.0);
				break;
			case rows::kDUp:
			case rows::kDDown:
			case rows::kDLeft:
			case rows::kDRight:
				NoStroke(a_c.clip);
				Polygon(a_c.clip, Rect(a_at.x - 15.0, a_at.y - 15.0, 30.0, 30.0), kPartFill);  // square, as the D-pad is: circles are for the face buttons
				DpadArrow(a_c.clip, a_at, a_b, 8.0, kText);
				break;
			default:
				Pill(a_c, a_at, 44.0, 24.0, rows::ShortName(a_b, a_f), 14.0);
				break;
			}
		}

		// ---- the panel ----

		struct State
		{
			std::string path;
			bool pageFound = false;
			int pageState = -1;
			bool remap = false;
			bool wrapped = false;
			bool uvcWrapSeen = false;
			bool apiThere = false;
			bool open = false;
			bool hintShown = false;
			int rowsSeen = 0;
			int fields = 0;
			double scale = 0.0;
			double originX = 0.0;
			double originY = 0.0;
			std::string font;
			std::string boxes;  // JSON
			std::string facesDrawn;
			std::string facesFrom;
			bool steamInput = false;
			bool configRead = false;
			std::string configType;
			std::string steamWhy;
			double pictureWidth = 0.0;
			std::string contextId;
			int contextIndex = 0;
			int contextCount = 0;
		};

		std::mutex g_lock;
		State g_state;  // g_lock

		std::string g_pictureSwf;  // main thread: what the picture clip holds
		bool g_pictureSeen = false;
		int g_pictureFrames = 0;

		std::atomic_int g_request{ 0 };  // 1 open, 2 close; the journal's frame takes it
		std::atomic_int g_contextStep{ 0 };  // contexts to move by; the journal's frame takes it
		int g_context = 0;  // main thread: which context is on the panel; each open starts at Gameplay
		std::atomic_bool g_open{ false };  // main thread writes; the input wrap reads on the same thread
		std::atomic<double> g_hintRight{ 1240.0 };
		std::atomic<double> g_hintY{ 640.0 };
		std::atomic_bool g_hintDirty{ false };

		std::string JsonEscape(std::string_view a_in)
		{
			std::string out;
			for (const char c : a_in)
			{
				if (c == '"' || c == '\\') { out += '\\'; }
				if (static_cast<unsigned char>(c) < 0x20) { out += ' '; continue; }
				out += c;
			}
			return out;
		}

		bool Number(const RE::GFxValue& a_object, const char* a_name, double& a_out)
		{
			RE::GFxValue value;
			if (!a_object.IsObject() || !a_object.GetMember(a_name, &value) || !value.IsNumber()) { return false; }
			a_out = value.GetNumber();
			return true;
		}

		std::string StringMember(const RE::GFxValue& a_object, const char* a_name)
		{
			RE::GFxValue value;
			if (!a_object.IsObject() || !a_object.GetMember(a_name, &value) || !value.IsString() || !value.GetString()) { return {}; }
			return value.GetString();
		}

		bool FindPage(RE::GFxMovieView* a_movie, RE::GFxValue& a_page, std::string& a_path)
		{
			for (const char* page : kPages)
			{
				if (a_movie->GetVariable(&a_page, page) && a_page.IsObject())
				{
					a_path = page;
					return true;
				}
			}
			return false;
		}

		// The font of the journal's own Controls rows; the game's own name for its main font when they cannot be read.
		std::string JournalFont(RE::GFxValue& a_page)
		{
			RE::GFxValue list, row, field, format;
			if (a_page.GetMember("MappingList", &list) && list.IsObject() && list.GetMember("Entry0", &row) && row.IsObject() && row.GetMember("textField", &field) && field.IsDisplayObject() &&
				field.Invoke("getTextFormat", &format, nullptr, 0) && format.IsObject())
			{
				const std::string font = StringMember(format, "font");
				if (!font.empty()) { return font; }
			}
			logger::debug("layout: the Controls rows' font could not be read; using the game's name for its main font");
			return "$EverywhereMediumFont";
		}

		// Scale and origin of the 1280 by 720 design inside what the screen shows of this movie.
		void Fit(RE::GFxMovieView* a_movie, double& a_scale, double& a_x, double& a_y, double& a_vx, double& a_vy, double& a_vw, double& a_vh)
		{
			a_vx = 0.0;
			a_vy = 0.0;
			a_vw = kDesignW;
			a_vh = kDesignH;
			RE::GFxValue rect;
			if (a_movie->GetVariable(&rect, "Stage.visibleRect") && rect.IsObject())
			{
				double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
				if (Number(rect, "x", x) && Number(rect, "y", y) && Number(rect, "width", w) && Number(rect, "height", h) && w > 1.0 && h > 1.0)
				{
					a_vx = x;
					a_vy = y;
					a_vw = w;
					a_vh = h;
				}
			}
			else
			{
				logger::debug("layout: no Stage.visibleRect; drawing to the movie's 1280 by 720");
			}
			a_scale = std::min(a_vw / kDesignW, a_vh / kDesignH);
			a_x = a_vx + (a_vw - kDesignW * a_scale) * 0.5;
			a_y = a_vy + (a_vh - kDesignH * a_scale) * 0.5;
		}

		void RemoveClip(RE::GFxMovieView* a_movie, const char* a_name)
		{
			RE::GFxValue root, clip;
			if (a_movie->GetVariable(&root, "_root") && root.IsObject() && root.GetMember(a_name, &clip) && clip.IsDisplayObject())
			{
				(void)clip.Invoke("removeMovieClip", nullptr, nullptr, 0);
			}
		}

		bool MakeClip(RE::GFxMovieView* a_movie, const char* a_name, int a_depth, RE::GFxValue& a_clip)
		{
			RE::GFxValue root;
			if (!a_movie->GetVariable(&root, "_root") || !root.IsObject()) { return false; }
			std::array<RE::GFxValue, 2> args{ RE::GFxValue(a_name), RE::GFxValue(static_cast<double>(a_depth)) };
			(void)root.Invoke("createEmptyMovieClip", nullptr, args.data(), args.size());
			return root.GetMember(a_name, &a_clip) && a_clip.IsDisplayObject();
		}

		// The panel takes the mouse, so a click cannot land on the sheet beneath it. A click on the context's name at the top steps: its
		// left half back, its right half on.
		class PanelMouse : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				if (!a_params.thisPtr) { return; }
				double x = 0.0, y = 1000.0;
				if (!Number(*a_params.thisPtr, "_xmouse", x) || !Number(*a_params.thisPtr, "_ymouse", y)) { return; }
				if (y < kNameBottom && x > kLeftBoxX + kBoxW && x < kRightBoxX) { g_contextStep = x < kCx ? -1 : 1; }
			}
		};

		// The dark sheet and the picture: a clip of their own beneath the panel. The panel is rebuilt at each step of LB and RB; this is not, so
		// the picture is loaded once for an open and does not flicker. The art is one SWF per pad, loaded by path like HUD Position Manager's
		// widgets; loading is asynchronous, so the boxes can come a moment before the picture.
		void BuildPicture(RE::GFxMovieView* a_movie, layoutsettings::Faces a_faces, double a_scale, double a_ox, double a_oy, double a_vx, double a_vy, double a_vw, double a_vh)
		{
			const auto& pad = PadOf(a_faces);
			RE::GFxValue root, clip;
			if (a_movie->GetVariable(&root, "_root") && root.IsObject() && root.GetMember(kPictureName, &clip) && clip.IsDisplayObject() && g_pictureSwf == pad.swf) { return; }
			RemoveClip(a_movie, kPictureName);
			g_pictureSwf.clear();
			g_pictureSeen = false;
			g_pictureFrames = 0;
			if (!MakeClip(a_movie, kPictureName, kPictureDepth, clip))
			{
				logger::warn("layout: the picture's clip could not be made");
				return;
			}
			clip.SetMember("_x", RE::GFxValue(a_ox));
			clip.SetMember("_y", RE::GFxValue(a_oy));
			clip.SetMember("_xscale", RE::GFxValue(a_scale * 100.0));
			clip.SetMember("_yscale", RE::GFxValue(a_scale * 100.0));
			// the whole screen behind it, in design units
			NoStroke(clip);
			Polygon(clip, Rect((a_vx - a_ox) / a_scale, (a_vy - a_oy) / a_scale, a_vw / a_scale, a_vh / a_scale), 0x000000);  // opaque: the journal must not show through (the owner, 2026-11-05)

			RE::GFxValue holder;
			std::array<RE::GFxValue, 2> holderArgs{ RE::GFxValue("at"), RE::GFxValue(1.0) };
			(void)clip.Invoke("createEmptyMovieClip", nullptr, holderArgs.data(), holderArgs.size());
			if (!clip.GetMember("at", &holder) || !holder.IsDisplayObject())
			{
				logger::warn("layout: the picture's holder could not be made");
				return;
			}
			holder.SetMember("_x", RE::GFxValue(pad.x));
			holder.SetMember("_y", RE::GFxValue(pad.y));
			RE::GFxValue picture;
			std::array<RE::GFxValue, 2> pictureArgs{ RE::GFxValue("pic"), RE::GFxValue(1.0) };
			(void)holder.Invoke("createEmptyMovieClip", nullptr, pictureArgs.data(), pictureArgs.size());
			if (!holder.GetMember("pic", &picture) || !picture.IsDisplayObject())
			{
				logger::warn("layout: the picture could not be made");
				return;
			}
			RE::GFxValue url{ pad.swf };
			const bool called = picture.Invoke("loadMovie", nullptr, &url, 1);
			g_pictureSwf = pad.swf;
			logger::info("layout: picture: loadMovie(\"{}\") {}", pad.swf, called ? "called" : "FAILED");
		}

		struct Box
		{
			int slot = -1;
			std::vector<rows::Line> lines;
			double y = 0.0;
			double h = 0.0;
		};

		// A column's boxes stacked from the top, each as tall as its lines, the gaps opened up evenly and the stack centred. When it would not
		// fit even so, the fullest boxes are cut down first: their last line says how many more.
		void PlaceColumn(std::vector<Box>& a_boxes)
		{
			if (a_boxes.empty()) { return; }
			const double room = kColBottom - kColTop;
			const auto heightOf = [](std::size_t a_lines) { return static_cast<double>(std::max<std::size_t>(a_lines, 1)) * kLineH + 2.0 * kBoxPad; };
			std::size_t cap = kLinesInBox;
			double total = 0.0;
			for (;; --cap)
			{
				total = kMinGap * static_cast<double>(a_boxes.size() - 1);
				for (const auto& box : a_boxes) { total += heightOf(std::min(box.lines.size(), cap)); }
				if (total <= room || cap <= 2) { break; }
			}
			for (auto& box : a_boxes)
			{
				if (box.lines.size() > cap)
				{
					const auto more = box.lines.size() - cap + 1;
					box.lines.resize(cap - 1);
					box.lines.push_back({ std::format("+{} {}", more, TR("UVCL_More", "more")), false });
				}
				box.h = heightOf(box.lines.size());
			}
			double heights = 0.0;
			for (const auto& box : a_boxes) { heights += box.h; }
			const double gap = std::min(kMaxGap, std::max(kMinGap, (room - heights) / static_cast<double>(a_boxes.size() - 1)));
			double y = kColTop + (room - heights - gap * static_cast<double>(a_boxes.size() - 1)) * 0.5;
			for (auto& box : a_boxes)
			{
				box.y = y;
				y += box.h + gap;
			}
		}

		void BuildPanel(RE::GFxMovieView* a_movie, RE::GFxValue& a_page, State& a_state)
		{
			RemoveClip(a_movie, kPanelName);
			const bool fresh = !g_open.load();  // not a step of LB or RB: read everything again
			layoutsettings::Load();  // a changed line needs no restart
			if (fresh) { steamconfig::Refresh(); }
			const auto faces = FacesNow(a_page);
			if (fresh) { logger::info("layout: pad: {}, from {}", layoutsettings::FacesName(faces), g_facesFrom); }

			Canvas canvas;
			if (!MakeClip(a_movie, kPanelName, kPanelDepth, canvas.clip))
			{
				logger::warn("layout: the panel's clip could not be made");
				return;
			}
			canvas.font = JournalFont(a_page);

			double scale = 1.0, ox = 0.0, oy = 0.0, vx = 0.0, vy = 0.0, vw = kDesignW, vh = kDesignH;
			Fit(a_movie, scale, ox, oy, vx, vy, vw, vh);
			canvas.clip.SetMember("_x", RE::GFxValue(ox));
			canvas.clip.SetMember("_y", RE::GFxValue(oy));
			canvas.clip.SetMember("_xscale", RE::GFxValue(scale * 100.0));
			canvas.clip.SetMember("_yscale", RE::GFxValue(scale * 100.0));

			static PanelMouse* mouse = new PanelMouse();
			RE::GFxValue fn;
			a_movie->CreateFunction(&fn, mouse);
			canvas.clip.SetMember("onPress", fn);
			canvas.clip.SetMember("useHandCursor", RE::GFxValue(false));

			BuildPicture(a_movie, faces, scale, ox, oy, vx, vy, vw, vh);
			auto& clip = canvas.clip;
			// nothing to see, but the panel takes the mouse all over the screen, as it did when the sheet was its own
			NoStroke(clip);
			Polygon(clip, Rect((vx - ox) / scale, (vy - oy) / scale, vw / scale, vh / scale), 0x000000, 0.0);

			int rowsSeen = 0;
			const auto contexts = rows::Read(faces, rowsSeen);
			const int contextCount = static_cast<int>(contexts.size());
			g_context = contextCount > 0 ? ((g_context % contextCount) + contextCount) % contextCount : 0;
			static const rows::Table noRows{};
			const rows::Table& table = contextCount > 0 ? contexts[static_cast<std::size_t>(g_context)].table : noRows;

			// the context's name, between the two buttons that change it, and which of how many
			const std::string contextName = contextCount > 0 ? contexts[static_cast<std::size_t>(g_context)].name : TR("UVCL_Title", "Controller Layout");
			Text(canvas, kCx - 190.0, 14.0, 380.0, 36.0, contextName, { 26.0, kText, "center", true });
			if (contextCount > 1)
			{
				Pill(canvas, { kCx - 240.0, 32.0 }, 44.0, 24.0, rows::ShortName(rows::kLB, faces), 14.0);
				Pill(canvas, { kCx + 240.0, 32.0 }, 44.0, 24.0, rows::ShortName(rows::kRB, faces), 14.0);
				Text(canvas, kCx - 60.0, 46.0, 120.0, 18.0, std::format("{} / {}", g_context + 1, contextCount), { 12.0, kDim, "center", true });
			}

			// the boxes: a column for each side, in the pad's own order so that no two leaders cross
			const auto& pad = PadOf(faces);
			std::vector<Box> left, right;
			const auto column = [&](const std::array<int, 9>& a_slots, std::vector<Box>& a_out)
			{
				for (const int slot : a_slots)
				{
					if (slot < 0) { continue; }
					Box box;
					box.slot = slot;
					box.lines = slot >= 100 ? TrackpadEntry(slot, faces) : table[static_cast<std::size_t>(slot)];
					a_out.push_back(std::move(box));
				}
				PlaceColumn(a_out);
			};
			column(pad.left, left);
			column(pad.right, right);

			// the leaders under the boxes: a short level stub, then straight to the control's rim, a ringed dot at its end
			const auto leaders = [&](const std::vector<Box>& a_boxes, bool a_right)
			{
				const double sx = a_right ? kRightBoxX : kLeftBoxX + kBoxW;
				const double stubX = sx + (a_right ? -kStub : kStub);
				for (const auto& box : a_boxes)
				{
					const double sy = box.y + box.h * 0.5;
					const auto anchor = AnchorOf(box.slot, faces);
					const double vx = stubX - anchor.x;
					const double vy = sy - anchor.y;
					const double length = std::max(1.0, std::sqrt(vx * vx + vy * vy));
					const Pt end{ anchor.x + vx / length * anchor.rim, anchor.y + vy / length * anchor.rim };
					Stroke(clip, 1.5, kPointer);
					OpenLine(clip, { { sx, sy }, { stubX, sy }, end });
					Stroke(clip, 1.3, kDot);
					Polygon(clip, Round(end, 3.4, 16), kBoxFill);
				}
				NoStroke(clip);
			};
			leaders(left, false);
			leaders(right, true);

			std::string json = "{";
			bool firstBox = true;
			const auto boxes = [&](const std::vector<Box>& a_boxes, double a_x)
			{
				for (const auto& box : a_boxes)
				{
					Stroke(clip, 1.0, kBoxEdge);
					Polygon(clip, Rect(a_x, box.y, kBoxW, box.h), kBoxFill);
					NoStroke(clip);
					const Pt glyphAt{ a_x + 30.0, box.y + box.h * 0.5 };
					if (box.slot >= 100) { Polygon(clip, Rect(glyphAt.x - 12.0, glyphAt.y - 12.0, 24.0, 24.0), kPartFill); }
					else { BoxGlyph(canvas, static_cast<rows::Button>(box.slot), faces, glyphAt); }
					if (!firstBox) { json += ","; }
					firstBox = false;
					json += std::format(R"("{}":[)", box.slot >= 100 ? TrackpadId(box.slot) : rows::ButtonId(static_cast<rows::Button>(box.slot)));
					double lineY = box.y + kBoxPad - 1.0;
					for (std::size_t i = 0; i < box.lines.size(); ++i)
					{
						const auto& line = box.lines[i];
						Text(canvas, a_x + 60.0, lineY, kBoxW - 66.0, kLineH + 2.0, line.text, { 14.0, line.mod ? 0xD8D8D8u : kText, "left", true });
						lineY += kLineH;
						json += std::format(R"({}"{}")", i ? "," : "", JsonEscape(line.text));
					}
					json += "]";
				}
			};
			boxes(left, kLeftBoxX);
			boxes(right, kRightBoxX);
			json += "}";

			// how to leave, beneath the pad
			const Pt closeAt{ kCx - 34.0, 660.0 };
			FaceGlyph(canvas, rows::kB, faces, closeAt, 13.0);
			Text(canvas, closeAt.x + 20.0, closeAt.y - 11.0, 180.0, 24.0, TR("UVCL_Close", "Close"), { 16.0, kText, "left", true });

			g_open = true;
			a_state.open = true;
			a_state.rowsSeen = rowsSeen;
			a_state.fields = canvas.fields;
			a_state.scale = scale;
			a_state.originX = ox;
			a_state.originY = oy;
			a_state.font = canvas.font;
			a_state.boxes = std::move(json);
			a_state.facesDrawn = layoutsettings::FacesName(faces);
			a_state.facesFrom = g_facesFrom;
			{
				const auto& steam = steamconfig::Current();
				a_state.steamInput = steam.steamInput;
				a_state.configRead = steam.configRead;
				a_state.configType = steam.configType;
				a_state.steamWhy = steam.configRead ? steam.logWhy : steam.why;
			}
			a_state.contextId = contextCount > 0 ? contexts[static_cast<std::size_t>(g_context)].id : std::string();
			a_state.contextIndex = g_context;
			a_state.contextCount = contextCount;
			logger::info("layout: drawn: \"{}\" ({} of {}), {} faces, {} rows read, {} text fields, scale {:.3f}, font \"{}\"", a_state.contextId, g_context + 1, contextCount, layoutsettings::FacesName(faces), rowsSeen, canvas.fields, scale, canvas.font);
		}

		void ClosePanel(RE::GFxMovieView* a_movie)
		{
			RemoveClip(a_movie, kPanelName);
			RemoveClip(a_movie, kPictureName);
			g_pictureSwf.clear();
			if (g_open.exchange(false)) { logger::info("layout: closed"); }
		}

		// The line on the Controls page that says the graphic is there.
		void BuildHint(RE::GFxMovieView* a_movie, RE::GFxValue& a_page)
		{
			RemoveClip(a_movie, kHintName);
			Canvas canvas;
			if (!MakeClip(a_movie, kHintName, kHintDepth, canvas.clip))
			{
				logger::warn("layout: the hint's clip could not be made");
				return;
			}
			canvas.font = JournalFont(a_page);
			double scale = 1.0, ox = 0.0, oy = 0.0, vx = 0.0, vy = 0.0, vw = kDesignW, vh = kDesignH;
			Fit(a_movie, scale, ox, oy, vx, vy, vw, vh);
			canvas.clip.SetMember("_x", RE::GFxValue(ox));
			canvas.clip.SetMember("_y", RE::GFxValue(oy));
			canvas.clip.SetMember("_xscale", RE::GFxValue(scale * 100.0));
			canvas.clip.SetMember("_yscale", RE::GFxValue(scale * 100.0));

			const double right = g_hintRight.load();
			const double y = g_hintY.load();
			const std::string text = TR("UVCL_Title", "Controller Layout");
			auto field = Text(canvas, right - 260.0, y - 11.0, 260.0, 24.0, text, { 16.0, kText, "right", true });
			double width = 200.0;
			if (field.IsDisplayObject()) { (void)Number(field, "textWidth", width); }
			steamconfig::Refresh();  // Auto's pad, for the Square or X drawn here
			FaceGlyph(canvas, rows::kX, FacesNow(a_page), { right - width - 24.0, y }, 12.0);
			logger::debug("layout: hint built at right {} y {}, text width {}", right, y, width);
		}

		// The page's handleInput, wrapped outside UVC's wrap.
		class LayoutInput : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				if (a_params.thisPtr && a_params.argCount >= 1 && a_params.args && a_params.args[0].IsObject() && Handle(*a_params.thisPtr, a_params.args[0]))
				{
					if (a_params.retVal) { a_params.retVal->SetBoolean(true); }
					return;
				}
				RE::GFxValue original;
				if (!a_params.thisPtr || !a_params.thisPtr->GetMember(kOurWrapMember, &original) || original.IsUndefined()) { return; }
				std::vector<RE::GFxValue> args;
				args.push_back(*a_params.thisPtr);
				for (std::uint32_t i = 0; i < a_params.argCount; ++i) { args.push_back(a_params.args[i]); }
				RE::GFxValue result;
				original.Invoke("call", &result, args.data(), args.size());
				if (a_params.retVal) { *a_params.retVal = result; }
			}

		private:
			static bool Handle(RE::GFxValue& a_page, const RE::GFxValue& a_details)
			{
				const std::string value = StringMember(a_details, "value");
				const std::string nav = StringMember(a_details, "navEquivalent");
				double code = -1.0;
				(void)Number(a_details, "code", code);

				if (g_open.load() || g_request.load() == 1)
				{
					if (value == "keyDown" && g_request.load() == 0)
					{
						// Every journal's names for Cancel: the game's and SkyUI's movies define gamepad_B and escape, Norden UI's escape-gamepad_B; B and Esc also arrive as tab.
						const bool cancel = nav == "tab" || nav == "shifttab" || nav == "escape" || nav == "gamepad_B" || nav == "escape-gamepad_B";
						const bool toggle = nav == "gamepad_X" || static_cast<int>(code) == 88;
						logger::trace("layout: input while open: nav \"{}\" code {}", nav, code);
						if (cancel || toggle) { g_request = 2; }
						else if (nav == "gamepad_L1" || nav == "left" || nav == "pageUp" || static_cast<int>(code) == 33) { g_contextStep = -1; }
						else if (nav == "gamepad_R1" || nav == "right" || nav == "pageDown" || static_cast<int>(code) == 34) { g_contextStep = 1; }
					}
					return true;
				}

				double state = -1.0;
				if (!Number(a_page, "iCurrentState", state) || static_cast<int>(state) != kInputMappingState) { return false; }
				RE::GFxValue remap;
				if (a_page.GetMember("bRemapMode", &remap) && remap.IsBool() && remap.GetBool()) { return false; }
				if (value == "keyDown" && (nav == "gamepad_X" || static_cast<int>(code) == 88))
				{
					logger::debug("layout: asked for by nav \"{}\" code {}", nav, code);
					g_request = 1;
					return true;
				}
				return false;
			}
		};

		void WrapInput(RE::GFxMovieView* a_movie, RE::GFxValue& a_page)
		{
			RE::GFxValue original;
			if (!a_page.GetMember("handleInput", &original) || original.IsUndefined())
			{
				logger::warn("layout: the System page has no handleInput; the graphic cannot be asked for");
				return;
			}
			static LayoutInput* handler = new LayoutInput();
			RE::GFxValue fn;
			a_movie->CreateFunction(&fn, handler);
			a_page.SetMember(kOurWrapMember, original);
			a_page.SetMember("handleInput", fn);
			logger::debug("layout: the System page's input now reaches this mod first");
		}

		void OnFrame(RE::GFxMovieView* a_movie)
		{
			static bool loggedPath = false;
			static bool loggedNoUvcWrap = false;
			static bool hintBuilt = false;
			static RE::GFxMovieView* hintMovie = nullptr;

			State state;
			{
				std::scoped_lock l(g_lock);
				state = g_state;  // keeps the last open's figures for the tool
			}
			state.apiThere = rows::ApiThere();
			RE::GFxValue page;
			std::string path;
			state.pageFound = FindPage(a_movie, page, path);
			if (!state.pageFound)
			{
				state.path.clear();
				std::scoped_lock l(g_lock);
				g_state = std::move(state);
				return;
			}
			if (!loggedPath)
			{
				loggedPath = true;
				logger::info("layout: the System page is at {}", path);
			}
			state.path = path;
			double pageState = -1.0;
			(void)Number(page, "iCurrentState", pageState);
			state.pageState = static_cast<int>(pageState);
			RE::GFxValue remapFlag;
			state.remap = page.GetMember("bRemapMode", &remapFlag) && remapFlag.IsBool() && remapFlag.GetBool();

			RE::GFxValue member;
			state.wrapped = page.GetMember(kOurWrapMember, &member) && !member.IsUndefined();
			state.uvcWrapSeen = page.GetMember(kUvcWrapMember, &member) && !member.IsUndefined();
			if (!state.wrapped && state.uvcWrapSeen && state.apiThere)
			{
				WrapInput(a_movie, page);
				state.wrapped = page.GetMember(kOurWrapMember, &member) && !member.IsUndefined();
			}
			const bool onControls = state.pageState == kInputMappingState;
			if (onControls && !state.uvcWrapSeen && !loggedNoUvcWrap)
			{
				loggedNoUvcWrap = true;
				logger::warn("layout: on the Controls page with no wrap from Unbind Vanilla Controls: it is not running here or is switched off, so the graphic is not offered");
			}

			// the panel's life. A new movie (the journal opened again) has no panel, whatever g_open says.
			RE::GFxValue root, panel;
			const bool panelThere = a_movie->GetVariable(&root, "_root") && root.IsObject() && root.GetMember(kPanelName, &panel) && panel.IsDisplayObject();
			if (g_open.load() && (!panelThere || !onControls))
			{
				logger::debug("layout: the panel is gone or the page left Controls (panel {}, state {})", panelThere, state.pageState);
				ClosePanel(a_movie);
				state.open = false;
			}
			const int request = g_request.exchange(0);
			if (request == 1 && !g_open.load())
			{
				if (onControls && !state.remap && state.wrapped)
				{
					g_context = 0;
					g_contextStep = 0;
					BuildPanel(a_movie, page, state);
				}
				else { logger::debug("layout: open asked for and refused: state {}, remap {}, wrapped {}", state.pageState, state.remap, state.wrapped); }
			}
			else if (request == 2 && g_open.load())
			{
				ClosePanel(a_movie);
				state.open = false;
			}
			const int step = g_contextStep.exchange(0);
			if (step != 0 && g_open.load())
			{
				g_context += step;
				BuildPanel(a_movie, page, state);  // it wraps g_context round
			}

			if (g_open.load() && !g_pictureSwf.empty() && !g_pictureSeen && root.IsObject())
			{
				RE::GFxValue picture;
				double width = 0.0;
				if (a_movie->GetVariable(&picture, std::format("_root.{}.at.pic", kPictureName).c_str()) && picture.IsDisplayObject()) { (void)Number(picture, "_width", width); }
				state.pictureWidth = width;
				if (width > 1.0)
				{
					g_pictureSeen = true;
					logger::info("layout: picture there after {} frames, {} wide", g_pictureFrames, width);
				}
				else if (++g_pictureFrames == 300)
				{
					logger::warn("layout: the picture {} has not arrived after 300 frames; the boxes and lines stand without it", g_pictureSwf);
				}
			}

			// the hint: only on the Controls page, with the graphic to be had, and no remap asking for a key
			if (hintMovie != a_movie || g_hintDirty.exchange(false))
			{
				hintMovie = a_movie;
				hintBuilt = false;
			}
			const bool wantHint = onControls && state.wrapped && !state.remap && !g_open.load();
			if (wantHint && !hintBuilt)
			{
				BuildHint(a_movie, page);
				hintBuilt = true;
			}
			RE::GFxValue hint;
			if (root.IsObject() && root.GetMember(kHintName, &hint) && hint.IsDisplayObject())
			{
				hint.SetMember("_visible", RE::GFxValue(wantHint));
				state.hintShown = wantHint;
			}
			else
			{
				state.hintShown = false;
				hintBuilt = false;
			}

			std::scoped_lock l(g_lock);
			g_state = std::move(state);
		}

		struct AdvanceMovie
		{
			static void thunk(RE::JournalMenu* a_this, float a_interval, std::uint32_t a_currentTime)
			{
				func(a_this, a_interval, a_currentTime);
				if (!a_this || !a_this->uiMovie) { return; }
				OnFrame(a_this->uiMovie.get());
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};
	}

	void Install()
	{
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_JournalMenu[0] };
		AdvanceMovie::func = vtbl.write_vfunc(0x5, AdvanceMovie::thunk);
		logger::info("layout: hook on JournalMenu::AdvanceMovie: vtable {:X}, slot 5", vtbl.address());
		if (!rows::ApiThere()) { logger::warn("layout: Unbind Vanilla Controls with UVC_ForEachBinding (1.3.0 or newer) is not loaded; the graphic is not offered"); }
	}

	void RequestOpen() { g_request = 1; }
	void RequestClose() { g_request = 2; }
	void RequestContext(int a_delta) { g_contextStep = a_delta; }

	void SetHintPosition(double a_right, double a_y)
	{
		g_hintRight = a_right;
		g_hintY = a_y;
		g_hintDirty = true;
	}

	std::string StateJson()
	{
		State s;
		{
			std::scoped_lock l(g_lock);
			s = g_state;
		}
		return std::format(
			R"("faces":"{}","facesDrawn":"{}","facesFrom":"{}","steamInput":{},"configRead":{},"configType":"{}","steamWhy":"{}","pictureWidth":{},"context":"{}","contextIndex":{},"contextCount":{},"apiThere":{},"pageFound":{},"pagePath":"{}","pageState":{},"onControls":{},"remap":{},"uvcWrapSeen":{},"wrapped":{},"open":{},"hintShown":{},"hintRight":{},"hintY":{},"rowsSeen":{},"textFields":{},"scale":{},"originX":{},"originY":{},"font":"{}","boxes":{})",
			layoutsettings::FacesName(layoutsettings::faces.load()), s.facesDrawn, JsonEscape(s.facesFrom), s.steamInput, s.configRead, JsonEscape(s.configType), JsonEscape(s.steamWhy), s.pictureWidth, s.contextId, s.contextIndex, s.contextCount, s.apiThere, s.pageFound, JsonEscape(s.path), s.pageState, s.pageState == kInputMappingState, s.remap, s.uvcWrapSeen, s.wrapped, s.open, s.hintShown,
			g_hintRight.load(), g_hintY.load(), s.rowsSeen, s.fields, s.scale, s.originX, s.originY, JsonEscape(s.font), s.boxes.empty() ? "{}" : s.boxes);
	}
}
