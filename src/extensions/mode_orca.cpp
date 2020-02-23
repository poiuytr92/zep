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
    : ZepMode_Vim(editor)
{
}

ZepMode_Orca::~ZepMode_Orca()
{
}

void ZepMode_Orca::SetupKeyMaps()
{
    // Standard choices
    AddGlobalKeyMaps();
    AddNavigationKeyMaps(true);
    AddSearchKeyMaps();
    AddOverStrikeMaps();
    AddCopyMaps();
   
    // Mode switching
    AddKeyMapWithCountRegisters({ &m_normalMap, &m_visualMap }, { "<Escape>" }, id_NormalMode);
    keymap_add({ &m_insertMap }, { "jk" }, id_NormalMode);
    keymap_add({ &m_insertMap }, { "<Escape>" }, id_NormalMode);
    AddKeyMapWithCountRegisters({ &m_visualMap }, { ":", "/", "?" }, id_ExMode);

    AddKeyMapWithCountRegisters({ &m_normalMap }, { "<Return>" }, id_MotionNextFirstChar);

    // Undo redo
    AddKeyMapWithCountRegisters({ &m_normalMap }, { "<C-r>" }, id_Redo);
    AddKeyMapWithCountRegisters({ &m_normalMap }, { "<C-z>", "u" }, id_Undo);
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

