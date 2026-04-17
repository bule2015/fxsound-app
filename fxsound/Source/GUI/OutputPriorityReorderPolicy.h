#pragma once

namespace FxSound::OutputPriorityReorderPolicy
{
    inline bool shouldStartDrag(int distance_from_drag_start, int threshold = 4)
    {
        return distance_from_drag_start >= threshold;
    }

    inline int resolveDropRow(int hovered_row, int current_row, int row_count)
    {
        if (row_count <= 0 || current_row < 0 || current_row >= row_count)
        {
            return current_row;
        }

        if (hovered_row < 0 || hovered_row >= row_count)
        {
            return current_row;
        }

        return hovered_row;
    }
}
