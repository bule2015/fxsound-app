#pragma once

namespace FxSound::OutputPriorityReorderPolicy
{
    struct DragSession
    {
        bool active = false;
        int source_row = -1;
        int target_row = -1;
    };

    inline bool shouldStartDrag(int distance_from_drag_start, int threshold = 4)
    {
        return distance_from_drag_start >= threshold;
    }

    inline DragSession beginDragSession(int source_row)
    {
        if (source_row < 0)
        {
            return {};
        }

        return { true, source_row, source_row };
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

    inline void updateDragSession(DragSession& session, int hovered_row, int row_count)
    {
        if (!session.active)
        {
            return;
        }

        session.target_row = resolveDropRow(hovered_row, session.source_row, row_count);
    }
}
