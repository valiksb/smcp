#define HAS_LIBREADLINE 1

#include "assert_macros.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/errno.h>
#include "help.h"
#include <unistd.h>

#if HAS_LIBREADLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#include "smcp.h"

#include "cmd_list.h"
#include "cmd_test.h"
#include "cmd_get.h"
#include "cmd_post.h"
#include "cmd_pair.h"

#include "url-helpers.h"


#define ERRORCODE_HELP          (1)
#define ERRORCODE_BADARG        (2)
#define ERRORCODE_NOCOMMAND     (3)
#define ERRORCODE_UNKNOWN       (4)
#define ERRORCODE_BADCOMMAND    (5)
#define ERRORCODE_NOREADLINE    (6)
#define ERRORCODE_QUIT          (7)

static arg_list_item_t option_list[] = {
	{ 'h', "help",	NULL, "Print Help"			  },
	{ 'd', "debug", NULL, "Enable debugging mode" },
	{ 'p', "port",	NULL, "Port number"			  },
	{ 0 }
};

void print_commands();

static int
tool_cmd_help(
	smcp_daemon_t smcp, int argc, char* argv[]
) {
	print_arg_list_help(option_list, "smcp [options] <sub-command> [args]");
	print_commands();
	return ERRORCODE_HELP;
}


static int
tool_cmd_cd(
	smcp_daemon_t smcp, int argc, char* argv[]
) {
	if(argc == 1) {
		printf("%s\n", getenv("SMCP_CURRENT_PATH"));
		return 0;
	}

	if(argc == 2) {
		char url[1000];
		strcpy(url, getenv("SMCP_CURRENT_PATH"));
		if(url_change(url, argv[1]))
			setenv("SMCP_CURRENT_PATH", url, 1);
		else
			return ERRORCODE_BADARG;
	}

	return 0;
}


struct {
	const char* name;
	const char* desc;
	int			(*entrypoint)(
		smcp_daemon_t smcp, int argc, char* argv[]);
	int			isHidden;
} commandList[] = {
	{
		"get",
		"Fetches the value of a variable.",
		&tool_cmd_get
	},
	{ "cat", NULL,
	  &tool_cmd_get,
	  1 },
	{
		"post",
		"Triggers an event.",
		&tool_cmd_post
	},
	{
		"pair",
		"Pairs an event to an action",
		&tool_cmd_pair
	},
	{
		"list",
		"Fetches the contents of a node.",
		&tool_cmd_list
	},
	{ "ls",	 NULL,
	  &tool_cmd_list,
	  1 },
	{
		"test",
		"Self test mode.",
		&tool_cmd_test
	},

	{
		"help",
		"Display this help.",
		&tool_cmd_help
	},
	{ "?",	 NULL,
	  &tool_cmd_help,
	  1 },

	{ "cd",	 "Change current directory or URL (command mode)",
	  &tool_cmd_cd },

	{ NULL }
};

void
print_commands() {
	int i;

	printf("Commands:\n");
	for(i = 0; commandList[i].name; ++i) {
		if(commandList[i].isHidden)
			continue;
		printf(
			"   %s %s%s\n",
			commandList[i].name,
			"                     " + strlen(commandList[i].name),
			commandList[i].desc
		);
	}
}

static int
exec_command(
	smcp_daemon_t smcp_daemon, int argc, char * argv[]
) {
	int ret = 0;
	int j;

	require(argc, bail);

	if((strcmp(argv[0],
				"quit") == 0) ||
	        (strcmp(argv[0],
				"exit") == 0) || (strcmp(argv[0], "q") == 0)) {
		ret = ERRORCODE_QUIT;
		goto bail;
	}

	for(j = 0; commandList[j].name; ++j) {
		if(strcmp(argv[0], commandList[j].name) == 0) {
			if(commandList[j].entrypoint) {
				ret = commandList[j].entrypoint(smcp_daemon, argc, argv);
				goto bail;
			} else {
				fprintf(stderr,
					"The command \"%s\" is not yet implemented.\n",
					commandList[j].name);
				ret = ERRORCODE_NOCOMMAND;
				goto bail;
			}
		}
	}

	fprintf(stderr, "The command \"%s\" is not recognised.\n", argv[0]);

	ret = ERRORCODE_BADCOMMAND;

bail:
	return ret;
}

int
main(
	int argc, char * argv[]
) {
	int ret = 0;
	int i, debug_mode = 0;
	int port = SMCP_DEFAULT_PORT + 1;
	bool istty = isatty(STDIN_FILENO);

#if !HAS_LIBREADLINE
	if(argc <= 1) {
		print_arg_list_help(option_list,
			"smcp [options] <sub-command> [args]");
		print_commands();
		return ERRORCODE_HELP;
	}
#endif
	for(i = 1; i < argc; i++) {
		if(argv[i][0] == '-' && argv[i][1] == '-') {
			if(strcmp(argv[i] + 2, "help") == 0) {
				print_arg_list_help(option_list,
					"smcp [options] <sub-command> [args]");
				print_commands();
				return ERRORCODE_HELP;
			} else if(strcmp(argv[i] + 2, "port") == 0) {
				port = atoi(argv[++i]);
			} else if(strcmp(argv[i] + 2, "debug") == 0) {
				debug_mode++;
			} else {
				fprintf(stderr,
					"%s: error: Unknown command line argument \"%s\".\n",
					argv[0],
					argv[i]);
				return ERRORCODE_BADARG;
				break;
			}
		} else if(argv[i][0] == '-') {
			int j;
			int ii = i;
			for(j = 1; j && argv[ii][j]; j++) {
				switch(argv[ii][j]) {
				case 'p':
					port = atoi(argv[++i]);
					break;
				case '?':
				case 'h':
					print_arg_list_help(option_list,
						"smcp [options] <sub-command> [args]");
					print_commands();
					return ERRORCODE_HELP;
					break;
				case 'd':
					debug_mode++;
					break;
				default:
					fprintf(
						stderr,
						"%s: error: Unknown command line argument \"-%c\".\n",
						argv[0],
						argv[ii][j]);
					return ERRORCODE_BADARG;
					break;
				}
			}
		} else {
			break;
		}
	}

	smcp_daemon_t smcp_daemon = smcp_daemon_create(port);
	setenv("SMCP_CURRENT_PATH", "smcp://localhost/", 0);

	if(i < argc) {
		ret = exec_command(smcp_daemon, argc - i, argv + i);
#if HAS_LIBREADLINE
		if(ret || (0 != strcmp(argv[i], "cd")))
#endif
		goto bail;
	}

#if HAS_LIBREADLINE
	// Command mode.
	require_action(NULL != readline, bail, ret = ERRORCODE_NOREADLINE);
	setenv("SMCP_HISTORY_FILE", tilde_expand("~/.smcp_history"), 0);
	rl_initialize();
	using_history();
	read_history(getenv("SMCP_HISTORY_FILE"));
	while(true) {
		char *l;
		char *inputstring;
		char *argv2[100];
		char **ap = argv2;
		int argc2 = 0;
		char prompt[128] = "";

		if(istty) {
			char* current_smcp_path = getenv("SMCP_CURRENT_PATH");
			snprintf(prompt,
				sizeof(prompt),
				"%s> ",
				current_smcp_path ? current_smcp_path : "");
		}

		if(!(l = readline(prompt)))
			break;

		if(!l[0])
			continue;

		add_history(l);

		inputstring = l;

		while((*ap = strsep(&inputstring, " \t\n\r"))) {
			if(**ap != '\0') {
				ap++;
				argc2++;
			}
		}
		if(argc2 > 0) {
			ret = exec_command(smcp_daemon, argc2, argv2);
			if(ret == ERRORCODE_QUIT) {
				ret = 0;
				break;
			} else if(ret && (ret != ERRORCODE_HELP)) {
				printf("Error %d\n", ret);
			}

			write_history(getenv("SMCP_HISTORY_FILE"));
		}

		free(l);
	}

#else // !HAS_LIBREADLINE
	fprintf(stderr, "%s: error: Missing command.\n", argv[0]);
	ret = ERRORCODE_NOCOMMAND;
#endif // !HAS_LIBREADLINE

bail:
	smcp_daemon_release(smcp_daemon);
	return ret;
}
