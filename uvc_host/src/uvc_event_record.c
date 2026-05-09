#include "uvc_event_record.h"

#include <errno.h>
#include <inttypes.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_DB_PATH       "/mnt/nova_ssd/events.db"
#define DEFAULT_SNAPSHOT_DIR  "/mnt/nova_ssd/events"

static const char *event_type_to_str(uint8_t event_type)
{
    switch (event_type) {
    case UVC_EVENT_TYPE_MOTION: return "motion";
    case UVC_EVENT_TYPE_HUMAN:  return "human";
    case UVC_EVENT_TYPE_SOUND:  return "sound";
    default:                    return "unknown";
    }
}

static const char *event_default_severity(uint8_t event_type)
{
    return event_type == UVC_EVENT_TYPE_HUMAN ? "high" : "medium";
}

static const char *event_default_description(uint8_t event_type)
{
    switch (event_type) {
    case UVC_EVENT_TYPE_MOTION: return "IPC 上报移动侦测事件";
    case UVC_EVENT_TYPE_HUMAN:  return "IPC 上报人形检测事件";
    case UVC_EVENT_TYPE_SOUND:  return "IPC 上报声音检测事件";
    default:                    return "IPC 上报未知检测事件";
    }
}

static uint64_t now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

static const char *db_path(void)
{
    const char *p = getenv("UVC_AI_DB_PATH");
    return (p && *p) ? p : DEFAULT_DB_PATH;
}

static const char *snapshot_root(void)
{
    const char *p = getenv("UVC_AI_SNAPSHOT_DIR");
    return (p && *p) ? p : DEFAULT_SNAPSHOT_DIR;
}

static int mkdir_p(const char *path)
{
    char tmp[512];
    size_t len = strlen(path);

    if (len == 0 || len >= sizeof(tmp))
        return -1;

    memcpy(tmp, path, len + 1);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) < 0 && errno != EEXIST)
        return -1;
    return 0;
}

static int ensure_parent_dir(const char *path)
{
    char dir[512];
    char *slash;

    if (strlen(path) >= sizeof(dir))
        return -1;
    strcpy(dir, path);
    slash = strrchr(dir, '/');
    if (!slash)
        return 0;
    *slash = '\0';
    return mkdir_p(dir);
}

static void make_event_id(char out[33], uint64_t ts_ms, const UvcFrameHeader_t *h)
{
    uint64_t mixed = ts_ms ^ ((uint64_t)getpid() << 32) ^
                     ((uint64_t)h->FrameIndex << 16) ^ h->Timestamp ^
                     ((uint64_t)h->Channel << 8) ^ h->EventType;
    snprintf(out, 33, "%016" PRIx64 "%016" PRIx64, ts_ms, mixed);
}

static int ensure_events_schema(sqlite3 *db)
{
    const char *sql =
        "CREATE TABLE IF NOT EXISTS events ("
        "id TEXT PRIMARY KEY,"
        "ts INTEGER NOT NULL,"
        "channel TEXT NOT NULL,"
        "source TEXT NOT NULL,"
        "snapshots TEXT,"
        "clip_start INTEGER,"
        "clip_end INTEGER,"
        "status TEXT NOT NULL DEFAULT 'pending',"
        "description TEXT,"
        "severity TEXT,"
        "tags TEXT,"
        "raw_motion REAL,"
        "extra TEXT,"
        "created_at INTEGER NOT NULL,"
        "analyzed_at INTEGER"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_events_ts ON events(ts);"
        "CREATE INDEX IF NOT EXISTS idx_events_status ON events(status);"
        "CREATE INDEX IF NOT EXISTS idx_events_channel_ts ON events(channel, ts);";
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[event_record] schema error: %s\n", err ? err : "unknown");
        sqlite3_free(err);
        return -1;
    }
    return 0;
}

static int save_snapshot(const char *root,
                         const char *date,
                         const char *eid,
                         const void *payload,
                         size_t payload_len,
                         char rel_path[256])
{
    char dir[512];
    char abs_path[768];
    FILE *fp;

    if (!payload || payload_len == 0) {
        rel_path[0] = '\0';
        return 0;
    }

    snprintf(rel_path, 256, "%s/%s/0_event.jpg", date, eid);
    snprintf(dir, sizeof(dir), "%s/%s/%s", root, date, eid);
    snprintf(abs_path, sizeof(abs_path), "%s/%s", root, rel_path);

    if (mkdir_p(dir) < 0) {
        fprintf(stderr, "[event_record] mkdir failed: %s (%s)\n", dir, strerror(errno));
        rel_path[0] = '\0';
        return -1;
    }

    fp = fopen(abs_path, "wb");
    if (!fp) {
        fprintf(stderr, "[event_record] open snapshot failed: %s (%s)\n",
                abs_path, strerror(errno));
        rel_path[0] = '\0';
        return -1;
    }
    if (fwrite(payload, 1, payload_len, fp) != payload_len) {
        fprintf(stderr, "[event_record] write snapshot failed: %s\n", abs_path);
        fclose(fp);
        rel_path[0] = '\0';
        return -1;
    }
    fclose(fp);
    return 0;
}

static int insert_event(sqlite3 *db,
                        const UvcFrameHeader_t *h,
                        const char *eid,
                        uint64_t ts,
                        const char *event_name,
                        const char *snapshot_rel,
                        size_t payload_len)
{
    static const char *sql =
        "INSERT OR REPLACE INTO events "
        "(id, ts, channel, source, snapshots, clip_start, clip_end, "
        " status, description, severity, tags, extra, created_at, analyzed_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, 'analyzed', ?, ?, ?, ?, ?, ?)";

    sqlite3_stmt *stmt = NULL;
    char channel[32];
    char snapshots[320];
    char tags[64];
    char extra[512];
    int cam_idx = h->Channel / UVC_CHANNELS_PER_SENSOR;
    int rc;

    snprintf(channel, sizeof(channel), "cam%d", cam_idx);
    if (snapshot_rel && snapshot_rel[0])
        snprintf(snapshots, sizeof(snapshots), "[\"%s\"]", snapshot_rel);
    else
        snprintf(snapshots, sizeof(snapshots), "[]");
    snprintf(tags, sizeof(tags), "[\"%s\"]", event_name);
    snprintf(extra, sizeof(extra),
             "{\"uvc_channel\":%u,\"event_type\":%u,"
             "\"frame_index\":%u,\"uvc_timestamp\":%" PRIu64 ","
             "\"payload_len\":%zu}",
             h->Channel, h->EventType, h->FrameIndex, h->Timestamp, payload_len);

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[event_record] prepare failed: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, eid, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)ts);
    sqlite3_bind_text(stmt, 3, channel, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, event_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, snapshots, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 6, (sqlite3_int64)ts - 1000);
    sqlite3_bind_int64(stmt, 7, (sqlite3_int64)ts + 4000);
    sqlite3_bind_text(stmt, 8, event_default_description(h->EventType), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 9, event_default_severity(h->EventType), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 10, tags, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, extra, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 12, (sqlite3_int64)now_ms());
    sqlite3_bind_int64(stmt, 13, (sqlite3_int64)now_ms());

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[event_record] insert failed: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return -1;
    }
    sqlite3_finalize(stmt);
    return 0;
}

int uvc_event_record_detect(const UvcFrameHeader_t *header,
                            const void *payload,
                            size_t payload_len)
{
    sqlite3 *db = NULL;
    uint64_t ts = header->Timestamp ? header->Timestamp : now_ms();
    time_t sec = (time_t)(ts / 1000ULL);
    struct tm tmv;
    char date[16];
    char eid[33];
    char rel_path[256];
    const char *root = snapshot_root();
    const char *dbp = db_path();
    const char *event_name = event_type_to_str(header->EventType);

    if (!localtime_r(&sec, &tmv))
        return -1;
    strftime(date, sizeof(date), "%Y%m%d", &tmv);
    make_event_id(eid, ts, header);

    rel_path[0] = '\0';
    (void)save_snapshot(root, date, eid, payload, payload_len, rel_path);

    if (ensure_parent_dir(dbp) < 0) {
        fprintf(stderr, "[event_record] db parent mkdir failed: %s\n", dbp);
        return -1;
    }

    if (sqlite3_open(dbp, &db) != SQLITE_OK) {
        fprintf(stderr, "[event_record] open db failed: %s\n",
                db ? sqlite3_errmsg(db) : dbp);
        if (db)
            sqlite3_close(db);
        return -1;
    }

    sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
    if (ensure_events_schema(db) < 0 || insert_event(
            db, header, eid, ts, event_name, rel_path, payload_len) < 0) {
        sqlite3_close(db);
        return -1;
    }

    sqlite3_close(db);
    fprintf(stdout, "[event_record] stored id=%s source=%s channel=cam%d snapshot=%s\n",
            eid, event_name, header->Channel / UVC_CHANNELS_PER_SENSOR,
            rel_path[0] ? rel_path : "none");
    return 0;
}
