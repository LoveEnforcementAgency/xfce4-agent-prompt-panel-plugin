#include "sessions.h"

#include <glib/gstdio.h>

static void test_index(void)
{
    const gchar *old = "session-260101-120000-old";
    const gchar *newer = "session-260101-130000-new";
    g_autofree gchar *root = g_dir_make_tmp("junie-sessions-test-XXXXXX", NULL);
    g_assert_nonnull(root);
    g_autofree gchar *old_path = g_build_filename(root, old, NULL);
    g_autofree gchar *new_path = g_build_filename(root, newer, NULL);
    g_assert_cmpint(g_mkdir(old_path, 0700), ==, 0);
    g_assert_cmpint(g_mkdir(new_path, 0700), ==, 0);
    g_autofree gchar *index_path = g_build_filename(root, "index.jsonl", NULL);
    const gchar *index =
        "{\"sessionId\":\"session-260101-120000-old\",\"createdAt\":1700000000000,"
        "\"projectDir\":\"/project/old\",\"taskName\":\"\"}\n"
        "not JSON\n"
        "{\"sessionId\":\"session-260101-130000-new\",\"updatedAt\":1800000000000,"
        "\"projectDir\":\"/project/new\",\"taskName\":\"Recent task\"}\n"
        "{\"sessionId\":\"session-260101-130000-new\",\"updatedAt\":1900000000000,"
        "\"projectDir\":\"/project/renamed\",\"taskName\":\"Updated task\"}\n"
        "{\"sessionId\":\"session-260101-130000-new\",\"updatedAt\":1750000000000,"
        "\"projectDir\":\"/project/stale\",\"taskName\":\"Stale task\"}\n"
        "{\"sessionId\":\"session-260101-140000-stale\",\"projectDir\":\"/project\"}\n";
    g_assert_true(g_file_set_contents(index_path, index, -1, NULL));

    g_autoptr(GError) error = NULL;
    g_autoptr(GPtrArray) sessions = assistant_sessions_load(ASSISTANT_JUNIE, root, NULL, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(sessions->len, ==, 2);
    AssistantSession *first = g_ptr_array_index(sessions, 0);
    AssistantSession *second = g_ptr_array_index(sessions, 1);
    g_assert_cmpstr(first->id, ==, newer);
    g_assert_cmpstr(first->name, ==, "Updated task");
    g_assert_cmpint(first->updated_at, ==, 1900000000);
    g_assert_cmpstr(first->directory, ==, "/project/renamed");
    g_assert_cmpstr(second->id, ==, old);
    g_assert_cmpstr(second->name, ==, "old");

    g_autoptr(GCancellable) cancel = g_cancellable_new();
    g_cancellable_cancel(cancel);
    g_assert_null(junie_sessions_load(root, cancel, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);

    g_assert_cmpint(g_remove(index_path), ==, 0);
    g_assert_cmpint(g_rmdir(old_path), ==, 0);
    g_assert_cmpint(g_rmdir(new_path), ==, 0);
    g_assert_cmpint(g_rmdir(root), ==, 0);
}

static void test_invalid_projects_and_timestamps(void)
{
    g_autofree gchar *root = g_dir_make_tmp("junie-invalid-test-XXXXXX", NULL);
    g_assert_nonnull(root);
    g_autofree gchar *session_path = g_build_filename(root, "session-test", NULL);
    g_assert_cmpint(g_mkdir(session_path, 0700), ==, 0);
    g_autofree gchar *index_path = g_build_filename(root, "index.jsonl", NULL);
    const gchar *invalid_projects[] = {
        "{\"sessionId\":\"session-test\"}\n",
        "{\"sessionId\":\"session-test\",\"projectDir\":\"relative/path\"}\n",
        "{\"sessionId\":\"session-test\",\"projectDir\":42}\n",
        "{\"sessionId\":\"../session-test\",\"projectDir\":\"/project\"}\n"
    };
    for (guint i = 0; i < G_N_ELEMENTS(invalid_projects); i++) {
        g_assert_true(g_file_set_contents(index_path, invalid_projects[i], -1, NULL));
        g_autoptr(GPtrArray) sessions = junie_sessions_load(root, NULL, NULL);
        g_assert_cmpuint(sessions->len, ==, 0);
    }
    const gchar *timestamps[] = { "1e100", "-1e100", "1800000000000.5",
                                  "\"1800000000000\"", "\"2027-01-15T08:00:00Z\"" };
    const gint64 expected[] = { 0, 0, 1800000000, 1800000000, 1800000000 };
    for (guint i = 0; i < G_N_ELEMENTS(timestamps); i++) {
        g_autofree gchar *index = g_strdup_printf(
            "{\"sessionId\":\"session-test\",\"projectDir\":\"/project\","
            "\"updatedAt\":%s}\n", timestamps[i]);
        g_assert_true(g_file_set_contents(index_path, index, -1, NULL));
        g_autoptr(GPtrArray) sessions = junie_sessions_load(root, NULL, NULL);
        g_assert_cmpuint(sessions->len, ==, 1);
        AssistantSession *session = g_ptr_array_index(sessions, 0);
        g_assert_cmpint(session->updated_at, ==, expected[i]);
    }
    g_assert_cmpint(g_remove(index_path), ==, 0);
    g_autoptr(GPtrArray) missing = junie_sessions_load(root, NULL, NULL);
    g_assert_cmpuint(missing->len, ==, 0);
    g_assert_cmpint(g_rmdir(session_path), ==, 0);
    g_assert_cmpint(g_rmdir(root), ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/junie/index-recency-deduplication-and-cancellation", test_index);
    g_test_add_func("/junie/invalid-projects-and-timestamps", test_invalid_projects_and_timestamps);
    return g_test_run();
}
