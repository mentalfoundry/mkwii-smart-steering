// Smart Steering on/off toggle on the drift select screens.
//
// On the single-player and split-screen drift select pages, in offline Grand
// Prix and VS, each player can press the menu "switch" button to turn Smart
// Steering on or off for themselves. The page's bottom bar, which fits
// one line, shows the state: on the single-player page at 70% scale ahead of
// the game's description of the highlighted drift mode, on the split-screen
// page (where the game leaves it empty) for every player. Everyone starts
// with it off, and a choice lasts until the game is restarted.
//
// The switch button is input action 8, which the game raises for menu button
// bit 0x100 (ButtonInfo::Update, 0x805EEF20) but never handles itself; Pulsar
// documents it as - (Wii Remote), X (Classic) and Z (GameCube). We register a
// global handler for it on each page's manipulator manager after the page's
// own onInit; nothing later clears global handlers.

#include "game.h"
#include "toggle.h"

namespace DriftSelectToggle {

static const int kMaxHudSlots = 4;
static bool sOn[kMaxHudSlots]; // .bss, zeroed by the loader: everyone starts off

static const int kTextMax = 96;
static u16 sText[kTextMax];
static MessageInfo sInfo;

// What the single-player page shows for each button (DriftSelect's
// onButtonSelect table: Manual, Automatic, drift explanation).
static const u32 kButtonDescriptions[3] = {0xD17, 0xD16, 0xCEE};
static s32 sSelectedButton = -1;

struct PageHandler {
    InputHandler base; // must stay first: the game calls through base.vtable
    void* page;
    bool multi;
};

static void OnSwitchPress(PageHandler* self, u32 hudSlot);

// vtable +0x8 is what CheckActions calls; the first two words are unused.
static const void* const sHandlerVtable[3] = {nullptr, nullptr, (const void*)&OnSwitchPress};
static PageHandler sSingleHandler = {{sHandlerVtable}, nullptr, false};
static PageHandler sMultiHandler = {{sHandlerVtable}, nullptr, true};

// Smart Steering only runs in offline GP and VS (see IsModeAllowed in
// SmartSteering.cpp); the drift pages are also used for TT, battle and online.
static bool IsAvailable() {
    if (RaceConfig_spInstance == nullptr) return false;
    u32 mode = RaceConfig_getMenuMode(RaceConfig_spInstance);
    return mode == 0 || mode == 1;
}

static int Append(int n, const char* s) {
    while (*s != '\0' && n < kTextMax - 1) sText[n++] = (u8)*s++;
    sText[n] = 0;
    return n;
}

static void ResetInfo() {
    u8* p = (u8*)&sInfo;
    for (u32 i = 0; i < sizeof(sInfo); ++i) p[i] = 0;
    for (int i = 0; i < 9; ++i) sInfo.playerId[i] = -1;
}

// Font scale escape, as the game's own messages use it (e.g. Menu.bmg 0x106A
// "{scale 70%}Race Rating"): 0x1A, size 8 / group 0, type 0, percent.
static int AppendScale(int n, u16 percent) {
    if (n + 4 >= kTextMax) return n;
    sText[n++] = 0x001A;
    sText[n++] = 0x0800;
    sText[n++] = 0x0000;
    sText[n++] = percent;
    sText[n] = 0;
    return n;
}

static void ShowText(void* page, u32 bmgId) {
    CtrlMenuInstructionText* bottom = MenuPage_getBottomText(page);
    if (bottom != nullptr) CtrlMenuInstructionText_setMessage(bottom, bmgId, &sInfo);
}

// Single player: our status, then the game's description of the highlighted
// button, on one line at 70% so both fit the bar. The scale has to come from
// our string because the slot-only messages place it first.
static void ShowSingle(void* page) {
    int n = AppendScale(0, 70);
    n = Append(n, "Smart Steering: ");
    n = Append(n, sOn[0] ? "On" : "Off");
    n = Append(n, " (-)");
    ResetInfo();
    sInfo.bmgToPass[0] = BMG_STRING;
    sInfo.strings[0] = sText;
    if (sSelectedButton >= 0 && sSelectedButton < 3) {
        n = Append(n, "   ");
        if (n < kTextMax - 1) sText[n++] = 0x30FB; // katakana middle dot, as the English menus use
        Append(n, "   ");
        sInfo.bmgToPass[1] = kButtonDescriptions[sSelectedButton];
    }
    ShowText(page, BMG_CONCAT9);
}

// Split screen: every player on the page.
static void ShowMulti(void* page) {
    u32 players = MenuPage_getActivePlayers(page);
    int n = Append(0, "Smart Steering (-)");
    for (int i = 0; i < kMaxHudSlots; ++i) {
        if (!(players & (1u << i))) continue;
        char label[] = "   P1: ";
        label[4] = (char)('1' + i);
        n = Append(n, label);
        n = Append(n, sOn[i] ? "On" : "Off");
    }
    ResetInfo();
    sInfo.strings[0] = sText;
    ShowText(page, BMG_STRING);
}

static void OnSwitchPress(PageHandler* self, u32 hudSlot) {
#ifdef MKWIISS_DEBUG
    OSReport("[SmartSteering] switch press: slot %d, %s page, mode %d\n", hudSlot, self->multi ? "multi" : "single",
             RaceConfig_spInstance != nullptr ? RaceConfig_getMenuMode(RaceConfig_spInstance) : -1);
#endif
    if (!IsAvailable() || self->page == nullptr) return;
    if (self->multi) {
        if (hudSlot >= kMaxHudSlots) return;
        sOn[hudSlot] = !sOn[hudSlot];
        ShowMulti(self->page);
    } else {
        sOn[0] = !sOn[0];
        ShowSingle(self->page);
    }
}

// ---- Hooks ----

static void Register(PageHandler& handler, void* page) {
    handler.page = page;
    ControlsManipulatorManager_setGlobalHandler(MenuPage_getManipulatorManager(page), INPUT_ACTION_SWITCH,
                                                &handler.base, false, false);
}

static void DriftSelectOnInit(void* page) {
    DriftSelectPage_onInit(page);
    Register(sSingleHandler, page);
}

// The game sets the bottom bar whenever a button is highlighted; ours
// replaces it afterwards.
static void DriftSelectOnButtonSelect(void* page, void* button, u32 hudSlot) {
    DriftSelectPage_onButtonSelect(page, button, hudSlot);
    sSelectedButton = PushButton_getId(button);
    if (IsAvailable()) ShowSingle(page);
}

static void MultiDriftSelectOnInit(void* page) {
    MultiDriftSelectPage_onInit(page);
    Register(sMultiHandler, page);
}

static void MultiDriftSelectOnActivate(void* page) {
    MultiDriftSelectPage_onActivate(page);
    if (IsAvailable()) ShowMulti(page);
}

// Pages::DriftSelect vtable (0x808D9DB0): onInit +0x28, onButtonSelect +0x64.
kmWritePointer(0x808D9DD8, DriftSelectOnInit);
kmWritePointer(0x808D9E14, DriftSelectOnButtonSelect);
// Pages::MultiDriftSelect vtable (0x808D9BC8): onInit +0x28, onActivate +0x30.
kmWritePointer(0x808D9BF0, MultiDriftSelectOnInit);
kmWritePointer(0x808D9BF8, MultiDriftSelectOnActivate);

} // namespace DriftSelectToggle

bool SmartSteeringToggle_isOn(s32 hudSlot) {
    if (hudSlot < 0 || hudSlot >= DriftSelectToggle::kMaxHudSlots) return false;
    return DriftSelectToggle::sOn[hudSlot];
}
