#pragma once

#include "zep/mode.h"
#include "zep/keymap.h"

namespace Zep
{

class ZepMode_Orca : public ZepMode
{
public:
    ZepMode_Orca(ZepEditor& editor);
    ~ZepMode_Orca();

    static const char* StaticName()
    {
        return "Orca";
    }

    // Zep Mode
    virtual void Begin() override;
    virtual const char* Name() const override { return StaticName(); }
    virtual void PreDisplay(ZepWindow& win) override;

    virtual void Init();
};

} // namespace Zep
