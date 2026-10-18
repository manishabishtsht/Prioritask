// PrioriTask - desktop front end (Dear ImGui + GLFW + OpenGL 3).
// Uses the same database, scheduler, advisor and commitments code as the
// CLI. Only the screens are different.

#include "advisor.h"
#include "commitments.h"
#include "database.h"
#include "scheduler.h"
#include "timeutil.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_stdlib.h"     // lets ImGui::InputText edit a std::string
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

const ImVec4 kAccent(0.45f, 0.75f, 1.00f, 1.0f);
const ImVec4 kWarn  (1.00f, 0.75f, 0.30f, 1.0f);
const ImVec4 kGood  (0.45f, 0.85f, 0.55f, 1.0f);
const ImVec4 kMuted (0.60f, 0.60f, 0.65f, 1.0f);
const ImVec4 kSoon  (0.95f, 0.85f, 0.40f, 1.0f);   // due within a day

// ---------------------------------------------------------------
// Look and feel. Everything here changes appearance only.
// ---------------------------------------------------------------
const ImVec4 kBtn      (0.26f, 0.47f, 0.88f, 1.0f);
const ImVec4 kBtnHover (0.36f, 0.57f, 0.96f, 1.0f);
const ImVec4 kBtnActive(0.20f, 0.40f, 0.78f, 1.0f);

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

// Priority 5 is red, 1 is grey, so urgency reads at a glance.
ImVec4 priorityColor(int p)
{
    switch (p) {
        case 5:  return ImVec4(1.00f, 0.47f, 0.45f, 1.0f);
        case 4:  return ImVec4(1.00f, 0.68f, 0.35f, 1.0f);
        case 3:  return ImVec4(0.95f, 0.85f, 0.40f, 1.0f);
        case 2:  return ImVec4(0.55f, 0.75f, 1.00f, 1.0f);
        default: return kMuted;
    }
}

void applyTheme()
{
    ImGuiStyle& st = ImGui::GetStyle();

    st.WindowPadding     = ImVec2(18, 14);
    st.FramePadding      = ImVec2(10, 6);
    st.ItemSpacing       = ImVec2(10, 9);
    st.ItemInnerSpacing  = ImVec2(8, 6);
    st.CellPadding       = ImVec2(8, 6);
    st.WindowRounding    = 0.0f;
    st.ChildRounding     = 10.0f;
    st.ChildBorderSize   = 1.0f;
    st.FrameRounding     = 6.0f;
    st.PopupRounding     = 8.0f;
    st.GrabRounding      = 6.0f;
    st.TabRounding       = 6.0f;
    st.ScrollbarRounding = 8.0f;
    st.WindowBorderSize  = 0.0f;
    st.FrameBorderSize   = 0.0f;
    st.PopupBorderSize   = 1.0f;

    ImVec4* c = st.Colors;
    const ImVec4 bg      (0.07f, 0.08f, 0.11f, 1.0f);
    const ImVec4 panel   (0.11f, 0.12f, 0.16f, 1.0f);
    const ImVec4 frame   (0.15f, 0.17f, 0.22f, 1.0f);
    const ImVec4 frameHov(0.20f, 0.23f, 0.30f, 1.0f);
    const ImVec4 border  (0.22f, 0.25f, 0.31f, 1.0f);

    c[ImGuiCol_Text]              = ImVec4(0.92f, 0.93f, 0.96f, 1.0f);
    c[ImGuiCol_TextDisabled]      = ImVec4(0.50f, 0.53f, 0.60f, 1.0f);
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
    c[ImGuiCol_TableHeaderBg]     = frame;
    c[ImGuiCol_TableBorderStrong] = border;
    c[ImGuiCol_TableBorderLight]  = ImVec4(0.17f, 0.19f, 0.24f, 1.0f);
    c[ImGuiCol_TableRowBg]        = ImVec4(0.00f, 0.00f, 0.00f, 0.0f);
    c[ImGuiCol_TableRowBgAlt]     = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
    c[ImGuiCol_PlotHistogram]     = kBtn;
    c[ImGuiCol_ScrollbarBg]       = bg;
    c[ImGuiCol_ScrollbarGrab]     = frame;
    c[ImGuiCol_ModalWindowDimBg]  = ImVec4(0.00f, 0.00f, 0.00f, 0.55f);
}

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
    std::vector<Task>            tasks;
    std::vector<CommitmentRow>   commitmentRows;   // as stored, for the schedule table
    std::vector<CommitmentBlock> commitments;      // as numbers, for the calculations
    bool showCompleted = false;

    // Plan my day
    bool       havePlan  = false;
    bool       planSaved = false;
    PlanResult plan;
    int        planNow = 0, planWake = 0, planWindow = 0, planBusy = 0, planBudget = 0;

    // My schedule: the add form
    std::string cName, cStart, cEnd, cError;
    bool        cDays[7] = {true, true, true, true, true, false, false};   // Mon-Fri

    int                     energy = 3;
    bool                    haveSuggestions = false;
    AdviceResult            advice;          // the last answer, with its status
    int                     adviceSlot = 0;  // the free slot it was asked for
    int                     slotMinutes = 0; // free slot picked during a commitment
    bool                    slotOutside = false;
    std::string             wakeText;
    int                     wakeSaved = -1;   // minutes, or -1 if none
    std::string             wakeMessage;

    TaskForm form;
    bool     openTaskDialog = false;

    int         deleteId = 0;
    std::string deleteTitle;
    bool        openDeleteDialog = false;

    std::string status;
};

std::vector<CommitmentBlock> toBlocks(const std::vector<CommitmentRow>& rows)
{
    std::vector<CommitmentBlock> out;
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
    f.editingId   = t.id;
    f.title       = t.title;
    f.description = t.description;
    f.category    = t.category;
    f.deadline    = t.deadline;
    f.priority    = t.priority;
    f.difficulty  = t.difficulty;
    f.energy      = t.energyRequired;
    f.estimated   = t.estimatedTime;
    f.minimum     = t.minTime;
    f.mandatory   = t.isMandatory;
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

    std::vector<AdviceItem> items;
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
// Cards: rounded panels that group related things together
// ---------------------------------------------------------------

// Width of each card when n of them share one row.
float cardWidth(int n)
{
    float avail = ImGui::GetContentRegionAvail().x;
    float gap   = ImGui::GetStyle().ItemSpacing.x;
    return (avail - gap * (n - 1)) / n;
}

// A small card with one big number and a short label underneath.
void statCard(const char* id, float width, ImFont* large, const ImVec4& color,
              const std::string& value, const char* label)
{
    ImGui::BeginChild(id, ImVec2(width, 100), ImGuiChildFlags_Borders);
    if (large) ImGui::PushFont(large);
    ImGui::TextColored(color, "%s", value.c_str());
    if (large) ImGui::PopFont();
    ImGui::TextColored(kMuted, "%s", label);
    ImGui::EndChild();
}

// ---------------------------------------------------------------
// "What now" tab: a dashboard
// ---------------------------------------------------------------
void drawNowTab(Database& db, AppState& s, ImFont* large)
{
    // ---- Row of summary cards ----
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

    float w = cardWidth(4);
    statCard("stat_pending", w, large, kAccent, std::to_string(pending), "pending tasks");
    ImGui::SameLine();
    statCard("stat_soon", w, large, dueSoon ? kSoon : kMuted, std::to_string(dueSoon), "due within 24 hours");
    ImGui::SameLine();
    statCard("stat_overdue", w, large, overdue ? kWarn : kMuted, std::to_string(overdue), "overdue");
    ImGui::SameLine();
    statCard("stat_free", w, large, kGood, formatDuration(freeMin), "free until wake-up, incl. sleep");

    ImGui::Spacing();

    // ---- Two panels side by side ----
    float leftW  = ImGui::GetContentRegionAvail().x * 0.40f;
    float height = -ImGui::GetFrameHeightWithSpacing();   // leave room for the status line

    // Left: how you feel
    ImGui::BeginChild("feel_panel", ImVec2(leftW, height), ImGuiChildFlags_Borders);
    {
        if (busy) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kWarn, "You're at %s until %s", busyWith.c_str(), tu::formatHM(freeAt).c_str());
            ImGui::TextColored(kMuted, "%s left. PrioriTask stays quiet unless you have a free slot.",
                               formatDuration(busyLeft).c_str());
            ImGui::PopTextWrapPos();
            ImGui::Spacing();

            // Free-slot picker. Clicking the chosen one again clears it.
            ImGui::TextColored(kMuted, "GOT A FREE SLOT?");
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
            if (ImGui::Checkbox("I'm okay going out (gym, shopping...)", &s.slotOutside))
                s.haveSuggestions = false;
            if (s.slotMinutes == 0)
                ImGui::TextColored(kMuted, "Pick how long you're free to get suggestions.");
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        }

        ImGui::TextColored(kMuted, "HOW DO YOU FEEL RIGHT NOW?");
        static const char* feel[6] = {"", "drained", "low", "okay", "good", "sharp"};
        s.energy = std::clamp(s.energy, 1, 5);
        char fmt[48];
        std::snprintf(fmt, sizeof(fmt), "%%d  -  %s", feel[s.energy]);   // shows "4  -  good"
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::SliderInt("##energy", &s.energy, 1, 5, fmt, ImGuiSliderFlags_AlwaysClamp))
            s.haveSuggestions = false;

        ImGui::Spacing();
        char ask[48];
        if (busy && s.slotMinutes > 0)
            std::snprintf(ask, sizeof(ask), "What can I do in %d min?", s.slotMinutes);
        else
            std::snprintf(ask, sizeof(ask), "What should I do now?");
        if (primaryButton(ask, ImVec2(-FLT_MIN, 46))) computeSuggestions(db, s);

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextColored(kMuted, "WAKE-UP TIME");
        ImGui::SetNextItemWidth(100);
        ImGui::InputText("##wake", &s.wakeText);
        ImGui::SameLine();
        if (ImGui::Button("Save##wake")) {
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
            ImGui::TextColored(kMuted, busy ? "QUIET MODE" : "SUGGESTION");
            ImGui::Spacing();
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
            ImGui::TextColored(kWarn, "QUIET MODE");
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
            ImGui::TextColored(kMuted, s.adviceSlot > 0 ? "DO THIS IN YOUR FREE SLOT" : "DO THIS NOW");
            if (large) ImGui::PushFont(large);
            ImGui::TextColored(kAccent, "%s", top.title.c_str());
            if (large) ImGui::PopFont();
            ImGui::TextColored(kMuted, "about %d minutes", top.estimatedTime);
            reasonsLine(top);

            if (s.advice.suggestions.size() > 1) {
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                ImGui::TextColored(kMuted, "AFTER THAT");
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
    ImGui::TextColored(kAccent, "%s", f.editingId ? "Edit task" : "New task");
    ImGui::Separator();

    ImGui::SetNextItemWidth(380); ImGui::InputText("Title", &f.title);
    ImGui::SetNextItemWidth(380); ImGui::InputText("Description", &f.description);
    ImGui::SetNextItemWidth(380); ImGui::InputText("Category", &f.category);

    ImGui::SetNextItemWidth(220);
    ImGui::SliderInt("Priority (5 = most important)", &f.priority, 1, 5, "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(220);
    ImGui::SliderInt("Difficulty", &f.difficulty, 1, 5, "%d", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(220);
    ImGui::SliderInt("Energy needed", &f.energy, 1, 5, "%d", ImGuiSliderFlags_AlwaysClamp);

    ImGui::SetNextItemWidth(220);
    ImGui::InputInt("Ideal minutes", &f.estimated, 5, 30);
    f.estimated = std::clamp(f.estimated, 1, 1440);
    ImGui::SetNextItemWidth(220);
    ImGui::InputInt("Minimum minutes", &f.minimum, 5, 30);
    f.minimum = std::clamp(f.minimum, 1, f.estimated);   // a floor can't exceed the ideal

    ImGui::Checkbox("Mandatory (never dropped)", &f.mandatory);
    ImGui::Checkbox("Needs me to go out (gym, shopping...)", &f.needsOutside);
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("Deadline", "2026-10-20 18:00", &f.deadline);

    if (!f.error.empty()) ImGui::TextColored(kWarn, "%s", f.error.c_str());

    ImGui::Spacing();
    if (primaryButton("Save", ImVec2(120, 0))) {
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
    if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void drawDeleteDialog(Database& db, AppState& s)
{
    if (!ImGui::BeginPopupModal("Delete task?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::Text("Delete \"%s\" permanently?", s.deleteTitle.c_str());
    ImGui::TextColored(kMuted, "Its saved plans are removed too.");
    ImGui::Spacing();

    if (dangerButton("Delete", ImVec2(120, 0))) {
        if (db.deleteTask(s.deleteId)) s.status = "Deleted: " + s.deleteTitle;
        reloadAll(db, s);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

// ---------------------------------------------------------------
// "Tasks" tab
// ---------------------------------------------------------------
void drawTasksTab(Database& db, AppState& s)
{
    if (primaryButton("+ Add task")) {
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
    ImGui::TextColored(kMuted, "   %d pending, %d minutes requested", pending, wanted);

    // Changing the list while looping over it would break the loop, so
    // changes are flagged here and the list is reloaded afterwards.
    bool changed = false;

    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("tasks", 9, flags, ImVec2(0, -ImGui::GetFrameHeightWithSpacing()))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Title",    ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Pri",      ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Wants",    ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Floor",    ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Must",     ImGuiTableColumnFlags_WidthFixed, 50);
        ImGui::TableSetupColumn("Out",      ImGuiTableColumnFlags_WidthFixed, 45);
        ImGui::TableSetupColumn("Deadline", ImGuiTableColumnFlags_WidthFixed, 160);
        ImGui::TableSetupColumn("Status",   ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("",         ImGuiTableColumnFlags_WidthFixed, 190);
        ImGui::TableHeadersRow();

        for (const Task& t : s.tasks) {
            bool done = (t.status == "Completed");
            if (done && !s.showCompleted) continue;

            ImGui::PushID(t.id);
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            if (done) ImGui::TextDisabled("%s", t.title.c_str());
            else      ImGui::Text("%s", t.title.c_str());

            ImGui::TableNextColumn(); ImGui::TextColored(priorityColor(t.priority), "%d", t.priority);
            ImGui::TableNextColumn(); ImGui::Text("%d", t.estimatedTime);
            ImGui::TableNextColumn(); ImGui::Text("%d", t.minTime);
            ImGui::TableNextColumn(); ImGui::Text("%s", t.isMandatory ? "yes" : "");
            ImGui::TableNextColumn(); ImGui::Text("%s", t.needsOutside ? "yes" : "");

            ImGui::TableNextColumn();
            long mins  = 0;
            bool known = !t.deadline.empty() && tu::minutesUntilDeadline(t.deadline, mins);
            if (t.deadline.empty())
                ImGui::TextDisabled("-");
            else if (!done && known && mins < 0)
                ImGui::TextColored(kWarn, "%s", t.deadline.c_str());   // overdue
            else if (!done && known && mins < 24 * 60)
                ImGui::TextColored(kSoon, "%s", t.deadline.c_str());   // due within a day
            else
                ImGui::Text("%s", t.deadline.c_str());

            ImGui::TableNextColumn(); ImGui::Text("%s", t.status.c_str());

            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Edit")) {
                s.form           = formFromTask(t);
                s.openTaskDialog = true;
            }
            if (!done) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Done")) {
                    if (db.markComplete(t.id)) s.status = "Completed: " + t.title;
                    changed = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete")) {
                s.deleteId         = t.id;
                s.deleteTitle      = t.title;
                s.openDeleteDialog = true;
            }

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

    std::vector<ShrinkItem> items;
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

void drawPlanTab(Database& db, AppState& s, ImFont* large)
{
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kMuted,
        "Fits your pending tasks into the time between now and your next wake-up, "
        "after your fixed commitments. Low-priority tasks are shortened first; if that "
        "is not enough, optional tasks are dropped.");
    ImGui::PopTextWrapPos();

    ImGui::Spacing();
    if (primaryButton("Plan my day", ImVec2(280, 42))) computePlan(db, s);
    if (!s.havePlan) return;

    ImGui::Spacing();
    float w = cardWidth(4);
    std::string wakeLabel = "until wake-up at " + tu::formatHM(s.planWake);
    statCard("plan_window", w, large, kAccent, formatDuration(s.planWindow), wakeLabel.c_str());
    ImGui::SameLine();
    statCard("plan_busy", w, large, kMuted, formatDuration(s.planBusy), "fixed commitments");
    ImGui::SameLine();
    statCard("plan_budget", w, large, kGood, formatDuration(s.planBudget), "available for tasks, incl. sleep");
    ImGui::SameLine();
    statCard("plan_dropped", w, large, s.plan.dropped.empty() ? kMuted : kWarn,
             std::to_string(s.plan.dropped.size()), "tasks dropped");
    ImGui::Spacing();

    const PlanResult& p = s.plan;
    if (p.kept.allocations.empty() && p.dropped.empty()) {
        ImGui::TextColored(kGood, "Nothing pending. Enjoy the free time.");
        return;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("plan", 6, flags)) {
        ImGui::TableSetupColumn("Task",    ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Pri",     ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Wanted",  ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Planned", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Lost",    ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Kept",    ImGuiTableColumnFlags_WidthFixed, 220);
        ImGui::TableHeadersRow();

        int total = 0;
        for (const Allocation& a : p.kept.allocations) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%s", a.title.c_str());
            ImGui::TableNextColumn(); ImGui::TextColored(priorityColor(priorityOf(s, a.id)), "%d", priorityOf(s, a.id));
            ImGui::TableNextColumn(); ImGui::Text("%d", a.requested);
            ImGui::TableNextColumn(); ImGui::Text("%d", a.allocated);
            ImGui::TableNextColumn();
            if (a.lost() > 0) ImGui::TextColored(kWarn, "%d", a.lost());
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
        ImGui::TableNextColumn(); ImGui::TextColored(kAccent, "%d", total);
        ImGui::TableNextColumn();
        ImGui::TableNextColumn(); ImGui::TextColored(kMuted, "of %d available", s.planBudget);

        ImGui::EndTable();
    }

    if (!p.dropped.empty()) {
        ImGui::SeparatorText("Dropped to make the day fit");
        for (const Allocation& a : p.dropped) {
            ImGui::TextColored(kWarn, "  %s", a.title.c_str());
            ImGui::SameLine();
            ImGui::TextColored(kMuted, "(priority %d, %d min) - move it to another day",
                               priorityOf(s, a.id), a.requested);
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
        ImGui::TextColored(kGood, "Saved for %s.", tu::todayString().c_str());
    } else if (primaryButton("Save this plan", ImVec2(200, 0))) {
        // Dropped tasks are saved with 0 minutes, so the history records
        // what was given up that day as well as what was done.
        std::vector<PlanRow> rows;
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
// "My schedule" tab
// ---------------------------------------------------------------
void drawScheduleTab(Database& db, AppState& s)
{
    static const char* kDays[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(kMuted,
        "Fixed commitments are times you are always busy: college, school, office hours, "
        "tuition, a work shift. Plan my day leaves them out, and What now tells you when "
        "you're in one.");
    ImGui::PopTextWrapPos();

    bool changed = false;

    ImGui::SeparatorText("Your commitments");
    if (s.commitmentRows.empty()) {
        ImGui::TextDisabled("None yet. Add one below.");
    } else if (ImGui::BeginTable("schedule", 4,
                   ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Days", ImGuiTableColumnFlags_WidthFixed, 200);
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 220);
        ImGui::TableSetupColumn("",     ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableHeadersRow();

        for (const CommitmentRow& c : s.commitmentRows) {
            ImGui::PushID(c.id);
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%s", c.label.c_str());
            ImGui::TableNextColumn(); ImGui::Text("%s", daysToText(c.days).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%s - %s", c.start.c_str(), c.end.c_str());
            if (tu::parseHM(c.end) <= tu::parseHM(c.start)) {
                ImGui::SameLine();
                ImGui::TextColored(kMuted, "(overnight)");
            }
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("Delete")) {
                if (db.deleteCommitment(c.id)) s.status = "Removed: " + c.label;
                changed = true;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (changed) reloadAll(db, s);

    ImGui::SeparatorText("Add a commitment");

    ImGui::SetNextItemWidth(300);
    ImGui::InputTextWithHint("Name", "College, School, Office, Tuition...", &s.cName);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Days:");
    for (int i = 0; i < 7; ++i) {
        ImGui::SameLine();
        ImGui::Checkbox(kDays[i], &s.cDays[i]);
    }
    ImGui::Text("     ");
    ImGui::SameLine();
    if (ImGui::SmallButton("Weekdays"))  for (int i = 0; i < 7; ++i) s.cDays[i] = (i < 5);
    ImGui::SameLine();
    if (ImGui::SmallButton("Every day")) for (int i = 0; i < 7; ++i) s.cDays[i] = true;
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear"))     for (int i = 0; i < 7; ++i) s.cDays[i] = false;

    ImGui::SetNextItemWidth(90);
    ImGui::InputTextWithHint("Starts", "09:00", &s.cStart);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::InputTextWithHint("Ends", "16:00", &s.cEnd);

    int startMin = tu::parseHM(s.cStart);
    int endMin   = tu::parseHM(s.cEnd);
    if (startMin >= 0 && endMin >= 0 && endMin < startMin) {
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "runs past midnight");
    }

    if (!s.cError.empty()) ImGui::TextColored(kWarn, "%s", s.cError.c_str());

    if (primaryButton("Add commitment", ImVec2(200, 0))) {
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
}

// Loads a real font if one is installed; otherwise scales the built-in one.
// Returns a larger font for headings, or nullptr if none was loaded.
ImFont* loadFonts(ImGuiIO& io, ImFont*& title)
{
    title = nullptr;
    const char* path = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    std::ifstream probe(path);
    if (!probe.good()) {
        io.FontGlobalScale = 1.3f;
        return nullptr;
    }
    io.Fonts->AddFontFromFileTTF(path, 18.0f);          // the first font is the default
    ImFont* large = io.Fonts->AddFontFromFileTTF(path, 26.0f);
    title         = io.Fonts->AddFontFromFileTTF(path, 34.0f);
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
    int wake   = savedWakeTime(db);
    s.wakeText  = (wake >= 0) ? tu::formatHM(wake) : "07:00";
    s.wakeSaved = wake;

    if (!glfwInit()) {
        std::fprintf(stderr, "Could not start GLFW.\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window = glfwCreateWindow(1150, 720, "PrioriTask", nullptr, nullptr);
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

        if (title) ImGui::PushFont(title);
        ImGui::TextColored(kAccent, "PrioriTask");
        if (title) ImGui::PopFont();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(kMuted, "  shrink your day to fit");

        std::string clock = tu::todayString() + "   " + tu::formatHM(tu::nowMinutes());
        ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(clock.c_str()).x - 24);
        ImGui::TextColored(kMuted, "%s", clock.c_str());
        ImGui::Spacing();

        if (ImGui::BeginTabBar("tabs")) {
            if (ImGui::BeginTabItem("What now"))    { drawNowTab(db, s, large);  ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Plan my day")) { drawPlanTab(db, s, large); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Tasks"))       { drawTasksTab(db, s);       ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("My schedule")) { drawScheduleTab(db, s);    ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }

        if (!s.status.empty()) ImGui::TextColored(kGood, "%s", s.status.c_str());

        ImGui::End();

        ImGui::Render();
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
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
