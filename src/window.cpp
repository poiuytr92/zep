#include <cctype>
#include <cmath>
#include <sstream>

#include "zep/buffer.h"
#include "zep/display.h"
#include "zep/mode.h"
#include "zep/scroller.h"
#include "zep/syntax.h"
#include "zep/tab_window.h"
#include "zep/theme.h"
#include "zep/window.h"

#include "zep/mcommon/logger.h"
#include "zep/mcommon/string/stringutils.h"
#include "zep/mcommon/utf8.h"

namespace
{
struct WindowPass
{
    enum Pass
    {
        Background = 0,
        Text,
        Max
    };
};
} // namespace

// A 'window' is like a vim window; i.e. a region inside a tab
namespace Zep
{

const float ScrollBarSize = 17.0f;
ZepWindow::ZepWindow(ZepTabWindow& window, ZepBuffer* buffer)
    : ZepComponent(window.GetEditor())
    , m_tabWindow(window)
    , m_pBuffer(buffer)
{
    m_bufferRegion = std::make_shared<Region>();
    m_numberRegion = std::make_shared<Region>();
    m_indicatorRegion = std::make_shared<Region>();
    m_textRegion = std::make_shared<Region>();
    m_airlineRegion = std::make_shared<Region>();
    m_vScrollRegion = std::make_shared<Region>();

    m_bufferRegion->flags = RegionFlags::Expanding;
    m_bufferRegion->layoutType = RegionLayoutType::VBox;

    m_numberRegion->flags = RegionFlags::Fixed;
    m_indicatorRegion->flags = RegionFlags::Fixed;
    m_vScrollRegion->flags = RegionFlags::Fixed;
    m_textRegion->flags = RegionFlags::Expanding;
    m_airlineRegion->flags = RegionFlags::Fixed;

    // Leave a gap before/after text for the line numbers
    // so they aren't quite so close

    m_editRegion = std::make_shared<Region>();
    m_editRegion->flags = RegionFlags::Expanding;
    m_editRegion->layoutType = RegionLayoutType::HBox;

    m_bufferRegion->children.push_back(m_editRegion);
    m_editRegion->children.push_back(m_numberRegion);
    m_editRegion->children.push_back(m_indicatorRegion);
    m_editRegion->children.push_back(m_textRegion);
    m_editRegion->children.push_back(m_vScrollRegion);

    // A little daylight between the indicators
    m_textRegion->padding = NVec2f(DPI_X(40), 0);

    m_bufferRegion->children.push_back(m_airlineRegion);

    m_vScroller = std::make_shared<Scroller>(GetEditor(), *m_vScrollRegion);
    m_vScroller->vertical = false;

    timer_start(m_toolTipTimer);
}

ZepWindow::~ZepWindow()
{
}

void ZepWindow::UpdateScrollers()
{
    m_scrollVisibilityChanged = false;

    // For now, scrollers are either on or off; and don't disappear
    auto old_percent = m_vScroller->vScrollVisiblePercent;
    if (m_maxDisplayLines == 0)
    {
        m_vScroller->vScrollVisiblePercent = 1.0f;
        m_scrollVisibilityChanged = (old_percent != m_vScroller->vScrollVisiblePercent);
        return;
    }
    m_vScroller->vScrollVisiblePercent = std::min(float(m_maxDisplayLines) / float(m_windowLines.size()), 1.0f);
    m_vScroller->vScrollPosition = std::abs(m_bufferOffsetYPx) / m_textSizePx.y;
    m_vScroller->vScrollLinePercent = 1.0f / m_windowLines.size();
    m_vScroller->vScrollPagePercent = m_vScroller->vScrollVisiblePercent;

    if (GetEditor().GetConfig().showScrollBar == 0 || ZTestFlags(m_windowFlags, WindowFlags::HideScrollBar))
    {
        m_vScrollRegion->fixed_size = 0.0f;
    }
    else
    {
        if (m_vScroller->vScrollVisiblePercent >= 1.0f && GetEditor().GetConfig().showScrollBar != 2)
        {
            m_vScrollRegion->fixed_size = 0.0f;
        }
        else
        {
            m_vScrollRegion->fixed_size = ScrollBarSize * GetEditor().GetPixelScale();
        }
    }

    if (m_vScrollRegion->rect.Width() != m_vScrollRegion->fixed_size)
    {
        m_scrollVisibilityChanged = true;
    }
}

void ZepWindow::UpdateAirline()
{
    m_airline.leftBoxes.clear();
    m_airline.rightBoxes.clear();

    if (IsActiveWindow())
    {
        m_airline.leftBoxes.push_back(AirBox{ GetBuffer().GetMode()->Name(), FilterActiveColor(m_pBuffer->GetTheme().GetColor(ThemeColor::Mode)) });
        switch (GetBuffer().GetMode()->GetEditorMode())
        {
            /*case EditorMode::Hidden:
            m_airline.leftBoxes.push_back(AirBox{ "HIDDEN", m_pBuffer->GetTheme().GetColor(ThemeColor::HiddenText) });
            break;
            */
        case EditorMode::Insert:
            m_airline.leftBoxes.push_back(AirBox{ "INSERT", FilterActiveColor(m_pBuffer->GetTheme().GetColor(ThemeColor::CursorInsert)) });
            break;
        case EditorMode::None:
        case EditorMode::Normal:
            m_airline.leftBoxes.push_back(AirBox{ "NORMAL", FilterActiveColor(m_pBuffer->GetTheme().GetColor(ThemeColor::CursorNormal)) });
            break;
        case EditorMode::Visual:
            m_airline.leftBoxes.push_back(AirBox{ "VISUAL", FilterActiveColor(m_pBuffer->GetTheme().GetColor(ThemeColor::VisualSelectBackground)) });
            break;
        };
    }
    m_airline.leftBoxes.push_back(AirBox{ m_pBuffer->GetDisplayName(), FilterActiveColor(m_pBuffer->GetTheme().GetColor(ThemeColor::AirlineBackground)) });
    m_airline.rightBoxes.push_back(AirBox{ std::to_string(m_pBuffer->GetLineEnds().size()) + " Lines", m_pBuffer->GetTheme().GetColor(ThemeColor::LineNumberBackground) });
}

void ZepWindow::SetCursorType(CursorType currentMode)
{
    m_cursorType = currentMode;
    GetEditor().ResetCursorTimer();
}

void ZepWindow::Notify(std::shared_ptr<ZepMessage> payload)
{
    if (payload->messageId == Msg::Buffer)
    {
        auto pMsg = std::static_pointer_cast<BufferMessage>(payload);

        if (pMsg->pBuffer != m_pBuffer)
        {
            return;
        }

        m_layoutDirty = true;

        if (pMsg->type != BufferMessageType::PreBufferChange)
        {
            // Make sure the cursor is on its 'display' part of the flash cycle after an edit.
            GetEditor().ResetCursorTimer();
        }

        // Remove tooltips that might be present
        DisableToolTipTillMove();
    }
    else if (payload->messageId == Msg::ComponentChanged)
    {
        if (payload->pComponent == m_vScroller.get())
        {
            auto pScroller = dynamic_cast<Scroller*>(payload->pComponent);
            m_bufferOffsetYPx = pScroller->vScrollPosition * (m_windowLines.size() * GetEditor().GetDisplay().GetFontHeightPixels());
            UpdateVisibleLineRange();
            EnsureCursorVisible();
            DisableToolTipTillMove();
        }
    }
    else if (payload->messageId == Msg::MouseMove)
    {
        if (!m_toolTips.empty())
        {
            if (ManhattanDistance(m_mouseHoverPos, payload->pos) > 4.0f)
            {
                timer_restart(m_toolTipTimer);
                m_toolTips.clear();
            }
        }
        else
        {
            timer_restart(m_toolTipTimer);
            m_mouseHoverPos = payload->pos;

            // Can now show tooltip again, due to mouse hover
            m_tipDisabledTillMove = false;
        }
    }
    else if (payload->messageId == Msg::ConfigChanged)
    {
        m_layoutDirty = true;
    }
}

void ZepWindow::SetDisplayRegion(const NRectf& region)
{
    if (m_bufferRegion->rect == region)
    {
        return;
    }

    m_displayRect = region;

    m_layoutDirty = true;
    m_bufferRegion->rect = region;

    m_airlineRegion->fixed_size = GetEditor().GetDisplay().GetFontHeightPixels();

    m_defaultLineSize = GetEditor().GetDisplay().GetFontHeightPixels();
}

void ZepWindow::EnsureCursorVisible()
{
    UpdateLayout();
    ByteIndex loc = m_bufferCursor;
    for (auto& line : m_windowLines)
    {
        if (line->lineByteRange.first <= loc && line->lineByteRange.second > loc)
        {
            auto cursorLine = line->spanLineIndex;
            if (cursorLine < m_visibleLineRange.x)
            {
                MoveCursorY(std::abs(m_visibleLineRange.x - cursorLine));
            }
            else if (cursorLine >= m_visibleLineRange.y)
            {
                MoveCursorY((long(m_visibleLineRange.y) - cursorLine) - 1);
            }
            m_cursorMoved = false;
            return;
        }
    }
}

void ZepWindow::ScrollToCursor()
{
    if (!m_cursorMoved)
    {
        return;
    }

    auto old_offset = m_bufferOffsetYPx;
    auto two_lines = (GetEditor().GetDisplay().GetFontHeightPixels() * 2);
    auto& cursorLine = GetCursorLineInfo(BufferToDisplay().y);

    if (m_bufferOffsetYPx > (cursorLine.spanYPx - two_lines))
    {
        m_bufferOffsetYPx -= (m_bufferOffsetYPx - (cursorLine.spanYPx - two_lines));
    }
    else if ((m_bufferOffsetYPx + m_textRegion->rect.Height() - two_lines) < cursorLine.spanYPx)
    {
        m_bufferOffsetYPx += cursorLine.spanYPx - (m_bufferOffsetYPx + m_textRegion->rect.Height() - two_lines);
    }

    m_bufferOffsetYPx = std::min(m_bufferOffsetYPx, m_textSizePx.y - float(m_maxDisplayLines) * (two_lines * .5f));
    m_bufferOffsetYPx = std::max(0.f, m_bufferOffsetYPx);

    if (old_offset != m_bufferOffsetYPx)
    {
        UpdateVisibleLineRange();
    }
    m_cursorMoved = false;
}

void ZepWindow::GetCharPointer(ByteIndex loc, const uint8_t*& pBegin, const uint8_t*& pEnd, bool& hiddenChar)
{
    static char invalidChar;
    static const char blankSpace = ' ';

    pBegin = &m_pBuffer->GetText()[loc];

    // Shown only one char for end of line
    hiddenChar = false;
    if (*pBegin == '\n' || *pBegin == 0)
    {
        invalidChar = '@' + *pBegin;
        if (m_windowFlags & WindowFlags::ShowCR)
        {
            pBegin = (const uint8_t*)&invalidChar;
        }
        else
        {
            pBegin = (const uint8_t*)&blankSpace;
        }
        hiddenChar = true;
    }

    pEnd = pBegin + utf8_codepoint_length(*pBegin);
}

float ZepWindow::GetLineTopPadding(long line)
{
    float height = DPI_Y((float)GetEditor().GetConfig().lineMargins.x);
    auto pLineWidgets = m_pBuffer->GetLineWidgets(line);
    if (pLineWidgets)
    {
        for (auto& widget : *pLineWidgets)
        {
            auto size = DPI_VEC2(widget->GetSize());
            auto margins = DPI_VEC2(GetEditor().GetConfig().widgetMargins);

            // Each widget has a margin then its height then the bottom
            height += margins.x;
            height += size.y;
            height += margins.y;
        }
    }
    // Add the line margin too, so the widget sits in a space similar to a line
    height += DPI_Y((float)GetEditor().GetConfig().lineMargins.y);

    return height;
}

// This is the most expensive part of window update; applying line span generation for wrapped text and unicode
// character sizes which may vary in byte count and physical pixel width
// It can take about a millisecond to do during editing on release buildj; but this is fast enough for now.
// There are several ways in which this function can be optimized:
// - Only regenerate spans which have changed, since only the edited line range will change.
// - Generate blocks of text, based on syntax highlighting, instead of single characters.
// - Have a no-wrap text mode and save a lot of the wrapping work.
// - Do some threading
void ZepWindow::UpdateLineSpans()
{
    TIME_SCOPE(UpdateLineSpans);

    m_maxDisplayLines = (long)std::max(0.0f, std::floor(m_textRegion->rect.Height() / m_defaultLineSize));

    const auto& textBuffer = m_pBuffer->GetText();

    long bufferLine = 0;
    long spanLine = 0;
    float bufferPosYPx = 0.0f;
    float screenPosX = 0.0f;

    // Nuke the existing spans
    // In future we can in-place modify for speed
    std::for_each(m_windowLines.begin(), m_windowLines.end(), [](SpanInfo* pInfo) { delete pInfo; });
    m_windowLines.clear();

    auto& display = GetEditor().GetDisplay();

    float textHeight = GetEditor().GetDisplay().GetFontHeightPixels();

    // Process every buffer line
    for (;;)
    {
        // We haven't processed this line yet, so we can't display anything
        // else
        if (m_pBuffer->GetLineCount() <= bufferLine)
            break;

        BufferByteRange lineByteRange;
        if (!m_pBuffer->GetLineOffsets(bufferLine, lineByteRange.first, lineByteRange.second))
            break;

        NVec2f padding = NVec2f(GetLineTopPadding(bufferLine), DPI_Y((float)GetEditor().GetConfig().lineMargins.y));
        float fullLineHeight = textHeight + padding.x + padding.y;

        // Start a new line
        SpanInfo* lineInfo = new SpanInfo();
        lineInfo->bufferLineNumber = bufferLine;
        lineInfo->spanLineIndex = spanLine;
        lineInfo->lineByteRange.first = lineByteRange.first;
        lineInfo->lineByteRange.second = lineByteRange.first;
        lineInfo->spanYPx = bufferPosYPx;
        lineInfo->padding = padding;
        lineInfo->textHeight = textHeight;
        lineInfo->pixelRenderRange.x = screenPosX;
        lineInfo->pixelRenderRange.y = screenPosX;

        // These offsets are 0 -> n + 1, i.e. the last offset the buffer returns is 1 beyond the current
        // Note: Must not use pointers into the character buffer!
        for (auto ch = lineByteRange.first; ch < lineByteRange.second; ch += utf8_codepoint_length(textBuffer[ch]))
        {
            const uint8_t* pCh = &textBuffer[ch];
            const auto textSize = display.GetCharSize(pCh);
            // Wrap if we have displayed at least one char, and we have to
            if (ZTestFlags(m_windowFlags, WindowFlags::WrapText) && ch != lineByteRange.first)
            {
                // At least a single char has wrapped; close the old line, start a new one
                if (((screenPosX + textSize.x) + textSize.x) >= (m_textRegion->rect.Width()))
                {
                    // Remember the offset beyond the end of the line
                    lineInfo->lineByteRange.second = ch;
                    lineInfo->pixelRenderRange.y = screenPosX;
                    m_windowLines.push_back(lineInfo);

                    // Next line
                    lineInfo = new SpanInfo();
                    spanLine++;
                    bufferPosYPx += fullLineHeight;

                    // Reset the line margin and height, because when we split a line we don't include a
                    // custom widget space above it.  That goes just above the first part of the line
                    padding.x = (float)GetEditor().GetConfig().lineMargins.x;
                    fullLineHeight = textHeight + padding.x + padding.y;

                    // Now jump to the next 'screen line' for the rest of this 'buffer line'
                    lineInfo->lineByteRange = BufferByteRange(ch, utf8_codepoint_length(textBuffer[ch]));
                    lineInfo->spanLineIndex = spanLine;
                    lineInfo->bufferLineNumber = bufferLine;
                    lineInfo->spanYPx = bufferPosYPx;
                    lineInfo->padding = padding;
                    lineInfo->textHeight = textHeight;
                    
                    screenPosX = 0.0f;
                    lineInfo->pixelRenderRange.x = screenPosX;
                    lineInfo->pixelRenderRange.y = screenPosX;
                }
                else
                {
                    screenPosX += textSize.x;
                }
            }
            else
            {
                screenPosX += textSize.x;
            }

            if (*pCh == '\n' && !ZTestFlags(m_windowFlags,WindowFlags::ShowCR))
            {
                screenPosX -= textSize.x;
            }

            lineInfo->spanYPx = bufferPosYPx;
            lineInfo->lineByteRange.second = ch + utf8_codepoint_length(textBuffer[ch]);
            lineInfo->pixelRenderRange.y = std::max(lineInfo->pixelRenderRange.y, screenPosX);
        }

        // Complete the line
        m_windowLines.push_back(lineInfo);

        // Next time round - down a buffer line, down a span line
        bufferLine++;
        spanLine++;
        screenPosX = 0.0f;
        bufferPosYPx += fullLineHeight;
    }

    // Sanity
    if (m_windowLines.empty())
    {
        SpanInfo* lineInfo = new SpanInfo();
        lineInfo->lineByteRange.first = 0;
        lineInfo->lineByteRange.second = 0;
        lineInfo->padding = NVec2f(0.0f);
        lineInfo->textHeight = 0.0f;
        lineInfo->bufferLineNumber = 0;
        lineInfo->pixelRenderRange = NVec2f(0.0f, 0.0f);
        m_windowLines.push_back(lineInfo);
    }

    for (auto& line : m_windowLines)
    {
        auto ch = line->lineByteRange.first;

        // TODO: Optimize
        line->lineCodePoints.clear();
        uint32_t points = 0;
        while (ch < line->lineByteRange.second)
        {
            LineCharInfo info;

            // Important note: We can't navigate the text buffer by pointers!
            // The gap buffer will get in the way; so need to be careful to use [] or an iterator
            // GetCharSize is cached for speed on debug builds.
            info.byteIndex = ByteIndex(ch);
            info.size = display.GetCharSize(&textBuffer[ch]);
            line->lineCodePoints.push_back(info);
            ch += utf8_codepoint_length(textBuffer[ch]);
            points++;
        }
    }

    UpdateVisibleLineRange();
    m_layoutDirty = true;
}

void ZepWindow::UpdateVisibleLineRange()
{
    TIME_SCOPE(UpdateVisibleLineRange);

    m_visibleLineExtents = NVec2f(m_bufferRegion->rect.Width(), 0);

    m_visibleLineRange.x = (long)m_windowLines.size();
    m_visibleLineRange.y = 0;
    m_textSizePx.x = 0;
    for (long line = 0; line < long(m_windowLines.size()); line++)
    {
        auto& windowLine = *m_windowLines[line];
        m_textSizePx.x = std::max(m_textSizePx.x, windowLine.pixelRenderRange.y - windowLine.pixelRenderRange.x);

        if ((windowLine.spanYPx + windowLine.FullLineHeightPx()) <= m_bufferOffsetYPx)
        {
            continue;
        }

        if ((windowLine.spanYPx - m_bufferOffsetYPx) >= m_textRegion->rect.Height())
        {
            break;
        }

        m_visibleLineRange.x = std::min(m_visibleLineRange.x, long(line));
        m_visibleLineRange.y = long(line);

        m_visibleLineExtents.x = std::min(windowLine.pixelRenderRange.x, m_visibleLineExtents.x);
        m_visibleLineExtents.y = std::max(windowLine.pixelRenderRange.y, m_visibleLineExtents.y);
    }
    
    m_textSizePx.y = m_windowLines[m_windowLines.size() - 1]->spanYPx + GetEditor().GetDisplay().GetFontHeightPixels() + DPI_Y(GetEditor().GetConfig().lineMargins.y);

    m_visibleLineRange.y++;
    UpdateScrollers();
}

const SpanInfo& ZepWindow::GetCursorLineInfo(long y)
{
    UpdateLayout();
    y = std::max(0l, y);
    y = std::min(y, long(m_windowLines.size() - 1));
    return *m_windowLines[y];
}

// Convert a normalized y coordinate to the window region
float ZepWindow::ToWindowY(float pos) const
{
    return pos - m_bufferOffsetYPx + m_textRegion->rect.topLeftPx.y;
}

float ZepWindow::TipBoxShadowWidth() const
{
    return DPI_X(4.0f);
}

void ZepWindow::DisplayToolTip(const NVec2f& pos, const RangeMarker& marker) const
{
    auto textSize = GetEditor().GetDisplay().GetTextSize((const uint8_t*)marker.description.c_str(), (const uint8_t*)(marker.description.c_str() + marker.description.size()));

    auto boxShadowWidth = TipBoxShadowWidth();

    // Draw a black area a little wider than the tip box.
    NRectf tipBox(pos.x, pos.y, textSize.x, textSize.y);
    tipBox.Adjust(0, 0, (textBorder + boxShadowWidth) * 2, (textBorder + boxShadowWidth) * 2);

    auto& display = GetEditor().GetDisplay();

    display.SetClipRect(m_textRegion->rect);
    display.DrawRectFilled(tipBox, m_pBuffer->GetTheme().GetColor(ThemeColor::Background));

    // Draw a lighter inner and a border the same color as the marker theme
    tipBox.Adjust(boxShadowWidth, boxShadowWidth, -boxShadowWidth, -boxShadowWidth);
    display.DrawRectFilled(tipBox, m_pBuffer->GetTheme().GetColor(marker.backgroundColor));
    display.DrawLine(tipBox.topLeftPx, tipBox.TopRight(), m_pBuffer->GetTheme().GetColor(marker.highlightColor));
    display.DrawLine(tipBox.BottomLeft(), tipBox.bottomRightPx, m_pBuffer->GetTheme().GetColor(marker.highlightColor));
    display.DrawLine(tipBox.topLeftPx, tipBox.BottomLeft(), m_pBuffer->GetTheme().GetColor(marker.highlightColor));
    display.DrawLine(tipBox.TopRight(), tipBox.bottomRightPx, m_pBuffer->GetTheme().GetColor(marker.highlightColor));

    // Draw the text in the box
    display.DrawChars(tipBox.topLeftPx + NVec2f(textBorder, textBorder), m_pBuffer->GetTheme().GetColor(marker.textColor), (const uint8_t*)marker.description.c_str());
}

NVec4f ZepWindow::GetBlendedColor(ThemeColor color) const
{
    auto col = m_pBuffer->GetTheme().GetColor(color);
    if (GetEditor().GetConfig().style == EditorStyle::Minimal)
    {
        float lastEdit = GetEditor().GetLastEditElapsedTime();
        if (lastEdit > GetEditor().GetConfig().backgroundFadeWait)
        {
            lastEdit -= GetEditor().GetConfig().backgroundFadeWait;
            col.w = std::max(0.0f, 1.0f - lastEdit / GetEditor().GetConfig().backgroundFadeTime);
        }
    }
    return col;
}

void ZepWindow::DrawLineWidgets(SpanInfo& lineInfo)
{
    auto pLineWidgets = m_pBuffer->GetLineWidgets(lineInfo.bufferLineNumber);
    if (!pLineWidgets)
    {
        return;
    }

    auto lineMargins = DPI_VEC2(GetEditor().GetConfig().lineMargins);
    auto widgetMargins = DPI_VEC2(GetEditor().GetConfig().widgetMargins);

    float currentY = lineMargins.x;
    for (auto& pWidget : *pLineWidgets)
    {
        auto widgetSize = DPI_VEC2(pWidget->GetSize());

        currentY += widgetMargins.x;
        pWidget->Draw(*m_pBuffer, NVec2f(lineInfo.pixelRenderRange.x, ToWindowY(currentY + lineInfo.spanYPx)));
        currentY += widgetMargins.y;
    }
}

// TODO: This function draws one char at a time.  It could be more optimal at the expense of some
// complexity.  Basically, I don't like the current implementation, but it works for now.
// The text is displayed acorrding to the region bounds and the display lineData
// Additionally (and perhaps that should be a seperate function), this code draws line numbers
bool ZepWindow::DisplayLine(SpanInfo& lineInfo, int displayPass)
{
    static const auto blankSpace = ' ';

    auto cursorCL = BufferToDisplay();

    auto& display = GetEditor().GetDisplay();
    display.SetClipRect(m_bufferRegion->rect);

    // Draw line numbers
    auto displayLineNumber = [&]() {
        if (!IsInsideTextRegion(NVec2i(0, lineInfo.spanLineIndex)))
            return;

        auto cursorBufferLine = GetCursorLineInfo(cursorCL.y).bufferLineNumber;
        std::string strNum;

        // In Vim mode show relative lines, unless in Ex mode (with hidden cursor)
        if (m_displayMode == DisplayMode::Vim && m_cursorType != CursorType::Hidden)
        {
            strNum = std::to_string(std::abs(lineInfo.bufferLineNumber - cursorBufferLine));
        }
        else
        {
            strNum = std::to_string(lineInfo.bufferLineNumber);
        }

        auto textSize = display.GetTextSize((const uint8_t*)strNum.c_str(), (const uint8_t*)(strNum.c_str() + strNum.size()));

        auto digitCol = m_pBuffer->GetTheme().GetColor(ThemeColor::LineNumber);
        if (lineInfo.BufferCursorInside(m_bufferCursor))
        {
            digitCol = m_pBuffer->GetTheme().GetColor(ThemeColor::CursorNormal);
        }

        // Numbers
        display.DrawChars(NVec2f(m_numberRegion->rect.bottomRightPx.x - textSize.x, ToWindowY(lineInfo.spanYPx + lineInfo.padding.x)), digitCol, (const uint8_t*)strNum.c_str(), (const uint8_t*)(strNum.c_str() + strNum.size()));
    };

    // Drawing commands for the whole line
    if (displayPass == WindowPass::Background)
    {
        display.SetClipRect(m_textRegion->rect);

        // Fill the background of the line
        display.DrawRectFilled(
            NRectf(
                NVec2f(lineInfo.pixelRenderRange.x, ToWindowY(lineInfo.spanYPx)),
                NVec2f(lineInfo.pixelRenderRange.y, ToWindowY(lineInfo.spanYPx + lineInfo.FullLineHeightPx()))),
            GetBlendedColor(ThemeColor::Background));

        if (lineInfo.BufferCursorInside(m_bufferCursor))
        {
            if (IsActiveWindow())
            {
                // Don't draw over the visual region
                if (GetBuffer().GetMode()->GetEditorMode() != EditorMode::Visual)
                {
                    auto& cursorLine = GetCursorLineInfo(cursorCL.y);

                    if (IsInsideTextRegion(cursorCL))
                    {
                        float lineSize = 1.0f * GetEditor().GetPixelScale();

                        // Normal mode spans the whole buffer, otherwise we just cover the visible text range
                        // This is all about making minimal mode as non-invasive as possible.
                        auto right = GetEditor().GetConfig().style == EditorStyle::Normal ? m_textRegion->rect.bottomRightPx.x : m_visibleLineExtents.y;

                        if (GetEditor().GetConfig().cursorLineSolid)
                        {
                            // Cursor line
                            display.DrawRectFilled(NRectf(NVec2f(m_textRegion->rect.topLeftPx.x, cursorLine.spanYPx - m_bufferOffsetYPx + m_textRegion->rect.topLeftPx.y), NVec2f(right, cursorLine.spanYPx - m_bufferOffsetYPx + m_textRegion->rect.topLeftPx.y + cursorLine.FullLineHeightPx())), GetBlendedColor(ThemeColor::CursorLineBackground));
                        }
                        else
                        {
                            // Cursor line
                            display.DrawRectFilled(
                                NRectf(
                                    NVec2f(m_textRegion->rect.Left(), ToWindowY(cursorLine.spanYPx)),
                                    NVec2f(right, ToWindowY(cursorLine.spanYPx + lineSize))),
                                GetBlendedColor(ThemeColor::TabInactive));

                            display.DrawRectFilled(
                                NRectf(
                                    NVec2f(m_textRegion->rect.Left(), ToWindowY(cursorLine.spanYPx + cursorLine.FullLineHeightPx() - lineSize)),
                                    NVec2f(right, ToWindowY(cursorLine.spanYPx + cursorLine.FullLineHeightPx()))),
                                GetBlendedColor(ThemeColor::TabInactive));

                            display.DrawRectFilled(
                                NRectf(
                                    NVec2f(right, ToWindowY(cursorLine.spanYPx)),
                                    NVec2f(right + lineSize, ToWindowY(cursorLine.spanYPx + cursorLine.FullLineHeightPx()))),
                                GetBlendedColor(ThemeColor::TabInactive));
                        }
                    }
                }
            }
        }
        display.SetClipRect(m_bufferRegion->rect);

        if (m_indicatorRegion->rect.Width() > 0)
        {
            display.SetClipRect(m_indicatorRegion->rect);

            // Show any markers in the left indicator region
            m_pBuffer->ForEachMarker(RangeMarkerType::Message, SearchDirection::Forward, lineInfo.lineByteRange.first, lineInfo.lineByteRange.second, [&](const std::shared_ptr<RangeMarker>& marker) {
                // >|< Text.  This is the bit between the arrows <-.  A vertical bar in the 'margin'
                if (marker->displayType & RangeMarkerDisplayType::Indicator)
                {
                    if (marker->IntersectsRange(lineInfo.lineByteRange))
                    {
                        display.DrawRectFilled(
                            NRectf(
                                NVec2f(
                                    m_indicatorRegion->rect.Center().x - m_indicatorRegion->rect.Width() / 4,
                                    ToWindowY(lineInfo.spanYPx + lineInfo.padding.x)),
                                NVec2f(
                                    m_indicatorRegion->rect.Center().x + m_indicatorRegion->rect.Width() / 4,
                                    ToWindowY(lineInfo.spanYPx + lineInfo.padding.x) + display.GetFontHeightPixels())),
                            m_pBuffer->GetTheme().GetColor(marker->highlightColor));
                    }
                }
                return true;
            });

            display.SetClipRect(m_bufferRegion->rect);
        }

        if (m_numberRegion->rect.Width() > 0)
        {
            display.SetClipRect(m_numberRegion->rect);
            displayLineNumber();
            display.SetClipRect(m_bufferRegion->rect);
        }
    }

    auto screenPosX = m_textRegion->rect.Left();
    auto pSyntax = m_pBuffer->GetSyntax();

    auto tipTimeSeconds = timer_get_elapsed_seconds(m_toolTipTimer);

    display.SetClipRect(m_textRegion->rect);

    //auto pText = &m_pBuffer->GetText()[0];
    // Walk from the start of the line to the end of the line (in buffer chars)
    for (auto cp : lineInfo.lineCodePoints)
    {
        const uint8_t* pCh;
        const uint8_t* pEnd;
        bool hiddenChar;
        GetCharPointer(cp.byteIndex, pCh, pEnd, hiddenChar);

        // TODO : Cache this for speed - a little sluggish on debug builds.
        if (displayPass == WindowPass::Background)
        {
            NRectf charRect(NVec2f(screenPosX, ToWindowY(lineInfo.spanYPx)), NVec2f(screenPosX + cp.size.x, ToWindowY(lineInfo.spanYPx + lineInfo.FullLineHeightPx())));
            if (charRect.Contains(m_mouseHoverPos))
            {
                // Record the mouse-over buffer location
                m_mouseBufferLocation = cp.byteIndex;
            }

            // If the syntax overrides the background, show it first
            if (pSyntax && pSyntax->GetSyntaxAt(cp.byteIndex).background != ThemeColor::None)
            {
                display.DrawRectFilled(charRect, m_pBuffer->GetTheme().GetColor(pSyntax->GetSyntaxAt(cp.byteIndex).background));
            }

            // Show any markers
            m_pBuffer->ForEachMarker(RangeMarkerType::All, SearchDirection::Forward, lineInfo.lineByteRange.first, lineInfo.lineByteRange.second, [&](const std::shared_ptr<RangeMarker>& marker) {
                // Don't show hidden markers
                if (marker->displayType == RangeMarkerDisplayType::Hidden)
                {
                    return true;
                }

                auto sel = marker->range;
                if (marker->ContainsLocation(cp.byteIndex))
                {
                    if (marker->displayType & RangeMarkerDisplayType::Underline)
                    {
                        display.DrawRectFilled(NRectf(NVec2f(screenPosX, ToWindowY(lineInfo.spanYPx + lineInfo.FullLineHeightPx()) - 1), NVec2f(screenPosX + cp.size.x, ToWindowY(lineInfo.spanYPx + lineInfo.FullLineHeightPx()))), m_pBuffer->GetTheme().GetColor(marker->highlightColor));
                    }

                    if (marker->displayType & RangeMarkerDisplayType::Background)
                    {
                        display.DrawRectFilled(charRect, m_pBuffer->GetTheme().GetColor(marker->backgroundColor));
                    }

                    // If this marker has an associated tooltip, pop it up after a time delay
                    // TODO: Make tooltip generation seperate to this display loop
                    if (m_toolTips.empty() && !m_tipDisabledTillMove && (tipTimeSeconds > 0.5f))
                    {
                        bool showTip = false;
                        if (marker->displayType & RangeMarkerDisplayType::Tooltip)
                        {
                            if (m_mouseBufferLocation == cp.byteIndex)
                            {
                                showTip = true;
                            }
                        }

                        // If we want the tip showing at anywhere on the line, show it
                        if (marker->displayType & RangeMarkerDisplayType::TooltipAtLine)
                        {
                            // TODO: This should be a helper function
                            // Checks for mouse pos inside a line string
                            if (m_mouseHoverPos.y >= ToWindowY(lineInfo.spanYPx) && m_mouseHoverPos.y < (ToWindowY(lineInfo.spanYPx) + cp.size.y) && (m_mouseHoverPos.x < m_textRegion->rect.topLeftPx.x + lineInfo.ByteLength() * cp.size.x))
                            {
                                showTip = true;
                            }
                        }

                        if (showTip)
                        {
                            // Register this tooltip
                            m_toolTips[NVec2f(m_mouseHoverPos.x, m_mouseHoverPos.y + textBorder)] = marker;
                        }
                    }
                }
                return true;
            });

            // Draw the visual selection marker second
            if (IsActiveWindow())
            {
                if (GetBuffer().HasSelection())
                {
                    auto sel = m_pBuffer->GetSelection();
                    if (sel.ContainsLocation(cp.byteIndex) && !hiddenChar)
                    {
                        display.DrawRectFilled(NRectf(NVec2f(screenPosX, ToWindowY(lineInfo.spanYPx)), NVec2f(screenPosX + cp.size.x, ToWindowY(lineInfo.spanYPx + lineInfo.FullLineHeightPx()))), m_pBuffer->GetTheme().GetColor(ThemeColor::VisualSelectBackground));
                    }
                }
            }
        }
        // Second pass, characters
        else
        {
            DrawLineWidgets(lineInfo);

            if (!hiddenChar || m_windowFlags & WindowFlags::ShowCR)
            {
                auto centerChar = NVec2f(screenPosX + cp.size.x / 2, ToWindowY(lineInfo.spanYPx) + cp.size.y / 2);
                if ((m_windowFlags & WindowFlags::ShowWhiteSpace) && pSyntax && pSyntax->GetSyntaxAt(cp.byteIndex).foreground == ThemeColor::Whitespace)
                {
                    // Show a dot
                    display.DrawRectFilled(NRectf(centerChar - NVec2f(1.0f, 1.0f), centerChar + NVec2f(1.0f, 1.0f)), m_pBuffer->GetTheme().GetColor(ThemeColor::Whitespace));
                }
                else
                {
                    NVec4f col;
                    if (hiddenChar)
                    {
                        col = m_pBuffer->GetTheme().GetColor(ThemeColor::HiddenText);
                    }
                    else
                    {
                        if (pSyntax)
                        {
                            col = m_pBuffer->GetTheme().GetColor(pSyntax->GetSyntaxAt(cp.byteIndex).foreground);
                        }
                        else
                        {
                            col = m_pBuffer->GetTheme().GetColor(ThemeColor::Text);
                        }
                    }

                    if (pSyntax)
                    {
                        auto backgroundColor = pSyntax->GetSyntaxAt(cp.byteIndex).background;
                        if (backgroundColor != ThemeColor::None)
                        {
                            display.DrawRectFilled(NRectf(centerChar - NVec2f(1.0f, 1.0f), centerChar + NVec2f(1.0f, 1.0f)), m_pBuffer->GetTheme().GetColor(backgroundColor));
                        }
                    }
                    display.DrawChars(NVec2f(screenPosX, ToWindowY(lineInfo.spanYPx + lineInfo.padding.x)), col, pCh, pEnd);
                }
            }
        }

        screenPosX += cp.size.x;
    }

    DisplayCursor();

    display.SetClipRect(NRectf{});

    return true;
}

bool ZepWindow::IsInsideTextRegion(NVec2i pos) const
{
    if (pos.y < m_visibleLineRange.x || pos.y >= m_visibleLineRange.y)
    {
        return false;
    }
    return true;
}

void ZepWindow::DisplayCursor()
{
    if (!IsActiveWindow())
        return;

    auto cursorCL = BufferToDisplay();

    // Draw the cursor
    auto cursorBufferLine = GetCursorLineInfo(cursorCL.y);

    if (!IsInsideTextRegion(cursorCL))
    {
        return;
    }

    NVec2f pos, cursorSize;
    GetCursorInfo(pos, cursorSize);

    // Draw the Cursor symbol
    auto cursorBlink = GetEditor().GetCursorBlinkState();

    if (!cursorBlink || (m_cursorType == CursorType::LineMarker))
    {
        switch (m_cursorType)
        {
        default:
        case CursorType::Hidden:
            break;

        case CursorType::LineMarker: {
            pos.x = m_indicatorRegion->rect.topLeftPx.x;
            GetEditor().GetDisplay().DrawRectFilled(NRectf(pos, NVec2f(pos.x + cursorSize.x, pos.y + cursorSize.y)), m_pBuffer->GetTheme().GetColor(ThemeColor::Light));
        }
        break;

        case CursorType::Insert: {
            GetEditor().GetDisplay().DrawRectFilled(NRectf(NVec2f(pos.x, pos.y), NVec2f(pos.x + 1, pos.y + cursorSize.y)), m_pBuffer->GetTheme().GetColor(ThemeColor::CursorInsert));
        }
        break;

        case CursorType::Normal:
        case CursorType::Visual: {
            GetEditor().GetDisplay().DrawRectFilled(NRectf(pos, NVec2f(pos.x + cursorSize.x, pos.y + cursorSize.y)), m_pBuffer->GetTheme().GetColor(ThemeColor::CursorNormal));
        }
        break;
        }
    }
}

ZepTabWindow& ZepWindow::GetTabWindow() const
{
    return m_tabWindow;
}

void ZepWindow::SetWindowFlags(uint32_t windowFlags)
{
    if (windowFlags != m_windowFlags)
    {
        m_windowFlags = windowFlags;

        // Most changes require layout update; do it anyway
        UpdateLayout(true);
    }
}

uint32_t ZepWindow::GetWindowFlags() const
{
    return m_windowFlags;
}

void ZepWindow::ToggleFlag(uint32_t flag)
{
    if (ZTestFlags(m_windowFlags, flag))
    {
        SetWindowFlags(m_windowFlags & ~flag);
    }
    else
    {
        SetWindowFlags(m_windowFlags | flag);
    }
}

long ZepWindow::GetMaxDisplayLines()
{
    UpdateLayout();
    return m_maxDisplayLines;
}

long ZepWindow::GetNumDisplayedLines()
{
    UpdateLayout();
    return std::min((long)m_windowLines.size(), GetMaxDisplayLines());
}

void ZepWindow::SetBufferCursor(ByteIndex location)
{
    // Don't move cursor if not necessary
    // This helps preserve 'lastCursorColumn' from being changed all the time
    // during line clamps, etc.
    if (location != m_bufferCursor)
    {
        m_bufferCursor = m_pBuffer->Clamp(location);
        m_lastCursorColumn = BufferToDisplay(m_bufferCursor).x;
        m_cursorMoved = true;
        DisableToolTipTillMove();
    }
}

void ZepWindow::DisableToolTipTillMove()
{
    m_tipDisabledTillMove = true;
    m_toolTips.clear();
}

void ZepWindow::SetBuffer(ZepBuffer* pBuffer)
{
    assert(pBuffer);

    m_pBuffer = pBuffer;
    m_layoutDirty = true;
    m_bufferOffsetYPx = 0;
    m_bufferCursor = pBuffer->Clamp(pBuffer->GetLastEditLocation());
    m_lastCursorColumn = 0;
    m_cursorMoved = false;
}

ByteIndex ZepWindow::GetBufferCursor()
{
    // Ensure cursor is always valid inside the buffer
    m_bufferCursor = m_pBuffer->Clamp(m_bufferCursor);

    return m_bufferCursor;
}

ZepBuffer& ZepWindow::GetBuffer() const
{
    return *m_pBuffer;
}

bool ZepWindow::IsActiveWindow() const
{
    return m_tabWindow.GetActiveWindow() == this;
}

NVec4f ZepWindow::FilterActiveColor(const NVec4f& col, float atten)
{
    if (!IsActiveWindow())
    {
        return NVec4f(Luminosity(col) * atten);
    }
    return col;
}

void ZepWindow::DisplayScrollers()
{
    if (m_vScrollRegion->rect.Empty())
        return;

    m_vScroller->Display(m_pBuffer->GetTheme());

    GetEditor().GetDisplay().SetClipRect(m_bufferRegion->rect);
}

void ZepWindow::UpdateLayout(bool force)
{
    if (m_layoutDirty || force)
    {
        // Border, and move the text across a bit
        if (ZTestFlags(m_windowFlags, WindowFlags::ShowLineNumbers) && GetEditor().GetConfig().showLineNumbers)
        {
            m_numberRegion->fixed_size = float(leftBorderChars) * GetEditor().GetDisplay().GetDefaultCharSize().x;
        }
        else
        {
            m_numberRegion->fixed_size = 0.0f;
        }

        if (ZTestFlags(m_windowFlags, WindowFlags::ShowIndicators) && GetEditor().GetConfig().showIndicatorRegion)
        {
            m_indicatorRegion->fixed_size = GetEditor().GetDisplay().GetDefaultCharSize().x * 1.5f;
        }
        else
        {
            m_indicatorRegion->fixed_size = 0.0f;
        }

        m_editRegion->margin = NVec4f(0, 0, 0, 0);

        LayoutRegion(*m_bufferRegion);

        UpdateLineSpans();

        // In grid style, draw a central window
        if (ZTestFlags(m_windowFlags, WindowFlags::GridStyle))
        {
            //LOG(DEBUG) << "Region: \n" << *m_bufferRegion;

            // Put it in the center of the text region
            auto widthMarginX = (m_textRegion->rect.Width() - m_textSizePx.x /*- m_textRegion->padding.x*/) / 2;
            auto widthMarginY = (m_textRegion->rect.Width() - m_textSizePx.x /*- m_textRegion->padding.y*/) / 2;
            auto heightMargin = (m_textRegion->rect.Height() - m_textSizePx.y) / 2;
            widthMarginX = std::max(widthMarginX, 0.0f);
            widthMarginY = std::max(widthMarginY, 0.0f);
            heightMargin = std::max(heightMargin, 0.0f);
            m_editRegion->margin = NVec4f(widthMarginX, heightMargin, widthMarginY, heightMargin);
            LayoutRegion(*m_editRegion);
            
            //LOG(DEBUG) << "Region: \n" << m_bufferRegion;
        }
        
        m_layoutDirty = false;
    }
}

void ZepWindow::GetCursorInfo(NVec2f& pos, NVec2f& size)
{
    auto cursorCL = BufferToDisplay();
    auto cursorBufferLine = GetCursorLineInfo(cursorCL.y);

    NVec2f cursorSize;
    bool found = false;
    float xPos = m_textRegion->rect.topLeftPx.x;

    int count = 0;
    for (auto ch : cursorBufferLine.lineCodePoints)
    {
        if (count == cursorCL.x)
        {
            found = true;
            cursorSize = ch.size;
            break;
        }
        count++;
        xPos += ch.size.x;
    }

    if (!found)
    {
        cursorSize = GetEditor().GetDisplay().GetDefaultCharSize();
        xPos += cursorSize.x;
    }

    pos = NVec2f(xPos, cursorBufferLine.spanYPx + cursorBufferLine.padding.x - m_bufferOffsetYPx + m_textRegion->rect.topLeftPx.y);
    size = cursorSize;
    size.y = cursorBufferLine.textHeight;
}

void ZepWindow::PlaceToolTip(const NVec2f& pos, ToolTipPos location, uint32_t lineGap, const std::shared_ptr<RangeMarker> spMarker)
{
    auto textSize = GetEditor().GetDisplay().GetTextSize((const uint8_t*)spMarker->description.c_str(), (const uint8_t*)(spMarker->description.c_str() + spMarker->description.size()));
    float boxShadowWidth = TipBoxShadowWidth();

    NRectf tipBox;
    float currentLineGap = lineGap + .5f;

    for (int i = 0; i < int(ToolTipPos::Count); i++)
    {
        auto genBox = [&]() {
            // Draw a black area a little wider than the tip box.
            tipBox = NRectf(pos.x, pos.y, textSize.x, textSize.y);
            tipBox.Adjust(textBorder + boxShadowWidth, textBorder + boxShadowWidth, textBorder + boxShadowWidth, textBorder + boxShadowWidth);

            float dist = currentLineGap * (GetEditor().GetDisplay().GetFontHeightPixels() + textBorder * 2);
            if (location == ToolTipPos::AboveLine)
            {
                dist += textSize.y;
                tipBox.Adjust(0.0f, -dist);
            }
            else if (location == ToolTipPos::BelowLine)
            {
                tipBox.Adjust(0.0f, dist);
            }
        };

        genBox();
        if (!NRectFits(m_textRegion->rect, tipBox, FitCriteria::X))
        {
            // If it is above or below, slide it to the left to fit
            if (location != ToolTipPos::RightLine)
            {
                // Move in X along to the left
                genBox();
                tipBox.Move(std::max(m_textRegion->rect.Left() + textBorder, (m_textRegion->rect.Right() - (tipBox.Width() + textBorder))), tipBox.Top());
            }
        }

        // Swap above below
        if (!NRectFits(m_textRegion->rect, tipBox, FitCriteria::Y))
        {
            switch (location)
            {
            case ToolTipPos::AboveLine:
                location = ToolTipPos::BelowLine;
                break;
            case ToolTipPos::BelowLine:
                location = ToolTipPos::AboveLine;
                break;
            case ToolTipPos::RightLine:
                location = ToolTipPos::AboveLine;
                break;
            }
        }
        else
        {
            break;
        }
    }

    m_toolTips[tipBox.topLeftPx] = spMarker;
}

void ZepWindow::Display()
{
    TIME_SCOPE(Display);

    auto pMode = GetBuffer().GetMode();
    pMode->PreDisplay(*this);

    // Ensure line spans are valid; updated if the text is changed or the window dimensions change
    UpdateLayout();
    ScrollToCursor();
    UpdateScrollers();

    // Second pass if the scroller visibility changed, since this can change the whole layout!
    if (m_scrollVisibilityChanged)
    {
        m_layoutDirty = true;
        m_cursorMoved = true;
        UpdateLayout();
        ScrollToCursor();
        UpdateScrollers();

        m_scrollVisibilityChanged = false;
    }

    auto& display = GetEditor().GetDisplay();
    auto cursorCL = BufferToDisplay(m_bufferCursor);
    m_mouseBufferLocation = ByteIndex{ -1 };

    // Always update
    UpdateAirline();

    UpdateLayout();

    if (GetEditor().GetConfig().style == EditorStyle::Normal)
    {
        // Fill the background color for the whole area, only in normal mode.
        display.DrawRectFilled(m_textRegion->rect, GetBlendedColor(ThemeColor::Background));

    }

    if (ZTestFlags(m_windowFlags, WindowFlags::GridStyle))
    {
        // Border around the edge
        display.DrawRect(m_textRegion->rect, GetBlendedColor(ThemeColor::TabInactive));
    }

    if (m_numberRegion->rect.Width() > 0)
    {
        display.DrawRectFilled(m_numberRegion->rect, GetBlendedColor(ThemeColor::LineNumberBackground));
    }

    if (m_indicatorRegion->rect.Width() > 0)
    {
        display.DrawRectFilled(m_indicatorRegion->rect, GetBlendedColor(ThemeColor::LineNumberBackground));
    }

    DisplayScrollers();

    // This is a line down the middle of a split
    if (GetEditor().GetConfig().style == EditorStyle::Normal &&
        !ZTestFlags(m_windowFlags, WindowFlags::HideSplitMark))
    {
        display.DrawRectFilled(
            NRectf(NVec2f(m_numberRegion->rect.topLeftPx.x, m_numberRegion->rect.topLeftPx.y), NVec2f(m_numberRegion->rect.topLeftPx.x + 1, m_numberRegion->rect.bottomRightPx.y)), GetBlendedColor(ThemeColor::TabInactive));
    }

    {
        TIME_SCOPE(DrawLine);
        for (int displayPass = 0; displayPass < WindowPass::Max; displayPass++)
        {
            for (long windowLine = m_visibleLineRange.x; windowLine < m_visibleLineRange.y; windowLine++)
            {
                auto& lineInfo = *m_windowLines[windowLine];
                if (!DisplayLine(lineInfo, displayPass))
                {
                    break;
                }
            }
        }
    }

    // Is the cursor on a tooltip row or mark?
    if (m_toolTips.empty())
    {
        auto cursorLine = GetCursorLineInfo(BufferToDisplay().y);

        // If this marker has an associated tooltip, pop it up after a time delay
        NVec2f pos, size;
        GetCursorInfo(pos, size);

        // Calculate our desired location for the tip.
        auto tipPos = [&](RangeMarker& marker) {
            NVec2f ret;
            if (marker.tipPos == ToolTipPos::RightLine)
            {
                ret = NVec2f(cursorLine.pixelRenderRange.y, pos.y);
            }
            else
            {
                ret = NVec2f(cursorLine.pixelRenderRange.x, pos.y);
            }
            return ret;
        };

        m_pBuffer->ForEachMarker(RangeMarkerType::All, SearchDirection::Forward, cursorLine.lineByteRange.first, cursorLine.lineByteRange.second, [&](const std::shared_ptr<RangeMarker>& marker) {
            if (marker->displayType == RangeMarkerDisplayType::Hidden)
            {
                return true;
            }

            auto sel = marker->range;
            if (marker->displayType & RangeMarkerDisplayType::CursorTip)
            {
                if (m_bufferCursor >= sel.first && m_bufferCursor < sel.second)
                {
                    PlaceToolTip(tipPos(*marker), marker->tipPos, 2, marker);
                }
            }

            if (marker->displayType & RangeMarkerDisplayType::CursorTipAtLine)
            {
                if ((cursorLine.lineByteRange.first <= sel.first && cursorLine.lineByteRange.second > sel.first) || (cursorLine.lineByteRange.first <= sel.second && cursorLine.lineByteRange.second > sel.second))
                {
                    PlaceToolTip(tipPos(*marker), marker->tipPos, 2, marker);
                }
            }
            return true;
        });
    }
    else
    {
        // No hanging tooltips if the markers on the page have gone
        if (m_pBuffer->GetRangeMarkers(RangeMarkerType::Message).empty())
        {
            m_toolTips.clear();
        }
    }

    // No tooltip, and we can show one, then ask for tooltips
    if (!m_tipDisabledTillMove && (timer_get_elapsed_seconds(m_toolTipTimer) > 0.5f) && m_toolTips.empty() && m_lastTipQueryPos != m_mouseHoverPos)
    {
        auto spMsg = std::make_shared<ToolTipMessage>(m_pBuffer, m_mouseHoverPos, m_mouseBufferLocation);
        GetEditor().Broadcast(spMsg);
        if (spMsg->handled && spMsg->spMarker != nullptr)
        {
            PlaceToolTip(NVec2f(m_mouseHoverPos.x, m_mouseHoverPos.y), spMsg->spMarker->tipPos, 1, spMsg->spMarker);
        }
        m_lastTipQueryPos = m_mouseHoverPos;
    }

    for (auto& toolTip : m_toolTips)
    {
        DisplayToolTip(toolTip.first, *toolTip.second);
    }

    display.SetClipRect(NRectf{});

    if (!GetEditor().GetCommandText().empty() || (GetEditor().GetConfig().autoHideCommandRegion == false))
    {
        // Airline and underline
        display.DrawRectFilled(m_airlineRegion->rect, GetBlendedColor(ThemeColor::AirlineBackground));

        auto airHeight = GetEditor().GetDisplay().GetFontHeightPixels();
        auto border = 12.0f;

        NVec2f screenPosYPx = m_airlineRegion->rect.topLeftPx;
        for (int i = 0; i < (int)m_airline.leftBoxes.size(); i++)
        {
            auto pText = (const uint8_t*)m_airline.leftBoxes[i].text.c_str();
            auto textSize = display.GetTextSize(pText, pText + m_airline.leftBoxes[i].text.size());
            textSize.x += border * 2;

            auto col = m_airline.leftBoxes[i].background;
            display.DrawRectFilled(NRectf(screenPosYPx, NVec2f(textSize.x + screenPosYPx.x, screenPosYPx.y + airHeight)), col);

            NVec4f textCol = m_pBuffer->GetTheme().GetComplement(m_airline.leftBoxes[i].background, IsActiveWindow() ? NVec4f(0.0f) : NVec4f(.5f, .5f, .5f, 0.0f));
            display.DrawChars(screenPosYPx + NVec2f(border, 0.0f), textCol, (const uint8_t*)(m_airline.leftBoxes[i].text.c_str()));
            screenPosYPx.x += textSize.x;
        }
    }
}

void ZepWindow::MoveCursorY(int yDistance, LineLocation clampLocation)
{
    UpdateLayout();

    timer_restart(m_toolTipTimer);
    m_toolTips.clear();

    // Get the cursor
    auto cursorCL = BufferToDisplay();
    if (cursorCL.x == -1)
        return;

    // Find the screen line relative target
    auto target = cursorCL + NVec2i(0, yDistance);
    target.y = std::max(0l, target.y);
    target.y = std::min(target.y, long(m_windowLines.size() - 1));

    auto& line = *m_windowLines[target.y];

    // Snap to the new vertical column if necessary (see comment below)
    if (target.x < m_lastCursorColumn)
        target.x = m_lastCursorColumn;

    assert(!line.lineCodePoints.empty());

    // Move to the same codepoint offset on the line below
    target.x = std::min(target.x, long(line.lineCodePoints.size() - 1));
    target.x = std::max(target.x, long(0));

    GlyphIterator cursorItr(*m_pBuffer, line.lineCodePoints[target.x].byteIndex);

    // We can't call the buffer's LineLocation code, because when moving in span lines,
    // we are technically not moving in buffer lines; we are stepping in wrapped buffer lines.
    switch (clampLocation)
    {
    default:
    case LineLocation::LineBegin:
    case LineLocation::LineFirstGraphChar:
    case LineLocation::BeyondLineEnd:
        assert(!"Not supported Y motion line clamp!");
        break;
    case LineLocation::LineLastNonCR: {
        // Don't skip back if we are right at the start of the line
        // (i.e. an empty line)
        if (target.x != 0 && (cursorItr.Char() == '\n' || cursorItr.Char() == 0))
        {
            cursorItr.MoveClamped(-1, LineLocation::LineLastNonCR);
        }
    }
    break;
    case LineLocation::LineCRBegin:
        // We already clamped to here above by testing for max codepoint
        // Last codepoint is the carriage return
        break;
    }

    m_bufferCursor = cursorItr.ToByteIndex();
    m_cursorMoved = true;

    GetEditor().ResetCursorTimer();

    m_pBuffer->SetLastEditLocation(m_bufferCursor);
}

NVec2i ZepWindow::BufferToDisplay()
{
    return BufferToDisplay(m_bufferCursor);
}

NVec2i ZepWindow::BufferToDisplay(const ByteIndex& loc)
{
    UpdateLayout();

    NVec2i ret(0, 0);
    int line_number = 0;

    // TODO: Performance; quick lookup for line
    for (auto& line : m_windowLines)
    {
        // If inside the line...
        if (line->lineByteRange.first <= loc && line->lineByteRange.second > loc)
        {
            ret.y = line_number;
            ret.x = 0;

            // Scan the code points for where we are
            for (auto& ch : line->lineCodePoints)
            {
                if (ch.byteIndex == loc)
                {
                    return ret;
                }
                ret.x++;
            }
        }
        line_number++;
    }

    assert(!m_windowLines.empty());
    if (m_windowLines.empty())
    {
        return NVec2i(0, 0);
    }

    // Max Last line, last code point offset
    ret.y = long(m_windowLines.size() - 1);
    ret.x = long(m_windowLines[m_windowLines.size() - 1]->lineCodePoints.size() - 1);
    return ret;
}

} // namespace Zep

#if 0
    // Ensure we can see the cursor
    NVec2i cursor(0, 0);
    cursor.x = m_pBuffer->GetBufferColumn(m_bufferCursor);
    cursor.y = m_pBuffer->GetBufferLine(m_bufferCursor) - m_pBuffer->GetBufferLine(m_dvisibleLineRange.x);

    // Handle the case where there is no need to scroll, since the visible lines are inside
    // The current screen rectangle.
    if (cursor.y >= m_nvisibleLineRange.y && m_linesFillScreen)
    {
        m_visibleLineRange.x = cursor.y - (m_nvisibleLineRange.y - m_vnisibleLineRange.x) + 1;
        m_linesChanged = true;
    }
    else if (cursor.y < m_nvisibleLineRange.x)
    {
        m_nvisibleLineRange.x = cursor.y;
        m_linesChanged = true;
    }

    // Clamp
    m_nvisibleLineRange.x = std::max(0l, (long)m_nvisibleLineRange.x);
#endif
