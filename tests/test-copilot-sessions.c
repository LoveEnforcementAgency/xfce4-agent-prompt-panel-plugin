#include "sessions.h"

#include <glib/gstdio.h>
#include <utime.h>

static gchar *root;

static void write_session(const gchar *id, const gchar *metadata, gint64 history_time)
{
    g_autofree gchar *directory = g_build_filename(root, id, NULL);
    g_assert_cmpint(g_mkdir(directory, 0700), ==, 0);
    g_autofree gchar *path = g_build_filename(directory, "workspace.yaml", NULL);
    g_assert_true(g_file_set_contents(path, metadata, -1, NULL));
    g_autofree gchar *history = g_build_filename(directory, "events.jsonl", NULL);
    g_assert_true(g_file_set_contents(history, history_time != 0 ? "{}\n" : "", -1, NULL));
    struct utimbuf times = { .actime = history_time, .modtime = history_time };
    g_assert_cmpint(g_utime(history, &times), ==, 0);
}

static void clean_sessions(void)
{
    g_autoptr(GDir) directory = g_dir_open(root, 0, NULL);
    const gchar *id;
    while ((id = g_dir_read_name(directory)) != NULL) {
        g_autofree gchar *path = g_build_filename(root, id, NULL);
        g_autofree gchar *metadata = g_build_filename(path, "workspace.yaml", NULL);
        g_autofree gchar *history = g_build_filename(path, "events.jsonl", NULL);
        g_assert_cmpint(g_remove(metadata), ==, 0);
        g_assert_cmpint(g_remove(history), ==, 0);
        g_assert_cmpint(g_rmdir(path), ==, 0);
    }
}

static void test_metadata(void)
{
    const gchar *old = "00000000-0000-4000-8000-000000000001";
    const gchar *newer = "00000000-0000-4000-8000-000000000002";
    const gchar *bad = "00000000-0000-4000-8000-000000000003";
    const gchar *empty = "00000000-0000-4000-8000-000000000004";
    const gchar *mismatch = "00000000-0000-4000-8000-000000000005";
    g_autofree gchar *metadata = g_strdup_printf(
        "id: %s\ncwd: '/project with spaces'\nname: 'Fix: ''quoted'' issue'\n"
        "updated_at: 2025-01-01T00:00:00Z\n", old);
    write_session(old, metadata, 1);
    g_autofree gchar *active = g_strdup_printf(
        "id: %s\ncwd: /project/newer\nname: >-\n  First line\n  second line\n"
        "updated_at: 2020-01-01T00:00:00Z\n", newer);
    write_session(newer, active, 1800000000);
    write_session(bad, "id: [broken YAML", 1);
    g_autofree gchar *empty_metadata = g_strdup_printf("id: %s\ncwd: /project\n", empty);
    write_session(empty, empty_metadata, 0);
    write_session(mismatch, metadata, 1);
    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) sessions = copilot_sessions_load(root, NULL, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(sessions->len, ==, 2);
    AssistantSession *first = g_ptr_array_index(sessions, 0);
    AssistantSession *second = g_ptr_array_index(sessions, 1);
    g_assert_cmpstr(first->id, ==, newer);
    g_assert_cmpstr(first->name, ==, "First line second line");
    g_assert_cmpstr(second->name, ==, "Fix: 'quoted' issue");
    g_assert_cmpstr(second->directory, ==, "/project with spaces");
    clean_sessions();
}

static void test_limit_and_missing(void)
{
    for (guint i = 0; i < 22; i++) {
        g_autofree gchar *id = g_strdup_printf("00000000-0000-4000-8000-%012u", i);
        g_autofree gchar *metadata = g_strdup_printf(
            "id: %s\ncwd: /project/fallback\nname: null\n", id);
        write_session(id, metadata, 1800000000 + i);
    }
    g_autoptr(GPtrArray) sessions = copilot_sessions_load(root, NULL, NULL);
    g_assert_cmpuint(sessions->len, ==, 20);
    AssistantSession *first = g_ptr_array_index(sessions, 0);
    g_assert_cmpstr(first->name, ==, "fallback");
    g_assert_cmpstr(first->id, ==, "00000000-0000-4000-8000-000000000021");
    g_autoptr(GCancellable) cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    g_autoptr(GError) error = NULL;
    g_assert_null(copilot_sessions_load(root, cancel, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
    g_autofree gchar *missing = g_build_filename(root, "not-created", NULL);
    g_autoptr(GPtrArray) none = copilot_sessions_load(missing, NULL, NULL);
    g_assert_cmpuint(none->len, ==, 0);
    clean_sessions();
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    root = g_dir_make_tmp("copilot-sessions-test-XXXXXX", NULL);
    g_assert_nonnull(root);
    g_test_add_func("/sessions/yaml-metadata-and-active-recency", test_metadata);
    g_test_add_func("/sessions/limit-fallback-and-cancellation", test_limit_and_missing);
    int result = g_test_run();
    g_assert_cmpint(g_rmdir(root), ==, 0);
    g_free(root);
    return result;
}
