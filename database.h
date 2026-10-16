#pragma once
#include <iostream>
#include <string>
#include <vector>
#include "sqlite3.h"

// One task in C++ (matches one row of the table)
struct Task {
    int id = 0;
    std::string title;
    std::string description;
    std::string category;
    int priority = 1;
    int difficulty = 1;
    int energyRequired = 1;
    int estimatedTime = 0;   // minutes
    int minTime = 0;         // minutes
    bool isMandatory = false;
    std::string deadline;    // "YYYY-MM-DD HH:MM" or empty
    std::string status = "Pending";
};

// One row to write into day_plan.
// Kept deliberately plain so database.h does not need to know about
// scheduler.h. main.cpp converts Allocation -> PlanRow.
struct PlanRow {
    int taskId    = 0;
    int requested = 0;
    int allocated = 0;
};

// One fixed commitment as stored in the database: college, school,
// office, tuition, a shift. Kept as plain text here; main.cpp turns it
// into numbers for the commitments module.
struct CommitmentRow {
    int         id = 0;
    std::string label;   // "College", "Office", ...
    std::string days;    // 7 chars, Mon..Sun, e.g. "1111100"
    std::string start;   // "HH:MM"
    std::string end;     // "HH:MM"; earlier than start means it runs past midnight
};

class Database {
    sqlite3* db = nullptr;

    // Safely read a text column (NULL becomes an empty string)
    static std::string colText(sqlite3_stmt* s, int i) {
        const unsigned char* t = sqlite3_column_text(s, i);
        return t ? reinterpret_cast<const char*>(t) : "";
    }

    // Shared by getAllTasks and getPendingTasks so the column order is
    // written out only once.
    std::vector<Task> runTaskQuery(const char* sql) {
        std::vector<Task> tasks;

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            std::cout << "Error: " << sqlite3_errmsg(db) << "\n";
            return tasks;
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Task t;
            t.id             = sqlite3_column_int(stmt, 0);
            t.title          = colText(stmt, 1);
            t.description    = colText(stmt, 2);
            t.category       = colText(stmt, 3);
            t.priority       = sqlite3_column_int(stmt, 4);
            t.difficulty     = sqlite3_column_int(stmt, 5);
            t.energyRequired = sqlite3_column_int(stmt, 6);
            t.estimatedTime  = sqlite3_column_int(stmt, 7);
            t.minTime        = sqlite3_column_int(stmt, 8);
            t.isMandatory    = sqlite3_column_int(stmt, 9) == 1;
            t.deadline       = colText(stmt, 10);
            t.status         = colText(stmt, 11);
            tasks.push_back(t);
        }
        sqlite3_finalize(stmt);
        return tasks;
    }

public:
    Database(const std::string& path) {
        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
            std::cout << "Could not open database\n";
            sqlite3_close(db);
            db = nullptr;
        }
    }

    ~Database() {
        if (db) sqlite3_close(db);
    }

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // The constructor cannot report failure, so the caller must check this
    // before using the object.
    bool isOpen() const { return db != nullptr; }

    bool createTable() {
        const char* sql =
            "CREATE TABLE IF NOT EXISTS tasks ("
            "task_id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "title TEXT NOT NULL,"
            "description TEXT,"
            "category TEXT,"
            "priority INTEGER NOT NULL CHECK (priority BETWEEN 1 AND 5),"
            "difficulty INTEGER NOT NULL CHECK (difficulty BETWEEN 1 AND 5),"
            "energy_required INTEGER NOT NULL CHECK (energy_required BETWEEN 1 AND 5),"
            "estimated_time INTEGER NOT NULL,"
            "min_time INTEGER NOT NULL,"
            "is_mandatory INTEGER NOT NULL DEFAULT 0,"
            "deadline TEXT,"
            "status TEXT NOT NULL DEFAULT 'Pending',"
            "created_at TEXT DEFAULT CURRENT_TIMESTAMP,"
            "completed_at TEXT,"
            "CHECK (min_time <= estimated_time));";

        char* err = nullptr;
        if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
            std::cout << "Error: " << err << "\n";
            sqlite3_free(err);
            return false;
        }
        return true;
    }

    // Lets the program set itself up on a fresh machine instead of needing
    // DB Browser. Safe to call every run.
    bool createDayPlanTable() {
        const char* sql =
            "CREATE TABLE IF NOT EXISTS day_plan ("
            "plan_id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "plan_date TEXT NOT NULL,"
            "task_id INTEGER NOT NULL REFERENCES tasks(task_id),"
            "requested_time INTEGER NOT NULL,"
            "allocated_time INTEGER NOT NULL,"
            "UNIQUE (plan_date, task_id));";

        char* err = nullptr;
        if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
            std::cout << "Error: " << err << "\n";
            sqlite3_free(err);
            return false;
        }
        return true;
    }

    // ---------------------------------------------------------------
    // Settings: a simple key/value store for user preferences that must
    // survive between runs, such as wake-up time. One table rather than
    // one column per setting, so adding a new preference later needs no
    // schema change.
    // ---------------------------------------------------------------
    bool createSettingsTable() {
        const char* sql =
            "CREATE TABLE IF NOT EXISTS settings ("
            "key   TEXT PRIMARY KEY,"
            "value TEXT NOT NULL);";

        char* err = nullptr;
        if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
            std::cout << "Error: " << err << "\n";
            sqlite3_free(err);
            return false;
        }
        return true;
    }

    // Returns true and fills `value` if the key exists; false if it has
    // never been set.
    bool getSetting(const std::string& key, std::string& value) {
        const char* sql = "SELECT value FROM settings WHERE key = ?;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            std::cout << "Error: " << sqlite3_errmsg(db) << "\n";
            return false;
        }
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);

        bool found = false;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            value = colText(stmt, 0);
            found = true;
        }
        sqlite3_finalize(stmt);
        return found;
    }

    // Insert or overwrite.
    bool setSetting(const std::string& key, const std::string& value) {
        const char* sql =
            "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?);";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            std::cout << "Error: " << sqlite3_errmsg(db) << "\n";
            return false;
        }
        sqlite3_bind_text(stmt, 1, key.c_str(),   -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);

        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        if (!ok) std::cout << "Error: " << sqlite3_errmsg(db) << "\n";
        sqlite3_finalize(stmt);
        return ok;
    }

    bool addTask(const Task& t) {
        const char* sql =
            "INSERT INTO tasks (title, description, category, priority, difficulty, "
            "energy_required, estimated_time, min_time, is_mandatory, deadline) "
            "VALUES (?,?,?,?,?,?,?,?,?,?);";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            std::cout << "Error: " << sqlite3_errmsg(db) << "\n";
            return false;
        }

        sqlite3_bind_text(stmt, 1, t.title.c_str(), -1, SQLITE_TRANSIENT);
        if (t.description.empty()) sqlite3_bind_null(stmt, 2);
        else sqlite3_bind_text(stmt, 2, t.description.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, t.category.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, t.priority);
        sqlite3_bind_int(stmt, 5, t.difficulty);
        sqlite3_bind_int(stmt, 6, t.energyRequired);
        sqlite3_bind_int(stmt, 7, t.estimatedTime);
        sqlite3_bind_int(stmt, 8, t.minTime);
        sqlite3_bind_int(stmt, 9, t.isMandatory ? 1 : 0);
        if (t.deadline.empty()) sqlite3_bind_null(stmt, 10);
        else sqlite3_bind_text(stmt, 10, t.deadline.c_str(), -1, SQLITE_TRANSIENT);

        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        if (!ok) std::cout << "Error: " << sqlite3_errmsg(db) << "\n";
        sqlite3_finalize(stmt);
        return ok;
    }

    // Overwrites every editable field of an existing task. Status and
    // timestamps are left alone; marking complete has its own method.
    bool updateTask(const Task& t) {
        const char* sql =
            "UPDATE tasks SET title = ?, description = ?, category = ?, "
            "priority = ?, difficulty = ?, energy_required = ?, "
            "estimated_time = ?, min_time = ?, is_mandatory = ?, deadline = ? "
            "WHERE task_id = ?;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            std::cout << "Error: " << sqlite3_errmsg(db) << "\n";
            return false;
        }

        sqlite3_bind_text(stmt, 1, t.title.c_str(), -1, SQLITE_TRANSIENT);
        if (t.description.empty()) sqlite3_bind_null(stmt, 2);
        else sqlite3_bind_text(stmt, 2, t.description.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, t.category.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (stmt, 4, t.priority);
        sqlite3_bind_int (stmt, 5, t.difficulty);
        sqlite3_bind_int (stmt, 6, t.energyRequired);
        sqlite3_bind_int (stmt, 7, t.estimatedTime);
        sqlite3_bind_int (stmt, 8, t.minTime);
        sqlite3_bind_int (stmt, 9, t.isMandatory ? 1 : 0);
        if (t.deadline.empty()) sqlite3_bind_null(stmt, 10);
        else sqlite3_bind_text(stmt, 10, t.deadline.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (stmt, 11, t.id);

        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        if (!ok) std::cout << "Error: " << sqlite3_errmsg(db) << "\n";
        sqlite3_finalize(stmt);

        if (ok && sqlite3_changes(db) == 0) {
            std::cout << "No task with id " << t.id << "\n";
            return false;
        }
        return ok;
    }

    std::vector<Task> getAllTasks() {
        return runTaskQuery(
            "SELECT task_id, title, description, category, priority, difficulty, "
            "energy_required, estimated_time, min_time, is_mandatory, deadline, status "
            "FROM tasks ORDER BY task_id;");
    }

    // Only what still needs doing. This is what the scheduler should see;
    // planning around finished work would be wrong.
    std::vector<Task> getPendingTasks() {
        return runTaskQuery(
            "SELECT task_id, title, description, category, priority, difficulty, "
            "energy_required, estimated_time, min_time, is_mandatory, deadline, status "
            "FROM tasks WHERE status <> 'Completed' ORDER BY task_id;");
    }

    bool markComplete(int taskId) {
        const char* sql =
            "UPDATE tasks SET status = 'Completed', "
            "completed_at = CURRENT_TIMESTAMP WHERE task_id = ?;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            std::cout << "Error: " << sqlite3_errmsg(db) << "\n";
            return false;
        }
        sqlite3_bind_int(stmt, 1, taskId);

        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        if (!ok) std::cout << "Error: " << sqlite3_errmsg(db) << "\n";
        sqlite3_finalize(stmt);

        // sqlite3_step succeeds even when no row matched the id, so check
        // that something was actually updated.
        if (ok && sqlite3_changes(db) == 0) {
            std::cout << "No task with id " << taskId << "\n";
            return false;
        }
        return ok;
    }

    // ---------------------------------------------------------------
    // Fixed commitments
    // ---------------------------------------------------------------
    bool createCommitmentsTable() {
        const char* sql =
            "CREATE TABLE IF NOT EXISTS commitments ("
            "commitment_id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "label      TEXT NOT NULL,"
            "days       TEXT NOT NULL CHECK (length(days) = 7),"
            "start_time TEXT NOT NULL,"
            "end_time   TEXT NOT NULL);";

        char* err = nullptr;
        if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
            std::cout << "Error: " << err << "\n";
            sqlite3_free(err);
            return false;
        }
        return true;
    }

    bool addCommitment(const CommitmentRow& c) {
        const char* sql =
            "INSERT INTO commitments (label, days, start_time, end_time) "
            "VALUES (?, ?, ?, ?);";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            std::cout << "Error: " << sqlite3_errmsg(db) << "\n";
            return false;
        }
        sqlite3_bind_text(stmt, 1, c.label.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, c.days.c_str(),  -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, c.start.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, c.end.c_str(),   -1, SQLITE_TRANSIENT);

        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        if (!ok) std::cout << "Error: " << sqlite3_errmsg(db) << "\n";
        sqlite3_finalize(stmt);
        return ok;
    }

    std::vector<CommitmentRow> getCommitments() {
        std::vector<CommitmentRow> out;
        const char* sql =
            "SELECT commitment_id, label, days, start_time, end_time "
            "FROM commitments ORDER BY start_time, commitment_id;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            std::cout << "Error: " << sqlite3_errmsg(db) << "\n";
            return out;
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            CommitmentRow c;
            c.id    = sqlite3_column_int(stmt, 0);
            c.label = colText(stmt, 1);
            c.days  = colText(stmt, 2);
            c.start = colText(stmt, 3);
            c.end   = colText(stmt, 4);
            out.push_back(c);
        }
        sqlite3_finalize(stmt);
        return out;
    }

    bool deleteCommitment(int id) {
        const char* sql = "DELETE FROM commitments WHERE commitment_id = ?;";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            std::cout << "Error: " << sqlite3_errmsg(db) << "\n";
            return false;
        }
        sqlite3_bind_int(stmt, 1, id);

        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        if (!ok) std::cout << "Error: " << sqlite3_errmsg(db) << "\n";
        sqlite3_finalize(stmt);

        if (ok && sqlite3_changes(db) == 0) {
            std::cout << "No commitment with id " << id << "\n";
            return false;
        }
        return ok;
    }

    // Removes a task and any saved plan rows that point at it. Both deletes
    // run in one transaction so a failure can never leave day_plan rows
    // referring to a task that no longer exists.
    bool deleteTask(int taskId) {
        if (sqlite3_exec(db, "BEGIN;", nullptr, nullptr, nullptr) != SQLITE_OK) {
            std::cout << "Error: " << sqlite3_errmsg(db) << "\n";
            return false;
        }

        const char* sqls[2] = {
            "DELETE FROM day_plan WHERE task_id = ?;",
            "DELETE FROM tasks    WHERE task_id = ?;"
        };

        bool ok = true;
        int  removedTasks = 0;

        for (int i = 0; i < 2 && ok; ++i) {
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, sqls[i], -1, &stmt, nullptr) != SQLITE_OK) {
                std::cout << "Error: " << sqlite3_errmsg(db) << "\n";
                ok = false;
                break;
            }
            sqlite3_bind_int(stmt, 1, taskId);
            if (sqlite3_step(stmt) != SQLITE_DONE) {
                std::cout << "Error: " << sqlite3_errmsg(db) << "\n";
                ok = false;
            }
            if (i == 1) removedTasks = sqlite3_changes(db);
            sqlite3_finalize(stmt);
        }

        if (ok && removedTasks == 0) {
            std::cout << "No task with id " << taskId << "\n";
            ok = false;
        }

        sqlite3_exec(db, ok ? "COMMIT;" : "ROLLBACK;", nullptr, nullptr, nullptr);
        return ok;
    }

    // Writes one day's allocations. INSERT OR REPLACE means re-planning the
    // same date overwrites the old plan instead of failing on the UNIQUE
    // constraint. Wrapped in a transaction so a partial plan is never saved.
    bool savePlan(const std::string& date, const std::vector<PlanRow>& rows) {
        if (rows.empty()) return true;

        char* err = nullptr;
        if (sqlite3_exec(db, "BEGIN;", nullptr, nullptr, &err) != SQLITE_OK) {
            std::cout << "Error: " << err << "\n";
            sqlite3_free(err);
            return false;
        }

        const char* sql =
            "INSERT OR REPLACE INTO day_plan "
            "(plan_date, task_id, requested_time, allocated_time) "
            "VALUES (?,?,?,?);";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            std::cout << "Error: " << sqlite3_errmsg(db) << "\n";
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }

        bool ok = true;
        for (const PlanRow& r : rows) {
            sqlite3_bind_text(stmt, 1, date.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int (stmt, 2, r.taskId);
            sqlite3_bind_int (stmt, 3, r.requested);
            sqlite3_bind_int (stmt, 4, r.allocated);

            if (sqlite3_step(stmt) != SQLITE_DONE) {
                std::cout << "Error: " << sqlite3_errmsg(db) << "\n";
                ok = false;
                break;
            }
            sqlite3_reset(stmt);
        }
        sqlite3_finalize(stmt);

        sqlite3_exec(db, ok ? "COMMIT;" : "ROLLBACK;", nullptr, nullptr, nullptr);
        return ok;
    }
};