#pragma once
#include <QString>
#include <QSharedPointer>

class BasePlayer;
class sqliteDB;

// Shared writer for watch_history rows. Every part of the app that persists a
// player's accumulated watch time goes through here, so the row shape, the
// end-position fallback and the row-id write-back live in one place.
namespace watchhistory {

struct RowState {
    int rowId = -1;          // <= 0 inserts a new row, > 0 updates it
    int videoId = -1;
    QString category;
    QString videoPath;
    double watchedStart = 0.0;
    double watchedEnd = 0.0;
    double watchedTime = 0.0;
    bool completed = false;
};

// Does this player have accumulated watch time, or an existing row to update?
bool hasWatchToPersist(const QSharedPointer<BasePlayer>& player);

// Can the row be linked to a library video, or to an externally tracked one?
bool isTrackedVideo(const QSharedPointer<BasePlayer>& player);

// Playhead to store as watched_end. A player that has not reported a position yet
// falls back to the untouched start position, or to the full duration when the
// player is known to be finished/gone (assumeEnded).
double resolveEndPosition(double position, double duration, double startProgress, bool assumeEnded);

// Upsert a row from explicit state. Writes the resulting row id back into st.rowId.
int flush(sqliteDB* db, RowState& st, int sessionId);

// Upsert a row from a live player, writing the row id back into
// player->activeWatchHistoryRowId.
int flushPlayer(sqliteDB* db, BasePlayer& player, int sessionId, bool completed, bool assumeEnded);

}
