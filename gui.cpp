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

// The add/edit form. editingId == 0 means "new task".
struct TaskForm {
    int         editingId = 0;
    std::string title, description, category, deadline;
    int         priority = 3, difficulty = 3, energy = 3;
    int         estimated = 60, minimum = 30;
    bool        mandatory = false;
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
    std::vector<Suggestion> suggestions;
    std::string             wakeText;
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
        a.isMandatory    = t.isMandatory;
        a.isSleep        = tu::isSleepTask(t.title);
        if (!t.deadline.empty())
            a.hasDeadline = tu::minutesUntilDeadline(t.deadline, a.minutesToDeadline);
        items.push_back(a);
    }

    AdviceContext ctx;
    ctx.userEnergy  = s.energy;
    ctx.nowMinutes  = tu::nowMinutes();
    ctx.wakeMinutes = wake;

    s.suggestions     = suggestNext(items, ctx, 3);
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
// "What now" tab
// ---------------------------------------------------------------
void drawNowTab(Database& db, AppState& s, ImFont* large)
{
    ImGui::Text("Today is %s, %s", tu::todayString().c_str(),
                tu::formatHM(tu::nowMinutes()).c_str());

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Wake-up time:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::InputText("##wake", &s.wakeText);
    ImGui::SameLine();
    if (ImGui::Button("Save##wake")) {
        int m = tu::parseHM(s.wakeText);
        if (m < 0) {
            s.wakeMessage = "Use HH:MM, for example 06:30";
        } else {
            s.wakeText = tu::formatHM(m);
            db.setSetting("wake_time", s.wakeText);
            s.wakeMessage     = "Saved.";
            s.haveSuggestions = false;
        }
    }
    if (!s.wakeMessage.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "%s", s.wakeMessage.c_str());
    }
    if (savedWakeTime(db) < 0) {
        ImGui::TextColored(kWarn, "No wake-up time saved yet. Using 07:00 until you save one.");
    }

    std::string busy;
    int freeAt = 0, left = 0;
    if (currentCommitment(s.commitments, tu::nowMinutes(), tu::todayIndex(), busy, freeAt, left)) {
        ImGui::Spacing();
        ImGui::TextColored(kWarn, "You're at %s until %s (%s left).", busy.c_str(),
                           tu::formatHM(freeAt).c_str(), formatDuration(left).c_str());
        ImGui::TextColored(kMuted, "Suggestions below are for when you're free.");
    }

    ImGui::SeparatorText("How do you feel right now?");
    static const char* feel[6] = {"", "drained", "low", "okay", "good", "sharp"};
    ImGui::SetNextItemWidth(260);
    if (ImGui::SliderInt("Energy", &s.energy, 1, 5, "%d", ImGuiSliderFlags_AlwaysClamp))
        s.haveSuggestions = false;
    s.energy = std::clamp(s.energy, 1, 5);
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "(%s)", feel[s.energy]);

    ImGui::Spacing();
    if (ImGui::Button("What should I do now?", ImVec2(280, 40))) computeSuggestions(db, s);

    if (!s.haveSuggestions) return;

    ImGui::Spacing();
    if (s.suggestions.empty()) {
        ImGui::TextColored(kGood, "Nothing pending right now. You're free.");
        return;
    }

    const Suggestion& top = s.suggestions[0];
    ImGui::SeparatorText("Do this now");
    if (large) ImGui::PushFont(large);
    ImGui::TextColored(kAccent, "%s", top.title.c_str());
    if (large) ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "(about %d min)", top.estimatedTime);
    reasonsLine(top);

    if (s.suggestions.size() > 1) {
        ImGui::SeparatorText("After that");
        for (std::size_t i = 1; i < s.suggestions.size(); ++i) {
            ImGui::BulletText("%s  (about %d min)", s.suggestions[i].title.c_str(),
                              s.suggestions[i].estimatedTime);
            ImGui::Indent();
            reasonsLine(s.suggestions[i]);
            ImGui::Unindent();
        }
    }
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
    ImGui::SetNextItemWidth(220);
    ImGui::InputTextWithHint("Deadline", "2026-10-20 18:00", &f.deadline);

    if (!f.error.empty()) ImGui::TextColored(kWarn, "%s", f.error.c_str());

    ImGui::Spacing();
    if (ImGui::Button("Save", ImVec2(120, 0))) {
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

    if (ImGui::Button("Delete", ImVec2(120, 0))) {
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
    if (ImGui::Button("+ Add task")) {
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
    if (ImGui::BeginTable("tasks", 8, flags, ImVec2(0, -ImGui::GetFrameHeightWithSpacing()))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Title",    ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Pri",      ImGuiTableColumnFlags_WidthFixed, 40);
        ImGui::TableSetupColumn("Wants",    ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Floor",    ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Must",     ImGuiTableColumnFlags_WidthFixed, 50);
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

            ImGui::TableNextColumn(); ImGui::Text("%d", t.priority);
            ImGui::TableNextColumn(); ImGui::Text("%d", t.estimatedTime);
            ImGui::TableNextColumn(); ImGui::Text("%d", t.minTime);
            ImGui::TableNextColumn(); ImGui::Text("%s", t.isMandatory ? "yes" : "");

            ImGui::TableNextColumn();
            long mins = 0;
            if (t.deadline.empty())
                ImGui::TextDisabled("-");
            else if (!done && tu::minutesUntilDeadline(t.deadline, mins) && mins < 0)
                ImGui::TextColored(kWarn, "%s", t.deadline.c_str());   // overdue
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
    if (ImGui::Button("Plan my day", ImVec2(280, 40))) computePlan(db, s);
    if (!s.havePlan) return;

    ImGui::Spacing();
    ImGui::Text("Now %s, wake-up %s: %d minutes", tu::formatHM(s.planNow).c_str(),
                tu::formatHM(s.planWake).c_str(), s.planWindow);
    if (s.planBusy > 0) {
        ImGui::SameLine();
        ImGui::TextColored(kMuted, " minus %d for fixed commitments", s.planBusy);
    }
    if (large) ImGui::PushFont(large);
    ImGui::TextColored(kAccent, "%d minutes for tasks (%dh %dm), including sleep",
                       s.planBudget, s.planBudget / 60, s.planBudget % 60);
    if (large) ImGui::PopFont();

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
            ImGui::TableNextColumn(); ImGui::Text("%d", priorityOf(s, a.id));
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
            ImGui::ProgressBar(kept, ImVec2(-FLT_MIN, 0), label);

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
    } else if (ImGui::Button("Save this plan", ImVec2(200, 0))) {
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

    if (ImGui::Button("Add commitment", ImVec2(200, 0))) {
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
ImFont* loadFonts(ImGuiIO& io)
{
    const char* path = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    std::ifstream probe(path);
    if (!probe.good()) {
        io.FontGlobalScale = 1.3f;
        return nullptr;
    }
    io.Fonts->AddFontFromFileTTF(path, 18.0f);          // the first font is the default
    return io.Fonts->AddFontFromFileTTF(path, 26.0f);
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
    s.wakeText = (wake >= 0) ? tu::formatHM(wake) : "07:00";

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
    ImGuiStyle& style  = ImGui::GetStyle();
    style.FrameRounding = 4.0f;
    style.FramePadding  = ImVec2(8, 5);
    style.ItemSpacing   = ImVec2(10, 8);

    ImFont* large = loadFonts(io);

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

        if (large) ImGui::PushFont(large);
        ImGui::TextColored(kAccent, "PrioriTask");
        if (large) ImGui::PopFont();
        ImGui::SameLine();
        ImGui::TextColored(kMuted, "   shrink your day to fit");

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
