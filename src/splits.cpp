#include "zep/splits.h"

namespace Zep
{

void LayoutRegion(Region& region)
{
    float totalFixedSize = 0.0f;
    float expanders = 0.0f;
    for (auto& r : region.children)
    {
        if (r->flags & RegionFlags::Fixed)
        {
            totalFixedSize += r->fixed_size;
        }
        else
        {
            expanders += 1.0f;
        }
    }

    NRectf currentRect = region.rect;
    auto remaining = ((region.layoutType == RegionLayoutType::HBox) ? currentRect.Width() : currentRect.Height()) - totalFixedSize;
    auto perExpanding = remaining / expanders;

    for (auto& r : region.children)
    {
        r->rect = currentRect;

        if (region.layoutType == RegionLayoutType::VBox)
        {
            if (r->flags & RegionFlags::Fixed)
            {
                r->rect.bottomRightPx.y = currentRect.topLeftPx.y + r->fixed_size;
                currentRect.topLeftPx.y += r->fixed_size;
            }
            else
            {
                r->rect.bottomRightPx.y = currentRect.topLeftPx.y + perExpanding;
                currentRect.topLeftPx.y += perExpanding;
            }
        }
        else if (region.layoutType == RegionLayoutType::HBox)
        {
            if (r->flags & RegionFlags::Fixed)
            {
                r->rect.bottomRightPx.x = currentRect.topLeftPx.x + r->fixed_size;
                currentRect.topLeftPx.x += r->fixed_size;
            }
            else
            {
                r->rect.bottomRightPx.x = currentRect.topLeftPx.x + perExpanding;
                currentRect.topLeftPx.x += perExpanding;
            }
        }
        r->rect.topLeftPx += r->margin;
        r->rect.bottomRightPx -= r->margin;
    }

    for (auto& r : region.children)
    {
        LayoutRegion(*r);
    }
}

} // namespace Zep