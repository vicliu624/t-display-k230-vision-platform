/* SPDX-License-Identifier: MIT
 * Compile the exact shipped backend against real PAM public headers.
 * Mock only NSS/PAM boundaries and allocation failures; never check real users.
 */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <security/pam_appl.h>

static int nss_error, no_user, elevated, allocation;
static int start_result, auth_result, account_result, cred_result, end_result;
static int starts, authentications, accounts, credentials, ends, last_status;
static const struct pam_conv *active_conv;
static char handle_storage;

static uid_t test_getuid(void) { return 1000; }
static uid_t test_geteuid(void) { return elevated ? 0 : 1000; }
static int test_getpwuid_r(uid_t id, struct passwd *record, char *buf,
		size_t size, struct passwd **result) {
	assert(id == 1000 && size >= 5);
	memcpy(buf, "tdvp", 5);
	record->pw_name = buf;
	*result = no_user ? NULL : record;
	return nss_error;
}
static void *test_calloc(size_t n, size_t size) {
	if(allocation > 0 && --allocation == 0) return NULL;
	return calloc(n, size);
}
static char *test_strdup(const char *s) {
	if(allocation > 0 && --allocation == 0) return NULL;
	return strdup(s);
}
static int test_pam_start(const char *service, const char *user,
		const struct pam_conv *conv, pam_handle_t **handle) {
	assert(!strcmp(service, "gtklock") && !strcmp(user, "tdvp"));
	++starts;
	active_conv = conv;
	*handle = (pam_handle_t *)&handle_storage;
	return start_result;
}
static void release_responses(struct pam_response *responses, int count) {
	for(int i = 0; i < count; ++i) free(responses[i].resp);
	free(responses);
}
static int test_pam_authenticate(pam_handle_t *handle, int flags) {
	assert(handle == (pam_handle_t *)&handle_storage);
	assert(flags == PAM_DISALLOW_NULL_AUTHTOK);
	++authentications;
	const struct pam_message prompts[] = {
		{PAM_PROMPT_ECHO_ON, "User"}, {PAM_PROMPT_ECHO_OFF, "Password"},
		{PAM_TEXT_INFO, "Information"}, {PAM_ERROR_MSG, "Warning"},
	};
	const struct pam_message *messages[] = {prompts, prompts + 1, prompts + 2, prompts + 3};
	struct pam_response *responses = NULL;
	int ret = active_conv->conv(4, messages, &responses, active_conv->appdata_ptr);
	if(ret != PAM_SUCCESS) return ret;
	assert(!strcmp(responses[0].resp, "tdvp"));
	assert(!strcmp(responses[1].resp, "test-only-password"));
	assert(responses[2].resp == NULL && responses[3].resp == NULL);
	for(int i = 0; i < 4; ++i) assert(responses[i].resp_retcode == 0);
	release_responses(responses, 4);
	return auth_result;
}
static int test_pam_acct_mgmt(pam_handle_t *handle, int flags) {
	assert(handle == (pam_handle_t *)&handle_storage && flags == 0);
	++accounts;
	return account_result;
}
static int test_pam_setcred(pam_handle_t *handle, int flags) {
	assert(handle == (pam_handle_t *)&handle_storage && flags == PAM_REFRESH_CRED);
	++credentials;
	return cred_result;
}
static int test_pam_end(pam_handle_t *handle, int status) {
	assert(handle == (pam_handle_t *)&handle_storage);
	++ends;
	last_status = status;
	return end_result;
}

#define getuid test_getuid
#define geteuid test_geteuid
#define getpwuid_r test_getpwuid_r
#define calloc test_calloc
#define strdup test_strdup
#define pam_start test_pam_start
#define pam_authenticate test_pam_authenticate
#define pam_acct_mgmt test_pam_acct_mgmt
#define pam_setcred test_pam_setcred
#define pam_end test_pam_end
#include TDVP_AUTH_SOURCE

static void reset(void) {
	nss_error = no_user = elevated = allocation = 0;
	start_result = auth_result = account_result = cred_result = end_result = PAM_SUCCESS;
	starts = authentications = accounts = credentials = ends = 0;
	last_status = -1;
}
static void rejects(void) { assert(auth_pw_check("test-only-password") == PW_FAILURE); }

int main(void) {
	reset();
	assert(auth_pw_check("test-only-password") == PW_SUCCESS);
	assert(starts == 1 && authentications == 1 && accounts == 1 && credentials == 1 && ends == 1);
	assert(last_status == PAM_SUCCESS);
	reset(); auth_result = PAM_AUTH_ERR; rejects();
	assert(accounts == 0 && credentials == 0 && ends == 1 && last_status == PAM_AUTH_ERR);
	reset(); account_result = PAM_ACCT_EXPIRED; rejects();
	assert(credentials == 0 && ends == 1 && last_status == PAM_ACCT_EXPIRED);
	reset(); account_result = PAM_NEW_AUTHTOK_REQD; rejects();
	assert(credentials == 0 && ends == 1 && last_status == PAM_NEW_AUTHTOK_REQD);
	reset(); cred_result = PAM_CRED_ERR; rejects();
	assert(ends == 1 && last_status == PAM_CRED_ERR);
	reset(); end_result = PAM_SYSTEM_ERR; rejects();
	reset(); start_result = PAM_SYSTEM_ERR; rejects();
	assert(authentications == 0 && ends == 0);
	reset(); nss_error = ERANGE; rejects(); assert(starts == 0);
	reset(); no_user = 1; rejects(); assert(starts == 0);
	reset(); elevated = 1; rejects(); assert(starts == 0);
	reset(); assert(auth_pw_check(NULL) == PW_FAILURE); assert(starts == 0);
	for(int i = 1; i <= 3; ++i) {
		reset(); allocation = i; rejects();
		assert(accounts == 0 && ends == 1);
	}
	reset();
	struct conv_data data = {"tdvp", "test-only-password"};
	const struct pam_message prompt = {PAM_PROMPT_ECHO_OFF, "Password"};
	const struct pam_message unknown = {9999, "Unknown"};
	const struct pam_message *messages[] = {&prompt, &unknown};
	struct pam_response *response = (void *)1;
	assert(conversation(0, messages, &response, &data) == PAM_CONV_ERR && response == NULL);
	assert(conversation(-1, messages, &response, &data) == PAM_CONV_ERR);
	assert(conversation(PAM_MAX_NUM_MSG + 1, messages, &response, &data) == PAM_CONV_ERR);
	assert(conversation(1, NULL, &response, &data) == PAM_CONV_ERR);
	assert(conversation(1, messages, NULL, &data) == PAM_CONV_ERR);
	assert(conversation(1, messages, &response, NULL) == PAM_CONV_ERR);
	assert(conversation(2, messages, &response, &data) == PAM_CONV_ERR && response == NULL);
	messages[1] = NULL;
	assert(conversation(2, messages, &response, &data) == PAM_CONV_ERR && response == NULL);
	char secret[] = "secret";
	wipe_string(secret);
	for(size_t i = 0; i < sizeof(secret); ++i) assert(secret[i] == 0);
	assert(auth_get_error() == NULL && auth_get_message() == NULL);
	for(int i = 0; i < 1000; ++i) {
		reset();
		auth_result = i % 2 ? PAM_AUTH_ERR : PAM_SUCCESS;
		assert(auth_pw_check("test-only-password") == (i % 2 ? PW_FAILURE : PW_SUCCESS));
		assert(ends == 1);
	}
	puts("PASS gtklock real backend: PAM lifecycle, account expiry, allocation/conversation failures, 1000 retries");
	return 0;
}
