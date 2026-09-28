#include "stdafx.h"
#include "WatchHistory.h"
#include "BasePlayer.h"
#include "sqliteDB.h"

namespace watchhistory {

bool hasWatchToPersist(const QSharedPointer<BasePlayer>& player)
{
    if (!player)
        return false;
    return player->videoWatchedTime() > 0.0 || player->activeWatchHistoryRowId > 0;
}

bool isTrackedVideo(const QSharedPointer<BasePlayer>& player)
{
    if (!player)
        return false;
    return player->video_id >= 0 || player->trackExternalVideo;
}

double resolveEndPosition(double position, double duration, double startProgress, bool assumeEnded)
{
    if (position >= 0.0)
        return position;
    if (assumeEnded && duration > 0.0)
        return duration;
    return startProgress;
}

int flush(sqliteDB* db, RowState& st, int sessionId)
{
    if (!db)
        return 0;
    return db->upsertWatchHistory(st.rowId, st.videoId, st.category, st.videoPath,
        st.watchedStart, st.watchedEnd, st.watchedTime, sessionId, st.completed);
}

int flushPlayer(sqliteDB* db, BasePlayer& player, int sessionId, bool completed, bool assumeEnded)
{
    RowState st;
    st.rowId = player.activeWatchHistoryRowId;
    st.videoId = player.video_id;
    st.category = player.category;
    // Name the row with the path that belongs to video_id. trackedVideoPath is only
    // ever written alongside video_id, whereas video_path is set from MPC's
    // CMD_NOWPLAYING and so still names the previous file until a change lands: on an
    // external file change the player is already relabelled when the outgoing video is
    // flushed, which would credit its watched time to the incoming video's path — and
    // for a video that is not in the DB that path is its only identity. Same for the
    // window while a programmatic change has not been confirmed by MPC yet.
    st.videoPath = player.trackedVideoPath.isEmpty() ? player.video_path : player.trackedVideoPath;
    st.watchedStart = player.startProgress;
    st.watchedEnd = resolveEndPosition(player.position, player.duration, player.startProgress, assumeEnded);
    st.watchedTime = player.videoWatchedTime();
    st.completed = completed;
    const int rowId = flush(db, st, sessionId);
    // upsertWatchHistory writes the new id back on INSERT; mirror that so the next
    // checkpoint updates the same row instead of inserting another one.
    player.activeWatchHistoryRowId = st.rowId;
    return rowId;
}

}
