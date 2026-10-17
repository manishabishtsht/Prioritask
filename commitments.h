#ifndef COMMITMENTS_H
#define COMMITMENTS_H

#include <string>
#include <vector>

// ---------------------------------------------------------------
// Fixed commitments: recurring blocks of time when the user is busy.
// College, school, an office job, tuition, a work shift -- the app does
// not care which. It only needs to know when the user is unavailable.
//
// Like the scheduler and advisor, this module knows nothing about SQLite
// or the clock. main.cpp passes the numbers in, so it can be tested alone.
// ---------------------------------------------------------------

// Times are minutes since midnight. If endMin <= startMin, the block runs
// past midnight and ends the next day (a night shift, say 22:00 to 06:00).
struct CommitmentBlock {
    std::string label;
    std::string days;        // 7 chars, Mon..Sun, '1' = happens that day
    int         startMin = 0;
    int         endMin   = 0;
};

// "mon-fri", "Mon,Wed,Fri", "sat", "weekdays", "weekends", "daily"
//   -> a 7-character mask such as "1111100".
// Returns an empty string if the text can't be understood.
std::string parseDays(const std::string& text);

// "1111100" -> "Mon-Fri",  "1010100" -> "Mon, Wed, Fri",  "1111111" -> "Every day"
std::string daysToText(const std::string& mask);

// How many minutes of commitments fall inside the window that starts now
// and lasts windowLen minutes. nowMin is minutes since midnight today;
// todayIdx is today's weekday, 0 = Monday ... 6 = Sunday.
// Overlapping commitments are counted once, not twice.
int busyMinutesInWindow(const std::vector<CommitmentBlock>& blocks,
                        int nowMin, int windowLen, int todayIdx);

// If the user is inside a commitment right now, returns true and fills in:
//   label       - which commitment
//   freeAtMin   - clock time they're next free (minutes since midnight);
//                 back-to-back commitments are treated as one block
//   minutesLeft - how long until then
bool currentCommitment(const std::vector<CommitmentBlock>& blocks,
                       int nowMin, int todayIdx,
                       std::string& label, int& freeAtMin, int& minutesLeft);

#endif