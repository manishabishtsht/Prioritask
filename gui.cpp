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
#include <GLFW/glfw3.h>

#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <string>

namespace {

// ---------------------------------------------------------------
// Colours
// ---------------------------------------------------------------
const ImVec4 kAccent(0.45f, 0.75f, 1.00f, 1.0f);
const ImVec4 kWarn  (1.00f, 0.62f, 0.30f, 1.0f);   // overdue
const ImVec4 kGood  (0.42f, 0.85f, 0.55f, 1.0f);
const ImVec4 kMuted (0.58f, 0.61f, 0.68f, 1.0f);
const ImVec4 kSoon  (0.96f, 0.82f, 0.36f, 1.0f);   // due within a day
const ImVec4 kDanger(1.00f, 0.47f, 0.45f, 1.0f);

const ImVec4 kBtn      (0.26f, 0.47f, 0.88f, 1.0f);
const ImVec4 kBtnHover (0.36f, 0.57f, 0.96f, 1.0f);
const ImVec4 kBtnActive(0.20f, 0.40f, 0.78f, 1.0f);

// ---------------------------------------------------------------
// Icons, from the Font Awesome 4 font (Ubuntu package fonts-font-awesome).
// If that font isn't installed, icons quietly disappear and every label
// still reads correctly.
// ---------------------------------------------------------------
bool gIcons = false;

enum Icon : unsigned {
    IconClock    = 0xF017, IconHourglass = 0xF252, IconTasks   = 0xF0AE,
    IconCalendar = 0xF073, IconPlus      = 0xF067, IconPencil  = 0xF040,
    IconCheck    = 0xF00C, IconTrash     = 0xF1F8, IconCoffee  = 0xF0F4,
    IconMarker   = 0xF041, IconBolt      = 0xF0E7, IconWarning = 0xF071,
    IconBed      = 0xF236, IconRefresh   = 0xF021, IconSave    = 0xF0C7,
    IconChart    = 0xF080, IconLeft      = 0xF053, IconRight   = 0xF054
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

// A solid blue button for the main action on each screen.
bool primaryButton(const char* label, const ImVec2& size = ImVec2(0, 0))
{
    ImGui::PushStyleColor(ImGuiCol_Button,        kBtn);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kBtnHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  kBtnActive);
    bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
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
    st.ChildRounding     = 12.0f;
    st.ChildBorderSize   = 1.0f;
    st.FrameRounding     = 8.0f;
    st.PopupRounding     = 10.0f;
    st.GrabRounding      = 8.0f;
    st.TabRounding       = 8.0f;
    st.ScrollbarRounding = 8.0f;
    st.WindowBorderSize  = 0.0f;
    st.FrameBorderSize   = 0.0f;
    st.PopupBorderSize   = 1.0f;

    ImVec4* c = st.Colors;
    const ImVec4 bg      (0.07f, 0.08f, 0.11f, 1.0f);
    const ImVec4 panel   (0.11f, 0.12f, 0.16f, 1.0f);
    const ImVec4 frame   (0.16f, 0.18f, 0.23f, 1.0f);
    const ImVec4 frameHov(0.21f, 0.24f, 0.31f, 1.0f);
    const ImVec4 border  (0.21f, 0.24f, 0.30f, 1.0f);

    c[ImGuiCol_Text]              = ImVec4(0.92f, 0.93f, 0.96f, 1.0f);
    c[ImGuiCol_TextDisabled]      = ImVec4(0.48f, 0.51f, 0.58f, 1.0f);
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
    c[ImGuiCol_TabHovered]        = kBtnHover;
    c[ImGuiCol_TabSelected]       = kBtn;
    c[ImGuiCol_TableHeaderBg]     = panel;
    c[ImGuiCol_TableBorderStrong] = border;
    c[ImGuiCol_TableBorderLight]  = ImVec4(0.16f, 0.18f, 0.23f, 1.0f);
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
    int          adviceSlot = 0;   // the free slot it was asked for
    int          slotMinutes = 0;  // free slot picked during a commitment
    bool         slotOutside = false;
    std::string  wakeText;
    int          wakeSaved = -1;   // minutes, or -1 if none
    std::string  wakeMessage;

    // This month
    int          monthYear = 0, monthNum = 0;   // the month on screen; 0 = not chosen yet
    bool         haveMonth = false;
    MonthReport  month;

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

    s.advice          = adviseNow(items, ctx, 3);
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

// A summary card: one big number, a label, and a coloured stripe down the
// left edge in the same colour.
void statCard(const char* id, float width, ImFont* large, const ImVec4& color,
              const std::string& value, const std::string& label)
{
    ImGui::BeginChild(id, ImVec2(width, 104), ImGuiChildFlags_Borders);

    ImVec2 p  = ImGui::GetWindowPos();
    ImVec2 sz = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p, ImVec2(p.x + sz.x, p.y + sz.y), false);   // the stripe sits in the padding
    dl->AddRectFilled(ImVec2(p.x + 10, p.y + 18), ImVec2(p.x + 14, p.y + sz.y - 18),
                      ImGui::GetColorU32(color), 2.0f);
    dl->PopClipRect();

    ImGui::Indent(8);
    if (large) ImGui::PushFont(large);
    ImGui::TextColored(color, "%s", value.c_str());
    if (large) ImGui::PopFont();
    ImGui::TextColored(kMuted, "%s", label.c_str());
    ImGui::Unindent(8);

    ImGui::EndChild();
}

// A muted, upper-case heading at the top of a panel.
void panelHeading(unsigned cp, const char* text)
{
    ImGui::TextColored(kMuted, "%s", withIcon(cp, text).c_str());
    ImGui::Spacing();
}

void reasonsLine(const Suggestion& sg)
{
    std::string r;
    for (std::size_t i = 0; i < sg.reasons.size(); ++i) {
        if (i) r += ", ";
        r += sg.reasons[i];
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kMuted, "because: %s", r.c_str());
    ImGui::PopTextWrapPos();
}

// ---------------------------------------------------------------
// "What now" tab: a dashboard
// ---------------------------------------------------------------
void drawNowTab(Database& db, AppState& s, ImFont* large)
{
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

    // ---- Row of summary cards ----
    float w = cardWidth(4);
    statCard("stat_pending", w, large, kAccent, std::to_string(pending),
             withIcon(IconTasks, "pending tasks"));
    ImGui::SameLine();
    statCard("stat_soon", w, large, dueSoon ? kSoon : kMuted, std::to_string(dueSoon),
             withIcon(IconClock, "due within 24 hours"));
    ImGui::SameLine();
    statCard("stat_overdue", w, large, overdue ? kWarn : kMuted, std::to_string(overdue),
             withIcon(IconWarning, "overdue"));
    ImGui::SameLine();
    statCard("stat_free", w, large, kGood, formatDuration(freeMin),
             withIcon(IconBed, "free until wake-up, incl. sleep"));

    ImGui::Spacing();

    // ---- Two panels side by side ----
    float leftW  = ImGui::GetContentRegionAvail().x * 0.40f;
    float height = -ImGui::GetFrameHeightWithSpacing();   // leave room for the status line

    // Left: how you feel
    ImGui::BeginChild("feel_panel", ImVec2(leftW, height), ImGuiChildFlags_Borders);
    {
        if (busy) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kWarn, "%s",
                withIcon(IconCalendar, ("You're at " + busyWith + " until " + tu::formatHM(freeAt)).c_str()).c_str());
            ImGui::TextColored(kMuted, "%s left. PrioriTask stays quiet unless you have a free slot.",
                               formatDuration(busyLeft).c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();

            // Free-slot picker. Clicking the chosen one again clears it.
            panelHeading(IconCoffee, "GOT A FREE SLOT?");
            static const int choices[5] = {15, 30, 45, 60, 90};
            for (int i = 0; i < 5; ++i) {
                if (i) ImGui::SameLine();
                char lbl[16];
                std::snprintf(lbl, sizeof(lbl), "%d min", choices[i]);
                bool on = (s.slotMinutes == choices[i]);
                if (on ? primaryButton(lbl) : ImGui::Button(lbl)) {
                    s.slotMinutes     = on ? 0 : choices[i];
                    s.haveSuggestions = false;
                }
            }
            if (ImGui::Checkbox(withIcon(IconMarker, "I'm okay going out (gym, shopping...)").c_str(),
                                &s.slotOutside))
                s.haveSuggestions = false;
            if (s.slotMinutes == 0)
                ImGui::TextColored(kMuted, "Pick how long you're free to get suggestions.");
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        }

        panelHeading(IconBolt, "HOW DO YOU FEEL RIGHT NOW?");
        static const char* feel[6] = {"", "drained", "low", "okay", "good", "sharp"};
        s.energy = clampTo(s.energy, 1, 5);
        char fmt[48];
        std::snprintf(fmt, sizeof(fmt), "%%d  -  %s", feel[s.energy]);   // shows "4  -  good"
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderInt("##energy", &s.energy, 1, 5, fmt, ImGuiSliderFlags_AlwaysClamp))
            s.haveSuggestions = false;

        ImGui::Spacing();
        std::string ask = (busy && s.slotMinutes > 0)
            ? "What can I do in " + std::to_string(s.slotMinutes) + " min?"
            : std::string("What should I do now?");
        if (primaryButton(withIcon(IconBolt, ask.c_str()).c_str(), ImVec2(-FLT_MIN, 48)))
            computeSuggestions(db, s);

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        panelHeading(IconBed, "WAKE-UP TIME");
        ImGui::SetNextItemWidth(110);
        ImGui::InputText("##wake", &s.wakeText);
        ImGui::SameLine();
        if (ImGui::Button(withIcon(IconSave, "Save").c_str())) {
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
        if (!s.wakeMessage.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(kMuted, "%s", s.wakeMessage.c_str());
        }
        ImGui::PushTextWrapPos(0.0f);
        if (s.wakeSaved < 0)
            ImGui::TextColored(kWarn, "No wake-up time saved yet, so 07:00 is assumed.");
        ImGui::TextColored(kMuted, "Plan my day uses this to work out how much time you have left.");
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right: the suggestion
    ImGui::BeginChild("advice_panel", ImVec2(0, height), ImGuiChildFlags_Borders);
    {
        // Lines saying what was left out, so nothing disappears silently.
        auto hiddenNotes = [&s]() {
            if (s.advice.hiddenOutside > 0)
                ImGui::TextColored(kMuted, "%d task(s) hidden: they need going out", s.advice.hiddenOutside);
            if (s.advice.hiddenTooLong > 0)
                ImGui::TextColored(kMuted, "%d task(s) hidden: too long for this slot", s.advice.hiddenTooLong);
        };

        if (!s.haveSuggestions) {
            panelHeading(IconHourglass, busy ? "QUIET MODE" : "SUGGESTION");
            ImGui::PushTextWrapPos(0.0f);
            if (busy)
                ImGui::TextColored(kMuted,
                    "You're at %s, so nothing is suggested. If you have a free slot, pick how "
                    "long on the left, and PrioriTask will suggest something that fits.", busyWith.c_str());
            else
                ImGui::TextColored(kMuted,
                    "Set your energy on the left and press the button. PrioriTask will pick "
                    "the best task for right now and explain why.");
            ImGui::PopTextWrapPos();
        } else if (s.advice.status == AdviceStatus::QuietBusy) {
            ImGui::TextColored(kWarn, "%s", withIcon(IconHourglass, "QUIET MODE").c_str());
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kMuted,
                "You're at %s, so nothing is suggested. Pick a free slot on the left first.",
                busyWith.c_str());
            ImGui::PopTextWrapPos();
        } else if (s.advice.status == AdviceStatus::NothingFits) {
            if (s.adviceSlot > 0) ImGui::TextColored(kWarn, "Nothing fits in %d minutes.", s.adviceSlot);
            else                  ImGui::TextColored(kGood, "Nothing pending right now. You're free.");
            hiddenNotes();
        } else {
            const Suggestion& top = s.advice.suggestions[0];
            panelHeading(IconCheck, s.adviceSlot > 0 ? "DO THIS IN YOUR FREE SLOT" : "DO THIS NOW");
            if (large) ImGui::PushFont(large);
            ImGui::TextColored(kAccent, "%s", top.title.c_str());
            if (large) ImGui::PopFont();
            pill(("P" + std::to_string(top.priority)).c_str(), priorityColor(top.priority));
            ImGui::SameLine(0, 18);
            ImGui::TextColored(kMuted, "%s", withIcon(IconClock,
                ("about " + std::to_string(top.estimatedTime) + " minutes").c_str()).c_str());
            reasonsLine(top);

            if (s.advice.suggestions.size() > 1) {
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                panelHeading(IconTasks, "AFTER THAT");
                for (std::size_t i = 1; i < s.advice.suggestions.size(); ++i) {
                    const Suggestion& sg = s.advice.suggestions[i];
                    ImGui::Text("%d.  %s", static_cast<int>(i) + 1, sg.title.c_str());
                    ImGui::SameLine();
                    ImGui::TextColored(kMuted, "(about %d min)", sg.estimatedTime);
                    ImGui::Indent();
                    reasonsLine(sg);
                    ImGui::Unindent();
                }
            }
            ImGui::Spacing();
            hiddenNotes();
        }
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
// "Tasks" tab
// ---------------------------------------------------------------
void drawTasksTab(Database& db, AppState& s)
{
    if (primaryButton(withIcon(IconPlus, "Add task").c_str())) {
        s.form           = TaskForm();
        s.openTaskDialog = true;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Show completed", &s.showCompleted);

    int pending = 0, wanted = 0;
    for (const Task& t : s.tasks) {
        if (t.status == "Completed") continue;
        ++pending;
        wanted += t.estimatedTime;
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kMuted, "   %d pending  -  %s requested", pending, formatDuration(wanted).c_str());

    // Changing the list while looping over it would break the loop, so
    // changes are flagged here and the list is reloaded afterwards.
    bool changed = false;

    ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("tasks", 7, flags, ImVec2(0, -ImGui::GetFrameHeightWithSpacing()))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Task",     ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Time",     ImGuiTableColumnFlags_WidthFixed, 170);
        ImGui::TableSetupColumn("Tags",     ImGuiTableColumnFlags_WidthFixed, 140);
        ImGui::TableSetupColumn("Deadline", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("Status",   ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableSetupColumn("",         ImGuiTableColumnFlags_WidthFixed, 130);
        ImGui::TableHeadersRow();

        for (const Task& t : s.tasks) {
            bool done = (t.status == "Completed");
            if (done && !s.showCompleted) continue;

            ImGui::PushID(t.id);
            ImGui::TableNextRow();

            // Task name, with its category underneath in a quieter colour.
            ImGui::TableNextColumn();
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
            if (t.isMandatory) pill("must", kAccent);
            if (t.isMandatory && t.needsOutside) ImGui::SameLine(0, 18);
            if (t.needsOutside) pill("out", kSoon);

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
void drawPlanTab(Database& db, AppState& s, ImFont* large)
{
    // Work the plan out as soon as the tab is opened, so it's never empty.
    if (!s.havePlan) computePlan(db, s);

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(kMuted,
        "Your pending tasks, fitted into the time left before you wake up, after fixed commitments.");
    ImGui::SameLine(ImGui::GetWindowWidth() - 190);
    if (ImGui::Button(withIcon(IconRefresh, "Recalculate").c_str(), ImVec2(160, 0))) computePlan(db, s);
    ImGui::Spacing();

    float w = cardWidth(4);
    statCard("plan_window", w, large, kAccent, formatDuration(s.planWindow),
             withIcon(IconBed, ("until wake-up at " + tu::formatHM(s.planWake)).c_str()));
    ImGui::SameLine();
    statCard("plan_busy", w, large, kMuted, formatDuration(s.planBusy),
             withIcon(IconCalendar, "fixed commitments"));
    ImGui::SameLine();
    statCard("plan_budget", w, large, kGood, formatDuration(s.planBudget),
             withIcon(IconHourglass, "available for tasks, incl. sleep"));
    ImGui::SameLine();
    statCard("plan_dropped", w, large, s.plan.dropped.empty() ? kMuted : kWarn,
             std::to_string(s.plan.dropped.size()), withIcon(IconWarning, "tasks dropped"));
    ImGui::Spacing();

    const PlanResult& p = s.plan;
    if (p.kept.allocations.empty() && p.dropped.empty()) {
        ImGui::TextColored(kGood, "Nothing pending. Enjoy the free time.");
        return;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("plan", 6, flags)) {
        ImGui::TableSetupColumn("Task",     ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Priority", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Wanted",   ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Planned",  ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Lost",     ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Kept",     ImGuiTableColumnFlags_WidthFixed, 260);
        ImGui::TableHeadersRow();

        int total = 0;
        for (const Allocation& a : p.kept.allocations) {
            int pri = priorityOf(s, a.id);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%s", a.title.c_str());
            ImGui::TableNextColumn(); pill(("P" + std::to_string(pri)).c_str(), priorityColor(pri));
            ImGui::TableNextColumn(); ImGui::Text("%d min", a.requested);
            ImGui::TableNextColumn(); ImGui::Text("%d min", a.allocated);
            ImGui::TableNextColumn();
            if (a.lost() > 0) ImGui::TextColored(kWarn, "-%d", a.lost());
            else              ImGui::TextDisabled("0");

            // How much of the task survived, as a bar.
            ImGui::TableNextColumn();
            float kept = a.requested > 0 ? static_cast<float>(a.allocated) / a.requested : 1.0f;
            char label[16];
            std::snprintf(label, sizeof(label), "%d%%", static_cast<int>(kept * 100 + 0.5f));
            ImVec4 bar = kept >= 0.80f ? ImVec4(0.27f, 0.62f, 0.40f, 1.0f)    // kept most of it
                       : kept >= 0.50f ? ImVec4(0.80f, 0.58f, 0.18f, 1.0f)    // squeezed
                       :                 ImVec4(0.80f, 0.34f, 0.32f, 1.0f);   // lost over half
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, bar);
            ImGui::ProgressBar(kept, ImVec2(-FLT_MIN, 0), label);
            ImGui::PopStyleColor();

            total += a.allocated;
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn(); ImGui::TextColored(kAccent, "Total");
        ImGui::TableNextColumn();
        ImGui::TableNextColumn();
        ImGui::TableNextColumn(); ImGui::TextColored(kAccent, "%d min", total);
        ImGui::TableNextColumn();
        ImGui::TableNextColumn(); ImGui::TextColored(kMuted, "of %d available", s.planBudget);

        ImGui::EndTable();
    }

    if (!p.dropped.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Dropped to make the day fit");
        for (const Allocation& a : p.dropped) {
            int pri = priorityOf(s, a.id);
            pill(("P" + std::to_string(pri)).c_str(), priorityColor(pri));
            ImGui::SameLine(0, 18);
            ImGui::TextColored(kWarn, "%s", a.title.c_str());
            ImGui::SameLine();
            ImGui::TextColored(kMuted, "(%d min) - move it to another day", a.requested);
        }
    }

    ImGui::Spacing();
    if (p.kept.deficit > 0)
        ImGui::TextColored(kMuted, "The remaining tasks were shortened by %d minutes in total.",
                           p.kept.deficit);
    else if (p.dropped.empty())
        ImGui::TextColored(kGood, "Everything fits at full length.");

    if (!p.feasible) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kWarn,
            "This day still doesn't fit. Even your mandatory tasks at their minimums are "
            "%d minutes over, and nothing optional is left to drop. Consider marking a task "
            "as not mandatory, or moving it to another day.", p.shortfall);
        ImGui::PopTextWrapPos();
        return;
    }

    ImGui::Spacing();
    if (s.planSaved) {
        ImGui::TextColored(kGood, "%s", withIcon(IconCheck, ("Saved for " + tu::todayString() + ".").c_str()).c_str());
    } else if (primaryButton(withIcon(IconSave, "Save this plan").c_str(), ImVec2(200, 0))) {
        // Dropped tasks are saved with 0 minutes, so the history records
        // what was given up that day as well as what was done.
        DynArray<PlanRow> rows;
        for (const Allocation& a : p.kept.allocations) rows.push_back({a.id, a.requested, a.allocated});
        for (const Allocation& a : p.dropped)          rows.push_back({a.id, a.requested, 0});

        if (db.savePlan(tu::todayString(), rows)) {
            s.planSaved = true;
            s.status    = "Plan saved for " + tu::todayString();
        } else {
            s.status = "Could not save the plan. Details are in the terminal.";
        }
    }
}

// ---------------------------------------------------------------
// "My schedule" tab: two cards side by side
// ---------------------------------------------------------------
void drawScheduleTab(Database& db, AppState& s)
{
    static const char* kDays[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

    ImGui::TextColored(kMuted,
        "Times you are always busy: college, school, office hours, tuition, a work shift.");
    ImGui::Spacing();

    float height = -ImGui::GetFrameHeightWithSpacing();
    float leftW  = ImGui::GetContentRegionAvail().x * 0.55f;
    bool  changed = false;

    // Left card: the list
    ImGui::BeginChild("schedule_list", ImVec2(leftW, height), ImGuiChildFlags_Borders);
    {
        panelHeading(IconCalendar, "YOUR COMMITMENTS");
        if (s.commitmentRows.empty()) {
            ImGui::TextDisabled("None yet. Add one on the right.");
        } else if (ImGui::BeginTable("schedule", 4,
                       ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Days", ImGuiTableColumnFlags_WidthFixed, 150);
            ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 190);
            ImGui::TableSetupColumn("",     ImGuiTableColumnFlags_WidthFixed, 50);
            ImGui::TableHeadersRow();

            for (const CommitmentRow& c : s.commitmentRows) {
                ImGui::PushID(c.id);
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%s", c.label.c_str());
                ImGui::TableNextColumn(); pill(daysToText(c.days).c_str(), kAccent);
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
    }
    ImGui::EndChild();

    if (changed) reloadAll(db, s);

    ImGui::SameLine();

    // Right card: the add form, with labels on the left
    ImGui::BeginChild("schedule_add", ImVec2(0, height), ImGuiChildFlags_Borders);
    {
        const float lw = 90.0f;
        panelHeading(IconPlus, "ADD A COMMITMENT");

        fieldLabel("Name", lw);
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##cname", "College, School, Office, Tuition...", &s.cName);

        fieldLabel("Days", lw);
        for (int i = 0; i < 7; ++i) {
            if (i) ImGui::SameLine();
            ImGui::Checkbox(kDays[i], &s.cDays[i]);
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

        int startMin = tu::parseHM(s.cStart);
        int endMin   = tu::parseHM(s.cEnd);
        if (startMin >= 0 && endMin >= 0 && endMin < startMin) {
            ImGui::SameLine();
            ImGui::TextColored(kMuted, "runs past midnight");
        }

        if (!s.cError.empty()) ImGui::TextColored(kWarn, "%s", s.cError.c_str());

        ImGui::Spacing();
        if (primaryButton(withIcon(IconPlus, "Add commitment").c_str(), ImVec2(-FLT_MIN, 0))) {
            std::string mask;
            for (int i = 0; i < 7; ++i) mask += s.cDays[i] ? '1' : '0';

            s.cError.clear();
            if (s.cName.empty())                     s.cError = "Give it a name.";
            else if (mask == "0000000")              s.cError = "Tick at least one day.";
            else if (startMin < 0 || endMin < 0)     s.cError = "Times must be HH:MM, for example 09:00.";
            else if (startMin == endMin)             s.cError = "Start and end can't be the same.";

            if (s.cError.empty()) {
                CommitmentRow c;
                c.label = s.cName;
                c.days  = mask;
                c.start = tu::formatHM(startMin);
                c.end   = tu::formatHM(endMin);
                if (db.addCommitment(c)) {
                    s.status = "Added: " + c.label + ", " + daysToText(mask) + ", " +
                               c.start + "-" + c.end;
                    s.cName.clear(); s.cStart.clear(); s.cEnd.clear();
                    reloadAll(db, s);
                } else {
                    s.cError = "Could not save. Details are in the terminal.";
                }
            }
        }

        ImGui::Spacing();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(kMuted,
            "Plan my day leaves these times out, and What now stays quiet during them "
            "unless you say you have a free slot.");
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
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

void drawMonthTab(Database& db, AppState& s, ImFont* large)
{
    static const char* kMonths[12] = {"January", "February", "March", "April", "May", "June", "July",
                                      "August", "September", "October", "November", "December"};
    if (!s.haveMonth) computeMonth(db, s);
    const MonthReport& r = s.month;

    int curY = 0, curM = 0;
    std::sscanf(tu::todayString().c_str(), "%d-%d", &curY, &curM);
    bool atCurrent = (r.year == curY && r.month == curM);

    // ---- Month picker: no going past the current month ----
    if (ImGui::Button((iconOr(IconLeft, "<") + "##prevmonth").c_str())) {
        if (--s.monthNum == 0) { s.monthNum = 12; --s.monthYear; }
        computeMonth(db, s);
    }
    ImGui::SameLine();
    if (large) ImGui::PushFont(large);
    ImGui::Text("%s %d", kMonths[r.month - 1], r.year);
    if (large) ImGui::PopFont();
    ImGui::SameLine();
    ImGui::BeginDisabled(atCurrent);
    if (ImGui::Button((iconOr(IconRight, ">") + "##nextmonth").c_str())) {
        if (++s.monthNum == 13) { s.monthNum = 1; ++s.monthYear; }
        computeMonth(db, s);
    }
    ImGui::EndDisabled();
    if (atCurrent) {
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "  so far this month");
    }
    ImGui::Spacing();

    // ---- Summary cards ----
    float w = cardWidth(4);
    statCard("m_done", w, large, kAccent, std::to_string(r.completed),
             withIcon(IconCheck, "tasks completed"));
    ImGui::SameLine();
    ImVec4 rateColor = r.onTimeRate < 0 ? kMuted : r.onTimeRate >= 80 ? kGood : r.onTimeRate >= 50 ? kSoon : kWarn;
    statCard("m_rate", w, large, rateColor,
             r.onTimeRate < 0 ? std::string("-") : std::to_string(r.onTimeRate) + "%",
             withIcon(IconClock, "on-time rate"));
    ImGui::SameLine();
    statCard("m_undone", w, large, r.undone ? kWarn : kMuted, std::to_string(r.undone),
             withIcon(IconWarning, "left undone"));
    ImGui::SameLine();
    statCard("m_planned", w, large, kAccent,
             std::to_string(r.daysPlanned) + " / " + std::to_string(r.daysInMonth),
             withIcon(IconCalendar, "days with a saved plan"));
    ImGui::Spacing();

    // ---- Bar chart: tasks completed on each day ----
    ImGui::BeginChild("m_chart", ImVec2(0, 205), ImGuiChildFlags_Borders);
    {
        panelHeading(IconChart, "TASKS COMPLETED EACH DAY");
        DynArray<float> perDay;
        int most = 1;
        for (int c : r.completedPerDay) {
            perDay.push_back(static_cast<float>(c));
            most = maxOf(most, c);
        }
        char overlay[64];
        if (r.completed == 0)
            std::snprintf(overlay, sizeof(overlay), "no tasks completed yet");
        else
            std::snprintf(overlay, sizeof(overlay), "%d on time, %d late", r.onTime, r.late);
        ImGui::PlotHistogram("##perday", perDay.begin(), static_cast<int>(perDay.size()), 0, overlay,
                             0.0f, static_cast<float>(most), ImVec2(-FLT_MIN, 100));
        ImGui::TextColored(kMuted, "day 1");
        ImGui::SameLine(ImGui::GetWindowWidth() - 90);
        ImGui::TextColored(kMuted, "day %d", r.daysInMonth);
        ImGui::TextColored(kMuted, "On-time rate = on time / (on time + late + left undone)");
    }
    ImGui::EndChild();
    ImGui::Spacing();

    // ---- Two lists: left undone, and finished late ----
    float half   = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    float height = -ImGui::GetFrameHeightWithSpacing();
    char heading[64];

    ImGui::BeginChild("m_undone_list", ImVec2(half, height), ImGuiChildFlags_Borders);
    {
        std::snprintf(heading, sizeof(heading), "LEFT UNDONE (%d)", r.undone);
        panelHeading(IconWarning, heading);
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

    ImGui::BeginChild("m_late_list", ImVec2(0, height), ImGuiChildFlags_Borders);
    {
        std::snprintf(heading, sizeof(heading), "FINISHED LATE (%d)", r.late);
        panelHeading(IconClock, heading);
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

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // One window filling the whole app window.
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("PrioriTask", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings);

        // ---- Header ----
        if (title) ImGui::PushFont(title);
        ImGui::TextColored(kAccent, "%s", withIcon(IconHourglass, "PrioriTask").c_str());
        if (title) ImGui::PopFont();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kMuted, "  shrink your day to fit");

        std::string clock = withIcon(IconCalendar, tu::todayString().c_str()) + "     " +
                            withIcon(IconClock, tu::formatHM(tu::nowMinutes()).c_str());
        ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(clock.c_str()).x - 28);
        ImGui::TextColored(kMuted, "%s", clock.c_str());
        ImGui::Spacing();

        // ---- Tabs. "###id" keeps each tab's identity the same with or without icons. ----
        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem((withIcon(IconBolt, "What now") + "###now").c_str())) {
                drawNowTab(db, s, large);  ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem((withIcon(IconHourglass, "Plan my day") + "###plan").c_str())) {
                drawPlanTab(db, s, large); ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem((withIcon(IconTasks, "Tasks") + "###tasks").c_str())) {
                drawTasksTab(db, s);       ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem((withIcon(IconCalendar, "My schedule") + "###schedule").c_str())) {
                drawScheduleTab(db, s);    ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem((withIcon(IconChart, "This month") + "###month").c_str())) {
                drawMonthTab(db, s, large); ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if (!s.status.empty()) ImGui::TextColored(kGood, "%s", s.status.c_str());

        ImGui::End();

        ImGui::Render();
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.07f, 0.08f, 0.11f, 1.0f);
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
