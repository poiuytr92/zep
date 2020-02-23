#pragma once

#include <vector>
#include <string>
#include <unordered_map>

#include "buffer.h"
#include "zep/mcommon/utf8/unchecked.h"

namespace Zep
{

class ZepTabWindow;
class ZepDisplay;
class Scroller;

struct Region;

struct LineCharInfo
{
    NVec2f size;
    ByteIndex byteIndex;
};
// Line information, calculated during display update.
// A collection of spans that show split lines on the display
struct SpanInfo
{
    BufferByteRange lineByteRange;                 // Begin/end range of the text buffer for this line, as always end is one beyond the end.
    std::vector<LineCharInfo> lineCodePoints;      // Codepoints
    long bufferLineNumber = 0;                     // Line in the original buffer, not the screen line
    float spanYPx = 0.0f;                          // Position in the buffer in pixels, if the screen was as big as the buffer.
    float textHeight = 0.0f;                       // Height of the text region in the line
    int spanLineIndex = 0;                         // The index of this line in spans; might be more than buffer index
    NVec2f pixelRenderRange;                       // The x limits of where this line is renderered
    NVec2f padding = NVec2f(1.0f, 1.0f);           // Padding above and below the line

    float FullLineHeightPx() const
    {
        return padding.x + padding.y + textHeight;
    }

    // The byte length, not code point length
    // TODO: This is not the way to measure text lengths
    long ByteLength() const
    {
        return lineByteRange.second - lineByteRange.first;
    }

    bool BufferCursorInside(ByteIndex offset) const
    {
        return offset >= lineByteRange.first && offset < lineByteRange.second;
    }
};

inline bool operator < (const SpanInfo& lhs, const SpanInfo& rhs)
{
    if (lhs.lineByteRange.first != rhs.lineByteRange.first)
    {
        return lhs.lineByteRange.first < rhs.lineByteRange.first;
    }
    return lhs.lineByteRange.second < rhs.lineByteRange.second;
}

enum class CursorType
{
    Hidden,
    Normal,
    Insert,
    Visual,
    LineMarker
};

enum class DisplayMode
{
    Normal,
    Vim
};

namespace WindowFlags
{
enum
{
    None = (0),
    ShowWhiteSpace = (1 << 0),
    ShowCR = (1 << 1),
    ShowLineNumbers = (1 << 2),
    ShowIndicators = (1 << 3),
    HideScrollBar = (1 << 4),
    Modal = (1 << 5),
    WrapText = (1 << 6), // Warning: this is not for general use yet. Has issues
    HideSplitMark = (1 << 7),
    GridStyle = (1 << 8)
};
}

struct AirBox
{
    std::string text;
    NVec4f background;
};

struct Airline
{
    std::vector<AirBox> leftBoxes;
    std::vector<AirBox> rightBoxes;
};

// Message to request a tooltip.
// Clients should return the marker information if appropriate
struct ToolTipMessage : public ZepMessage
{
    ToolTipMessage(ZepBuffer* pBuff, const NVec2f& pos, const ByteIndex& loc = ByteIndex{-1})
        : ZepMessage(Msg::ToolTip, pos)
        , pBuffer(pBuff)
        , location(loc)
    {
    }

    ZepBuffer* pBuffer;
    ByteIndex location;
    std::shared_ptr<RangeMarker> spMarker;
};

// Display state for a single pane of text.
// Window shows a buffer, and is parented by a TabWindow
// The buffer can change, but the window must always have an active buffer
// Editor operations such as select and change are local to a displayed pane
class ZepWindow : public ZepComponent
{
public:
    ZepWindow(ZepTabWindow& window, ZepBuffer* buffer);
    virtual ~ZepWindow();

    virtual void Notify(std::shared_ptr<ZepMessage> message) override;

    // Display
    virtual void SetDisplayRegion(const NRectf& region);
    virtual void Display();

    // Cursor
    virtual ByteIndex GetBufferCursor();
    virtual void SetBufferCursor(ByteIndex location);
    virtual void SetCursorType(CursorType currentMode);
    virtual void MoveCursorY(int yDistance, LineLocation clampLocation = LineLocation::LineLastNonCR);
    virtual NVec2i BufferToDisplay();

    // Flags
    virtual void SetWindowFlags(uint32_t windowFlags);
    virtual uint32_t GetWindowFlags() const;
    virtual void ToggleFlag(uint32_t flag);

    virtual long GetMaxDisplayLines();
    virtual long GetNumDisplayedLines();

    virtual ZepBuffer& GetBuffer() const;
    virtual void SetBuffer(ZepBuffer* pBuffer);

    ZepTabWindow& GetTabWindow() const;

private:
    NVec4f FilterActiveColor(const NVec4f& col, float atten = 1.0f);

    void UpdateLayout(bool force = false);
    void UpdateAirline();
    void UpdateScrollers();
    void UpdateLineSpans();
    void EnsureCursorVisible();
    void UpdateVisibleLineRange();

    NVec2i BufferToDisplay(const ByteIndex& location);

    void ScrollToCursor();
    bool IsInsideTextRegion(NVec2i pos) const;

    void GetCharPointer(ByteIndex loc, const uint8_t*& pBegin, const uint8_t*& pEnd, bool& invalidChar);
    const SpanInfo& GetCursorLineInfo(long y);

    float ToWindowY(float pos) const;
    float TipBoxShadowWidth() const;

    // Display
    void DisplayCursor();
    void DisplayToolTip(const NVec2f& pos, const RangeMarker& marker) const;
    bool DisplayLine(SpanInfo& lineInfo, int displayPass);
    void DisplayScrollers();
    void DisableToolTipTillMove();

    NVec4f GetBlendedColor(ThemeColor color) const;
    void GetCursorInfo(NVec2f& pos, NVec2f& size);

    void PlaceToolTip(const NVec2f& pos, ToolTipPos location, uint32_t lineGap, const std::shared_ptr<RangeMarker> spMarker);

    void DrawLineWidgets(SpanInfo& lineInfo);
    float GetLineTopPadding(long line);
    
    bool IsActiveWindow() const;

private:
    NRectf m_displayRect;
    std::shared_ptr<Region> m_bufferRegion;  // region of the display we are showing on.
    std::shared_ptr<Region> m_editRegion;   // region of the window buffer editing 
    std::shared_ptr<Region> m_textRegion;    // region of the display for text.
    std::shared_ptr<Region> m_airlineRegion; // Airline
    std::shared_ptr<Region> m_numberRegion;     // Numbers
    std::shared_ptr<Region> m_indicatorRegion;  // Indicators (between numbers and text)
    std::shared_ptr<Region> m_vScrollRegion;    // Vertical scroller
    Airline m_airline;

    // Owner tab
    ZepTabWindow& m_tabWindow;

    // Buffer
    ZepBuffer* m_pBuffer = nullptr;

    // Scroll bar, if visible
    std::shared_ptr<Scroller> m_vScroller;

    // Wrap ; need horizontal offset for this to be turned on.
    // This will indeed stop lines wrapping though!  You just can't move to the far right and have
    // the text scroll; which isn't a big deal, but a work item.
    // TODO Use flags instead
    bool m_layoutDirty = true;
    bool m_scrollVisibilityChanged = true;
    bool m_cursorMoved = true;
    uint32_t m_windowFlags = WindowFlags::ShowWhiteSpace | WindowFlags::ShowIndicators | WindowFlags::ShowLineNumbers | WindowFlags::WrapText;

    // Cursor
    CursorType m_cursorType = CursorType::Normal;   // Type of cursor
    ByteIndex m_bufferCursor = 0;                   // Location in buffer coordinates.  Each window has a different buffer cursor
    long m_lastCursorColumn = 0;                    // The last cursor column (could be removed and recalculated)

    // Visual stuff
    DisplayMode m_displayMode = DisplayMode::Vim;
    std::vector<std::string> m_statusLines; // Status information, shown under the buffer

    // Setup of displayed lines
    std::vector<SpanInfo*> m_windowLines;   // Information about the currently displayed lines
    float m_bufferOffsetYPx = 0.0f;         // The Scroll position within the text
    NVec2f m_textSizePx;                    // The calculated size of the buffer text
    NVec2i m_visibleLineRange = {0, 0};     // Offset of the displayed area into the text
    long m_maxDisplayLines = 0;
    float m_defaultLineSize = 0;
    NVec2f m_visibleLineExtents;            // The pixel extents of all the lines

    // Tooltips
    timer m_toolTipTimer;                // Timer for when the tip is shown
    NVec2f m_mouseHoverPos;              // Current location for the tip
    ByteIndex m_mouseBufferLocation;     // The character in the buffer the tip pos is over, or -1
    NVec2f m_lastTipQueryPos;            // last query location for the tip
    bool m_tipDisabledTillMove = false;  // Certain operations will stop the tip until the mouse is moved
    std::map<NVec2f, std::shared_ptr<RangeMarker>> m_toolTips;  // All tooltips for a given position, currently only 1 at a time

};

} // namespace Zep
