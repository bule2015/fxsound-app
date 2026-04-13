#pragma once

namespace FxSound::LanguageLayoutPolicy
{
	template <typename GetLabelWidth>
	inline int getPreferredWidth(int min_width, int label_padding, int label_count, GetLabelWidth&& get_label_width)
	{
		auto longest_label_width = 0;
		for (int label_index = 0; label_index < label_count; ++label_index)
		{
			const auto label_width = get_label_width(label_index);
			if (label_width > longest_label_width)
			{
				longest_label_width = label_width;
			}
		}

		const auto preferred_width = longest_label_width + label_padding;
		return preferred_width >= min_width ? preferred_width : min_width;
	}
}
