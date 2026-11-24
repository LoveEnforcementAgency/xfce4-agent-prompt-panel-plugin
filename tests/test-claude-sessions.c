#include "sessions.h"

#include <glib/gstdio.h>
#include <utime.h>

static gchar *new_root(void)
{
    g_autofree gchar *root = g_dir_make_tmp("claude-sessions-test-XXXXXX", NULL);
    g_assert_nonnull(root);
    return g_steal_pointer(&root);
}

static gchar *make_project(const gchar *root)
{
    g_autofree gchar *project = g_build_filename(root, "-project-with-spaces", NULL);
    g_assert_cmpint(g_mkdir(project, 0700), ==, 0);
    return g_steal_pointer(&project);
}

static void write_history(const gchar *project, const gchar *id, const gchar *contents)
{
    g_autofree gchar *filename = g_strconcat(id, ".jsonl", NULL);
    g_autofree gchar *path = g_build_filename(project, filename, NULL);
    g_assert_true(g_file_set_contents(path, contents, -1, NULL));
}

static void set_history_time(const gchar *project, const gchar *id, gint64 timestamp)
{
    g_autofree gchar *filename = g_strconcat(id, ".jsonl", NULL);
    g_autofree gchar *path = g_build_filename(project, filename, NULL);
    struct utimbuf times = { .actime = timestamp, .modtime = timestamp };
    g_assert_cmpint(g_utime(path, &times), ==, 0);
}

static void remove_project(const gchar *root, const gchar *project)
{
    g_autoptr(GDir) directory = g_dir_open(project, 0, NULL);
    const gchar *filename;
    while ((filename = g_dir_read_name(directory)) != NULL) {
        g_autofree gchar *path = g_build_filename(project, filename, NULL);
        g_assert_cmpint(g_remove(path), ==, 0);
    }
    g_assert_cmpint(g_rmdir(project), ==, 0);
    g_assert_cmpint(g_rmdir(root), ==, 0);
}

static void test_history_records(void)
{
    const gchar *first_id = "00000000-0000-4000-8000-000000000001";
    const gchar *second_id = "00000000-0000-4000-8000-000000000002";
    const gchar *invalid_id = "00000000-0000-4000-8000-000000000003";
    g_autofree gchar *root = new_root();
    g_autofree gchar *project = make_project(root);
    g_autofree gchar *first_history = g_strdup_printf(
        "{\"type\":\"user\",\"sessionId\":\"%s\",\"cwd\":\"/project/with spaces\","
        "\"timestamp\":\"2026-01-01T10:00:00Z\",\"message\":{\"content\":\"First prompt\"}}\n"
        "{\"type\":\"summary\",\"sessionId\":\"%s\",\"summary\":\"Useful summary\","
        "\"timestamp\":\"2026-01-01T10:05:00Z\"}\n",
        first_id, first_id);
    write_history(project, first_id, first_history);
    set_history_time(project, first_id, 1600000000);
    g_autofree gchar *second_history = g_strdup_printf(
        "not JSON\n{\"type\":\"user\",\"sessionId\":\"%s\",\"cwd\":\"/project/fallback\","
        "\"timestamp\":1700000000,\"message\":{\"content\":[{\"type\":\"text\","
        "\"text\":\"Prompt fallback\"}]}}\n",
        second_id);
    write_history(project, second_id, second_history);
    set_history_time(project, second_id, 1600000000);
    g_autofree gchar *invalid_history = g_strdup_printf(
        "{\"type\":\"user\",\"sessionId\":\"%s\",\"cwd\":\"relative/path\"}\n",
        invalid_id);
    write_history(project, invalid_id, invalid_history);
    set_history_time(project, invalid_id, 1600000000);
    write_history(project, "not-a-uuid", "{}\n");

    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) sessions = assistant_sessions_load(
        ASSISTANT_CLAUDE, root, NULL, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(sessions->len, ==, 2);
    AssistantSession *first = g_ptr_array_index(sessions, 0);
    AssistantSession *second = g_ptr_array_index(sessions, 1);
    g_assert_cmpstr(first->id, ==, first_id);
    g_assert_cmpstr(first->name, ==, "Useful summary");
    g_assert_cmpstr(first->directory, ==, "/project/with spaces");
    g_assert_cmpint(first->updated_at, ==, 1767261900);
    g_assert_cmpstr(second->id, ==, second_id);
    g_assert_cmpstr(second->name, ==, "fallback");
    g_assert_cmpstr(second->directory, ==, "/project/fallback");
    remove_project(root, project);
}

static void test_limit_missing_and_cancellation(void)
{
    g_autofree gchar *root = new_root();
    g_autofree gchar *project = make_project(root);
    gchar *newest_id = NULL;
    for (guint i = 0; i < 22; i++) {
        g_autofree gchar *id = g_uuid_string_random();
        g_autofree gchar *history = g_strdup_printf(
            "{\"type\":\"user\",\"sessionId\":\"%s\",\"cwd\":\"/project\","
            "\"timestamp\":%u,\"message\":{\"content\":\"Task %u\"}}\n",
            id, 1900000000 + i, i);
        write_history(project, id, history);
        set_history_time(project, id, 1600000000);
        if (i == 21)
            newest_id = g_strdup(id);
    }

    g_autoptr(GPtrArray) sessions = claude_sessions_load(root, NULL, NULL);
    g_assert_cmpuint(sessions->len, ==, 20);
    g_assert_cmpstr(((AssistantSession *) g_ptr_array_index(sessions, 0))->id,
                    ==, newest_id);
    g_free(newest_id);

    g_autoptr(GCancellable) cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    g_autoptr(GError) error = NULL;
    g_assert_null(claude_sessions_load(root, cancel, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);

    g_autofree gchar *missing = g_build_filename(root, "missing", NULL);
    g_autoptr(GPtrArray) empty = claude_sessions_load(missing, NULL, NULL);
    g_assert_cmpuint(empty->len, ==, 0);
    remove_project(root, project);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/claude/history-records", test_history_records);
    g_test_add_func("/claude/limit-missing-and-cancellation",
                    test_limit_missing_and_cancellation);
    return g_test_run();
}