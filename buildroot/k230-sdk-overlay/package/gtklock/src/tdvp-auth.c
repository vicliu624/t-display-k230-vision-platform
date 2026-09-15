/* SPDX-License-Identifier: GPL-3.0-or-later
 * TDVP's bounded, synchronous PAM backend for gtklock 4.0.0.
 * Called only by the authentication worker, never by the GTK main thread.
 * No fork/pipe protocol, global attempt state, PAM conversation UI, or shell.
 */
#define _POSIX_C_SOURCE 200809L
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <security/pam_appl.h>
#include "auth.h"

struct conv_data {
	const char *username;
	const char *password;
};

static void wipe_string(char *value) {
	if(value) {
		volatile unsigned char *p = (volatile unsigned char *)value;
		size_t size = strlen(value);
		while(size--) *p++ = 0;
	}
}

static int conversation(int count, const struct pam_message **messages,
		struct pam_response **response, void *opaque) {
	const struct conv_data *data = opaque;
	struct pam_response *answers;
	if(!response) return PAM_CONV_ERR;
	*response = NULL;
	if(count <= 0 || count > PAM_MAX_NUM_MSG || !messages || !data ||
			!data->username || !data->password) return PAM_CONV_ERR;
	answers = calloc((size_t)count, sizeof(*answers));
	if(!answers) return PAM_BUF_ERR;
	for(int i = 0; i < count; ++i) {
		const char *text;
		if(!messages[i]) goto fail;
		switch(messages[i]->msg_style) {
		case PAM_PROMPT_ECHO_ON:
			text = data->username;
			break;
		case PAM_PROMPT_ECHO_OFF:
			text = data->password;
			break;
		case PAM_TEXT_INFO:
		case PAM_ERROR_MSG:
			/* The UI uses a generic, local error, not arbitrary PAM markup. */
			continue;
		default:
			goto fail;
		}
		answers[i].resp = strdup(text);
		if(!answers[i].resp) goto fail;
	}
	*response = answers; /* PAM owns and disposes of successful responses. */
	return PAM_SUCCESS;
fail:
	for(int i = 0; i < count; ++i) {
		wipe_string(answers[i].resp);
		free(answers[i].resp);
	}
	free(answers);
	return PAM_CONV_ERR;
}

/* Kept for the upstream API; this backend emits only a terminal result. */
char *auth_get_error(void) { return NULL; }
char *auth_get_message(void) { return NULL; }

enum pwcheck auth_pw_check(const char *password) {
	struct passwd record, *user = NULL;
	char user_buffer[16384];
	pam_handle_t *handle = NULL;
	int status, end_status;
	if(!password || getuid() != geteuid()) return PW_FAILURE;
	if(getpwuid_r(getuid(), &record, user_buffer, sizeof(user_buffer), &user) ||
			!user || !user->pw_name || !user->pw_name[0]) return PW_FAILURE;
	struct conv_data data = { user->pw_name, password };
	struct pam_conv conv = { conversation, &data };
	status = pam_start("gtklock", user->pw_name, &conv, &handle);
	if(status != PAM_SUCCESS) return PW_FAILURE;
	status = pam_authenticate(handle, PAM_DISALLOW_NULL_AUTHTOK);
	if(status == PAM_SUCCESS) status = pam_acct_mgmt(handle, 0);
	/* Unlock does not create a PAM session or change an expired password. */
	if(status == PAM_SUCCESS) status = pam_setcred(handle, PAM_REFRESH_CRED);
	end_status = pam_end(handle, status);
	return status == PAM_SUCCESS && end_status == PAM_SUCCESS ? PW_SUCCESS : PW_FAILURE;
}
