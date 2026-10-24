#pragma once

// Input Manager's public API (Nexus 174545, https://github.com/vinymayan/Input-Manager, include/InputManagerAPI.h,
// GPL-3.0) - the declarations this mod calls, in the header's own order, because the interfaces are called through their
// vtables and every virtual before the one called must be declared for the slot to line up. Only the direct-export path
// is used (InputManager.dll's GetInputManagerAPIEx), so nothing here needs SKSE messaging.
//
// Numbering, read from Input Manager's source (2026-10-23): a PC key is the SKSE code (0-255 keyboard, 256+ mouse), a
// gamepad key is the XInput-style mask PLUS 266 (A = 0x1000 + 266), and 0 is "no key" on both. An action state of 0 is
// none, 1 tap, 2 hold, 3 gesture (modifier only), 4 press. UpdateActionMappingV2 writes every field of the action and
// saves the action files at once; a null name leaves the action's name alone.

#include <cstddef>
#include <cstdint>

namespace InputManagerAPI
{
	inline constexpr std::uint32_t kAPIVersion2 = 2;
	inline constexpr std::uint32_t kGamepadOffset = 266;

	struct ActionInfo
	{
		int id;
		const char* name;
		std::uint32_t pcMainKey;
		int pcMainAction;
		int pcMainTapCount;
		std::uint32_t pcModifierKey;
		int pcModAction;
		int pcModTapCount;
		std::uint32_t gamepadMainKey;
		int gamepadMainAction;
		int gamepadMainTapCount;
		std::uint32_t gamepadModifierKey;
		int gamepadModAction;
		int gamepadModTapCount;
		int gamepadGestureStick;
		bool useCustomTimings;
		float holdDuration;
		float tapWindow;
		bool isValid;
	};

	struct MotionInfo
	{
		int id;
		const char* name;
		float timeWindow;
		std::uint32_t pcSequence[20];
		int pcSequenceLength;
		std::uint32_t padSequence[20];
		int padSequenceLength;
		bool isValid;
	};

	enum class ActionUpdateCode : std::uint32_t
	{
		kSuccess = 0,
		kInvalidActionID,
		kInvalidMapping,
		kInvalidPCGesture,
		kInvalidGamepadGesture,
		kNameConflict,
		kPCMappingConflict,
		kGamepadMappingConflict,
		kPersistenceFailure,
		kInternalError
	};

	struct ActionUpdateResultV2
	{
		std::uint32_t structSize = sizeof(ActionUpdateResultV2);
		std::uint32_t success = 0;
		ActionUpdateCode code = ActionUpdateCode::kInternalError;
		std::int32_t requestedActionID = -1;
		std::int32_t conflictingActionID = -1;
		char message[192]{};
	};
	static_assert(sizeof(ActionUpdateResultV2) == 212);

	class IInputManager
	{
	public:
		virtual ~IInputManager() = default;
		// inputType: 0 = action, 1 = motion, 2 = gesture
		virtual std::size_t GetInputCount(int inputType) = 0;
		virtual const char* GetInputName(int inputType, int inputID) = 0;
		virtual int CreateInput(int inputType, const char* inputName) = 0;
		virtual bool DeleteInput(int inputType, int inputID) = 0;
		virtual void UpdateListener(int inputType, int inputID, const char* modName, const char* purpose, bool isRegistering,
			const int* validMainActions = nullptr, int mainCount = 0, const int* validModActions = nullptr, int modCount = 0) = 0;
		virtual std::size_t GetListenerCount(int inputType, int inputID) = 0;
		virtual const char* GetListenerModName(int inputType, int inputID, std::size_t index) = 0;
		virtual ActionInfo GetActionInfo(int actionID) = 0;
		virtual bool UpdateActionMapping(int actionID, const ActionInfo& newMapping) = 0;
		virtual MotionInfo GetMotionInfo(int motionID) = 0;
		virtual bool UpdateMotionMapping(int motionID, const MotionInfo& newMapping) = 0;
	};

	class IInputManagerV2 : public IInputManager
	{
	public:
		virtual std::uint32_t GetAPIVersion() const = 0;
		virtual ActionUpdateResultV2 UpdateActionMappingV2(int actionID, const ActionInfo& newMapping) = 0;
	};
}
