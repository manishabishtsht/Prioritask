// PrioriTask - desktop front end (Dear ImGui + GLFW + OpenGL 3).
// Uses the same database, scheduler, advisor and commitments code as the
// CLI. Only the screens are different.

#include "advisor.h"
#include "commitments.h"
#include "database.h"
#include "monthly.h"
#include "scheduler.h"
#include "timeutil.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_stdlib.h"     // lets ImGui::InputText edit a std::string
#include "imgui_internal.h"   // ShadeVertsLinearColorGradientKeepAlpha, for gradients
#include <GLFW/glfw3.h>

#include <cfloat>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <string>

namespace {

// ---------------------------------------------------------------
// Colours
// ---------------------------------------------------------------
// The amber theme: warm near-black surfaces, one amber accent, and green,
// blue and red kept for meaning (done, information, danger).
const ImVec4 kAccent(0.98f, 0.74f, 0.25f, 1.0f);   // amber
const ImVec4 kWarn  (1.00f, 0.58f, 0.28f, 1.0f);   // overdue
const ImVec4 kGood  (0.36f, 0.84f, 0.52f, 1.0f);
const ImVec4 kMuted (0.60f, 0.60f, 0.65f, 1.0f);
const ImVec4 kSoon  (0.98f, 0.80f, 0.36f, 1.0f);   // due within a day
const ImVec4 kDanger(1.00f, 0.45f, 0.42f, 1.0f);
const ImVec4 kInfo  (0.42f, 0.62f, 1.00f, 1.0f);   // categories, plain information

const ImVec4 kBtn      (0.98f, 0.72f, 0.22f, 1.0f);
const ImVec4 kBtnHover (1.00f, 0.80f, 0.36f, 1.0f);
const ImVec4 kBtnActive(0.90f, 0.62f, 0.14f, 1.0f);
const ImVec4 kBtnText  (0.10f, 0.08f, 0.04f, 1.0f);   // dark text on amber

const ImVec4 kBg      (0.035f, 0.035f, 0.045f, 1.0f);  // the window
const ImVec4 kPanel   (0.075f, 0.075f, 0.090f, 1.0f);  // cards
const ImVec4 kPanelTop(0.105f, 0.100f, 0.115f, 1.0f);  // top of a card's gradient
const ImVec4 kEdge    (1.00f, 1.00f, 1.00f, 0.08f);    // card borders

// ---------------------------------------------------------------
// Icons, from the Font Awesome 4 font (Ubuntu package fonts-font-awesome).
// If that font isn't installed, icons quietly disappear and every label
// still reads correctly.
// ---------------------------------------------------------------
bool gIcons = false;
ImFont* gTitleFont = nullptr;   // the largest font, for the focus timer

enum Icon : unsigned {
    IconClock    = 0xF017, IconHourglass = 0xF252, IconTasks   = 0xF0AE,
    IconCalendar = 0xF073, IconPlus      = 0xF067, IconPencil  = 0xF040,
    IconCheck    = 0xF00C, IconTrash     = 0xF1F8, IconCoffee  = 0xF0F4,
    IconMarker   = 0xF041, IconBolt      = 0xF0E7, IconWarning = 0xF071,
    IconBed      = 0xF236, IconRefresh   = 0xF021, IconSave    = 0xF0C7,
    IconChart    = 0xF080, IconLeft      = 0xF053, IconRight   = 0xF054,
    IconPlay     = 0xF04B, IconPause     = 0xF04C, IconStop    = 0xF04D,
    IconFire     = 0xF06D, IconGrid      = 0xF009, IconList    = 0xF03A,
    IconHome     = 0xF015, IconBulb      = 0xF0EB, IconUser    = 0xF007,
    IconShuffle  = 0xF074, IconSignal    = 0xF012, IconStar    = 0xF005,
    IconCircleQ  = 0xF059, IconCrosshair = 0xF05B, IconBook    = 0xF02D,
    IconHeart    = 0xF004, IconCircle    = 0xF111, IconTrophy  = 0xF091,
    IconSearch   = 0xF002, IconCheckCircle = 0xF058, IconCircleO = 0xF10C,
    IconTimes    = 0xF00D
};

// An icon as UTF-8 text. These code points all take three bytes.
std::string glyph(unsigned cp)
{
    std::string s;
    s += static_cast<char>(0xE0 | (cp >> 12));
    s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    s += static_cast<char>(0x80 | (cp & 0x3F));
    return s;
}

// "icon  text", or just "text" when icons aren't available.
std::string withIcon(unsigned cp, const char* text)
{
    return gIcons ? glyph(cp) + "  " + text : std::string(text);
}

// The icon on its own, or a word in its place when icons aren't available.
std::string iconOr(unsigned cp, const char* word)
{
    return gIcons ? glyph(cp) : std::string(word);
}

// ---------------------------------------------------------------
// Small building blocks. Everything here changes appearance only.
// ---------------------------------------------------------------

// A solid amber button, with dark text, for the main action on each screen.
bool primaryButton(const char* label, const ImVec2& size = ImVec2(0, 0))
{
    ImGui::PushStyleColor(ImGuiCol_Button,        kBtn);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kBtnHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  kBtnActive);
    ImGui::PushStyleColor(ImGuiCol_Text,          kBtnText);
    bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return pressed;
}

// A see-through button with a thin light outline, for the second choice.
bool ghostButton(const char* label, const ImVec2& size = ImVec2(0, 0))
{
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.0f, 0.0f, 0.0f, 0.25f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.0f, 1.0f, 1.0f, 0.14f));
    ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(1.0f, 1.0f, 1.0f, 0.22f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return pressed;
}

// A red button for actions that can't be undone.
bool dangerButton(const char* label, const ImVec2& size = ImVec2(0, 0))
{
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.72f, 0.26f, 0.26f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.84f, 0.33f, 0.33f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.62f, 0.20f, 0.20f, 1.0f));
    bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

// A small rounded badge: coloured text on a faint background of the same colour.
void pill(const char* text, const ImVec4& col)
{
    const float padX = 8.0f, padY = 1.0f;
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 p  = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(p.x, p.y - padY), ImVec2(p.x + ts.x + 2 * padX, p.y + ts.y + padY),
        ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.18f)), 999.0f);
    ImGui::SetCursorScreenPos(ImVec2(p.x + padX, p.y));
    ImGui::TextColored(col, "%s", text);
}

// A label on the LEFT of a field, the usual reading order. (Dear ImGui puts
// labels on the right by default, which is what made the forms look odd.)
void fieldLabel(const char* text, float width)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kMuted, "%s", text);
    ImGui::SameLine(width);
}

// A tooltip on whatever was drawn just before.
void tip(const char* text)
{
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
}

// ---------------------------------------------------------------
// Measuring: layouts are sized from the real font, never from guessed
// pixel counts, so nothing is cut off whatever font or window size is used.
// ---------------------------------------------------------------

// Width of a button with this label. Text after "##" is an ID, not drawn.
float buttonWidth(const std::string& label)
{
    return ImGui::CalcTextSize(label.c_str(), nullptr, true).x + 2.0f * ImGui::GetStyle().FramePadding.x;
}

// Width of buttons placed side by side with SameLine().
float buttonRowWidth(std::initializer_list<std::string> labels)
{
    float w = 0.0f;
    int   n = 0;
    for (const std::string& l : labels) { w += buttonWidth(l); ++n; }
    return w + (n > 1 ? (n - 1) * ImGui::GetStyle().ItemSpacing.x : 0.0f);
}

// A fixed table column wide enough for the widest of these texts.
float columnFor(std::initializer_list<const char*> samples)
{
    float w = 0.0f;
    for (const char* t : samples) w = maxOf(w, ImGui::CalcTextSize(t).x);
    return w + 2.0f * ImGui::GetStyle().CellPadding.x;
}

// The text, shortened with "..." if it is wider than maxWidth.
std::string fitText(const std::string& text, float maxWidth)
{
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;
    std::string t = text;
    while (!t.empty() && ImGui::CalcTextSize((t + "...").c_str()).x > maxWidth) {
        // Remove one whole character: continuation bytes first, then its first byte.
        while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80) t.pop_back();
        if (!t.empty()) t.pop_back();
    }
    return t + "...";
}

// Priority 5 is red, 1 is grey, so urgency reads at a glance.
ImVec4 priorityColor(int p)
{
    switch (p) {
        case 5:  return kDanger;
        case 4:  return ImVec4(1.00f, 0.68f, 0.35f, 1.0f);
        case 3:  return kSoon;
        case 2:  return ImVec4(0.55f, 0.75f, 1.00f, 1.0f);
        default: return kMuted;
    }
}

void applyTheme()
{
    ImGuiStyle& st = ImGui::GetStyle();

    st.WindowPadding     = ImVec2(20, 16);
    st.FramePadding      = ImVec2(12, 7);
    st.ItemSpacing       = ImVec2(10, 10);
    st.ItemInnerSpacing  = ImVec2(8, 6);
    st.CellPadding       = ImVec2(10, 8);
    st.WindowRounding    = 0.0f;
    st.ChildRounding     = 16.0f;
    st.ChildBorderSize   = 1.0f;
    st.FrameRounding     = 10.0f;
    st.PopupRounding     = 12.0f;
    st.GrabRounding      = 10.0f;
    st.TabRounding       = 10.0f;
    st.ScrollbarRounding = 10.0f;
    st.ScrollbarSize     = 10.0f;
    st.WindowBorderSize  = 0.0f;
    st.FrameBorderSize   = 0.0f;
    st.PopupBorderSize   = 1.0f;

    ImVec4* c = st.Colors;
    const ImVec4 bg      = kBg;
    const ImVec4 panel   = kPanel;
    const ImVec4 frame   (0.13f, 0.13f, 0.15f, 1.0f);
    const ImVec4 frameHov(0.19f, 0.18f, 0.20f, 1.0f);
    const ImVec4 border  (0.17f, 0.17f, 0.19f, 1.0f);

    c[ImGuiCol_Text]              = ImVec4(0.95f, 0.94f, 0.92f, 1.0f);
    c[ImGuiCol_TextDisabled]      = ImVec4(0.46f, 0.46f, 0.50f, 1.0f);
    c[ImGuiCol_TextSelectedBg]    = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.30f);
    c[ImGuiCol_NavHighlight]      = kAccent;
    c[ImGuiCol_WindowBg]          = bg;
    c[ImGuiCol_ChildBg]           = panel;
    c[ImGuiCol_PopupBg]           = panel;
    c[ImGuiCol_Border]            = border;
    c[ImGuiCol_FrameBg]           = frame;
    c[ImGuiCol_FrameBgHovered]    = frameHov;
    c[ImGuiCol_FrameBgActive]     = frameHov;
    c[ImGuiCol_Button]            = frame;
    c[ImGuiCol_ButtonHovered]     = frameHov;
    c[ImGuiCol_ButtonActive]      = kBtnActive;
    c[ImGuiCol_Header]            = frame;
    c[ImGuiCol_HeaderHovered]     = frameHov;
    c[ImGuiCol_HeaderActive]      = frameHov;
    c[ImGuiCol_Separator]         = border;
    c[ImGuiCol_SliderGrab]        = kBtn;
    c[ImGuiCol_SliderGrabActive]  = kBtnHover;
    c[ImGuiCol_CheckMark]         = kBtnHover;
    c[ImGuiCol_Tab]               = panel;
    c[ImGuiCol_TabHovered]        = frameHov;
    c[ImGuiCol_TabSelected]       = frame;
    c[ImGuiCol_TableHeaderBg]     = panel;
    c[ImGuiCol_TableBorderStrong] = border;
    c[ImGuiCol_TableBorderLight]  = ImVec4(0.15f, 0.15f, 0.17f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered] = frameHov;
    c[ImGuiCol_ScrollbarGrabActive]  = kBtnActive;
    c[ImGuiCol_TableRowBg]        = ImVec4(0.00f, 0.00f, 0.00f, 0.0f);
    c[ImGuiCol_TableRowBgAlt]     = ImVec4(1.00f, 1.00f, 1.00f, 0.025f);
    c[ImGuiCol_PlotHistogram]     = kBtn;
    c[ImGuiCol_ScrollbarBg]       = bg;
    c[ImGuiCol_ScrollbarGrab]     = frame;
    c[ImGuiCol_ModalWindowDimBg]  = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);
}

// ---------------------------------------------------------------
// State
// ---------------------------------------------------------------

// The add/edit form. editingId == 0 means "new task".
struct TaskForm {
    int         editingId = 0;
    std::string title, description, category, deadline;
    int         priority = 3, difficulty = 3, energy = 3;
    int         estimated = 60, minimum = 30;
    bool        mandatory = false;
    bool        needsOutside = false;
    std::string error;
};

// Everything the screens remember between frames.
struct AppState {
    DynArray<Task>            tasks;
    DynArray<CommitmentRow>   commitmentRows;   // as stored, for the schedule table
    DynArray<CommitmentBlock> commitments;      // as numbers, for the calculations
    bool showCompleted = false;
    bool listView = false;         // Tasks tab: false = cards, true = table
    int  taskFilter = 1;           // Tasks tab: 0 = all, 1 = pending, 2 = completed
    std::string search;            // Tasks tab: the search box
    bool scheduleList = false;     // My schedule: false = week grid, true = list

    // Plan my day
    bool       havePlan  = false;
    bool       planSaved = false;
    PlanResult plan;
    int        planNow = 0, planWake = 0, planWindow = 0, planBusy = 0, planBudget = 0;

    // My schedule: the add form
    std::string cName, cStart, cEnd, cError;
    bool        cDays[7] = {true, true, true, true, true, false, false};   // Mon-Fri

    // What now
    int          energy = 3;
    bool         haveSuggestions = false;
    AdviceResult advice;           // the last answer, with its status
    int          pick = 0;         // which suggestion is shown ("Choose another" moves it)
    int          adviceSlot = 0;   // the free slot it was asked for
    int          slotMinutes = 0;  // free slot picked during a commitment
    bool         slotOutside = false;
    std::string  wakeText;
    int          wakeSaved = -1;   // minutes, or -1 if none
    std::string  wakeMessage;

    // Navigation and greeting
    int          page = 0;                          // the sidebar page on screen
    std::string  userName, nameText, nameMessage;   // the name used in the greeting

    // This month
    int          monthYear = 0, monthNum = 0;   // the month on screen; 0 = not chosen yet
    bool         haveMonth = false;
    MonthReport  month;

    // Focus timer (What now)
    bool         focusActive = false, focusPaused = false;
    int          focusTaskId = 0;
    std::string  focusTitle;
    double       focusSeconds = 0.0;         // the timer's length
    double       focusStartedAt = 0.0;       // ImGui::GetTime() when last started
    double       focusElapsedBefore = 0.0;   // time run before the last pause

    TaskForm form;
    bool     openTaskDialog = false;

    int         deleteId = 0;
    std::string deleteTitle;
    bool        openDeleteDialog = false;

    std::string status;
};

// ---------------------------------------------------------------
// Data: unchanged from before
// ---------------------------------------------------------------
DynArray<CommitmentBlock> toBlocks(const DynArray<CommitmentRow>& rows)
{
    DynArray<CommitmentBlock> out;
    for (const CommitmentRow& r : rows) {
        int s = tu::parseHM(r.start), e = tu::parseHM(r.end);
        if (s < 0 || e < 0 || r.days.size() != 7) continue;
        CommitmentBlock b;
        b.label = r.label; b.days = r.days; b.startMin = s; b.endMin = e;
        out.push_back(b);
    }
    return out;
}

// Reading the database 60 times a second would be wasteful, so data is
// cached here and reloaded only after something changes.
void reloadAll(Database& db, AppState& s)
{
    s.tasks           = db.getAllTasks();
    s.commitmentRows  = db.getCommitments();
    s.commitments     = toBlocks(s.commitmentRows);
    s.haveSuggestions = false;
    s.havePlan        = false;     // the old plan no longer matches the data
    s.haveMonth       = false;     // nor does the monthly report
}

int savedWakeTime(Database& db)
{
    std::string v;
    if (!db.getSetting("wake_time", v)) return -1;
    return tu::parseHM(v);
}

TaskForm formFromTask(const Task& t)
{
    TaskForm f;
    f.editingId    = t.id;
    f.title        = t.title;
    f.description  = t.description;
    f.category     = t.category;
    f.deadline     = t.deadline;
    f.priority     = t.priority;
    f.difficulty   = t.difficulty;
    f.energy       = t.energyRequired;
    f.estimated    = t.estimatedTime;
    f.minimum      = t.minTime;
    f.mandatory    = t.isMandatory;
    f.needsOutside = t.needsOutside;
    return f;
}

Task taskFromForm(const TaskForm& f)
{
    Task t;
    t.id             = f.editingId;
    t.title          = f.title;
    t.description    = f.description;
    t.category       = f.category;
    t.deadline       = f.deadline;
    t.priority       = f.priority;
    t.difficulty     = f.difficulty;
    t.energyRequired = f.energy;
    t.estimatedTime  = f.estimated;
    t.minTime        = f.minimum;
    t.isMandatory    = f.mandatory;
    t.needsOutside   = f.needsOutside;
    return t;
}

void computeSuggestions(Database& db, AppState& s)
{
    int wake = savedWakeTime(db);
    if (wake < 0) wake = 7 * 60;

    DynArray<AdviceItem> items;
    for (const Task& t : s.tasks) {
        if (t.status == "Completed") continue;
        AdviceItem a;
        a.id             = t.id;
        a.title          = t.title;
        a.priority       = t.priority;
        a.energyRequired = t.energyRequired;
        a.estimatedTime  = t.estimatedTime;
        a.minimumTime    = t.minTime;
        a.isMandatory    = t.isMandatory;
        a.needsOutside   = t.needsOutside;
        a.isSleep        = tu::isSleepTask(t.title);
        if (!t.deadline.empty())
            a.hasDeadline = tu::minutesUntilDeadline(t.deadline, a.minutesToDeadline);
        items.push_back(a);
    }

    // During a commitment, only a free slot the user picked unlocks
    // suggestions, and going out is off unless they ticked it.
    std::string busyWith;
    int freeAt = 0, left = 0;
    bool busy = currentCommitment(s.commitments, tu::nowMinutes(), tu::todayIndex(),
                                  busyWith, freeAt, left);

    AdviceContext ctx;
    ctx.userEnergy   = s.energy;
    ctx.nowMinutes   = tu::nowMinutes();
    ctx.wakeMinutes  = wake;
    ctx.busyNow      = busy;
    ctx.freeMinutes  = busy ? s.slotMinutes : 0;
    ctx.allowOutside = busy ? s.slotOutside : true;

    s.advice          = adviseNow(items, ctx, 5);
    s.pick            = 0;
    s.adviceSlot      = ctx.freeMinutes;
    s.haveSuggestions = true;
}

int priorityOf(const AppState& s, int id)
{
    for (const Task& t : s.tasks) if (t.id == id) return t.priority;
    return 0;
}

void computePlan(Database& db, AppState& s)
{
    int wake = savedWakeTime(db);
    if (wake < 0) wake = 7 * 60;

    // The window runs from now to the next wake-up. Fixed commitments in
    // that window are not available, so they come off the top.
    s.planNow    = tu::nowMinutes();
    s.planWake   = wake;
    s.planWindow = tu::minutesUntil(wake);
    s.planBusy   = busyMinutesInWindow(s.commitments, s.planNow, s.planWindow, tu::todayIndex());
    s.planBudget = s.planWindow - s.planBusy;

    DynArray<ShrinkItem> items;
    for (const Task& t : s.tasks) {
        if (t.status == "Completed") continue;
        ShrinkItem it;
        it.id        = t.id;
        it.title     = t.title;
        it.requested = t.estimatedTime;
        it.minimum   = t.minTime;
        it.priority  = t.priority;
        it.mandatory = t.isMandatory;
        items.push_back(it);
    }

    s.plan      = planWithDropping(items, s.planBudget);
    s.havePlan  = true;
    s.planSaved = false;
}

// ---------------------------------------------------------------
// Cards
// ---------------------------------------------------------------

// Width of each card when n of them share one row.
float cardWidth(int n)
{
    float avail = ImGui::GetContentRegionAvail().x;
    float gap   = ImGui::GetStyle().ItemSpacing.x;
    return (avail - gap * (n - 1)) / n;
}

// A thin glowing line across the top of the current panel, fading out at both ends.
void panelAccent(const ImVec4& col)
{
    ImVec2 p  = ImGui::GetWindowPos();
    ImVec2 sz = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p, ImVec2(p.x + sz.x, p.y + sz.y), false);
    ImU32 solid = ImGui::GetColorU32(col);
    ImU32 clear = ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.0f));
    float mid = p.x + sz.x * 0.5f, y0 = p.y + 1.0f, y1 = p.y + 3.0f;
    dl->AddRectFilledMultiColor(ImVec2(p.x + 14.0f, y0), ImVec2(mid, y1), clear, solid, solid, clear);
    dl->AddRectFilledMultiColor(ImVec2(mid, y0), ImVec2(p.x + sz.x - 14.0f, y1), solid, clear, clear, solid);
    dl->PopClipRect();
}

// ---------------------------------------------------------------
// Amber look: gradients, glass cards, glows and the mood faces.
// Drawing only; none of this touches the data.
// ---------------------------------------------------------------
ImU32 rgba(const ImVec4& c, float a)
{
    return ImGui::GetColorU32(ImVec4(c.x, c.y, c.z, a));
}

// A rounded rectangle whose colour fades from `from` (at g0) to `to` (at g1).
void gradientRect(ImDrawList* dl, ImVec2 a, ImVec2 b, const ImVec4& from, const ImVec4& to,
                  float rounding, ImVec2 g0, ImVec2 g1, float alpha = 1.0f)
{
    int start = dl->VtxBuffer.Size;
    dl->AddRectFilled(a, b, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, alpha)), rounding);
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, start, dl->VtxBuffer.Size, g0, g1,
                                                  ImGui::GetColorU32(from), ImGui::GetColorU32(to));
}

// A soft round glow: strongest in the middle, fading to nothing at `radius`.
void radialGlow(ImDrawList* dl, ImVec2 c, float radius, const ImVec4& col, float alpha)
{
    const int n = 64;
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    const ImU32 inner = rgba(col, alpha), mid = rgba(col, alpha * 0.30f), outer = rgba(col, 0.0f);
    dl->PrimReserve(n * 9, 1 + 2 * n);
    const ImDrawIdx base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
    dl->PrimWriteVtx(c, uv, inner);
    for (int i = 0; i < n; ++i) {
        float a = 6.2831853f * i / n;
        dl->PrimWriteVtx(ImVec2(c.x + std::cos(a) * radius * 0.45f, c.y + std::sin(a) * radius * 0.45f), uv, mid);
    }
    for (int i = 0; i < n; ++i) {
        float a = 6.2831853f * i / n;
        dl->PrimWriteVtx(ImVec2(c.x + std::cos(a) * radius, c.y + std::sin(a) * radius), uv, outer);
    }
    for (int i = 0; i < n; ++i) {
        ImDrawIdx m0 = base + 1 + i, m1 = base + 1 + (i + 1) % n;
        ImDrawIdx o0 = m0 + n, o1 = m1 + n;
        dl->PrimWriteIdx(base); dl->PrimWriteIdx(m0); dl->PrimWriteIdx(m1);
        dl->PrimWriteIdx(m0);   dl->PrimWriteIdx(o0); dl->PrimWriteIdx(o1);
        dl->PrimWriteIdx(m0);   dl->PrimWriteIdx(o1); dl->PrimWriteIdx(m1);
    }
}

// Card styles: a plain dark glass card, or the warm amber "hero" card.
enum CardStyle { CardPlain = 0, CardHero };

// BeginChild with the amber look: a soft top-to-bottom gradient and a hairline
// border instead of the flat panel colour. Always pair with ImGui::EndChild().
bool beginCard(const char* id, ImVec2 size, CardStyle style = CardPlain, ImGuiWindowFlags flags = 0)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Border,  ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    bool open = ImGui::BeginChild(id, size, ImGuiChildFlags_Borders, flags);
    ImGui::PopStyleColor(2);

    ImVec2 p  = ImGui::GetWindowPos();
    ImVec2 sz = ImGui::GetWindowSize();
    ImVec2 q(p.x + sz.x, p.y + sz.y);
    float  r  = ImGui::GetStyle().ChildRounding;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p, q, false);
    if (style == CardHero) {
        // Dark brown at the top left, warming to amber at the bottom right.
        gradientRect(dl, p, q, ImVec4(0.11f, 0.085f, 0.055f, 1.0f), ImVec4(0.40f, 0.26f, 0.07f, 1.0f),
                     r, p, q);
        dl->PushClipRect(ImVec2(p.x + 1, p.y + 1), ImVec2(q.x - 1, q.y - 1), true);
        radialGlow(dl, ImVec2(q.x - sz.x * 0.18f, p.y + sz.y * 0.30f), sz.y * 0.85f, kAccent, 0.30f);
        dl->PopClipRect();
        dl->AddRect(p, q, rgba(kAccent, 0.55f), r, 0, 1.2f);
    } else {
        gradientRect(dl, p, q, kPanelTop, kPanel, r, p, ImVec2(p.x, q.y));
        dl->AddRect(p, q, ImGui::GetColorU32(kEdge), r, 0, 1.0f);
    }
    dl->PopClipRect();
    return open;
}

// A card heading: an amber icon, then the text in the normal colour.
void cardHeading(unsigned cp, const char* text, const char* note = nullptr)
{
    if (gIcons) {
        ImGui::TextColored(kAccent, "%s", glyph(cp).c_str());
        ImGui::SameLine();
    }
    ImGui::Text("%s", text);
    if (note) {
        ImGui::SameLine(0, 12);
        ImGui::TextColored(kMuted, "%s", note);
    }
    ImGui::Spacing();
}

// A small rounded chip with an icon (cp = 0 for none), like the date in the top bar.
const ImVec2 kChipPad(12.0f, 6.0f);

float chipWidth(unsigned cp, const std::string& text)
{
    std::string label = cp ? withIcon(cp, text.c_str()) : text;
    return ImGui::CalcTextSize(label.c_str()).x + 2.0f * kChipPad.x;
}

void infoChip(unsigned cp, const std::string& text, const ImVec4& col, bool filled = false)
{
    std::string label = cp ? withIcon(cp, text.c_str()) : text;
    ImVec2 ts = ImGui::CalcTextSize(label.c_str());
    ImVec2 pad = kChipPad;
    ImVec2 p  = ImGui::GetCursorScreenPos();
    ImVec2 q(p.x + ts.x + 2 * pad.x, p.y + ts.y + 2 * pad.y);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, q, filled ? rgba(col, 0.14f) : rgba(ImVec4(1, 1, 1, 1), 0.035f), 999.0f);
    dl->AddRect(p, q, filled ? rgba(col, 0.45f) : ImGui::GetColorU32(kEdge), 999.0f);
    dl->AddText(ImVec2(p.x + pad.x, p.y + pad.y), ImGui::GetColorU32(col), label.c_str());
    ImGui::Dummy(ImVec2(q.x - p.x, q.y - p.y));
}

// How each energy level is named and coloured on the mood tiles.
const char* energyName(int level)
{
    static const char* names[6] = {"", "Exhausted", "Low", "Okay", "Energetic", "Focused"};
    return names[clampTo(level, 1, 5)];
}

ImVec4 energyColor(int level)
{
    if (level <= 2) return ImVec4(0.58f, 0.58f, 0.62f, 1.0f);   // grey
    if (level == 3) return kAccent;                               // amber
    return kGood;                                                 // green
}

// A simple round face: closed eyes and a frown at 1, a wide smile at 5.
void drawFace(ImDrawList* dl, ImVec2 c, float r, int level, const ImVec4& col)
{
    dl->AddCircleFilled(c, r, rgba(col, 1.0f), 40);
    dl->AddCircleFilled(ImVec2(c.x - r * 0.25f, c.y - r * 0.30f), r * 0.45f, rgba(ImVec4(1, 1, 1, 1), 0.10f), 24);

    const ImU32 ink = IM_COL32(28, 22, 12, 235);
    const float ex = r * 0.34f, ey = r * 0.18f, er = r * 0.11f;
    if (level == 1) {
        dl->AddLine(ImVec2(c.x - ex - er * 1.3f, c.y - ey), ImVec2(c.x - ex + er * 1.3f, c.y - ey), ink, r * 0.10f);
        dl->AddLine(ImVec2(c.x + ex - er * 1.3f, c.y - ey), ImVec2(c.x + ex + er * 1.3f, c.y - ey), ink, r * 0.10f);
    } else {
        dl->AddCircleFilled(ImVec2(c.x - ex, c.y - ey), er, ink, 12);
        dl->AddCircleFilled(ImVec2(c.x + ex, c.y - ey), er, ink, 12);
    }
    static const float bend[6] = {0.0f, -0.55f, -0.25f, 0.0f, 0.45f, 0.80f};   // + smiles, - frowns
    float k  = bend[clampTo(level, 1, 5)];
    float my = c.y + r * 0.36f - k * r * 0.12f;
    float hw = r * (0.30f + 0.08f * (k > 0 ? k : 0.0f));
    dl->AddBezierQuadratic(ImVec2(c.x - hw, my), ImVec2(c.x, my + k * r * 0.50f), ImVec2(c.x + hw, my),
                           ink, r * 0.10f, 16);
}

// One mood tile: the face, the number and the name. Returns true when clicked.
bool moodTile(int level, bool selected, float w, float h)
{
    ImGui::PushID(level);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton("##mood", ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImVec2 q(p.x + w, p.y + h);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = 12.0f;
    if (selected) {
        gradientRect(dl, p, q, ImVec4(0.30f, 0.21f, 0.07f, 1.0f), ImVec4(0.16f, 0.12f, 0.06f, 1.0f), r, p, ImVec2(p.x, q.y));
        dl->AddRect(p, q, rgba(kAccent, 0.95f), r, 0, 1.5f);
    } else {
        dl->AddRectFilled(p, q, rgba(ImVec4(1, 1, 1, 1), hovered ? 0.07f : 0.035f), r);
        dl->AddRect(p, q, ImGui::GetColorU32(kEdge), r);
    }

    float  lineH = ImGui::GetTextLineHeight();
    float  faceR = minOf(w * 0.24f, (h - 2.0f * lineH - 20.0f) * 0.5f);
    float  top   = p.y + (h - (2.0f * faceR + 6.0f + 2.0f * lineH)) * 0.5f;   // centred in the tile
    ImVec2 fc(p.x + w * 0.5f, top + faceR);
    drawFace(dl, fc, faceR, level, energyColor(level));

    std::string num  = std::to_string(level);
    std::string name = fitText(energyName(level), w - 6.0f);
    ImVec4 textCol = selected ? ImGui::GetStyle().Colors[ImGuiCol_Text] : kMuted;
    ImVec2 ns = ImGui::CalcTextSize(num.c_str()), ls = ImGui::CalcTextSize(name.c_str());
    float ty = fc.y + faceR + 6.0f;
    dl->AddText(ImVec2(p.x + (w - ns.x) * 0.5f, ty), ImGui::GetColorU32(textCol), num.c_str());
    dl->AddText(ImVec2(p.x + (w - ls.x) * 0.5f, ty + lineH), ImGui::GetColorU32(textCol), name.c_str());
    return pressed;
}

// ---------------------------------------------------------------
// Shared by the amber tabs: category colours, metric cards, the donut
// ---------------------------------------------------------------

// The category in lower case, for matching.
std::string lowerCase(std::string t)
{
    for (char& c : t) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return t;
}

bool contains(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

// One colour per category, so a task looks the same on every tab. Known
// names get fixed colours; any other category gets one from a palette.
ImVec4 categoryColor(const std::string& category, const std::string& title = "")
{
    std::string c = lowerCase(category);
    if (tu::isSleepTask(title))                        return ImVec4(0.52f, 0.56f, 0.98f, 1.0f);   // periwinkle
    if (contains(c, "study") || contains(c, "college")) return ImVec4(0.66f, 0.50f, 1.00f, 1.0f);  // purple
    if (contains(c, "health") || contains(c, "gym"))    return ImVec4(1.00f, 0.62f, 0.32f, 1.0f);  // orange
    if (contains(c, "personal"))                        return ImVec4(0.36f, 0.84f, 0.58f, 1.0f);  // green
    if (contains(c, "curr") || contains(c, "club"))   return ImVec4(0.38f, 0.66f, 1.00f, 1.0f);  // blue
    if (c.empty())                                      return kMuted;
    static const ImVec4 palette[5] = {
        ImVec4(0.98f, 0.45f, 0.62f, 1.0f), ImVec4(0.30f, 0.80f, 0.85f, 1.0f), ImVec4(0.90f, 0.80f, 0.35f, 1.0f),
        ImVec4(0.75f, 0.55f, 0.40f, 1.0f), ImVec4(0.60f, 0.85f, 0.35f, 1.0f)};
    unsigned h = 0;
    for (char ch : c) h = h * 31u + static_cast<unsigned char>(ch);
    return palette[h % 5];
}

unsigned categoryIcon(const std::string& category, const std::string& title = "")
{
    std::string c = lowerCase(category);
    if (tu::isSleepTask(title))                         return IconBed;
    if (contains(c, "study") || contains(c, "college")) return IconBook;
    if (contains(c, "health") || contains(c, "gym"))    return IconHeart;
    if (contains(c, "personal"))                        return IconUser;
    if (contains(c, "curr") || contains(c, "club"))   return IconStar;
    return IconCircle;
}

// A metric card like the concept: an icon in a tinted rounded square, a big
// value and a label. `glow` tints the whole card in the colour (for the
// numbers that matter most).
void metricCard(const char* id, float width, ImFont* large, unsigned cp, const ImVec4& color,
                const std::string& value, const std::string& label, const char* tooltip = nullptr,
                bool glow = false)
{
    const ImGuiStyle& st = ImGui::GetStyle();
    float valueH = large ? large->FontSize : ImGui::GetTextLineHeight();
    float height = 2.0f * st.WindowPadding.y + valueH + ImGui::GetTextLineHeight() + 6.0f;
    beginCard(id, ImVec2(width, height), CardPlain, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (glow) {
        dl->PushClipRect(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), false);
        gradientRect(dl, ImVec2(wp.x + 1, wp.y + 1), ImVec2(wp.x + ws.x - 1, wp.y + ws.y - 1),
                     ImVec4(color.x * 0.16f, color.y * 0.16f, color.z * 0.16f, 1.0f), kPanel,
                     st.ChildRounding, wp, ImVec2(wp.x + ws.x, wp.y));
        dl->AddRect(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), rgba(color, 0.35f), st.ChildRounding);
        dl->PopClipRect();
    }

    // The icon tile, as tall as the two lines of text.
    float  tile = valueH + ImGui::GetTextLineHeight() + 2.0f;
    ImVec2 p    = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, ImVec2(p.x + tile, p.y + tile), rgba(color, 0.12f), 12.0f);
    dl->AddRect(p, ImVec2(p.x + tile, p.y + tile), rgba(color, 0.30f), 12.0f);
    if (gIcons) {
        std::string g = glyph(cp);
        if (large) ImGui::PushFont(large);
        ImVec2 gs = ImGui::CalcTextSize(g.c_str());
        dl->AddText(ImVec2(p.x + (tile - gs.x) * 0.5f, p.y + (tile - gs.y) * 0.5f), ImGui::GetColorU32(color), g.c_str());
        if (large) ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(tile, tile));
    ImGui::SameLine(0, 16);

    ImGui::BeginGroup();
    float textW = ImGui::GetContentRegionAvail().x;
    if (large) ImGui::PushFont(large);
    ImGui::TextColored(glow ? color : ImGui::GetStyle().Colors[ImGuiCol_Text], "%s", fitText(value, textW).c_str());
    if (large) ImGui::PopFont();
    ImGui::TextColored(kMuted, "%s", fitText(label, textW).c_str());
    ImGui::EndGroup();

    ImGui::EndChild();
    if (tooltip) tip(tooltip);
}

// A page heading. Right-hand buttons are drawn by the caller between
// headerRightBegin() and headerRightEnd(), measured by their total width.
struct HeaderPos { float x0, y0, y1, right; };

HeaderPos pageHeader(ImFont* large, const char* title, const char* subtitle)
{
    HeaderPos h;
    h.x0    = ImGui::GetCursorPosX();
    h.y0    = ImGui::GetCursorPosY();
    h.right = h.x0 + ImGui::GetContentRegionAvail().x;
    if (large) ImGui::PushFont(large);
    ImGui::Text("%s", title);
    if (large) ImGui::PopFont();
    ImGui::TextColored(kMuted, "%s", subtitle);
    ImGui::Spacing();
    h.y1 = ImGui::GetCursorPosY();
    return h;
}

void headerRightBegin(const HeaderPos& h, float width)
{
    ImGui::SetCursorPos(ImVec2(h.right - width, h.y0 + 4.0f));
}

void headerRightEnd(const HeaderPos& h)
{
    ImGui::SetCursorPos(ImVec2(h.x0, maxOf(h.y1, ImGui::GetCursorPosY())));
}

// A donut chart: segments go clockwise from 12 o'clock, with small gaps.
void donut(ImVec2 centre, float radius, float thickness, const float* values, const ImVec4* colors, int n)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircle(centre, radius, rgba(ImVec4(1, 1, 1, 1), 0.06f), 96, thickness);
    float total = 0.0f;
    for (int i = 0; i < n; ++i) total += maxOf(values[i], 0.0f);
    if (total <= 0.0f) return;
    float a   = -1.5707963f;
    float gap = 0.035f;
    for (int i = 0; i < n; ++i) {
        float sweep = 6.2831853f * maxOf(values[i], 0.0f) / total;
        if (sweep <= 0.0f) continue;
        float g = sweep > 3.0f * gap ? gap : 0.0f;
        radialGlow(dl, ImVec2(centre.x + std::cos(a + sweep * 0.5f) * radius, centre.y + std::sin(a + sweep * 0.5f) * radius),
                   thickness * 1.6f, colors[i], 0.10f);
        dl->PathArcTo(centre, radius, a + g * 0.5f, a + sweep - g * 0.5f, 64);
        dl->PathStroke(ImGui::GetColorU32(colors[i]), 0, thickness);
        a += sweep;
    }
}

// A dot and a label, for chart legends.
void legendRow(const ImVec4& col, const char* label, const std::string& value, float valueX)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    float  h = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + 5.0f, p.y + h * 0.5f), 5.0f, ImGui::GetColorU32(col));
    ImGui::SetCursorScreenPos(ImVec2(p.x + 18.0f, p.y));
    ImGui::Text("%s", label);
    ImGui::SameLine();
    ImGui::SetCursorPosX(valueX);
    ImGui::TextColored(kMuted, "%s", value.c_str());
}

// A summary card: one big number and a label, with a glow line in the card's colour.
void statCard(const char* id, float width, ImFont* large, const ImVec4& color,
              const std::string& value, const std::string& label, const char* tooltip = nullptr)
{
    const ImGuiStyle& st = ImGui::GetStyle();
    float valueH = large ? large->FontSize : ImGui::GetTextLineHeight();
    float height = 2.0f * st.WindowPadding.y + valueH + st.ItemSpacing.y + ImGui::GetTextLineHeight() + 6.0f;
    ImGui::BeginChild(id, ImVec2(width, height), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    panelAccent(color);

    ImGui::Indent(8);
    if (large) ImGui::PushFont(large);
    ImGui::TextColored(color, "%s", fitText(value, ImGui::GetContentRegionAvail().x).c_str());
    if (large) ImGui::PopFont();
    ImGui::TextColored(kMuted, "%s", fitText(label, ImGui::GetContentRegionAvail().x).c_str());
    ImGui::Unindent(8);

    ImGui::EndChild();
    if (tooltip) tip(tooltip);   // explains what the number means
}

// A muted, upper-case heading at the top of a panel.
void panelHeading(unsigned cp, const char* text)
{
    ImGui::TextColored(kMuted, "%s", withIcon(cp, text).c_str());
    ImGui::Spacing();
}

// ---------------------------------------------------------------
// What now: acting on a suggestion, the focus timer, today's ring
// ---------------------------------------------------------------
DynArray<MonthTask> toMonthTasks(const AppState& s)
{
    DynArray<MonthTask> items;
    for (const Task& t : s.tasks) {
        MonthTask m;
        m.id          = t.id;
        m.title       = t.title;
        m.priority    = t.priority;
        m.deadline    = t.deadline;
        m.completed   = (t.status == "Completed");
        m.completedAt = t.completedAt;
        m.minTime     = t.minTime;
        items.push_back(m);
    }
    return items;
}

// Seconds the focus timer has run, not counting pauses.
double focusElapsed(const AppState& s)
{
    return s.focusElapsedBefore + (s.focusPaused ? 0.0 : ImGui::GetTime() - s.focusStartedAt);
}

void startFocus(AppState& s, const Suggestion& sg)
{
    s.focusActive        = true;
    s.focusPaused        = false;
    s.focusTaskId        = sg.id;
    s.focusTitle         = sg.title;
    s.focusSeconds       = sg.estimatedTime * 60.0;   // already capped to a free slot, if any
    s.focusStartedAt     = ImGui::GetTime();
    s.focusElapsedBefore = 0.0;
}

// Marks a task done from the What now tab, then shows the next suggestion.
void markDoneFromNow(Database& db, AppState& s, int id, std::string title)
{
    bool stillPending = false;
    for (const Task& t : s.tasks)
        if (t.id == id && t.status != "Completed") stillPending = true;
    if (stillPending && db.markComplete(id)) s.status = "Completed: " + title;
    if (s.focusTaskId == id) s.focusActive = false;
    reloadAll(db, s);
    computeSuggestions(db, s);   // the next suggestion appears straight away
}

// A ring that fills clockwise from 12 o'clock, with a label in the middle.
void progressRing(float fraction, float radius, const char* centre, const ImVec4& col)
{
    const float thickness = 7.0f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 c(p.x + radius + thickness, p.y + radius + thickness);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircle(c, radius, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.08f)), 64, thickness);
    if (fraction > 0.0f) {
        const float top = -1.5707963f;                       // 12 o'clock, in radians
        dl->PathArcTo(c, radius, top, top + minOf(fraction, 1.0f) * 6.2831853f, 64);
        dl->PathStroke(ImGui::GetColorU32(col), 0, thickness);
    }
    ImVec2 ts = ImGui::CalcTextSize(centre);
    dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), ImGui::GetColorU32(col), centre);
    ImGui::Dummy(ImVec2(2 * (radius + thickness), 2 * (radius + thickness)));
}

// The focus timer, shown in place of the suggestion while it runs.
void drawFocus(Database& db, AppState& s, ImFont* /*large*/)
{
    double used = focusElapsed(s);
    double left = s.focusSeconds - used;
    bool   over = left < 0;
    int    secs = static_cast<int>(over ? -left : left);

    ImGui::TextColored(kAccent, "%s", withIcon(IconHourglass, s.focusPaused ? "FOCUS  -  PAUSED" : "FOCUS").c_str());
    ImGui::Spacing();
    if (gTitleFont) ImGui::PushFont(gTitleFont);
    ImGui::Text("%s", fitText(s.focusTitle, ImGui::GetContentRegionAvail().x - 6.0f).c_str());
    if (gTitleFont) ImGui::PopFont();
    ImGui::Spacing();

    char clock[32];
    std::snprintf(clock, sizeof(clock), "%s%02d:%02d", over ? "+" : "", secs / 60, secs % 60);
    if (gTitleFont) ImGui::PushFont(gTitleFont);
    ImGui::TextColored(over ? kSoon : (s.focusPaused ? kMuted : kGood), "%s", clock);
    if (gTitleFont) ImGui::PopFont();
    if (over)
        ImGui::TextColored(kSoon, "Time is up. Finish off, or mark it done.");
    else
        ImGui::TextColored(kMuted, "left of %d minutes", static_cast<int>(s.focusSeconds / 60));

    float frac = s.focusSeconds > 0 ? static_cast<float>(minOf(used / s.focusSeconds, 1.0)) : 1.0f;
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, over ? kSoon : kGood);
    ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 10), "");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    if (s.focusPaused) {
        if (primaryButton(withIcon(IconPlay, "Resume").c_str())) {
            s.focusPaused    = false;
            s.focusStartedAt = ImGui::GetTime();
        }
    } else if (ImGui::Button(withIcon(IconPause, "Pause").c_str())) {
        s.focusElapsedBefore += ImGui::GetTime() - s.focusStartedAt;
        s.focusPaused = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("+5 min")) s.focusSeconds += 5 * 60;
    ImGui::SameLine();
    if (ImGui::Button(withIcon(IconStop, "Stop").c_str())) s.focusActive = false;
    ImGui::SameLine();
    bool finish = primaryButton(withIcon(IconCheck, "Mark done").c_str());

    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kMuted, "The timer keeps running if you switch tabs. Nothing is saved "
                               "until you mark the task done.");
    ImGui::PopTextWrapPos();

    if (finish) markDoneFromNow(db, s, s.focusTaskId, s.focusTitle);
}

// ---------------------------------------------------------------
// Home: what to do now, how today is going, and what comes next
// ---------------------------------------------------------------

// One insight in a tinted box: blue for a tip, green for good news, orange for a warning.
void insightTile(const Insight& in)
{
    const ImVec4 col = in.kind == 2 ? kWarn : in.kind == 1 ? kGood : kAccent;
    const float  pad = 10.0f;
    float  w     = ImGui::GetContentRegionAvail().x;
    float  wrapW = w - 2.0f * pad - 8.0f;
    ImVec2 ts    = ImGui::CalcTextSize(in.text.c_str(), nullptr, false, wrapW);
    ImVec2 p     = ImGui::GetCursorScreenPos();
    float  h     = ts.y + 2.0f * pad;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.10f)), 8.0f);
    dl->AddRectFilled(ImVec2(p.x, p.y + 6.0f), ImVec2(p.x + 3.0f, p.y + h - 6.0f), ImGui::GetColorU32(col), 2.0f);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x + pad + 8.0f, p.y + pad),
                ImGui::GetColorU32(ImGuiCol_Text), in.text.c_str(), nullptr, wrapW);
    ImGui::Dummy(ImVec2(w, h));
}

// Bars for the last few days, today highlighted; hover a bar for its date.
void weekChart(const DynArray<DayCount>& days, float height)
{
    static const char* names[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    if (days.empty()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p      = ImGui::GetCursorScreenPos();
    float  w      = ImGui::GetContentRegionAvail().x;
    float  lineH  = ImGui::GetTextLineHeight();
    float  top    = p.y + lineH + 6.0f;              // room for the number above the tallest bar
    float  bottom = p.y + height - lineH - 8.0f;     // room for the day names
    if (bottom - top < 20.0f) bottom = top + 20.0f;

    int most = 1;
    for (const DayCount& d : days) most = maxOf(most, d.count);
    float slot = w / static_cast<float>(days.size());
    float barW = minOf(slot * 0.5f, 46.0f);
    dl->AddLine(ImVec2(p.x, bottom + 0.5f), ImVec2(p.x + w, bottom + 0.5f),
                ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.08f)));

    for (std::size_t i = 0; i < days.size(); ++i) {
        const DayCount& d = days[i];
        bool  isToday = (i + 1 == days.size());
        float cx = p.x + slot * (static_cast<float>(i) + 0.5f);
        float barTop = bottom - maxOf((bottom - top) * d.count / most, 3.0f);

        // Each bar fades from bright at the top to dark at the bottom.
        ImVec4 hi = isToday ? kAccent : kBtn;
        ImVec4 lo(hi.x * 0.35f, hi.y * 0.35f, hi.z * 0.55f, d.count ? 0.9f : 0.35f);
        if (d.count == 0) hi.w = 0.35f;
        dl->AddRectFilledMultiColor(ImVec2(cx - barW * 0.5f, barTop), ImVec2(cx + barW * 0.5f, bottom),
                                    ImGui::GetColorU32(hi), ImGui::GetColorU32(hi),
                                    ImGui::GetColorU32(lo), ImGui::GetColorU32(lo));
        if (d.count > 0) {
            std::string v = std::to_string(d.count);
            ImVec2 vs = ImGui::CalcTextSize(v.c_str());
            dl->AddText(ImVec2(cx - vs.x * 0.5f, barTop - vs.y - 3.0f),
                        ImGui::GetColorU32(isToday ? kAccent : ImGui::GetStyle().Colors[ImGuiCol_Text]), v.c_str());
        }
        const char* label = isToday ? "Today" : names[d.weekday];
        ImVec2 ls = ImGui::CalcTextSize(label);
        dl->AddText(ImVec2(cx - ls.x * 0.5f, bottom + 6.0f), ImGui::GetColorU32(isToday ? kAccent : kMuted), label);

        if (ImGui::IsMouseHoveringRect(ImVec2(cx - slot * 0.5f, top), ImVec2(cx + slot * 0.5f, bottom + lineH + 8.0f)))
            ImGui::SetTooltip("%s %d: %d task%s completed", names[d.weekday], d.dayOfMonth, d.count, d.count == 1 ? "" : "s");
    }
    ImGui::Dummy(ImVec2(w, height));
}

// The next pending tasks: soonest deadline first, then highest priority.
void upNextList(const AppState& s, int maxRows)
{
    DynArray<const Task*> list;
    for (const Task& t : s.tasks)
        if (t.status != "Completed") list.push_back(&t);
    mergeSort(list, [](const Task* a, const Task* b) {
        long am = 0, bm = 0;
        bool aHas = !a->deadline.empty() && tu::minutesUntilDeadline(a->deadline, am);
        bool bHas = !b->deadline.empty() && tu::minutesUntilDeadline(b->deadline, bm);
        if (aHas != bHas) return aHas;
        if (aHas && am != bm) return am < bm;
        if (a->priority != b->priority) return a->priority > b->priority;
        return a->id < b->id;
    });
    if (list.empty()) {
        ImGui::TextColored(kGood, "Nothing pending. Enjoy the free time.");
        return;
    }

    ImDrawList*  dl    = ImGui::GetWindowDrawList();
    const ImVec4 text  = ImGui::GetStyle().Colors[ImGuiCol_Text];
    float        w     = ImGui::GetContentRegionAvail().x;
    float        lineH = ImGui::GetTextLineHeight();
    float        rowH  = 2.0f * lineH + 16.0f;

    for (std::size_t i = 0; i < list.size() && static_cast<int>(i) < maxRows; ++i) {
        const Task& t = *list[i];
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool hover = ImGui::IsMouseHoveringRect(p, ImVec2(p.x + w, p.y + rowH));
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + rowH),
                          ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, hover ? 0.07f : 0.035f)), 8.0f);
        dl->AddCircleFilled(ImVec2(p.x + 16.0f, p.y + rowH * 0.5f), 5.0f, ImGui::GetColorU32(priorityColor(t.priority)));

        // Due time on the right, coloured by urgency.
        long   mins = 0;
        bool   known = !t.deadline.empty() && tu::minutesUntilDeadline(t.deadline, mins);
        std::string due = !known ? std::string("no deadline")
                        : mins < 0 ? "overdue " + formatDuration(mins) : "in " + formatDuration(mins);
        ImVec4 dueCol = !known ? kMuted : mins < 0 ? kWarn : mins < 24 * 60 ? kSoon : text;
        ImVec2 ds = ImGui::CalcTextSize(due.c_str());
        dl->AddText(ImVec2(p.x + w - ds.x - 12.0f, p.y + 8.0f), ImGui::GetColorU32(dueCol), due.c_str());

        float textX = p.x + 32.0f;
        std::string title  = fitText(t.title, w - 32.0f - ds.x - 28.0f);
        std::string detail = "P" + std::to_string(t.priority) + "  \xC2\xB7  " + std::to_string(t.estimatedTime) + " min" +
                             (t.category.empty() ? "" : "  \xC2\xB7  " + t.category);
        dl->AddText(ImVec2(textX, p.y + 8.0f), ImGui::GetColorU32(text), title.c_str());
        dl->AddText(ImVec2(textX, p.y + 10.0f + lineH), ImGui::GetColorU32(kMuted), fitText(detail, w - 44.0f).c_str());
        ImGui::Dummy(ImVec2(w, rowH));
        if (hover && title != t.title) ImGui::SetTooltip("%s", t.title.c_str());
    }
}

// "Why this task?": the advisor's short reasons, written as sentences.
std::string reasonSentence(const std::string& r)
{
    if (r.rfind("priority ", 0) == 0) return "Priority " + r.substr(9) + " of 5";
    if (r == "mandatory")             return "Marked as a must-do";
    if (r == "suits your energy")     return "Matches your current energy";
    std::string t = r;
    if (!t.empty() && t[0] >= 'a' && t[0] <= 'z') t[0] = static_cast<char>(t[0] - 'a' + 'A');
    return t;
}

// One line of the "Why this task?" list: a green tick, or an amber warning.
void reasonRow(const std::string& text, bool good)
{
    if (gIcons) {
        ImGui::TextColored(good ? kGood : kWarn, "%s", glyph(good ? IconCheck : IconWarning).c_str());
        ImGui::SameLine();
    } else {
        ImGui::TextColored(good ? kGood : kWarn, "%s", good ? "+" : "!");
        ImGui::SameLine();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::Text("%s", text.c_str());
    ImGui::PopTextWrapPos();
}

// The decoration behind the greeting: a glowing eclipse over a dark ridge.
void heroBackdrop(ImVec2 p, ImVec2 q)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float w = q.x - p.x, h = q.y - p.y;
    dl->PushClipRect(p, q, true);

    ImVec2 sun(p.x + w * 0.80f, p.y + h * 0.42f);
    float  r = minOf(h * 0.20f, 30.0f);
    radialGlow(dl, sun, h * 1.3f, ImVec4(0.95f, 0.55f, 0.15f, 1.0f), 0.16f);
    radialGlow(dl, sun, r * 3.2f, kAccent, 0.45f);
    dl->AddCircleFilled(sun, r, IM_COL32(14, 11, 8, 255), 48);
    dl->AddCircle(sun, r, rgba(ImVec4(1.0f, 0.88f, 0.55f, 1.0f), 0.95f), 48, 2.0f);

    // Two layers of hills, the far one lighter, fading out towards the left.
    for (int layer = 0; layer < 2; ++layer) {
        const int n = 24;
        ImVec2 pts[n + 2];
        float base = q.y - h * (layer == 0 ? 0.30f : 0.12f);
        for (int i = 0; i < n; ++i) {
            float x = p.x + w * 0.35f + (w * 0.65f) * i / (n - 1);
            float t = static_cast<float>(i) / (n - 1);
            float y = base - h * (0.10f + 0.10f * std::sin(t * 9.0f + layer * 2.0f) + 0.06f * std::sin(t * 23.0f + layer));
            pts[i] = ImVec2(x, y);
        }
        pts[n]     = ImVec2(q.x, q.y);
        pts[n + 1] = ImVec2(p.x + w * 0.35f, q.y);
        dl->AddConcavePolyFilled(pts, n + 2, layer == 0 ? IM_COL32(24, 20, 18, 200) : IM_COL32(12, 11, 12, 235));
    }
    // Fade the left edge of the hills into the page background.
    ImU32 bg = ImGui::GetColorU32(kBg), clear = rgba(kBg, 0.0f);
    dl->AddRectFilledMultiColor(ImVec2(p.x + w * 0.34f, p.y), ImVec2(p.x + w * 0.55f, q.y), bg, clear, clear, bg);
    // And the top, bottom and right edges, so the glow never ends in a hard line.
    dl->AddRectFilledMultiColor(p, ImVec2(q.x, p.y + h * 0.30f), bg, bg, clear, clear);
    dl->AddRectFilledMultiColor(ImVec2(p.x, q.y - h * 0.25f), q, clear, clear, bg, bg);
    dl->AddRectFilledMultiColor(ImVec2(q.x - 60.0f, p.y), q, clear, bg, bg, clear);
    dl->PopClipRect();
}

void drawHome(Database& db, AppState& s, ImFont* large)
{
    const ImGuiStyle& st = ImGui::GetStyle();
    std::string now = tu::todayString() + " " + tu::formatHM(tu::nowMinutes());

    // ---- The numbers the page uses ----
    int pending = 0, dueSoon = 0, overdue = 0;
    for (const Task& t : s.tasks) {
        if (t.status == "Completed") continue;
        ++pending;
        long m = 0;
        if (!t.deadline.empty() && tu::minutesUntilDeadline(t.deadline, m)) {
            if (m < 0)            ++overdue;
            else if (m < 24 * 60) ++dueSoon;
        }
    }
    std::string busyWith;
    int  freeAt = 0, busyLeft = 0;
    bool busy = currentCommitment(s.commitments, tu::nowMinutes(), tu::todayIndex(),
                                  busyWith, freeAt, busyLeft);
    if (!busy) s.slotMinutes = 0;   // a free slot only means something during a commitment

    int wake    = (s.wakeSaved >= 0) ? s.wakeSaved : 7 * 60;
    int window  = tu::minutesUntil(wake);
    int freeMin = window - busyMinutesInWindow(s.commitments, tu::nowMinutes(), window, tu::todayIndex());
    DynArray<MonthTask> monthTasks = toMonthTasks(s);
    TodayStats today = todayStats(monthTasks, now);

    // The suggestion is always ready: it is worked out again whenever the
    // energy, the free slot or the task list changes.
    if (!s.haveSuggestions) computeSuggestions(db, s);

    // ---- Greeting over the eclipse backdrop ----
    float lineH   = ImGui::GetTextLineHeight();
    float titleH  = gTitleFont ? gTitleFont->FontSize : lineH;
    float bannerH = lineH + titleH + 2.0f * lineH + 24.0f + 3.0f * st.ItemSpacing.y + 30.0f;
    ImVec2 bp = ImGui::GetCursorScreenPos();
    ImVec2 bq(bp.x + ImGui::GetContentRegionAvail().x, bp.y + bannerH);
    heroBackdrop(ImVec2(bp.x - st.WindowPadding.x, bp.y - st.WindowPadding.y),
                 ImVec2(bq.x + st.WindowPadding.x, bq.y));

    int hour = tu::nowMinutes() / 60;
    std::string greeting = std::string(hour < 12 ? "Good morning" : hour < 17 ? "Good afternoon" : "Good evening") +
                           (s.userName.empty() ? std::string("") : ", " + s.userName);
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextColored(ImVec4(0.85f, 0.83f, 0.80f, 1.0f), "%s", greeting.c_str());
    if (gTitleFont) ImGui::PushFont(gTitleFont);
    ImGui::Text("What should you do now?");
    if (gTitleFont) ImGui::PopFont();
    ImGui::TextColored(kMuted, "A smart suggestion based on your tasks, time, energy and deadlines.");
    ImGui::Spacing();

    // Today at a glance, as chips under the title.
    if (busy) {
        infoChip(IconCalendar, "At " + busyWith + " until " + tu::formatHM(freeAt), kWarn, true);
        ImGui::SameLine();
    }
    infoChip(IconTasks, std::to_string(pending) + " pending", ImGui::GetStyle().Colors[ImGuiCol_Text]);
    ImGui::SameLine();
    infoChip(IconClock, today.openToday ? std::to_string(today.openToday) + " due today" : std::string("Nothing due today"),
             today.openToday ? kSoon : kMuted, today.openToday > 0);
    tip("Pending tasks whose deadline is today, or has passed.");
    if (dueSoon) {
        ImGui::SameLine();
        infoChip(IconClock, std::to_string(dueSoon) + " due within 24h", kSoon, true);
    }
    if (overdue) {
        ImGui::SameLine();
        infoChip(IconWarning, std::to_string(overdue) + " overdue", kWarn, true);
    }
    ImGui::SameLine();
    infoChip(IconBed, formatDuration(freeMin) + " free until wake-up", kGood);
    tip("Minutes from now until your wake-up time, minus college or work hours.");
    ImGui::SetCursorScreenPos(ImVec2(bp.x, maxOf(ImGui::GetCursorScreenPos().y, bq.y - 26.0f)));

    // ---- Main row: mood and wake-up on the left, the next move on the right ----
    float leftW  = (ImGui::GetContentRegionAvail().x - st.ItemSpacing.x) * 0.40f;
    float tileH  = lineH * 2.0f + 70.0f;
    float slotH  = busy ? (lineH * 2.0f + ImGui::GetFrameHeight() * 2.0f + 3.0f * st.ItemSpacing.y + 8.0f) : 0.0f;
    float moodH  = 2.0f * st.WindowPadding.y + lineH * 3.0f + st.ItemSpacing.y * 4.0f + tileH + slotH + 4.0f;
    float wakeH  = 2.0f * st.WindowPadding.y + lineH + st.ItemSpacing.y * 2.0f + ImGui::GetFrameHeight() + 10.0f;
    // The next-move card needs room for its title, badges, reasons and buttons.
    int   reasonLines = 1;   // "Fits in your available time"
    if (!s.advice.suggestions.empty())
        reasonLines += static_cast<int>(s.advice.suggestions[clampTo(s.pick, 0, static_cast<int>(s.advice.suggestions.size()) - 1)].reasons.size());
    float heroNeed = 2.0f * st.WindowPadding.y + titleH + lineH * (5.0f + reasonLines) +
                     st.ItemSpacing.y * (9.0f + reasonLines) + ImGui::GetFrameHeight() + 14.0f + 24.0f;
    if (s.focusActive) heroNeed = maxOf(heroNeed, 2.0f * st.WindowPadding.y + titleH * 2.0f + lineH * 6.0f + 10.0f * st.ItemSpacing.y + 30.0f);
    float rowH   = maxOf(maxOf(moodH + st.ItemSpacing.y + wakeH, 330.0f), heroNeed);
    moodH        = rowH - st.ItemSpacing.y - wakeH;
    // Spare height goes to the mood tiles (bigger faces), up to a point.
    float spare  = moodH - (2.0f * st.WindowPadding.y + lineH * 3.0f + st.ItemSpacing.y * 4.0f + tileH + slotH + 4.0f);
    tileH       += clampTo(spare, 0.0f, tileH * 0.6f);

    ImGui::BeginGroup();
    beginCard("mood", ImVec2(leftW, moodH), CardPlain, ImGuiWindowFlags_NoScrollbar);
    {
        cardHeading(IconBolt, "How are you feeling?");
        s.energy = clampTo(s.energy, 1, 5);
        float gap = 8.0f;
        float tw  = (ImGui::GetContentRegionAvail().x - 4.0f * gap) / 5.0f;
        for (int lv = 1; lv <= 5; ++lv) {
            if (lv > 1) ImGui::SameLine(0, gap);
            if (moodTile(lv, s.energy == lv, tw, tileH) && s.energy != lv) {
                s.energy          = lv;
                s.haveSuggestions = false;
            }
        }
        tip("How much energy you have right now. Tasks that need more are moved down.");

        static const char* hints[6] = {"",
            "Running on empty: light, short tasks come first.",
            "Low energy: easier tasks are moved up.",
            "Okay: most tasks are fine, the hardest ones wait.",
            "Energetic: a good time for demanding work.",
            "Focused: hard, important tasks are moved up."};
        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kMuted, "%s", hints[s.energy]);
        ImGui::PopTextWrapPos();

        if (busy) {
            ImGui::Spacing();
            ImGui::TextColored(kMuted, "%s", withIcon(IconCoffee, ("Got a free slot during " + busyWith + "?").c_str()).c_str());
            static const int choices[5] = {15, 30, 45, 60, 90};
            for (int i = 0; i < 5; ++i) {
                char lbl[16];
                std::snprintf(lbl, sizeof(lbl), "%d min", choices[i]);
                if (i && ImGui::GetContentRegionAvail().x > buttonWidth(lbl) + st.ItemSpacing.x) ImGui::SameLine();
                bool on = (s.slotMinutes == choices[i]);
                if (on ? primaryButton(lbl) : ghostButton(lbl)) {
                    s.slotMinutes     = on ? 0 : choices[i];
                    s.haveSuggestions = false;
                }
            }
            if (ImGui::Checkbox(withIcon(IconMarker, "I'm okay going out").c_str(), &s.slotOutside))
                s.haveSuggestions = false;
        }
    }
    ImGui::EndChild();

    beginCard("wake", ImVec2(leftW, wakeH), CardPlain, ImGuiWindowFlags_NoScrollbar);
    {
        cardHeading(IconBed, "Wake-up time");
        std::string saveLbl = withIcon(IconSave, "Save") + "##wakesave_home";
        float btnW = buttonWidth(saveLbl);
        ImGui::SetNextItemWidth(maxOf(90.0f, ImGui::GetContentRegionAvail().x * 0.45f));
        ImGui::InputTextWithHint("##wake_home", "HH:MM", &s.wakeText);
        ImGui::SameLine();
        if (ghostButton(saveLbl.c_str(), ImVec2(btnW, 0))) {
            int m = tu::parseHM(s.wakeText);
            if (m < 0) {
                s.wakeMessage = "Use HH:MM, e.g. 06:30";
            } else {
                s.wakeText = tu::formatHM(m);
                db.setSetting("wake_time", s.wakeText);
                s.wakeSaved       = m;
                s.wakeMessage     = "Saved.";
                s.haveSuggestions = false;
                s.havePlan        = false;
            }
        }
        ImGui::SameLine();
        if (!s.wakeMessage.empty())  ImGui::TextColored(kMuted, "%s", s.wakeMessage.c_str());
        else if (s.wakeSaved < 0)    ImGui::TextColored(kWarn, "Not saved: 07:00 assumed");
    }
    ImGui::EndChild();
    ImGui::EndGroup();

    ImGui::SameLine();

    // Buttons only record what was clicked; the action runs after the card is
    // drawn, because marking a task done reloads the lists the card shows.
    int         doneId = 0;
    std::string doneTitle;
    bool        startPick = false;

    beginCard("hero", ImVec2(0, rowH), CardHero, ImGuiWindowFlags_NoScrollbar);
    ImGui::Indent(6);
    if (s.focusActive) {
        drawFocus(db, s, large);
    } else {
        auto hiddenNotes = [&s]() {
            if (s.advice.hiddenOutside > 0)
                ImGui::TextColored(kMuted, "%d task(s) hidden: they need going out", s.advice.hiddenOutside);
            if (s.advice.hiddenTooLong > 0)
                ImGui::TextColored(kMuted, "%d task(s) hidden: too long for this slot", s.advice.hiddenTooLong);
        };
        auto bigLine = [](const char* text, const ImVec4& col) {
            if (gTitleFont) ImGui::PushFont(gTitleFont);
            ImGui::TextColored(col, "%s", text);
            if (gTitleFont) ImGui::PopFont();
        };

        if (s.advice.status == AdviceStatus::QuietBusy) {
            ImGui::TextColored(kAccent, "%s", withIcon(IconHourglass, "QUIET MODE").c_str());
            ImGui::Spacing();
            bigLine(("At " + busyWith).c_str(), ImGui::GetStyle().Colors[ImGuiCol_Text]);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kMuted, "Nothing is suggested until %s. If you have a free slot, pick how long "
                                       "on the left and a task that fits will appear here.",
                               tu::formatHM(freeAt).c_str());
            ImGui::PopTextWrapPos();
        } else if (s.advice.status == AdviceStatus::NothingFits || s.advice.suggestions.empty()) {
            ImGui::TextColored(kAccent, "%s", withIcon(IconHourglass, "YOUR NEXT MOVE").c_str());
            ImGui::Spacing();
            if (s.adviceSlot > 0) {
                bigLine(("Nothing fits in " + std::to_string(s.adviceSlot) + " min").c_str(), kSoon);
                ImGui::TextColored(kMuted, "Try a longer slot, or allow going out.");
            } else {
                bigLine("You're all clear", kGood);
                ImGui::TextColored(kMuted, "Nothing pending right now. Add a task, or enjoy the free time.");
            }
            hiddenNotes();
        } else {
            int count = static_cast<int>(s.advice.suggestions.size());
            s.pick = clampTo(s.pick, 0, count - 1);
            const Suggestion& sg = s.advice.suggestions[s.pick];
            const Task* task = nullptr;
            for (const Task& t : s.tasks) if (t.id == sg.id) task = &t;

            // Header line, with the badge on the right.
            float x0    = ImGui::GetCursorPosX(), y0 = ImGui::GetCursorPosY();
            float right = x0 + ImGui::GetContentRegionAvail().x - 6.0f;
            ImGui::TextColored(kAccent, "%s", withIcon(IconHourglass,
                s.adviceSlot > 0 ? "FOR YOUR FREE SLOT" : "YOUR NEXT MOVE").c_str());
            float y1 = ImGui::GetCursorPosY();
            unsigned    bIcon = s.pick == 0 ? static_cast<unsigned>(IconStar) : 0u;
            std::string bText = s.pick == 0 ? std::string("Recommended")
                              : "Option " + std::to_string(s.pick + 1) + " of " + std::to_string(count);
            ImGui::SetCursorPos(ImVec2(right - chipWidth(bIcon, bText), y0 - 6.0f));
            infoChip(bIcon, bText, kAccent, true);
            ImGui::SetCursorPos(ImVec2(x0, y1));
            ImGui::Spacing();

            // Title.
            if (gTitleFont) ImGui::PushFont(gTitleFont);
            ImGui::Text("%s", fitText(sg.title, ImGui::GetContentRegionAvail().x - 6.0f).c_str());
            if (gTitleFont) ImGui::PopFont();
            if (ImGui::IsItemHovered() && fitText(sg.title, 1e9f) != sg.title) ImGui::SetTooltip("%s", sg.title.c_str());

            // Badges: priority, category, must-do.
            pill(("P" + std::to_string(sg.priority) + " Priority").c_str(), priorityColor(sg.priority));
            if (task && !task->category.empty()) {
                ImGui::SameLine(0, 22);
                pill(task->category.c_str(), kInfo);
            }
            if (task && task->isMandatory) {
                ImGui::SameLine(0, 22);
                pill("Must", kGood);
            }
            ImGui::Spacing();

            // Time, effort and energy fit.
            int diff = task ? task->difficulty : 3;
            const char* effort = diff <= 2 ? "Low effort" : diff == 3 ? "Medium effort" : "High effort";
            bool fitsEnergy = !task || task->energyRequired <= s.energy;
            ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_Text], "%s",
                withIcon(IconClock, (std::to_string(sg.estimatedTime) + " min").c_str()).c_str());
            ImGui::SameLine(0, 26);
            ImGui::Text("%s", withIcon(IconSignal, effort).c_str());
            ImGui::SameLine(0, 26);
            ImGui::TextColored(fitsEnergy ? kAccent : kWarn, "%s",
                withIcon(IconBolt, fitsEnergy ? "Fits your energy" : "Needs more energy").c_str());

            // A faint amber rule.
            ImGui::Spacing();
            {
                ImVec2 a = ImGui::GetCursorScreenPos();
                float  w = ImGui::GetContentRegionAvail().x - 6.0f;
                ImU32 on = rgba(kAccent, 0.35f), off = rgba(kAccent, 0.0f);
                ImGui::GetWindowDrawList()->AddRectFilledMultiColor(a, ImVec2(a.x + w, a.y + 1.0f), on, off, off, on);
                ImGui::Dummy(ImVec2(w, 1.0f));
            }
            ImGui::Spacing();

            // Why this task?
            ImGui::TextColored(kAccent, "%s", gIcons ? glyph(IconCircleQ).c_str() : "?");
            ImGui::SameLine();
            ImGui::Text("Why this task?");
            for (const std::string& r : sg.reasons) {
                bool warning = r.rfind("needs energy", 0) == 0;
                reasonRow(reasonSentence(r), !warning);
            }
            if (sg.estimatedTime <= freeMin)
                reasonRow("Fits in your available time (" + formatDuration(freeMin) + " left)", true);
            else
                reasonRow("Longer than the time left today (" + formatDuration(freeMin) + ")", false);
            hiddenNotes();

            // Buttons, pinned to the bottom of the card.
            float btnH = ImGui::GetFrameHeight() + 14.0f;
            float y    = ImGui::GetWindowHeight() - st.WindowPadding.y - btnH;
            if (ImGui::GetCursorPosY() < y) ImGui::SetCursorPosY(y);
            float avail   = ImGui::GetContentRegionAvail().x - 6.0f;
            std::string doneLbl = iconOr(IconCheck, "Done") + "##done_pick";
            float doneW   = btnH;
            float startW  = (avail - doneW - 2.0f * st.ItemSpacing.x) * 0.56f;
            float otherW  = avail - doneW - 2.0f * st.ItemSpacing.x - startW;
            if (primaryButton(withIcon(IconPlay, "Start Task").c_str(), ImVec2(startW, btnH))) startPick = true;
            tip("Starts a focus timer for the suggested minutes.");
            ImGui::SameLine();
            ImGui::BeginDisabled(count < 2);
            if (ghostButton(withIcon(IconShuffle, "Choose Another").c_str(), ImVec2(otherW, btnH)))
                s.pick = (s.pick + 1) % count;
            ImGui::EndDisabled();
            if (count < 2) tip("No other task fits right now.");
            ImGui::SameLine();
            if (ghostButton(doneLbl.c_str(), ImVec2(doneW, btnH))) { doneId = sg.id; doneTitle = sg.title; }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mark \"%s\" done", sg.title.c_str());
        }
    }
    ImGui::Unindent(6);
    ImGui::EndChild();

    if (startPick && s.pick < static_cast<int>(s.advice.suggestions.size()))
        startFocus(s, s.advice.suggestions[s.pick]);
    if (doneId) markDoneFromNow(db, s, doneId, doneTitle);

    // ---- Bottom row: today, the last 7 days, and what's next ----
    ImGui::Spacing();
    float bottomH = maxOf(260.0f, ImGui::GetContentRegionAvail().y);
    float third   = (ImGui::GetContentRegionAvail().x - 2.0f * st.ItemSpacing.x) / 3.0f;

    beginCard("today_panel", ImVec2(third, bottomH));
    {
        cardHeading(IconFire, "Today");
        int   todayTotal = today.doneToday + today.openToday;
        float todayFrac  = todayTotal > 0 ? static_cast<float>(today.doneToday) / todayTotal : 0.0f;
        char  ringLabel[24];
        std::snprintf(ringLabel, sizeof(ringLabel), "%d/%d", today.doneToday, todayTotal);
        ImGui::BeginGroup();
        progressRing(todayFrac, 30.0f, ringLabel, todayFrac >= 1.0f ? kGood : kAccent);
        ImGui::EndGroup();
        tip("Tasks finished today, out of those finished today plus those due today or overdue.");
        ImGui::SameLine(0, 16);
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(0, 4));
        if (todayTotal == 0)            ImGui::Text("Nothing due today");
        else if (today.openToday == 0)  ImGui::TextColored(kGood, "All of today's work is done");
        else                            ImGui::Text("%d done, %d still due", today.doneToday, today.openToday);
        if (today.streak > 0)
            ImGui::TextColored(kAccent, "%s", withIcon(IconFire, (std::to_string(today.streak) + "-day streak").c_str()).c_str());
        else
            ImGui::TextColored(kMuted, "Finish a task to start a streak");
        ImGui::EndGroup();
        tip("Streak: days in a row with at least one task marked Done.");
        ImGui::Spacing();
        DynArray<Insight> insights = buildInsights(monthTasks, now, freeMin);
        for (const Insight& in : insights) insightTile(in);
    }
    ImGui::EndChild();

    ImGui::SameLine();
    beginCard("week_panel", ImVec2(third, bottomH));
    {
        DynArray<DayCount> days = lastDaysCompleted(monthTasks, now, 7);
        int total = 0;
        for (const DayCount& d : days) total += d.count;
        cardHeading(IconChart, "Last 7 days");
        ImGui::TextColored(kMuted, "%d task%s completed", total, total == 1 ? "" : "s");
        weekChart(days, maxOf(120.0f, ImGui::GetContentRegionAvail().y));
    }
    ImGui::EndChild();

    ImGui::SameLine();
    beginCard("upnext_panel", ImVec2(0, bottomH));
    {
        cardHeading(IconTasks, "Up next");
        upNextList(s, 8);
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------
// Task dialogs
// ---------------------------------------------------------------
void drawTaskDialog(Database& db, AppState& s)
{
    if (!ImGui::BeginPopupModal("Task", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    TaskForm& f = s.form;
    const float lw = 170.0f;   // where the fields start, after the labels

    ImGui::TextColored(kAccent, "%s", f.editingId ? withIcon(IconPencil, "Edit task").c_str()
                                                  : withIcon(IconPlus, "New task").c_str());
    ImGui::Separator();
    ImGui::Spacing();

    fieldLabel("Title", lw);
    ImGui::SetNextItemWidth(380); ImGui::InputText("##title", &f.title);
    fieldLabel("Description", lw);
    ImGui::SetNextItemWidth(380); ImGui::InputText("##desc", &f.description);
    fieldLabel("Category", lw);
    ImGui::SetNextItemWidth(380); ImGui::InputTextWithHint("##cat", "Study, Health, Errands...", &f.category);

    fieldLabel("Priority", lw);
    ImGui::SetNextItemWidth(220);
    ImGui::SliderInt("##pri", &f.priority, 1, 5, "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SameLine(); ImGui::TextColored(kMuted, "5 = most important");
    fieldLabel("Difficulty", lw);
    ImGui::SetNextItemWidth(220);
    ImGui::SliderInt("##diff", &f.difficulty, 1, 5, "%d", ImGuiSliderFlags_AlwaysClamp);
    fieldLabel("Energy needed", lw);
    ImGui::SetNextItemWidth(220);
    ImGui::SliderInt("##energy", &f.energy, 1, 5, "%d", ImGuiSliderFlags_AlwaysClamp);

    fieldLabel("Ideal minutes", lw);
    ImGui::SetNextItemWidth(220);
    ImGui::InputInt("##est", &f.estimated, 5, 30);
    f.estimated = clampTo(f.estimated, 1, 1440);
    fieldLabel("Minimum minutes", lw);
    ImGui::SetNextItemWidth(220);
    ImGui::InputInt("##min", &f.minimum, 5, 30);
    f.minimum = clampTo(f.minimum, 1, f.estimated);   // a floor can't exceed the ideal

    fieldLabel("Deadline", lw);
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##deadline", "2026-10-20 18:00", &f.deadline);

    fieldLabel("", lw);
    ImGui::Checkbox("Mandatory (never dropped)", &f.mandatory);
    fieldLabel("", lw);
    ImGui::Checkbox(withIcon(IconMarker, "Needs me to go out (gym, shopping...)").c_str(), &f.needsOutside);

    if (!f.error.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(kWarn, "%s", f.error.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (primaryButton(withIcon(IconSave, "Save").c_str(), ImVec2(140, 0))) {
        f.error.clear();
        if (f.title.empty())
            f.error = "Title cannot be empty.";
        else if (!f.deadline.empty() && !tu::isValidDeadline(f.deadline))
            f.error = "Deadline must look like 2026-10-20 18:00 or 2026-10-20.";

        if (f.error.empty()) {
            Task t  = taskFromForm(f);
            bool ok = f.editingId ? db.updateTask(t) : db.addTask(t);
            if (ok) {
                s.status = (f.editingId ? "Updated: " : "Added: ") + t.title;
                reloadAll(db, s);
                ImGui::CloseCurrentPopup();
            } else {
                f.error = "Could not save. Details are in the terminal.";
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(140, 0))) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void drawDeleteDialog(Database& db, AppState& s)
{
    if (!ImGui::BeginPopupModal("Delete task?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::Text("Delete \"%s\" permanently?", s.deleteTitle.c_str());
    ImGui::TextColored(kMuted, "Its saved plans are removed too.");
    ImGui::Spacing();

    if (dangerButton(withIcon(IconTrash, "Delete").c_str(), ImVec2(140, 0))) {
        if (db.deleteTask(s.deleteId)) s.status = "Deleted: " + s.deleteTitle;
        reloadAll(db, s);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(140, 0))) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

// ---------------------------------------------------------------
// Tasks tab, card view: one card per task, most important first
// ---------------------------------------------------------------
// Whether a task passes the filter pills and the search box.
bool taskVisible(const AppState& s, const Task& t)
{
    bool done = (t.status == "Completed");
    if (s.taskFilter == 1 && done)  return false;
    if (s.taskFilter == 2 && !done) return false;
    if (!s.search.empty()) {
        std::string q = lowerCase(s.search);
        if (!contains(lowerCase(t.title), q.c_str()) && !contains(lowerCase(t.category), q.c_str())) return false;
    }
    return true;
}

// A small round icon button with no frame (edit, delete, done on a card).
bool iconButton(const char* id, unsigned cp, const char* fallback, const ImVec4& col, float size, const char* tooltip)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, ImVec2(size, size));
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c(p.x + size * 0.5f, p.y + size * 0.5f);
    dl->AddCircleFilled(c, size * 0.5f, rgba(hovered ? col : ImVec4(1, 1, 1, 1), hovered ? 0.18f : 0.06f), 24);
    std::string g = gIcons ? glyph(cp) : std::string(fallback);
    ImVec2 gs = ImGui::CalcTextSize(g.c_str());
    dl->AddText(ImVec2(c.x - gs.x * 0.5f, c.y - gs.y * 0.5f), ImGui::GetColorU32(hovered ? col : kMuted), g.c_str());
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("%s", tooltip);
    }
    return pressed;
}

void drawTaskCards(Database& db, AppState& s, bool& changed)
{
    // Order: pending before completed, then higher priority, then the
    // soonest deadline (tasks with a deadline before those without).
    DynArray<const Task*> order;
    for (const Task& t : s.tasks)
        if (taskVisible(s, t)) order.push_back(&t);
    mergeSort(order, [](const Task* a, const Task* b) {
        bool aDone = a->status == "Completed", bDone = b->status == "Completed";
        if (aDone != bDone) return !aDone;
        if (a->priority != b->priority) return a->priority > b->priority;
        long am = 0, bm = 0;
        bool aHas = !a->deadline.empty() && tu::minutesUntilDeadline(a->deadline, am);
        bool bHas = !b->deadline.empty() && tu::minutesUntilDeadline(b->deadline, bm);
        if (aHas != bHas) return aHas;
        if (aHas && am != bm) return am < bm;
        return a->id < b->id;
    });

    const ImGuiStyle& st = ImGui::GetStyle();
    const float gap   = 14.0f;
    const float lineH = ImGui::GetTextLineHeight();
    const float tile  = lineH + 18.0f;
    const float minWidth   = maxOf(320.0f, ImGui::CalcTextSize("000 min    overdue 00d 00h").x + 2.0f * st.WindowPadding.x + 80.0f);
    const float cardHeight = 2.0f * st.WindowPadding.y + tile + 10.0f + 3.0f * (lineH + st.ItemSpacing.y) + lineH + 8.0f;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("cards_area", ImVec2(0, 0));
    ImGui::PopStyleColor();
    float avail     = ImGui::GetContentRegionAvail().x;
    int   columns   = maxOf(1, static_cast<int>((avail + gap) / (minWidth + gap)));
    float cardWidth = (avail - gap * (columns - 1)) / columns;

    for (std::size_t i = 0; i < order.size(); ++i) {
        const Task& t = *order[i];
        bool done = (t.status == "Completed");
        if (i % columns != 0) ImGui::SameLine(0, gap);
        else if (i > 0)       ImGui::Dummy(ImVec2(0, gap - st.ItemSpacing.y));

        ImGui::PushID(t.id);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Border,  ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("card", ImVec2(cardWidth, cardHeight), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleColor(2);

        // The card: tinted in the priority colour, with a glowing edge on hover.
        ImVec2 p  = ImGui::GetWindowPos();
        ImVec2 sz = ImGui::GetWindowSize();
        ImVec2 q(p.x + sz.x, p.y + sz.y);
        bool   hover = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        ImVec4 pc    = done ? kMuted : priorityColor(t.priority);
        ImVec4 cc    = done ? kMuted : categoryColor(t.category, t.title);
        float  r     = st.ChildRounding;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(p, q, false);
        gradientRect(dl, p, q, ImVec4(pc.x * 0.16f + 0.03f, pc.y * 0.16f + 0.03f, pc.z * 0.16f + 0.03f, 1.0f),
                     kPanel, r, p, ImVec2(p.x + sz.x * 0.6f, q.y));
        if (hover) radialGlow(dl, ImVec2(p.x + sz.x * 0.5f, q.y), sz.x * 0.6f, pc, 0.10f);
        dl->AddRect(p, q, rgba(pc, hover ? 0.95f : (done ? 0.25f : 0.55f)), r, 0, hover ? 1.6f : 1.2f);
        dl->PopClipRect();

        // Top row: the category icon, and the actions on the right.
        ImVec2 tp = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(tp, ImVec2(tp.x + tile, tp.y + tile), rgba(cc, 0.14f), 10.0f);
        dl->AddRect(tp, ImVec2(tp.x + tile, tp.y + tile), rgba(cc, 0.35f), 10.0f);
        if (gIcons) {
            std::string g = glyph(categoryIcon(t.category, t.title));
            ImVec2 gs = ImGui::CalcTextSize(g.c_str());
            dl->AddText(ImVec2(tp.x + (tile - gs.x) * 0.5f, tp.y + (tile - gs.y) * 0.5f), ImGui::GetColorU32(cc), g.c_str());
        }

        float btn   = tile - 4.0f;
        float right = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
        float y0    = ImGui::GetCursorPosY();
        int   nBtns = hover ? 3 : 1;
        ImGui::SetCursorPos(ImVec2(right - nBtns * btn - (nBtns - 1) * 6.0f, y0 + 2.0f));
        if (hover) {
            if (iconButton("##edit", IconPencil, "E", kAccent, btn, "Edit")) {
                s.form           = formFromTask(t);
                s.openTaskDialog = true;
            }
            ImGui::SameLine(0, 6);
            if (iconButton("##delete", IconTrash, "X", kDanger, btn, "Delete")) {
                s.deleteId         = t.id;
                s.deleteTitle      = t.title;
                s.openDeleteDialog = true;
            }
            ImGui::SameLine(0, 6);
        }
        if (done) {
            ImVec2 bp = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(btn, btn));
            dl->AddCircleFilled(ImVec2(bp.x + btn * 0.5f, bp.y + btn * 0.5f), btn * 0.5f, rgba(kGood, 0.85f), 24);
            std::string g = iconOr(IconCheck, "v");
            ImVec2 gs = ImGui::CalcTextSize(g.c_str());
            dl->AddText(ImVec2(bp.x + (btn - gs.x) * 0.5f, bp.y + (btn - gs.y) * 0.5f), ImGui::GetColorU32(kBtnText), g.c_str());
            tip(("Done " + t.completedAt).c_str());
        } else if (iconButton("##done", IconCheck, "v", kGood, btn, "Mark done")) {
            if (db.markComplete(t.id)) s.status = "Completed: " + t.title;
            changed = true;
        }
        ImGui::SetCursorPosY(y0 + tile + 10.0f);

        // Title and category.
        float textW = ImGui::GetContentRegionAvail().x;
        std::string title = fitText(t.title, textW);
        if (done) ImGui::TextDisabled("%s", title.c_str());
        else      ImGui::Text("%s", title.c_str());
        if (title != t.title) tip(t.title.c_str());
        ImGui::TextColored(kMuted, "%s", fitText(t.category.empty() ? std::string("No category") : t.category, textW).c_str());

        // Badges.
        pill(("P" + std::to_string(t.priority)).c_str(), pc);
        if (t.isMandatory)  { ImGui::SameLine(0, 20); pill("Must", kInfo); }
        if (t.needsOutside) { ImGui::SameLine(0, 20); pill("Out", kSoon); }
        if (done)           { ImGui::SameLine(0, 20); pill("Done", kGood); }

        // Time on the left, the deadline on the right.
        std::string timeText = withIcon(IconClock, (std::to_string(t.estimatedTime) + " min").c_str());
        ImGui::Text("%s", timeText.c_str());
        tip(("At least " + std::to_string(t.minTime) + " min if the day is tight.").c_str());
        long mins  = 0;
        bool known = !t.deadline.empty() && tu::minutesUntilDeadline(t.deadline, mins);
        std::string due; ImVec4 dueCol = kMuted;
        if (t.deadline.empty())    due = "no deadline";
        else if (done || !known)   due = t.deadline;
        else if (mins < 0)       { due = "overdue " + formatDuration(mins); dueCol = kWarn; }
        else                     { due = "in " + formatDuration(mins); dueCol = mins < 24 * 60 ? kSoon : kMuted; }
        due = withIcon(IconCalendar, due.c_str());
        ImGui::SameLine(right - ImGui::CalcTextSize(due.c_str()).x);
        ImGui::TextColored(dueCol, "%s", due.c_str());
        if (!t.deadline.empty()) tip(t.deadline.c_str());

        ImGui::EndChild();
        ImGui::PopID();
    }
    ImGui::EndChild();
}

// A rounded toggle, filled amber when it is the active choice.
bool segmentButton(const char* label, bool active)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16, ImGui::GetStyle().FramePadding.y + 2));
    bool pressed = active ? primaryButton(label) : ghostButton(label);
    ImGui::PopStyleVar(2);
    return pressed;
}

float segmentWidth(const std::string& label)
{
    return ImGui::CalcTextSize(label.c_str(), nullptr, true).x + 32.0f;
}

// ---------------------------------------------------------------
// "Tasks" tab
// ---------------------------------------------------------------
void drawTasksTab(Database& db, AppState& s, ImFont* large)
{
    const ImGuiStyle& st = ImGui::GetStyle();
    int all = 0, pending = 0, completed = 0, wanted = 0;
    for (const Task& t : s.tasks) {
        ++all;
        if (t.status == "Completed") { ++completed; continue; }
        ++pending;
        wanted += t.estimatedTime;
    }

    // ---- Heading, with search and the view toggle on the right ----
    HeaderPos h = pageHeader(large, "Task Manager", "Add, track and manage all your tasks in one place.");
    float searchW = 280.0f, iconW = ImGui::GetFrameHeight() + 8.0f;
    headerRightBegin(h, searchW + 2.0f * (iconW + st.ItemSpacing.x));
    ImGui::SetNextItemWidth(searchW);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
    ImGui::InputTextWithHint("##search", (iconOr(IconSearch, "") + "  Search tasks...").c_str(), &s.search);
    ImGui::PopStyleVar();
    ImGui::SameLine();
    if ((s.listView ? ghostButton : primaryButton)((iconOr(IconGrid, "Cards") + "##cards").c_str(), ImVec2(iconW, 0)))
        s.listView = false;
    tip("Cards");
    ImGui::SameLine();
    if ((s.listView ? primaryButton : ghostButton)((iconOr(IconList, "List") + "##list").c_str(), ImVec2(iconW, 0)))
        s.listView = true;
    tip("List");
    headerRightEnd(h);

    // ---- Filter pills on the left, Add Task on the right ----
    std::string lAll  = "All (" + std::to_string(all) + ")";
    std::string lPend = "Pending (" + std::to_string(pending) + ")";
    std::string lDone = "Completed (" + std::to_string(completed) + ")";
    float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    if (segmentButton((lAll  + "##fall").c_str(),  s.taskFilter == 0)) s.taskFilter = 0;
    ImGui::SameLine();
    if (segmentButton((lPend + "##fpend").c_str(), s.taskFilter == 1)) s.taskFilter = 1;
    ImGui::SameLine();
    if (segmentButton((lDone + "##fdone").c_str(), s.taskFilter == 2)) s.taskFilter = 2;
    ImGui::SameLine(0, 18);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kMuted, "%s of work pending", formatDuration(wanted).c_str());
    std::string add = withIcon(IconPlus, "Add Task");
    ImGui::SameLine(rowRight - segmentWidth(add));
    if (segmentButton(add.c_str(), true)) {
        s.form           = TaskForm();
        s.openTaskDialog = true;
    }
    ImGui::Spacing();

    int shown = 0;
    for (const Task& t : s.tasks) if (taskVisible(s, t)) ++shown;
    if (shown == 0)
        ImGui::TextColored(kMuted, "%s", s.tasks.empty()   ? "No tasks yet. Press Add Task to create your first one."
                                       : !s.search.empty() ? "No task matches your search."
                                       : s.taskFilter == 2 ? "Nothing completed yet."
                                                           : "Every task is done. Nice work.");

    // Changing the list while looping over it would break the loop, so
    // changes are flagged here and the list is reloaded afterwards.
    bool changed = false;

    if (!s.listView) {
        drawTaskCards(db, s, changed);
    } else {
    // ---- List view: the table, inside a card ----
    beginCard("tasks_table_card", ImVec2(0, 0));
    ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("tasks", 7, flags, ImVec2(0, 0))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Task",     ImGuiTableColumnFlags_WidthStretch);
        // Column widths measured from the font, so nothing is cut off.
        const float pillPad = 16.0f;   // pill() adds 8 px on each side
        ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, columnFor({"Priority"}));
        ImGui::TableSetupColumn("Time",     ImGuiTableColumnFlags_WidthFixed, columnFor({"Time", "450 min  (min 420)"}));
        ImGui::TableSetupColumn("Tags",     ImGuiTableColumnFlags_WidthFixed,
            maxOf(columnFor({"Tags"}), columnFor({"Must"}) + ImGui::CalcTextSize("Out").x + 2 * pillPad + 18.0f));
        ImGui::TableSetupColumn("Deadline", ImGuiTableColumnFlags_WidthFixed, columnFor({"Deadline", "overdue 99d 23h"}));
        ImGui::TableSetupColumn("Status",   ImGuiTableColumnFlags_WidthFixed,
            maxOf(columnFor({"Status"}), columnFor({"pending"}) + pillPad));
        ImGui::TableSetupColumn("",         ImGuiTableColumnFlags_WidthFixed,
            buttonRowWidth({iconOr(IconPencil, "Edit"), iconOr(IconCheck, "Done"), iconOr(IconTrash, "Delete")})
            + 2.0f * ImGui::GetStyle().CellPadding.x);
        ImGui::TableHeadersRow();

        for (const Task& t : s.tasks) {
            if (!taskVisible(s, t)) continue;
            bool done = (t.status == "Completed");

            ImGui::PushID(t.id);
            ImGui::TableNextRow();

            // Task name, with its category underneath in a quieter colour.
            ImGui::TableNextColumn();
            if (gIcons) {
                ImGui::TextColored(done ? kMuted : categoryColor(t.category, t.title), "%s",
                                   glyph(categoryIcon(t.category, t.title)).c_str());
                ImGui::SameLine();
            }
            if (done) ImGui::TextDisabled("%s", t.title.c_str());
            else      ImGui::Text("%s", t.title.c_str());
            if (!t.category.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("  %s", t.category.c_str());
            }

            ImGui::TableNextColumn();
            pill(("P" + std::to_string(t.priority)).c_str(), priorityColor(t.priority));

            ImGui::TableNextColumn();
            ImGui::Text("%d min", t.estimatedTime);
            ImGui::SameLine();
            ImGui::TextDisabled("(min %d)", t.minTime);

            ImGui::TableNextColumn();
            if (t.isMandatory) pill("Must", kInfo);
            if (t.isMandatory && t.needsOutside) ImGui::SameLine(0, 18);
            if (t.needsOutside) pill("Out", kSoon);

            // Deadline as a distance in time: "in 2d 4h" or "overdue 3h".
            ImGui::TableNextColumn();
            long mins  = 0;
            bool known = !t.deadline.empty() && tu::minutesUntilDeadline(t.deadline, mins);
            if (t.deadline.empty())
                ImGui::TextDisabled("-");
            else if (done || !known)
                ImGui::TextDisabled("%s", t.deadline.c_str());
            else if (mins < 0)
                ImGui::TextColored(kWarn, "overdue %s", formatDuration(mins).c_str());
            else if (mins < 24 * 60)
                ImGui::TextColored(kSoon, "in %s", formatDuration(mins).c_str());
            else
                ImGui::Text("in %s", formatDuration(mins).c_str());
            if (!t.deadline.empty()) tip(t.deadline.c_str());

            ImGui::TableNextColumn();
            if (done) pill("done", kGood);
            else      pill("pending", kMuted);

            // Actions as icon buttons, with tooltips.
            ImGui::TableNextColumn();
            if (ImGui::SmallButton((iconOr(IconPencil, "Edit") + "##edit").c_str())) {
                s.form           = formFromTask(t);
                s.openTaskDialog = true;
            }
            tip("Edit");
            if (!done) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, kGood);
                if (ImGui::SmallButton((iconOr(IconCheck, "Done") + "##done").c_str())) {
                    if (db.markComplete(t.id)) s.status = "Completed: " + t.title;
                    changed = true;
                }
                ImGui::PopStyleColor();
                tip("Mark done");
            }
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, kDanger);
            if (ImGui::SmallButton((iconOr(IconTrash, "Delete") + "##delete").c_str())) {
                s.deleteId         = t.id;
                s.deleteTitle      = t.title;
                s.openDeleteDialog = true;
            }
            ImGui::PopStyleColor();
            tip("Delete");

            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    }   // end of list view

    if (changed) reloadAll(db, s);

    // Popups are opened here, outside the table, so the name used to open
    // them matches the name used to draw them.
    if (s.openTaskDialog)   { ImGui::OpenPopup("Task");         s.openTaskDialog   = false; }
    drawTaskDialog(db, s);
    if (s.openDeleteDialog) { ImGui::OpenPopup("Delete task?"); s.openDeleteDialog = false; }
    drawDeleteDialog(db, s);
}

// ---------------------------------------------------------------
// "Plan my day" tab
// ---------------------------------------------------------------
// One entry on the Plan My Day timeline: a task, or a fixed commitment.
struct TimelineRow {
    int         start = 0;          // minutes from today's midnight (may pass 24:00)
    int         minutes = 0;
    int         requested = 0;      // what the task asked for (0 for commitments)
    std::string title, category;
    bool        commitment = false;
};

const Task* findTask(const AppState& s, int id)
{
    for (const Task& t : s.tasks) if (t.id == id) return &t;
    return nullptr;
}

// Lays the kept tasks out one after another from now, sleep last. A fixed
// commitment that is on when the next task would start is put in first, so
// the times are a sensible suggested order, not exact appointments.
DynArray<TimelineRow> buildTimeline(const AppState& s)
{
    DynArray<const Allocation*> order;
    for (const Allocation& a : s.plan.kept.allocations) if (!tu::isSleepTask(a.title)) order.push_back(&a);
    for (const Allocation& a : s.plan.kept.allocations) if (tu::isSleepTask(a.title))  order.push_back(&a);

    DynArray<TimelineRow> rows;
    int t   = ((s.planNow + 4) / 5) * 5;   // start at the next 5 minutes
    int end = s.planNow + s.planWindow;
    for (const Allocation* a : order) {
        for (int guard = 0; guard < 4 && t < end; ++guard) {
            std::string label;
            int freeAt = 0, left = 0;
            int day = (tu::todayIndex() + t / 1440) % 7;
            if (!currentCommitment(s.commitments, t % 1440, day, label, freeAt, left) || left <= 0) break;
            TimelineRow c;
            c.start = t; c.minutes = left; c.title = label; c.commitment = true;
            rows.push_back(c);
            t += left;
        }
        if (a->allocated <= 0) continue;
        TimelineRow r;
        r.start     = t;
        r.minutes   = a->allocated;
        r.requested = a->requested;
        r.title     = a->title;
        if (const Task* task = findTask(s, a->id)) r.category = task->category;
        rows.push_back(r);
        t += a->allocated;
    }
    return rows;
}

// One timeline row: the time, a dot on the line, and a tinted bar.
void timelineRow(const TimelineRow& r, float timeW, float rowH, bool last)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float  w = ImGui::GetContentRegionAvail().x;
    float  lineH = ImGui::GetTextLineHeight();
    ImVec4 col  = r.commitment ? kInfo : categoryColor(r.category, r.title);
    ImU32  text = ImGui::GetColorU32(ImGuiCol_Text);

    std::string time = tu::formatHM(r.start % 1440);
    dl->AddText(ImVec2(p.x, p.y + (rowH - lineH) * 0.5f), text, time.c_str());

    // The vertical line and the dot.
    float lx = p.x + timeW + 14.0f, cy = p.y + rowH * 0.5f;
    if (!last) dl->AddLine(ImVec2(lx, cy), ImVec2(lx, cy + rowH + ImGui::GetStyle().ItemSpacing.y),
                           rgba(ImVec4(1, 1, 1, 1), 0.12f), 2.0f);
    radialGlow(dl, ImVec2(lx, cy), 14.0f, col, 0.35f);
    dl->AddCircleFilled(ImVec2(lx, cy), 5.5f, ImGui::GetColorU32(col));

    // The bar, tinted in the category colour.
    ImVec2 a(lx + 18.0f, p.y), b(p.x + w, p.y + rowH);
    gradientRect(dl, a, b, ImVec4(col.x * 0.32f, col.y * 0.32f, col.z * 0.32f, 1.0f),
                 ImVec4(col.x * 0.10f, col.y * 0.10f, col.z * 0.10f, 1.0f), 10.0f, a, ImVec2(b.x, a.y));
    dl->AddRect(a, b, rgba(col, 0.35f), 10.0f);

    bool squeezed = !r.commitment && r.requested > r.minutes;
    std::string mins = squeezed ? std::to_string(r.minutes) + " of " + std::to_string(r.requested) + " min"
                                : std::to_string(r.minutes) + " min";
    ImVec2 ms = ImGui::CalcTextSize(mins.c_str());
    dl->AddText(ImVec2(b.x - ms.x - 14.0f, p.y + (rowH - lineH) * 0.5f),
                squeezed ? ImGui::GetColorU32(kSoon) : text, mins.c_str());

    float tx = a.x + 14.0f;
    if (gIcons) {
        std::string g = glyph(r.commitment ? IconCalendar : categoryIcon(r.category, r.title));
        dl->AddText(ImVec2(tx, p.y + (rowH - lineH) * 0.5f), ImGui::GetColorU32(col), g.c_str());
        tx += ImGui::GetFontSize() + 10.0f;
    }
    std::string title = fitText(r.title, b.x - ms.x - 30.0f - tx);
    dl->AddText(ImVec2(tx, p.y + (rowH - lineH) * 0.5f), text, title.c_str());

    ImGui::Dummy(ImVec2(w, rowH));
    if (ImGui::IsItemHovered()) {
        std::string endAt = tu::formatHM((r.start + r.minutes) % 1440);
        if (squeezed)
            ImGui::SetTooltip("%s\n%s - %s\nShortened from %d to %d min so the day fits.", r.title.c_str(),
                              time.c_str(), endAt.c_str(), r.requested, r.minutes);
        else
            ImGui::SetTooltip("%s\n%s - %s", r.title.c_str(), time.c_str(), endAt.c_str());
    }
}

void savePlanNow(Database& db, AppState& s)
{
    // Dropped tasks are saved with 0 minutes, so the history records
    // what was given up that day as well as what was done.
    DynArray<PlanRow> rows;
    for (const Allocation& a : s.plan.kept.allocations) rows.push_back({a.id, a.requested, a.allocated});
    for (const Allocation& a : s.plan.dropped)          rows.push_back({a.id, a.requested, 0});
    if (db.savePlan(tu::todayString(), rows)) {
        s.planSaved = true;
        s.status    = "Plan saved for " + tu::todayString();
    } else {
        s.status = "Could not save the plan. Details are in the terminal.";
    }
}

// A tinted box with a bulb icon and one message, used for verdicts.
void calloutBox(const std::string& text, const ImVec4& col)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float  cw    = ImGui::GetContentRegionAvail().x;
    float  wrapW = cw - 60.0f;
    ImVec2 ts    = ImGui::CalcTextSize(text.c_str(), nullptr, false, wrapW);
    float  ch    = maxOf(ts.y + 28.0f, 54.0f);
    ImVec2 a     = ImGui::GetCursorScreenPos(), b(a.x + cw, a.y + ch);
    gradientRect(dl, a, b, ImVec4(col.x * 0.22f, col.y * 0.22f, col.z * 0.22f, 1.0f),
                 ImVec4(col.x * 0.07f, col.y * 0.07f, col.z * 0.07f, 1.0f), 12.0f, a, ImVec2(b.x, a.y));
    dl->AddRect(a, b, rgba(col, 0.45f), 12.0f);
    if (gIcons) dl->AddText(ImVec2(a.x + 18.0f, a.y + (ch - ImGui::GetTextLineHeight()) * 0.5f),
                            ImGui::GetColorU32(col), glyph(IconBulb).c_str());
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(a.x + 46.0f, a.y + (ch - ts.y) * 0.5f),
                ImGui::GetColorU32(ImGuiCol_Text), text.c_str(), nullptr, wrapW);
    ImGui::Dummy(ImVec2(cw, ch));
}

void drawPlanTab(Database& db, AppState& s, ImFont* large)
{
    // Work the plan out as soon as the tab is opened, so it's never empty.
    if (!s.havePlan) computePlan(db, s);
    const PlanResult& p  = s.plan;
    const ImGuiStyle& st = ImGui::GetStyle();

    // ---- Heading, with Recalculate and Save on the right ----
    HeaderPos h = pageHeader(large, "Your Optimized Day Plan",
                             "A balanced schedule from your tasks, commitments and the time left before you wake up.");
    std::string recalc = withIcon(IconRefresh, "Recalculate");
    std::string save   = s.planSaved ? withIcon(IconCheck, "Saved") : withIcon(IconSave, "Save Plan");
    float btnH = ImGui::GetFrameHeight() + 8.0f;
    float rW = buttonWidth(recalc) + 16.0f, sW = buttonWidth(save) + 16.0f;
    headerRightBegin(h, rW + st.ItemSpacing.x + sW);
    if (ghostButton(recalc.c_str(), ImVec2(rW, btnH))) computePlan(db, s);
    ImGui::SameLine();
    ImGui::BeginDisabled(s.planSaved || !p.feasible);
    if (primaryButton(save.c_str(), ImVec2(sW, btnH))) savePlanNow(db, s);
    ImGui::EndDisabled();
    if (!p.feasible)      tip("This day doesn't fit yet, so it can't be saved.");
    else if (s.planSaved) tip(("Saved for " + tu::todayString()).c_str());
    headerRightEnd(h);

    int planned = 0;
    for (const Allocation& a : p.kept.allocations) planned += a.allocated;
    int freeLeft = maxOf(0, s.planBudget - planned);

    // ---- Four metric cards ----
    float w = cardWidth(4);
    metricCard("plan_window", w, large, IconHourglass, kAccent, formatDuration(s.planWindow),
               "until wake-up at " + tu::formatHM(s.planWake), "From now until you next wake up.");
    ImGui::SameLine();
    metricCard("plan_busy", w, large, IconCalendar, kInfo, formatDuration(s.planBusy), "fixed commitments",
               "College or work hours inside that window. They come off the top.");
    ImGui::SameLine();
    metricCard("plan_tasks", w, large, IconTasks, kGood, std::to_string(planned) + " min", "planned tasks",
               "Minutes given to your tasks in this plan, sleep included.", true);
    ImGui::SameLine();
    metricCard("plan_budget", w, large, IconCheckCircle, kGood, std::to_string(s.planBudget) + " min",
               "available for tasks", "The window until wake-up, minus fixed commitments.", true);
    ImGui::Spacing();

    if (p.kept.allocations.empty() && p.dropped.empty()) {
        beginCard("plan_empty", ImVec2(0, 120));
        cardHeading(IconCheck, "Nothing pending");
        ImGui::TextColored(kMuted, "Add tasks on the Tasks tab and they will be planned here.");
        ImGui::EndChild();
        return;
    }

    // ---- Timeline on the left, the time ring on the right ----
    DynArray<TimelineRow> rows = buildTimeline(s);
    float rowH  = ImGui::GetFrameHeight() + 12.0f;
    float listH = 2.0f * st.WindowPadding.y + 2.0f * ImGui::GetTextLineHeightWithSpacing() + 2.0f * st.ItemSpacing.y +
                  rows.size() * (rowH + st.ItemSpacing.y) + 8.0f;
    float areaH = maxOf(listH, 400.0f);
    float leftW = (ImGui::GetContentRegionAvail().x - st.ItemSpacing.x) * 0.60f;

    beginCard("plan_timeline", ImVec2(leftW, areaH));
    {
        cardHeading(IconList, "Today's Schedule");
        float timeW = ImGui::CalcTextSize("00:00").x;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            timelineRow(rows[i], timeW, rowH, i + 1 == rows.size());
            ImGui::PopID();
        }
        ImGui::TextColored(kMuted, "A suggested order from now. Hover a row for its times.");
    }
    ImGui::EndChild();

    ImGui::SameLine();

    beginCard("plan_ring", ImVec2(0, areaH));
    {
        cardHeading(IconChart, "Where the time goes");
        float  budget = static_cast<float>(maxOf(s.planBudget, 1));
        int    pct    = static_cast<int>(100.0f * planned / budget + 0.5f);
        float  values[3] = {static_cast<float>(planned), static_cast<float>(s.planBusy), static_cast<float>(freeLeft)};
        ImVec4 colors[3] = {kAccent, kInfo, ImVec4(0.45f, 0.45f, 0.50f, 1.0f)};

        float  avail  = ImGui::GetContentRegionAvail().x;
        float  radius = clampTo(avail * 0.17f, 48.0f, 84.0f);
        float  thick  = radius * 0.22f;
        ImVec2 cp     = ImGui::GetCursorScreenPos();
        ImVec2 centre(cp.x + radius + thick, cp.y + radius + thick);
        donut(centre, radius, thick, values, colors, 3);

        char big[16];
        std::snprintf(big, sizeof(big), "%d%%", pct);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (large) ImGui::PushFont(large);
        ImVec2 bs = ImGui::CalcTextSize(big);
        dl->AddText(ImVec2(centre.x - bs.x * 0.5f, centre.y - bs.y * 0.70f), ImGui::GetColorU32(ImGuiCol_Text), big);
        if (large) ImGui::PopFont();
        const char* sub = "time planned";
        ImVec2 ss = ImGui::CalcTextSize(sub);
        dl->AddText(ImVec2(centre.x - ss.x * 0.5f, centre.y + bs.y * 0.32f), ImGui::GetColorU32(kMuted), sub);
        ImGui::Dummy(ImVec2(2.0f * (radius + thick), 2.0f * (radius + thick)));
        tip("Planned task minutes, out of the time available for tasks.");

        // Legend to the right of the ring: shares of the whole window.
        ImGui::SameLine(0, 28);
        ImGui::BeginGroup();
        float total = static_cast<float>(maxOf(s.planWindow, 1));
        ImGui::Dummy(ImVec2(0, maxOf(0.0f, radius + thick - 1.8f * ImGui::GetTextLineHeightWithSpacing())));
        float valueX = ImGui::GetCursorPosX() + 18.0f + ImGui::CalcTextSize("Commitments").x + 24.0f;
        auto share = [total](int m) {
            char v[48];
            std::snprintf(v, sizeof(v), "%d min (%d%%)", m, static_cast<int>(100.0f * m / total + 0.5f));
            return std::string(v);
        };
        legendRow(kAccent,   "Tasks",       share(planned),    valueX);
        legendRow(kInfo,     "Commitments", share(s.planBusy), valueX);
        legendRow(colors[2], "Free",        share(freeLeft),   valueX);
        ImGui::EndGroup();
        ImGui::Spacing();

        // The verdict.
        if (!p.feasible)
            calloutBox("This day doesn't fit. Even your mandatory tasks at their minimums are " +
                       std::to_string(p.shortfall) + " min over. Make a task optional or move it to another day.", kDanger);
        else if (!p.dropped.empty())
            calloutBox(std::to_string(p.dropped.size()) + " optional task" + (p.dropped.size() == 1 ? " was" : "s were") +
                       " moved out so the rest fits.", kWarn);
        else if (p.kept.deficit > 0)
            calloutBox("Tasks were shortened by " + std::to_string(p.kept.deficit) + " min in total so the day fits.", kSoon);
        else
            calloutBox("Everything fits at full length.", kAccent);

        if (!p.dropped.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(kMuted, "Move these to another day:");
            for (const Allocation& a : p.dropped) {
                int pri = priorityOf(s, a.id);
                pill(("P" + std::to_string(pri)).c_str(), priorityColor(pri));
                ImGui::SameLine(0, 18);
                ImGui::Text("%s", a.title.c_str());
                ImGui::SameLine();
                ImGui::TextColored(kMuted, "(%d min)", a.requested);
            }
        }
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------
// "My schedule" tab: two cards side by side
// ---------------------------------------------------------------
// The dates of this week, Monday first: day of month, and "Nov 16 - Nov 22, 2026".
void weekDates(int dayOfMonth[7], std::string& range)
{
    static const char* mon[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    std::tm first = tu::localNow();
    first.tm_mday -= tu::todayIndex();
    first.tm_hour = 12;            // midday, so daylight-saving changes can't shift the date
    std::mktime(&first);
    std::tm last = first;
    for (int i = 0; i < 7; ++i) {
        std::tm d = first;
        d.tm_mday += i;
        std::mktime(&d);
        dayOfMonth[i] = d.tm_mday;
        if (i == 6) last = d;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %d - %s %d, %d", mon[first.tm_mon], first.tm_mday,
                  mon[last.tm_mon], last.tm_mday, last.tm_year + 1900);
    range = buf;
}

// Checks the add-commitment form and saves it. Returns true when saved.
bool addCommitmentFromForm(Database& db, AppState& s)
{
    int startMin = tu::parseHM(s.cStart);
    int endMin   = tu::parseHM(s.cEnd);
    std::string mask;
    for (int i = 0; i < 7; ++i) mask += s.cDays[i] ? '1' : '0';

    s.cError.clear();
    if (s.cName.empty())                     s.cError = "Give it a name.";
    else if (mask == "0000000")              s.cError = "Tick at least one day.";
    else if (startMin < 0 || endMin < 0)     s.cError = "Times must be HH:MM, for example 09:00.";
    else if (startMin == endMin)             s.cError = "Start and end can't be the same.";
    if (!s.cError.empty()) return false;

    CommitmentRow c;
    c.label = s.cName;
    c.days  = mask;
    c.start = tu::formatHM(startMin);
    c.end   = tu::formatHM(endMin);
    if (!db.addCommitment(c)) {
        s.cError = "Could not save. Details are in the terminal.";
        return false;
    }
    s.status = "Added: " + c.label + ", " + daysToText(mask) + ", " + c.start + "-" + c.end;
    s.cName.clear(); s.cStart.clear(); s.cEnd.clear();
    reloadAll(db, s);
    return true;
}

// The add-commitment form, in a popup.
void drawCommitmentDialog(Database& db, AppState& s)
{
    static const char* kDays[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Add commitment", nullptr, ImGuiWindowFlags_NoResize)) return;

    const float lw = 80.0f;
    ImGui::TextColored(kMuted, "Times you are always busy: college, school, office, tuition, a work shift.");
    ImGui::Spacing();
    fieldLabel("Name", lw);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##cname", "College, School, Office, Tuition...", &s.cName);

    fieldLabel("Days", lw);
    for (int i = 0; i < 7; ++i) {
        if (i) ImGui::SameLine(0, 6);
        ImGui::PushID(i);
        if (segmentButton(kDays[i], s.cDays[i])) s.cDays[i] = !s.cDays[i];
        ImGui::PopID();
    }
    fieldLabel("", lw);
    if (ImGui::SmallButton("Weekdays"))  for (int i = 0; i < 7; ++i) s.cDays[i] = (i < 5);
    ImGui::SameLine();
    if (ImGui::SmallButton("Every day")) for (int i = 0; i < 7; ++i) s.cDays[i] = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear"))     for (int i = 0; i < 7; ++i) s.cDays[i] = false;

    fieldLabel("Time", lw);
    ImGui::SetNextItemWidth(100);
    ImGui::InputTextWithHint("##cstart", "09:00", &s.cStart);
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "to");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::InputTextWithHint("##cend", "16:00", &s.cEnd);
    int startMin = tu::parseHM(s.cStart), endMin = tu::parseHM(s.cEnd);
    if (startMin >= 0 && endMin >= 0 && endMin < startMin) {
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "runs past midnight");
    }
    if (!s.cError.empty()) ImGui::TextColored(kWarn, "%s", s.cError.c_str());

    ImGui::Spacing();
    float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (primaryButton(withIcon(IconPlus, "Add commitment").c_str(), ImVec2(half, 0)) && addCommitmentFromForm(db, s))
        ImGui::CloseCurrentPopup();
    ImGui::SameLine();
    if (ghostButton("Cancel", ImVec2(-FLT_MIN, 0))) {
        s.cError.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// The week grid: days across, hours down, commitments as blue blocks,
// today's column in amber with a line at the current time.
void weekGrid(Database& db, AppState& s, float height, bool& changed)
{
    static const char* kDays[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    int dom[7];
    std::string range;
    weekDates(dom, range);

    // Hours shown: 08:00-22:00, stretched to fit every commitment.
    int hStart = 8, hEnd = 22;
    for (const CommitmentRow& c : s.commitmentRows) {
        int a = tu::parseHM(c.start), b = tu::parseHM(c.end);
        if (a < 0 || b < 0) continue;
        if (b <= a) { hEnd = 24; hStart = (b > 0) ? 0 : minOf(hStart, a / 60); continue; }
        hStart = minOf(hStart, a / 60);
        hEnd   = maxOf(hEnd, (b + 59) / 60);
    }
    hEnd = minOf(hEnd, 24);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p     = ImGui::GetCursorScreenPos();
    float  w     = ImGui::GetContentRegionAvail().x;
    float  lineH = ImGui::GetTextLineHeight();
    float  timeW = ImGui::CalcTextSize("00:00").x + 16.0f;
    float  hdrH  = 2.0f * lineH + 14.0f;
    float  colW  = (w - timeW) / 7.0f;
    float  top   = p.y + hdrH, bottom = p.y + height;
    float  perMin = (bottom - top) / ((hEnd - hStart) * 60.0f);
    int    today = tu::todayIndex();
    auto   yAt  = [&](int minute) { return top + (minute - hStart * 60) * perMin; };

    // Today's column.
    {
        float x0 = p.x + timeW + today * colW, x1 = x0 + colW;
        gradientRect(dl, ImVec2(x0, p.y), ImVec2(x1, bottom), ImVec4(0.26f, 0.18f, 0.06f, 1.0f),
                     ImVec4(0.10f, 0.08f, 0.05f, 1.0f), 10.0f, ImVec2(x0, p.y), ImVec2(x0, bottom), 0.85f);
        dl->AddRect(ImVec2(x0, p.y), ImVec2(x1, bottom), rgba(kAccent, 0.45f), 10.0f);
    }
    // Day headers.
    for (int d = 0; d < 7; ++d) {
        float cx = p.x + timeW + (d + 0.5f) * colW;
        ImU32 col = ImGui::GetColorU32(d == today ? kAccent : kMuted);
        std::string num = std::to_string(dom[d]);
        ImVec2 ns = ImGui::CalcTextSize(kDays[d]), ds = ImGui::CalcTextSize(num.c_str());
        dl->AddText(ImVec2(cx - ns.x * 0.5f, p.y + 6.0f), col, kDays[d]);
        dl->AddText(ImVec2(cx - ds.x * 0.5f, p.y + 6.0f + lineH),
                    d == today ? ImGui::GetColorU32(kAccent) : ImGui::GetColorU32(ImGuiCol_Text), num.c_str());
    }
    // Hour lines and labels (every hour, or every 2 hours if tight).
    int step = (perMin * 60.0f < lineH * 1.6f) ? 2 : 1;
    for (int hr = hStart; hr <= hEnd; ++hr) {
        float y = yAt(hr * 60);
        dl->AddLine(ImVec2(p.x + timeW, y), ImVec2(p.x + w, y), rgba(ImVec4(1, 1, 1, 1), hr % step == 0 ? 0.07f : 0.03f));
        if ((hr - hStart) % step == 0 && hr < hEnd) {
            std::string t = tu::formatHM((hr % 24) * 60);
            dl->AddText(ImVec2(p.x, y - lineH * 0.5f), ImGui::GetColorU32(kMuted), t.c_str());
        }
    }
    for (int d = 1; d < 7; ++d) {
        float x = p.x + timeW + d * colW;
        dl->AddLine(ImVec2(x, top), ImVec2(x, bottom), rgba(ImVec4(1, 1, 1, 1), 0.05f));
    }

    // Commitment blocks.
    ImGui::Dummy(ImVec2(w, height));
    static int menuId = 0;
    static std::string menuLabel;
    for (const CommitmentRow& c : s.commitmentRows) {
        int a = tu::parseHM(c.start), b = tu::parseHM(c.end);
        if (a < 0 || b < 0 || c.days.size() != 7) continue;
        for (int d = 0; d < 7; ++d) {
            if (c.days[d] != '1') continue;
            // Overnight blocks are drawn in two parts: tonight and tomorrow morning.
            int parts[2][3] = {{d, a, b > a ? b : 1440}, {(d + 1) % 7, 0, b}};
            int nParts = (b > a) ? 1 : 2;
            for (int k = 0; k < nParts; ++k) {
                int day = parts[k][0];
                int s0  = maxOf(parts[k][1], hStart * 60), s1 = minOf(parts[k][2], hEnd * 60);
                if (s1 <= s0) continue;
                ImVec2 q0(p.x + timeW + day * colW + 4.0f, yAt(s0) + 2.0f);
                ImVec2 q1(q0.x + colW - 8.0f, yAt(s1) - 2.0f);
                bool hover = ImGui::IsMouseHoveringRect(q0, q1) && ImGui::IsWindowHovered();
                gradientRect(dl, q0, q1, ImVec4(0.16f, 0.30f, 0.66f, 1.0f), ImVec4(0.09f, 0.17f, 0.42f, 1.0f),
                             8.0f, q0, ImVec2(q0.x, q1.y));
                dl->AddRect(q0, q1, rgba(kInfo, hover ? 1.0f : 0.6f), 8.0f, 0, hover ? 1.6f : 1.0f);
                float bw = q1.x - q0.x - 8.0f;
                std::string name = fitText(c.label, bw), when = fitText(c.start + " - " + c.end, bw);
                float cx = (q0.x + q1.x) * 0.5f;
                float ty = q0.y + 6.0f;
                if (q1.y - q0.y > lineH * 2.2f) ty = (q0.y + q1.y) * 0.5f - lineH;
                if (q1.y - q0.y > lineH + 6.0f) {
                    ImVec2 ns = ImGui::CalcTextSize(name.c_str());
                    dl->AddText(ImVec2(cx - ns.x * 0.5f, ty), IM_COL32(255, 255, 255, 255), name.c_str());
                }
                if (q1.y - q0.y > lineH * 2.2f) {
                    ImVec2 ws = ImGui::CalcTextSize(when.c_str());
                    dl->AddText(ImVec2(cx - ws.x * 0.5f, ty + lineH + 2.0f), rgba(ImVec4(0.8f, 0.86f, 1.0f, 1.0f), 0.85f), when.c_str());
                }
                if (hover) {
                    ImGui::SetTooltip("%s\n%s - %s  (%s)\nClick to delete", c.label.c_str(), c.start.c_str(),
                                      c.end.c_str(), daysToText(c.days).c_str());
                    if (ImGui::IsMouseClicked(0)) {
                        menuId = c.id; menuLabel = c.label;
                        ImGui::OpenPopup("commitment_menu");
                    }
                }
            }
        }
    }

    // The current time, across today's column.
    int now = tu::nowMinutes();
    if (now >= hStart * 60 && now <= hEnd * 60) {
        float y  = yAt(now);
        float x0 = p.x + timeW + today * colW, x1 = x0 + colW;
        dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), ImGui::GetColorU32(kAccent), 2.0f);
        dl->AddCircleFilled(ImVec2(x0, y), 4.5f, ImGui::GetColorU32(kAccent));
    }

    if (ImGui::BeginPopup("commitment_menu")) {
        ImGui::Text("%s", menuLabel.c_str());
        ImGui::Spacing();
        if (dangerButton(withIcon(IconTrash, "Delete this commitment").c_str())) {
            if (db.deleteCommitment(menuId)) s.status = "Removed: " + menuLabel;
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// Wake-up time and name, on one row.
void drawPreferences(Database& db, AppState& s)
{
    cardHeading(IconBed, "Preferences");
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kMuted, "Wake up");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::InputTextWithHint("##wake", "HH:MM", &s.wakeText);
    ImGui::SameLine();
    if (ghostButton((withIcon(IconSave, "Save") + "##wakesave").c_str())) {
        int m = tu::parseHM(s.wakeText);
        if (m < 0) {
            s.wakeMessage = "Use HH:MM, e.g. 06:30";
        } else {
            s.wakeText = tu::formatHM(m);
            db.setSetting("wake_time", s.wakeText);
            s.wakeSaved       = m;
            s.wakeMessage     = "Saved.";
            s.haveSuggestions = false;
            s.havePlan        = false;
        }
    }
    ImGui::SameLine();
    if (!s.wakeMessage.empty()) ImGui::TextColored(kMuted, "%s", s.wakeMessage.c_str());
    else if (s.wakeSaved < 0)   ImGui::TextColored(kWarn, "Not saved: 07:00 assumed");
    else                        ImGui::TextColored(kMuted, " ");

    ImGui::SameLine(0, 48);
    ImGui::TextColored(kMuted, "Your name");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("##name", "for the greeting", &s.nameText);
    ImGui::SameLine();
    if (ghostButton((withIcon(IconSave, "Save") + "##namesave").c_str())) {
        db.setSetting("user_name", s.nameText);
        s.userName    = s.nameText;
        s.nameMessage = "Saved.";
    }
    if (!s.nameMessage.empty()) { ImGui::SameLine(); ImGui::TextColored(kMuted, "%s", s.nameMessage.c_str()); }
}

// ---------------------------------------------------------------
// "My schedule" tab: the week grid (or a list) and preferences
// ---------------------------------------------------------------
void drawScheduleTab(Database& db, AppState& s, ImFont* large)
{
    const ImGuiStyle& st = ImGui::GetStyle();
    bool changed = false;

    HeaderPos h = pageHeader(large, "Weekly Schedule",
                             "Your fixed commitments. Plan My Day leaves these times out, and What Now stays quiet during them.");
    std::string add = withIcon(IconPlus, "Add Commitment");
    headerRightBegin(h, segmentWidth(add));
    if (segmentButton(add.c_str(), true)) {
        s.cError.clear();
        ImGui::OpenPopup("Add commitment");
    }
    drawCommitmentDialog(db, s);
    headerRightEnd(h);

    // The week's dates, and Week / List on the right.
    int dom[7];
    std::string range;
    weekDates(dom, range);
    float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    infoChip(IconCalendar, range, ImGui::GetStyle().Colors[ImGuiCol_Text]);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kMuted, "  repeats every week");
    std::string lWeek = "Week", lList = "List";
    ImGui::SameLine(rowRight - segmentWidth(lWeek) - segmentWidth(lList) - st.ItemSpacing.x);
    if (segmentButton((lWeek + "##wk").c_str(), !s.scheduleList)) s.scheduleList = false;
    ImGui::SameLine();
    if (segmentButton((lList + "##ls").c_str(), s.scheduleList))  s.scheduleList = true;
    ImGui::Spacing();

    float prefsH = 2.0f * st.WindowPadding.y + ImGui::GetTextLineHeightWithSpacing() + st.ItemSpacing.y +
                   ImGui::GetFrameHeight() + 6.0f;
    float mainH  = maxOf(360.0f, ImGui::GetContentRegionAvail().y - prefsH - st.ItemSpacing.y);

    beginCard("schedule_main", ImVec2(0, mainH), CardPlain, ImGuiWindowFlags_NoScrollbar);
    if (!s.scheduleList) {
        if (s.commitmentRows.empty())
            ImGui::TextColored(kMuted, "No commitments yet. Press Add Commitment to block out college, work or tuition.");
        weekGrid(db, s, ImGui::GetContentRegionAvail().y, changed);
    } else if (s.commitmentRows.empty()) {
        ImGui::TextColored(kMuted, "No commitments yet. Press Add Commitment to add one.");
    } else if (ImGui::BeginTable("schedule", 4,
                   ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Days", ImGuiTableColumnFlags_WidthFixed,
            columnFor({"Days", "Mon, Tue, Wed, Thu, Sat"}) + 16.0f);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, columnFor({"Time", "23:00 - 07:00  (overnight)"}));
        ImGui::TableSetupColumn("",     ImGuiTableColumnFlags_WidthFixed,
            buttonWidth(iconOr(IconTrash, "Delete")) + 2.0f * ImGui::GetStyle().CellPadding.x);
        ImGui::TableHeadersRow();
        for (const CommitmentRow& c : s.commitmentRows) {
            ImGui::PushID(c.id);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (gIcons) { ImGui::TextColored(kInfo, "%s", glyph(IconCalendar).c_str()); ImGui::SameLine(); }
            ImGui::Text("%s", c.label.c_str());
            ImGui::TableNextColumn(); pill(daysToText(c.days).c_str(), kInfo);
            ImGui::TableNextColumn();
            ImGui::Text("%s - %s", c.start.c_str(), c.end.c_str());
            if (tu::parseHM(c.end) <= tu::parseHM(c.start)) {
                ImGui::SameLine();
                ImGui::TextColored(kMuted, "(overnight)");
            }
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, kDanger);
            if (ImGui::SmallButton((iconOr(IconTrash, "Delete") + "##del").c_str())) {
                if (db.deleteCommitment(c.id)) s.status = "Removed: " + c.label;
                changed = true;
            }
            ImGui::PopStyleColor();
            tip("Delete");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();

    beginCard("schedule_prefs", ImVec2(0, prefsH), CardPlain, ImGuiWindowFlags_NoScrollbar);
    drawPreferences(db, s);
    ImGui::EndChild();

    if (changed) reloadAll(db, s);
}

// ---------------------------------------------------------------
// "This month" tab: a monthly report card
// ---------------------------------------------------------------
void computeMonth(Database& db, AppState& s)
{
    if (s.monthYear == 0)   // first time: show the current month
        std::sscanf(tu::todayString().c_str(), "%d-%d", &s.monthYear, &s.monthNum);

    DynArray<MonthTask> items;
    for (const Task& t : s.tasks) {
        MonthTask m;
        m.id          = t.id;
        m.title       = t.title;
        m.priority    = t.priority;
        m.deadline    = t.deadline;
        m.completed   = (t.status == "Completed");
        m.completedAt = t.completedAt;
        items.push_back(m);
    }

    char from[48], to[48];
    std::snprintf(from, sizeof(from), "%04d-%02d-01", s.monthYear, s.monthNum);
    std::snprintf(to,   sizeof(to),   "%04d-%02d-%02d", s.monthYear, s.monthNum,
                  daysInMonth(s.monthYear, s.monthNum));
    DynArray<std::string> plans = db.getPlannedDays(from, to);

    std::string now = tu::todayString() + " " + tu::formatHM(tu::nowMinutes());
    s.month     = buildMonthReport(items, plans, s.monthYear, s.monthNum, now);
    s.haveMonth = true;
}

// A filled area chart of running totals: values[i] is the total after day i+1.
// Days with something new get a dot; the last day glows.
void areaChart(const DynArray<int>& totals, const DynArray<int>& perDay, int daysInMonth, float height)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p      = ImGui::GetCursorScreenPos();
    float  w      = ImGui::GetContentRegionAvail().x;
    float  lineH  = ImGui::GetTextLineHeight();
    float  axisW  = ImGui::CalcTextSize("000").x + 12.0f;
    float  left   = p.x + axisW, right = p.x + w - 10.0f;
    float  top    = p.y + 8.0f, bottom = p.y + height - lineH - 10.0f;
    int    most   = 1;
    for (int v : totals) most = maxOf(most, v);
    int    yMax   = most <= 4 ? 4 : ((most + 3) / 4) * 4;   // round up so the grid lines are whole numbers

    auto xAt = [&](int day) { return left + (right - left) * (day - 1) / maxOf(1, daysInMonth - 1); };
    auto yAt = [&](float v) { return bottom - (bottom - top) * v / yMax; };

    for (int g = 0; g <= 4; ++g) {
        float y = yAt(yMax * g / 4.0f);
        dl->AddLine(ImVec2(left, y), ImVec2(right, y), rgba(ImVec4(1, 1, 1, 1), g == 0 ? 0.12f : 0.05f));
        std::string lbl = std::to_string(yMax * g / 4);
        ImVec2 ls = ImGui::CalcTextSize(lbl.c_str());
        dl->AddText(ImVec2(left - ls.x - 10.0f, y - lineH * 0.5f), ImGui::GetColorU32(kMuted), lbl.c_str());
    }
    for (int d : {1, 5, 10, 15, 20, 25, daysInMonth}) {
        if (d > daysInMonth || (d == 25 && daysInMonth - 25 < 3)) continue;
        std::string lbl = std::to_string(d);
        ImVec2 ls = ImGui::CalcTextSize(lbl.c_str());
        dl->AddText(ImVec2(xAt(d) - ls.x * 0.5f, bottom + 6.0f), ImGui::GetColorU32(kMuted), lbl.c_str());
    }

    int n = static_cast<int>(totals.size());
    if (n >= 1) {
        // The fill: one quad per day, amber at the line fading to clear at the axis.
        const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
        ImU32 hi = rgba(kAccent, 0.32f), lo = rgba(kAccent, 0.0f);
        for (int i = 0; i + 1 < n; ++i) {
            ImVec2 a(xAt(i + 1), yAt(static_cast<float>(totals[i]))), b(xAt(i + 2), yAt(static_cast<float>(totals[i + 1])));
            dl->PrimReserve(6, 4);
            ImDrawIdx base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
            dl->PrimWriteVtx(a, uv, hi); dl->PrimWriteVtx(b, uv, hi);
            dl->PrimWriteVtx(ImVec2(b.x, bottom), uv, lo); dl->PrimWriteVtx(ImVec2(a.x, bottom), uv, lo);
            dl->PrimWriteIdx(base); dl->PrimWriteIdx(base + 1); dl->PrimWriteIdx(base + 2);
            dl->PrimWriteIdx(base); dl->PrimWriteIdx(base + 2); dl->PrimWriteIdx(base + 3);
        }
        // The line.
        for (int i = 0; i < n; ++i) dl->PathLineTo(ImVec2(xAt(i + 1), yAt(static_cast<float>(totals[i]))));
        dl->PathStroke(ImGui::GetColorU32(kAccent), 0, 2.5f);
        // Dots on days with completions, and a glow on the latest day.
        for (int i = 0; i < n; ++i) {
            ImVec2 c(xAt(i + 1), yAt(static_cast<float>(totals[i])));
            if (i < static_cast<int>(perDay.size()) && perDay[i] > 0) dl->AddCircleFilled(c, 4.0f, ImGui::GetColorU32(kAccent));
            if (i + 1 == n) {
                radialGlow(dl, c, 18.0f, kAccent, 0.55f);
                dl->AddCircleFilled(c, 5.5f, IM_COL32(255, 236, 190, 255));
            }
            if (ImGui::IsMouseHoveringRect(ImVec2(c.x - 6, top), ImVec2(c.x + 6, bottom)) && ImGui::IsWindowHovered())
                ImGui::SetTooltip("Day %d: %d completed, %d so far", i + 1,
                                  i < static_cast<int>(perDay.size()) ? perDay[i] : 0, totals[i]);
        }
    }
    ImGui::Dummy(ImVec2(w, height));
}

// A small card in the right column: icon, value, label.
void miniStat(const char* id, float height, ImFont* large, unsigned cp, const ImVec4& col, const std::string& value,
              const std::string& label, const char* tooltip)
{
    beginCard(id, ImVec2(0, height), CardPlain, ImGuiWindowFlags_NoScrollbar);
    float  lineH = ImGui::GetTextLineHeight();
    float  avail = ImGui::GetContentRegionAvail().y;
    float  valueH = large ? large->FontSize : lineH;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + maxOf(0.0f, (avail - valueH - lineH - 4.0f) * 0.5f));
    if (gIcons) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float  r = lineH + 2.0f;
        ImVec2 c(p.x + r, p.y + lineH + 2.0f);
        radialGlow(dl, c, r * 1.8f, col, 0.25f);
        std::string g = glyph(cp);
        if (large) ImGui::PushFont(large);
        ImVec2 gs = ImGui::CalcTextSize(g.c_str());
        dl->AddText(ImVec2(c.x - gs.x * 0.5f, c.y - gs.y * 0.5f), ImGui::GetColorU32(col), g.c_str());
        if (large) ImGui::PopFont();
        ImGui::Dummy(ImVec2(2.0f * r, 2.0f * lineH));
        ImGui::SameLine(0, 14);
    }
    ImGui::BeginGroup();
    if (large) ImGui::PushFont(large);
    ImGui::Text("%s", value.c_str());
    if (large) ImGui::PopFont();
    ImGui::TextColored(kMuted, "%s", label.c_str());
    ImGui::EndGroup();
    ImGui::EndChild();
    tip(tooltip);
}

void drawMonthTab(Database& db, AppState& s, ImFont* large)
{
    static const char* kMonths[12] = {"January", "February", "March", "April", "May", "June", "July",
                                      "August", "September", "October", "November", "December"};
    if (!s.haveMonth) computeMonth(db, s);
    const MonthReport& r = s.month;
    const ImGuiStyle& st = ImGui::GetStyle();

    int curY = 0, curM = 0, curD = 0;
    std::sscanf(tu::todayString().c_str(), "%d-%d-%d", &curY, &curM, &curD);
    bool atCurrent = (r.year == curY && r.month == curM);

    // ---- Heading: month picker (no going past the current month) ----
    float x0 = ImGui::GetCursorPosX(), y0 = ImGui::GetCursorPosY();
    float right = x0 + ImGui::GetContentRegionAvail().x;
    float arrow = (large ? large->FontSize : ImGui::GetFrameHeight()) + 8.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.0f);
    if (ghostButton((iconOr(IconLeft, "<") + "##prevmonth").c_str(), ImVec2(arrow, arrow))) {
        if (--s.monthNum == 0) { s.monthNum = 12; --s.monthYear; }
        computeMonth(db, s);
    }
    ImGui::SameLine(0, 14);
    if (large) ImGui::PushFont(large);
    ImGui::SetCursorPosY(y0 + (arrow - ImGui::GetTextLineHeight()) * 0.5f - st.FramePadding.y);
    ImGui::Text("%s %d", kMonths[r.month - 1], r.year);
    if (large) ImGui::PopFont();
    ImGui::SameLine(0, 14);
    ImGui::SetCursorPosY(y0);
    ImGui::BeginDisabled(atCurrent);
    if (ghostButton((iconOr(IconRight, ">") + "##nextmonth").c_str(), ImVec2(arrow, arrow))) {
        if (++s.monthNum == 13) { s.monthNum = 1; ++s.monthYear; }
        computeMonth(db, s);
    }
    ImGui::EndDisabled();
    ImGui::PopStyleVar();
    if (!atCurrent) {
        std::string back = withIcon(IconCalendar, "This month");
        ImGui::SameLine(right - segmentWidth(back));
        ImGui::SetCursorPosY(y0);
        if (segmentButton(back.c_str(), false)) {
            s.monthYear = curY; s.monthNum = curM;
            computeMonth(db, s);
        }
    }
    ImGui::SetCursorPos(ImVec2(x0, y0 + arrow + 10.0f));
    ImGui::TextColored(kMuted, "%s", atCurrent ? "Your productivity and progress so far this month."
                                               : "Your productivity and progress for this month.");
    ImGui::Spacing();

    // ---- Four metric cards ----
    float w = cardWidth(4);
    metricCard("m_done", w, large, IconCheck, kGood, std::to_string(r.completed), "tasks completed",
               "Tasks marked Done during this month.", true);
    ImGui::SameLine();
    ImVec4 rateColor = r.onTimeRate < 0 ? kMuted : r.onTimeRate >= 80 ? kGood : r.onTimeRate >= 50 ? kSoon : kWarn;
    metricCard("m_rate", w, large, IconClock, rateColor,
               r.onTimeRate < 0 ? std::string("-") : std::to_string(r.onTimeRate) + "%", "on-time rate",
               "On time / (on time + late + left undone).", r.onTimeRate >= 0);
    ImGui::SameLine();
    metricCard("m_undone", w, large, IconWarning, r.undone ? kWarn : kMuted, std::to_string(r.undone), "left undone",
               "Deadline passed this month, and the task was not done by the end of it.", r.undone > 0);
    ImGui::SameLine();
    metricCard("m_planned", w, large, IconCalendar, kInfo,
               std::to_string(r.daysPlanned) + " / " + std::to_string(r.daysInMonth), "days with a saved plan",
               "Days this month on which you pressed Save Plan.");
    ImGui::Spacing();

    // ---- Progress chart on the left, three small cards on the right ----
    int shownDays = atCurrent ? minOf(curD, r.daysInMonth) : r.daysInMonth;
    DynArray<int> totals;
    int running = 0, busiest = 0, busiestDay = 0;
    for (int d = 0; d < shownDays && d < static_cast<int>(r.completedPerDay.size()); ++d) {
        running += r.completedPerDay[d];
        totals.push_back(running);
        if (r.completedPerDay[d] > busiest) { busiest = r.completedPerDay[d]; busiestDay = d + 1; }
    }
    DynArray<MonthTask> monthTasks = toMonthTasks(s);
    TodayStats ts = todayStats(monthTasks, tu::todayString() + " " + tu::formatHM(tu::nowMinutes()));

    float rowH   = 320.0f;
    float chartW = (ImGui::GetContentRegionAvail().x - st.ItemSpacing.x) * 0.72f;
    beginCard("m_chart", ImVec2(chartW, rowH), CardPlain, ImGuiWindowFlags_NoScrollbar);
    {
        cardHeading(IconChart, "Monthly Progress", "tasks completed, running total");
        areaChart(totals, r.completedPerDay, r.daysInMonth, ImGui::GetContentRegionAvail().y);
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginGroup();
    float miniH = (rowH - 2.0f * st.ItemSpacing.y) / 3.0f;
    miniStat("m_streak", miniH, large, IconFire, kAccent,
             std::to_string(ts.streak) + (ts.streak == 1 ? " day" : " days"), "current streak",
             "Days in a row with at least one task marked Done.");
    miniStat("m_busiest", miniH, large, IconTrophy, kSoon,
             busiest ? std::to_string(busiest) + " on day " + std::to_string(busiestDay) : std::string("-"),
             "busiest day", "The day this month with the most tasks completed.");
    miniStat("m_late", miniH, large, IconClock, r.late ? kWarn : kGood, std::to_string(r.late), "finished late",
             "Completed this month, but after the deadline.");
    ImGui::EndGroup();
    ImGui::Spacing();

    // ---- Category breakdown: tasks completed this month, by category ----
    char prefix[16];
    std::snprintf(prefix, sizeof(prefix), "%04d-%02d", r.year, r.month);
    DynArray<std::string> cats;
    DynArray<int>         counts;
    int total = 0;
    for (const Task& t : s.tasks) {
        if (t.status != "Completed" || t.completedAt.rfind(prefix, 0) != 0) continue;
        std::string c = t.category.empty() ? std::string("No category") : t.category;
        std::size_t k = 0;
        while (k < cats.size() && lowerCase(cats[k]) != lowerCase(c)) ++k;
        if (k == cats.size()) { cats.push_back(c); counts.push_back(0); }
        ++counts[k];
        ++total;
    }
    float lineH = ImGui::GetTextLineHeight();
    int   legendRows = maxOf(1, static_cast<int>((cats.size() + 1) / 2));
    float catH = 2.0f * st.WindowPadding.y + lineH + st.ItemSpacing.y * 2.0f + 16.0f + legendRows * (lineH + st.ItemSpacing.y) + 8.0f;
    beginCard("m_cats", ImVec2(0, catH), CardPlain, ImGuiWindowFlags_NoScrollbar);
    {
        cardHeading(IconTasks, "Category Breakdown", "tasks completed this month, by category");
        if (total == 0) {
            ImGui::TextColored(kMuted, "Complete a task to see where your effort goes.");
        } else {
            // One rounded bar split into coloured parts.
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 p  = ImGui::GetCursorScreenPos();
            float  bw = ImGui::GetContentRegionAvail().x, bh = 16.0f, x = p.x;
            dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + bh), rgba(ImVec4(1, 1, 1, 1), 0.05f), 999.0f);
            for (std::size_t k = 0; k < cats.size(); ++k) {
                float seg = bw * counts[k] / total;
                ImDrawFlags corners = (k == 0 ? ImDrawFlags_RoundCornersLeft : 0) |
                                      (k + 1 == cats.size() ? ImDrawFlags_RoundCornersRight : 0);
                if (corners == 0) corners = ImDrawFlags_RoundCornersNone;
                dl->AddRectFilled(ImVec2(x, p.y), ImVec2(x + seg - (k + 1 < cats.size() ? 2.0f : 0.0f), p.y + bh),
                                  ImGui::GetColorU32(categoryColor(cats[k])), bh * 0.5f, corners);
                x += seg;
            }
            ImGui::Dummy(ImVec2(bw, bh));
            ImGui::Spacing();
            float colW = ImGui::GetContentRegionAvail().x * 0.5f;
            float lx0  = ImGui::GetCursorPosX();
            for (std::size_t k = 0; k < cats.size(); ++k) {
                if (k % 2 == 1) { ImGui::SameLine(lx0 + colW); }
                char v[32];
                std::snprintf(v, sizeof(v), "%d%%  (%d)", static_cast<int>(100.0f * counts[k] / total + 0.5f), counts[k]);
                float base = lx0 + ((k % 2 == 1) ? colW : 0.0f);
                legendRow(categoryColor(cats[k]), cats[k].c_str(), v, base + colW * 0.55f);
            }
        }
    }
    ImGui::EndChild();
    ImGui::Spacing();

    // ---- Two lists: left undone, and finished late ----
    float half  = (ImGui::GetContentRegionAvail().x - st.ItemSpacing.x) * 0.5f;
    int   rows  = maxOf(1, maxOf(static_cast<int>(r.undoneTasks.size()), static_cast<int>(r.lateTasks.size())));
    float listH = maxOf(140.0f, 2.0f * st.WindowPadding.y + (rows + 1) * ImGui::GetFrameHeightWithSpacing() + 10.0f);
    char heading[64];

    beginCard("m_undone_list", ImVec2(half, listH));
    {
        std::snprintf(heading, sizeof(heading), "Left undone (%d)", r.undone);
        cardHeading(IconWarning, heading);
        if (r.undoneTasks.empty())
            ImGui::TextColored(kGood, "%s", atCurrent ? "Nothing missed so far this month." : "Nothing missed this month.");
        for (const MonthItem& it : r.undoneTasks) {
            pill(("P" + std::to_string(it.priority)).c_str(), priorityColor(it.priority));
            ImGui::SameLine(0, 18);
            ImGui::Text("%s", it.title.c_str());
            ImGui::SameLine();
            if (it.stillPending)
                ImGui::TextColored(kWarn, "overdue %s", formatDuration(it.minutesLate).c_str());
            else
                ImGui::TextColored(kMuted, "done %s late, after this month", formatDuration(it.minutesLate).c_str());
            tip(("Deadline: " + it.deadline).c_str());
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    beginCard("m_late_list", ImVec2(0, listH));
    {
        std::snprintf(heading, sizeof(heading), "Finished late (%d)", r.late);
        cardHeading(IconClock, heading);
        if (r.lateTasks.empty())
            ImGui::TextColored(kMuted, "No late finishes.");
        for (const MonthItem& it : r.lateTasks) {
            pill(("P" + std::to_string(it.priority)).c_str(), priorityColor(it.priority));
            ImGui::SameLine(0, 18);
            ImGui::Text("%s", it.title.c_str());
            ImGui::SameLine();
            ImGui::TextColored(kSoon, "%s late", formatDuration(it.minutesLate).c_str());
            tip(("Deadline: " + it.deadline).c_str());
        }
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------
// Top bar navigation and page titles
// ---------------------------------------------------------------
enum Page { PageHome = 0, PagePlan, PageTasks, PageSchedule, PageMonth };

// One tab in the top bar. The open page gets a warm fill, an amber outline
// and a glowing underline.
bool navTab(const char* id, unsigned icon, const char* label, bool active)
{
    std::string text = withIcon(icon, label);
    ImVec2 ts = ImGui::CalcTextSize(text.c_str());
    float  w  = ts.x + 32.0f, h = ImGui::GetFrameHeight() + 6.0f;
    ImVec2 p  = ImGui::GetCursorScreenPos();
    bool pressed = ImGui::InvisibleButton(id, ImVec2(w, h));
    bool hovered = ImGui::IsItemHovered();

    ImVec2 q(p.x + w, p.y + h);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (active) {
        gradientRect(dl, p, q, ImVec4(0.22f, 0.16f, 0.07f, 1.0f), ImVec4(0.11f, 0.09f, 0.07f, 1.0f),
                     10.0f, p, ImVec2(p.x, q.y));
        dl->AddRect(p, q, rgba(kAccent, 0.50f), 10.0f);
        ImU32 solid = rgba(kAccent, 1.0f), clear = rgba(kAccent, 0.0f);
        float mid = p.x + w * 0.5f;
        dl->AddRectFilledMultiColor(ImVec2(p.x + 8.0f, q.y - 2.0f), ImVec2(mid, q.y), clear, solid, solid, clear);
        dl->AddRectFilledMultiColor(ImVec2(mid, q.y - 2.0f), ImVec2(q.x - 8.0f, q.y), solid, clear, clear, solid);
    } else if (hovered) {
        dl->AddRectFilled(p, q, rgba(ImVec4(1, 1, 1, 1), 0.05f), 10.0f);
    }
    ImVec4 col = (active || hovered) ? ImGui::GetStyle().Colors[ImGuiCol_Text] : kMuted;
    dl->AddText(ImVec2(p.x + 16.0f, p.y + (h - ts.y) * 0.5f), ImGui::GetColorU32(col), text.c_str());
    return pressed;
}

// The bar across the top: the brand, the date and time, the running focus
// timer, your initial, and the page tabs under them.
void drawTopBar(AppState& s, ImFont* large)
{
    const ImGuiStyle& st = ImGui::GetStyle();
    float x0    = ImGui::GetCursorPosX();
    float right = x0 + ImGui::GetContentRegionAvail().x;
    float y0    = ImGui::GetCursorPosY();
    float brandH = large ? large->FontSize : ImGui::GetTextLineHeight();

    if (large) ImGui::PushFont(large);
    if (gIcons) {
        ImGui::TextColored(kAccent, "%s", glyph(IconHourglass).c_str());
        ImGui::SameLine();
    }
    ImGui::Text("PrioriTask");
    if (large) ImGui::PopFont();
    float afterBrand = ImGui::GetCursorPosY();

    // Right side, measured first so it lines up with the window edge.
    std::string dateText = tu::todayString();
    std::string timeText = tu::formatHM(tu::nowMinutes());
    std::string focusText;
    if (s.focusActive) {
        double left = s.focusSeconds - focusElapsed(s);
        int    secs = static_cast<int>(left < 0 ? -left : left);
        char   clock[48];
        std::snprintf(clock, sizeof(clock), "Focus  %s%02d:%02d%s", left < 0 ? "+" : "", secs / 60, secs % 60,
                      s.focusPaused ? "  paused" : "");
        focusText = clock;
    }
    float chipH  = ImGui::GetTextLineHeight() + 2.0f * kChipPad.y;
    float avatar = maxOf(brandH + 8.0f, chipH + 4.0f);
    float gap    = 10.0f;
    float total  = chipWidth(IconCalendar, dateText) + gap + chipWidth(IconClock, timeText) + gap + avatar;
    if (s.focusActive) total += chipWidth(IconHourglass, focusText) + gap;

    float chipY = y0 + (avatar - chipH) * 0.5f;
    ImGui::SetCursorPos(ImVec2(right - total, chipY));
    if (s.focusActive) {
        infoChip(IconHourglass, focusText, kAccent, true);
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("Back to the focus timer");
        }
        if (ImGui::IsItemClicked()) s.page = 0;
        ImGui::SameLine(0, gap);
        ImGui::SetCursorPosY(chipY);
    }
    infoChip(IconCalendar, dateText, ImGui::GetStyle().Colors[ImGuiCol_Text]);
    ImGui::SameLine(0, gap);
    ImGui::SetCursorPosY(chipY);
    infoChip(IconClock, timeText, ImGui::GetStyle().Colors[ImGuiCol_Text]);
    ImGui::SameLine(0, gap);
    ImGui::SetCursorPosY(y0);

    // Your initial in an amber circle (an icon if no name is saved).
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(avatar, avatar));
        ImVec2 c(p.x + avatar * 0.5f, p.y + avatar * 0.5f);
        float  r = avatar * 0.5f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        radialGlow(dl, c, r * 1.8f, kAccent, 0.18f);
        int start = dl->VtxBuffer.Size;
        dl->AddCircleFilled(c, r, IM_COL32_WHITE, 40);
        ImGui::ShadeVertsLinearColorGradientKeepAlpha(dl, start, dl->VtxBuffer.Size,
            ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r),
            ImGui::GetColorU32(ImVec4(1.0f, 0.84f, 0.45f, 1.0f)), ImGui::GetColorU32(ImVec4(0.86f, 0.52f, 0.12f, 1.0f)));
        dl->AddCircle(c, r, rgba(ImVec4(1, 1, 1, 1), 0.35f), 40, 1.0f);
        std::string initial;
        if (!s.userName.empty() && static_cast<unsigned char>(s.userName[0]) < 0x80)
            initial = std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(s.userName[0]))));
        else if (gIcons)
            initial = glyph(IconUser);
        if (!initial.empty()) {
            if (large) ImGui::PushFont(large);
            ImVec2 ts = ImGui::CalcTextSize(initial.c_str());
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), ImGui::GetColorU32(kBtnText), initial.c_str());
            if (large) ImGui::PopFont();
        }
        if (ImGui::IsItemHovered()) {
            if (s.userName.empty()) ImGui::SetTooltip("Add your name in My Schedule > Preferences");
            else                    ImGui::SetTooltip("Hi %s! Change your name in My Schedule", s.userName.c_str());
        }
    }

    // The page tabs.
    ImGui::SetCursorPos(ImVec2(x0, maxOf(afterBrand, y0 + avatar + st.ItemSpacing.y)));
    static const unsigned    icons[5]  = {IconHome, IconList, IconTasks, IconCalendar, IconChart};
    static const char* const labels[5] = {"What Now", "Plan My Day", "Tasks", "My Schedule", "This Month"};
    for (int i = 0; i < 5; ++i) {
        if (i) ImGui::SameLine(0, 6);
        ImGui::PushID(i);
        if (navTab("##tab", icons[i], labels[i], s.page == i)) s.page = i;
        ImGui::PopID();
    }

    // A hairline under the tabs, across the whole window.
    ImVec2 wp = ImGui::GetWindowPos();
    float  y  = ImGui::GetCursorScreenPos().y - st.ItemSpacing.y + 1.0f;
    ImGui::GetWindowDrawList()->AddLine(ImVec2(wp.x, y), ImVec2(wp.x + ImGui::GetWindowWidth(), y),
                                        ImGui::GetColorU32(kEdge));
}

// Faint warm glows behind everything, so the black isn't flat.
void drawBackdrop()
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    ImVec2 p = vp->Pos, sz = vp->Size;
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), ImGui::GetColorU32(kBg));
    radialGlow(dl, ImVec2(p.x + sz.x * 0.85f, p.y + sz.y * 0.05f), sz.x * 0.45f, ImVec4(0.95f, 0.60f, 0.20f, 1.0f), 0.07f);
    radialGlow(dl, ImVec2(p.x + sz.x * 0.05f, p.y + sz.y * 1.00f), sz.x * 0.40f, ImVec4(0.55f, 0.45f, 0.35f, 1.0f), 0.05f);
}

// The heading at the top of every page except Home.
void pageTitle(ImFont* large, const char* title, const char* subtitle)
{
    if (large) ImGui::PushFont(large);
    ImGui::Text("%s", title);
    if (large) ImGui::PopFont();
    ImGui::TextColored(kMuted, "%s", subtitle);
    ImGui::Spacing();
}

// ---------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------

// Looks for the first of `names` in the project's fonts/ folder and the
// system font folders. Returns its full path, or "" if none is installed.
std::string findFont(std::initializer_list<const char*> names)
{
    namespace fs = std::filesystem;
    DynArray<std::string> found(names.size());

    for (const char* dir : {"fonts", "/usr/share/fonts", "/usr/local/share/fonts"}) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            std::string file = it->path().filename().string();
            std::size_t i = 0;
            for (const char* n : names) {
                if (found[i].empty() && file == n) found[i] = it->path().string();
                ++i;
            }
        }
    }
    for (const std::string& f : found) if (!f.empty()) return f;
    return "";
}

// Loads a modern font (Inter) if installed, else DejaVu Sans, and merges the
// icon font into each size. Returns the heading font; `title` gets the largest.
ImFont* loadFonts(ImGuiIO& io, ImFont*& title)
{
    title = nullptr;
    std::string text  = findFont({"Inter-Regular.otf", "Inter-Regular.ttf", "InterVariable.ttf",
                                  "DejaVuSans.ttf"});
    std::string bold  = findFont({"Inter-SemiBold.otf", "Inter-SemiBold.ttf", "Inter-Bold.otf",
                                  "Inter-Bold.ttf", "DejaVuSans-Bold.ttf"});
    std::string icons = findFont({"fontawesome-webfont.ttf", "FontAwesome.ttf"});
    if (bold.empty()) bold = text;

    std::printf("PrioriTask fonts -> text: %s | headings: %s | icons: %s\n",
                text.empty()  ? "built-in" : text.c_str(),
                bold.empty()  ? "built-in" : bold.c_str(),
                icons.empty() ? "none"     : icons.c_str());

    if (text.empty()) {
        io.FontGlobalScale = 1.3f;
        return nullptr;
    }

    static const ImWchar iconRange[] = {0xF000, 0xF2E0, 0};
    auto add = [&](const std::string& path, float size) {
        ImFont* f = io.Fonts->AddFontFromFileTTF(path.c_str(), size);
        if (f && !icons.empty()) {
            ImFontConfig cfg;
            cfg.MergeMode        = true;    // icons go into the same font as the text
            cfg.PixelSnapH       = true;
            cfg.GlyphMinAdvanceX = size;    // every icon the same width, so labels line up
            io.Fonts->AddFontFromFileTTF(icons.c_str(), size * 0.9f, &cfg, iconRange);
            gIcons = true;
        }
        return f;
    };

    add(text, 18.0f);                    // the first font added is the default
    ImFont* large = add(bold, 26.0f);
    title         = add(bold, 34.0f);
    return large;
}

// One frame of the whole app: the backdrop, the top bar, then the open page.
void drawFrame(Database& db, AppState& s, ImFont* large)
{
    drawBackdrop();

    // One window filling the whole app window. Its background is see-through
    // so the warm glows behind it show.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 14));
    ImGui::Begin("PrioriTask", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();

    drawTopBar(s, large);

    // ---- The chosen page under the bar ----
    float statusH = s.status.empty() ? 0.0f : ImGui::GetFrameHeightWithSpacing();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2, 14));
    ImGui::BeginChild("page", ImVec2(0, -statusH), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    switch (s.page) {
        case PagePlan:     drawPlanTab(db, s, large);     break;
        case PageTasks:    drawTasksTab(db, s, large);    break;
        case PageSchedule: drawScheduleTab(db, s, large); break;
        case PageMonth:    drawMonthTab(db, s, large);    break;
        default:
            drawHome(db, s, large);
            break;
    }
    ImGui::EndChild();

    if (!s.status.empty()) ImGui::TextColored(kGood, "%s", withIcon(IconCheck, s.status.c_str()).c_str());

    ImGui::End();
}

} // namespace


int main()
{
    Database db("tasks.db");
    if (!db.isOpen()) {
        std::fprintf(stderr, "Database could not be opened.\n");
        return 1;
    }
    db.createTable();
    db.createDayPlanTable();
    db.createSettingsTable();
    db.createCommitmentsTable();

    AppState s;
    reloadAll(db, s);
    int wake    = savedWakeTime(db);
    s.wakeText  = (wake >= 0) ? tu::formatHM(wake) : "07:00";
    s.wakeSaved = wake;
    db.getSetting("user_name", s.userName);
    s.nameText = s.userName;

    if (!glfwInit()) {
        std::fprintf(stderr, "Could not start GLFW.\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    // Open already maximised, so there is never a smaller window whose
    // shadow the display could leave behind.
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(1280, 800, "PrioriTask", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Could not open a window.\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io    = ImGui::GetIO();
    io.IniFilename = nullptr;            // don't write imgui.ini into the project

    ImGui::StyleColorsDark();
    applyTheme();

    ImFont* title = nullptr;
    ImFont* large = loadFonts(io, title);
    gTitleFont    = title;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        drawFrame(db, s, large);

        ImGui::Render();
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(kBg.x, kBg.y, kBg.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
