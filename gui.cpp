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
    std::vector<CommitmentBlock> commitments;
    bool showCompleted = false;

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

std::vector<CommitmentBlock> loadCommitments(Database& db)
{
    std::vector<CommitmentBlock> out;
    for (const CommitmentRow& r : db.getCommitments()) {
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
    s.commitments     = loadCommitments(db);
    s.haveSuggestions = false;
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
            if (ImGui::BeginTabItem("What now")) { drawNowTab(db, s, large); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Tasks"))    { drawTasksTab(db, s);       ImGui::EndTabItem(); }
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
