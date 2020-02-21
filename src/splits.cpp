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
            // This region needs its size plus margin
            totalFixedSize += (r->fixed_size + (r->padding.x + r->padding.y));
        }
        else
        {
            expanders += 1.0f;
        }
    }

    NRectf currentRect = region.rect;
    currentRect.Adjust(region.margin.x, region.margin.y, -region.margin.z, -region.margin.w);

    auto remaining = ((region.layoutType == RegionLayoutType::HBox) ? currentRect.Width() : currentRect.Height()) - totalFixedSize;
    auto perExpanding = remaining / expanders;

    perExpanding = std::max(0.0f, perExpanding);

    for (auto& rcChild : region.children)
    {
        rcChild->rect = currentRect;

        if (region.layoutType == RegionLayoutType::VBox)
        {
            rcChild->rect.topLeftPx.y += rcChild->padding.x;
            if (rcChild->flags & RegionFlags::Fixed)
            {
                rcChild->rect.bottomRightPx.y = rcChild->rect.Top() + rcChild->fixed_size;
                currentRect.topLeftPx.y = rcChild->rect.Bottom() + rcChild->padding.y;
            }
            else
            {
                rcChild->rect.bottomRightPx.y = rcChild->rect.Top() + perExpanding;
                currentRect.topLeftPx.y = rcChild->rect.Bottom() + rcChild->padding.y;
            }
        }
        else if (region.layoutType == RegionLayoutType::HBox)
        {
            rcChild->rect.topLeftPx.x += rcChild->padding.x;
            if (rcChild->flags & RegionFlags::Fixed)
            {
                rcChild->rect.bottomRightPx.x = rcChild->rect.Left() + rcChild->fixed_size;
                currentRect.topLeftPx.x = rcChild->rect.Right() + rcChild->padding.y;
            }
            else
            {
                rcChild->rect.bottomRightPx.x = rcChild->rect.Left() + perExpanding;
                currentRect.topLeftPx.x = rcChild->rect.Right() + rcChild->padding.y;
            }
        }
    }

    for (auto& r : region.children)
    {
        LayoutRegion(*r);
    }
}

} // namespace Zep