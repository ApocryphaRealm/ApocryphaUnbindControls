// The request API's exports (include/UVCRequestAPI.h is the header other mods copy). Thin: unbinder decides everything.
#include "PCH.h"

#include "UVCRequestAPI.h"
#include "Unbinder.h"

#include <algorithm>
#include <cstring>
#include <string>

namespace
{
	const char* Text(const char* a_text) { return a_text ? a_text : ""; }

	void Fill(const unbinder::RequestAnswer& a_answer, UVCRequestAPI::Answer* a_out)
	{
		if (!a_out) { return; }
		a_out->status = a_answer.status;
		a_out->key = a_answer.key;
		const std::size_t n = std::min(a_answer.why.size(), sizeof(a_out->why) - 1);
		std::memcpy(a_out->why, a_answer.why.data(), n);
		a_out->why[n] = '\0';
	}
}

extern "C"
{
	DLLEXPORT std::int32_t UVC_GetApiVersion() { return UVCRequestAPI::kVersion; }

	DLLEXPORT void UVC_RequestBind(const char* a_plugin, const char* a_action, const char* a_label, const char* a_context, std::int32_t a_device, std::uint32_t a_key, std::uint32_t a_alternative, UVCRequestAPI::Answer* a_out)
	{
		Fill(unbinder::AskRequest(Text(a_plugin), Text(a_action), Text(a_label), Text(a_context), a_device, a_key, a_alternative), a_out);
	}

	DLLEXPORT void UVC_ReleaseBind(const char* a_plugin, const char* a_action, const char* a_context, std::int32_t a_device, UVCRequestAPI::Answer* a_out)
	{
		Fill(unbinder::ReleaseRequest(Text(a_plugin), Text(a_action), Text(a_context), a_device), a_out);
	}

	DLLEXPORT void UVC_QueryBind(const char* a_plugin, const char* a_action, const char* a_context, std::int32_t a_device, UVCRequestAPI::Answer* a_out)
	{
		Fill(unbinder::QueryRequest(Text(a_plugin), Text(a_action), Text(a_context), a_device), a_out);
	}

	DLLEXPORT void UVC_ForEachBinding(std::int32_t a_device, UVCRequestAPI::BindingRowFn_t a_fn, void* a_user)
	{
		if (!a_fn) { return; }
		for (const auto& row : unbinder::AllBindings(a_device))
		{
			const UVCRequestAPI::BindingRow out{ row.block.c_str(), row.event.c_str(), row.label.c_str(), row.key, row.modifier };
			a_fn(&out, a_user);
		}
	}
}
