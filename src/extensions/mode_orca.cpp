#include <cctype>
#include <sstream>

#include "zep/mcommon/animation/timer.h"
#include "zep/mcommon/logger.h"
#include "zep/mcommon/string/stringutils.h"

#include "zep/keymap.h"
#include "zep/tab_window.h"
#include "zep/theme.h"


#include "zep/extensions/mode_orca.h"

namespace Zep
{

ZepMode_Orca::ZepMode_Orca(ZepEditor& editor)
    : ZepMode(editor)
{
    Init();
}

ZepMode_Orca::~ZepMode_Orca()
{
}

void ZepMode_Orca::Init()
{
    AddStandardKeyMaps();

    // Normal and Visual
    /*
    keymap_vim({ &m_normalMap, &m_visualMap }, { "Y" }, id_YankLine);
    keymap_vim({ &m_normalMap }, { "yy" }, id_YankLine);
    keymap_vim({ &m_normalMap, &m_visualMap }, { "p" }, id_PasteAfter);
    keymap_vim({ &m_normalMap, &m_visualMap }, { "P" }, id_PasteBefore);

    keymap_vim({ &m_normalMap, &m_visualMap }, { "x", "<Del>" }, id_Delete);
    keymap_vim({ &m_normalMap, &m_visualMap }, { "J" }, id_JoinLines);

    // Motions

    // Line Motions
    keymap_vim({ &m_normalMap, &m_visualMap }, { "$" }, id_MotionLineEnd);
    keymap_add({ &m_normalMap, &m_visualMap }, { "0" }, id_MotionLineBegin);
    keymap_vim({ &m_normalMap, &m_visualMap }, { "^" }, id_MotionLineFirstChar);

    // Page Motinos
    keymap_vim({ &m_normalMap, &m_visualMap }, { "<C-f>", "<PageDown>" }, id_MotionPageForward);
    keymap_vim({ &m_normalMap, &m_visualMap }, { "<C-b>", "<PageUp>" }, id_MotionPageBackward);
    keymap_vim({ &m_normalMap, &m_visualMap }, { "<C-d>" }, id_MotionHalfPageForward);
    keymap_vim({ &m_normalMap, &m_visualMap }, { "<C-u>" }, id_MotionHalfPageBackward);
    keymap_vim({ &m_normalMap, &m_visualMap }, { "G" }, id_MotionGotoLine);

    // Word motions
    keymap_vim({ &m_normalMap, &m_visualMap }, { "w" }, id_MotionWord);
    keymap_vim({ &m_normalMap, &m_visualMap }, { "b" }, id_MotionBackWord);
    keymap_vim({ &m_normalMap, &m_visualMap }, { "W" }, id_MotionWORD);
    keymap_vim({ &m_normalMap, &m_visualMap }, { "B" }, id_MotionBackWORD);
    keymap_vim({ &m_normalMap, &m_visualMap }, { "e" }, id_MotionEndWord);
    keymap_vim({ &m_normalMap, &m_visualMap }, { "E" }, id_MotionEndWORD);
    keymap_vim({ &m_normalMap, &m_visualMap }, { "ge" }, id_MotionBackEndWord);
    keymap_vim({ &m_normalMap, &m_visualMap }, { "gE" }, id_MotionBackEndWORD);
    keymap_vim({ &m_normalMap, &m_visualMap }, { "gg" }, id_MotionGotoBeginning);

    keymap_vim({ &m_visualMap }, { "C" }, id_ChangeLine);
    keymap_vim({ &m_visualMap }, { "y" }, id_Yank);

    // Not necessary?
    keymap_vim({ &m_normalMap, &m_visualMap }, { "<Escape>" }, id_NormalMode);


    // Visual mode
    keymap_vim({ &m_visualMap }, { "aW" }, id_VisualSelectAWORD);
    keymap_vim({ &m_visualMap }, { "aw" }, id_VisualSelectAWord);
    keymap_vim({ &m_visualMap }, { "iW" }, id_VisualSelectInnerWORD);
    keymap_vim({ &m_visualMap }, { "iw" }, id_VisualSelectInnerWord);
    keymap_vim({ &m_visualMap }, { "d" }, id_VisualDelete);
    keymap_vim({ &m_visualMap }, { "c" }, id_VisualChange);
    keymap_vim({ &m_visualMap }, { "s" }, id_VisualSubstitute);

    // Normal mode only
    keymap_vim({ &m_normalMap }, { "i" }, id_InsertMode);
    keymap_vim({ &m_normalMap }, { "H" }, id_PreviousTabWindow);
    keymap_vim({ &m_normalMap }, { "o" }, id_OpenLineBelow);
    keymap_vim({ &m_normalMap }, { "O" }, id_OpenLineAbove);
    keymap_vim({ &m_normalMap }, { "V" }, id_VisualLineMode);
    keymap_vim({ &m_normalMap }, { "v" }, id_VisualMode);

    keymap_vim({ &m_normalMap }, { "d<D>w", "dw" }, id_DeleteWord);
    keymap_vim({ &m_normalMap }, { "dW" }, id_DeleteWORD);
    keymap_vim({ &m_normalMap }, { "daw" }, id_DeleteAWord);
    keymap_vim({ &m_normalMap }, { "daW" }, id_DeleteAWORD);
    keymap_vim({ &m_normalMap }, { "diw" }, id_DeleteInnerWord);
    keymap_vim({ &m_normalMap }, { "diW" }, id_DeleteInnerWORD);
    keymap_vim({ &m_normalMap }, { "D", "d$" }, id_DeleteToLineEnd);
    keymap_vim({ &m_normalMap }, { "d<D>d", "dd" }, id_DeleteLine);
    keymap_vim({ &m_normalMap }, { "dt<.>" }, id_DeleteToChar);

    keymap_vim({ &m_normalMap }, { "cw" }, id_ChangeWord);
    keymap_vim({ &m_normalMap }, { "cW" }, id_ChangeWORD);
    keymap_vim({ &m_normalMap }, { "ciw" }, id_ChangeInnerWord);
    keymap_vim({ &m_normalMap }, { "ciW" }, id_ChangeInnerWORD);
    keymap_vim({ &m_normalMap }, { "caw" }, id_ChangeAWord);
    keymap_vim({ &m_normalMap }, { "caW" }, id_ChangeAWORD);
    keymap_vim({ &m_normalMap }, { "C", "c$" }, id_ChangeToLineEnd);
    keymap_vim({ &m_normalMap }, { "cc" }, id_ChangeLine);
    
    keymap_vim({ &m_normalMap }, { "ct<.>" }, id_ChangeToChar);

    keymap_vim({ &m_normalMap, &m_visualMap }, { "r<.>" }, id_Replace);

    keymap_vim({ &m_normalMap }, { "S" }, id_SubstituteLine);
    keymap_vim({ &m_normalMap }, { "s" }, id_Substitute);
    keymap_vim({ &m_normalMap }, { "A" }, id_AppendToLine);
    keymap_vim({ &m_normalMap }, { "a" }, id_Append);
    keymap_vim({ &m_normalMap }, { "I" }, id_InsertAtFirstChar);
    keymap_vim({ &m_normalMap }, { "<Return>" }, id_MotionNextFirstChar);

    keymap_vim({ &m_normalMap }, { "<C-r>" }, id_Redo);
    keymap_vim({ &m_normalMap }, { "<C-z>", "u" }, id_Undo);

    keymap_vim({ &m_normalMap, &m_visualMap }, { "f<.>" }, id_Find);
    keymap_vim({ &m_normalMap, &m_visualMap }, { "F<.>" }, id_FindBackwards);


    // Insert Mode
    keymap_add({ &m_insertMap }, { "<Backspace>" }, id_Backspace);
    keymap_add({ &m_insertMap }, { "<Return>" }, id_InsertCarriageReturn);
    keymap_add({ &m_insertMap }, { "<Tab>" }, id_InsertTab);
    keymap_add({ &m_insertMap }, { "jk" }, id_NormalMode);
    keymap_add({ &m_insertMap }, { "<Escape>" }, id_NormalMode);
    */
}

void ZepMode_Orca::Begin()
{
    if (GetCurrentWindow())
    {
        GetCurrentWindow()->SetCursorType(CursorType::Normal);
        GetEditor().SetCommandText(m_currentCommand);
    
        //m_editRegion->margin = NVec4f(50, 50, 50, 50);
    }
    m_currentMode = EditorMode::Normal;
    m_currentCommand.clear();
    m_dotCommand.clear();
    m_pendingEscape = false;
    

}

void ZepMode_Orca::PreDisplay(ZepWindow&)
{

}

} // namespace Zep

