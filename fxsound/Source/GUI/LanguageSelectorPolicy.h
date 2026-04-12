#pragma once

#include <string_view>

namespace FxSound::LanguageSelectorPolicy
{
    template <typename GetLanguageCode>
    inline int resolveLanguageIndex(std::wstring_view language_code,
        int language_count,
        GetLanguageCode&& get_language_code)
    {
        int resolved_index = 0;
        size_t resolved_code_length = 0;

        for (int language_index = 0; language_index < language_count; ++language_index)
        {
            const auto candidate = get_language_code(language_index);
            if (candidate.empty() || language_code.size() < candidate.size())
            {
                continue;
            }

            if (language_code.substr(0, candidate.size()) == candidate &&
                candidate.size() > resolved_code_length)
            {
                resolved_index = language_index;
                resolved_code_length = candidate.size();
            }
        }

        return resolved_index;
    }
}
