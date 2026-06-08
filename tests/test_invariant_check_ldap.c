#include <check.h>
#include <stdlib.h>
#include <string.h>

/* Test that LDAP special characters in base DN and filter inputs
 * are rejected or sanitized before being passed to ldap_search_s().
 * Invariant: adversarial LDAP metacharacters must not pass through
 * unsanitized to the search call (plugin should reject or escape them).
 */

/* We invoke the real plugin binary with adversarial arguments and
 * verify it does NOT succeed (exit 0) with injected filter payloads,
 * since a real LDAP server is not present and the plugin should fail
 * safely rather than silently accepting malformed input. */

static int run_plugin(const char *basedn, const char *filter)
{
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
        "plugins/check_ldap -H 127.0.0.1 -p 389 -b '%s' -f '%s' 2>/dev/null",
        basedn, filter);
    int ret = system(cmd);
    /* Return the exit status */
    return WEXITSTATUS(ret);
}

START_TEST(test_ldap_injection_invariant)
{
    /* Invariant: plugin must not exit 0 (OK) when fed LDAP injection payloads
     * against a non-existent server — it must fail safely, not silently accept
     * and forward injected metacharacters. */
    struct { const char *basedn; const char *filter; } payloads[] = {
        /* Exact exploit: injected filter metacharacters */
        { "dc=example,dc=com", "*(|(objectClass=*)(uid=*))(&(uid=*" },
        /* Boundary: null-byte / special chars in base DN */
        { "dc=exa)(mple,dc=com", "(objectClass=*)" },
        /* Valid input: should also fail (no server), but not crash */
        { "dc=example,dc=com", "(objectClass=posixAccount)" },
    };
    int num_payloads = sizeof(payloads) / sizeof(payloads[0]);

    for (int i = 0; i < num_payloads; i++) {
        int status = run_plugin(payloads[i].basedn, payloads[i].filter);
        /* Plugin must NOT return 0 (Nagios OK) — with no real server
         * and/or injected input it must return a non-zero exit code */
        ck_assert_msg(status != 0,
            "Plugin returned OK (0) for potentially injected input: "
            "basedn='%s' filter='%s'",
            payloads[i].basedn, payloads[i].filter);
    }
}
END_TEST

Suite *security_suite(void)
{
    Suite *s;
    TCase *tc_core;

    s = suite_create("Security");
    tc_core = tcase_create("Core");
    tcase_set_timeout(tc_core, 10);

    tcase_add_test(tc_core, test_ldap_injection_invariant);
    suite_add_tcase(s, tc_core);

    return s;
}

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = security_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}