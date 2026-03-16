#pragma once

#ifndef HOUNDTTS_AZURE_AUTH_H
#define HOUNDTTS_AZURE_AUTH_H

#include "httplib.h"

#include <string>

namespace HoundTTS {
namespace AzureAuth {

// Build the standard Azure Cognitive Services subscription headers.
// key    : Ocp-Apim-Subscription-Key value
// region : Azure region e.g. "eastus" (used by Translator; ignored if empty)
// Returns an httplib::Headers object ready for use in any Azure REST call.
inline httplib::Headers MakeHeaders(const std::string& key,
                                    const std::string& region)
{
    httplib::Headers h;
    h.emplace("Ocp-Apim-Subscription-Key", key);
    if (!region.empty())
        h.emplace("Ocp-Apim-Subscription-Region", region);
    return h;
}

} // namespace AzureAuth
} // namespace HoundTTS

#endif // HOUNDTTS_AZURE_AUTH_H
