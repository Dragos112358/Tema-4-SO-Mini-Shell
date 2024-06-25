// SPDX-License-Identifier: BSD-3-Clause

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <ctype.h>
#include <fcntl.h>
#include "cmd.h"
#include "utils.h"

#define READ		0
#define WRITE		1

extern char **environ;
/**
 * Internal change-directory command.
 */
static int shell_cd(word_t *dir)
{
	/* TODO: Execute cd. */
	if (chdir(dir->string) != 0) {
		perror("cd");
		return 1; // Error
	}
	return 0; // Success
}
/**
 * Internal exit/quit command.
 */
static int shell_exit(void)
{
	/* TODO: Execute exit/quit. */

	exit(EXIT_FAILURE);

	return -1; /* TODO: Replace with actual exit code. */
}

/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
void redirect_in(simple_command_t *s)
{
	if (s->in != NULL) {
		int file_directory = open(s->in->string, O_RDONLY);

		if (file_directory == -1) {
			perror("open");
			exit(EXIT_FAILURE);
		}

		if (dup2(file_directory, STDIN_FILENO) == -1) {
			perror("dup2");
			close(file_directory);
			exit(EXIT_FAILURE);
		}

	close(file_directory);
	}
}
void redirect_out(simple_command_t *s)
{
	int file_directory;

	if (s->io_flags & IO_OUT_APPEND)
		file_directory = open(s->out->string, O_WRONLY | O_CREAT | O_APPEND, 0666);
	else
		file_directory = open(s->out->string, O_WRONLY | O_CREAT | O_TRUNC, 0666);

	if (file_directory == -1) {
		perror("open");
		exit(EXIT_FAILURE);
	}

	// Close STDOUT_FILENO before redirectin
	if (close(STDOUT_FILENO) == -1) {
		perror("close");
		exit(EXIT_FAILURE);
	}

	if (dup2(file_directory, STDOUT_FILENO) == -1) {
		perror("dup2");
		exit(EXIT_FAILURE);
	}

	close(file_directory);
}
void redirect_error(simple_command_t *s)
{
	int file_directory;

	if (s->io_flags && IO_OUT_APPEND) {
		file_directory = open(s->err->string, O_WRONLY | O_CREAT | O_APPEND, 0666);
		dup2(file_directory, STDERR_FILENO);
	} else {
		file_directory = open(s->err->string, O_WRONLY | O_CREAT | O_TRUNC, 0666);
		dup2(file_directory, STDERR_FILENO);

		if (!(s->io_flags && IO_OUT_APPEND)) {
			if (dup2(file_directory, STDOUT_FILENO) == -1) {
				perror("dup2");
				close(file_directory);
				exit(EXIT_FAILURE);
			}
		}
	}

	// Close the file descriptor after redirection
	close(file_directory);
}
char *substitute_variables(const char *input)
{
	char *result = strdup(input);
	char *ptr = result;

	while (*ptr != '\0') {
		if (*ptr == '$' && isalpha(*(ptr + 1))) {
			char *var_start = ptr + 1;
			char *var_end = var_start + 1;

			while (isalnum(*var_end) || *var_end == '_')
				var_end++;
			// Copy variable value if found
			if (var_start != var_end) {
				*var_end = '\0';
				char *var_name = strndup(var_start, var_end - var_start);
				char *var_value = getenv(var_name);

				if (var_value != NULL) {
					// Substitute variable with its value
					size_t var_length = var_end - var_start;
					size_t var_value_length = strlen(var_value);
					size_t remaining_length = strlen(ptr + 1 + var_length);

					memmove(ptr + var_value_length + 1, ptr + 1 + var_length, remaining_length);
					memcpy(ptr + 1, var_value, var_value_length);
				}
				free(var_name);
			}
		}
		ptr++;
	}
	return result;
}
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	/* TODO: Sanity checks. */
	if (s == NULL || s->verb == NULL) {
		fprintf(stderr, "Invalid command\n");
		return 1; // Error
	}
	word_t *param = s->params;

	while (param != NULL) {
		param->string = substitute_variables(param->string);
		param = param->next_word;
	}
	if (strcmp(s->verb->string, "exit") == 0 || strcmp(s->verb->string, "quit") == 0)
		return shell_exit();
	if (strcmp(s->verb->string, "cd") == 0) {
		// Execute cd command
		if (s->out != NULL) {
			int file_directory = open(s->out->string, O_WRONLY | O_CREAT | O_TRUNC, 0666);

			if (file_directory == -1) {
				perror("open");
				return 1; // Error
			}
		}
		if (s->params != NULL && s->params->string != NULL)
			return shell_cd(s->params);
		// If no directory is specified, go to the home directory
		char *director_home = getenv("HOME");

		if (director_home != NULL) {
			word_t *dir = malloc(sizeof(word_t));

			dir->string = director_home;
			dir->next_word = NULL;
			return shell_cd(dir);
		}
		fprintf(stderr, "cd: No home directory\n");
		return 1; // Error
	}
	// Automatically execute basic commands using execvp
	pid_t pid = fork();

	if (pid == -1) {
		perror("fork");
		return 1; // Error
	}

	if (pid == 0) {
		// TODO: Perform redirections in child (if needed)
		if (s->in != NULL)
			redirect_in(s);
		if (s->out != NULL)
			redirect_out(s);
		if (s->err != NULL)
			redirect_error(s);
		// Check if it's a gcc -o executable file.c case
		if (strcmp(s->verb->string, "gcc") == 0 && s->params != NULL) {
			char *fisier_input = NULL;
			char *fisier_output = "output"; // default output file

			// Traverse the parameters to find the input and output files
			for (word_t *param = s->params; param; param = param->next_word) {
				if (strcmp(param->string, "-o") == 0 && param->next_word) {
					// Set the next parameter as output file if -o is present
					fisier_output = (char *) param->next_word->string;
				} else {
					// Set the first non-option parameter as input file
					fisier_input = (char *) param->string;
				}
			}
			if (fisier_input != NULL) {
				execlp("gcc", "gcc", "-o", fisier_output, fisier_input, NULL);
			} else {
				fprintf(stderr, "Invalid gcc command\n");
				exit(EXIT_FAILURE);
			}
		} else {
			// Load executable in child
			// Build an array of pointers for execvp
			char *args[s->params ? (s->params->next_word ? 3 : 2) : 2]; // Max three parameters

			args[0] = (char *) s->verb->string;
			if (s->params) {
				args[1] = (char *) s->params->string;
				if (s->params->next_word) {
					args[2] = (char *) s->params->next_word->string;
					args[3] = NULL; // Null-terminate the array
				} else {
					args[2] = NULL; // Null-terminate the array
				}
			} else {
				args[1] = NULL; // Null-terminate the array
			}
			execvp(s->verb->string, args);
		}
		fprintf(stderr, "Execution failed for '%s'\n", s->verb->string);
		exit(EXIT_FAILURE);
	} else {
		int status_child;

		waitpid(pid, &status_child, 0); // Asteapta procesul-copil
		return WEXITSTATUS(status_child);
	}
}
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level, command_t *father)
{
	int status1, status2;
	pid_t pid1 = fork();

	if (pid1 == -1)
		exit(EXIT_FAILURE);

	if (pid1 != 0) {
		// Parent process
		pid_t pid2 = fork();

		if (pid2 == -1) {
			perror("fork");
			exit(EXIT_FAILURE);
		}

		if (pid2 == 0)
			_exit(parse_command(cmd2, level + 1, father));

		// Parent continues here
		waitpid(pid1, &status1, 0);
		waitpid(pid2, &status2, 0);
		return status1 == 0 && status2 == 0;
	}

	// Child process
	_exit(parse_command(cmd1, level + 1, father));
	// The following line won't be reached, as _exit() doesn't return.
	return true;
}


/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level, command_t *father)
{
	int pipe_file_directory[2];

	if (pipe(pipe_file_directory) == -1) {
		perror("pipe");
		exit(EXIT_FAILURE);
	}

	pid_t pid1 = fork();

	if (pid1 == -1) {
		perror("fork");
		exit(EXIT_FAILURE);
	}

	if (pid1 == 0) {
		close(pipe_file_directory[READ]);
		dup2(pipe_file_directory[WRITE], STDOUT_FILENO);
		close(pipe_file_directory[WRITE]);
		_exit(parse_command(cmd1, level + 1, father));
	}

	pid_t pid2 = fork();

	if (pid2 == -1) {
		perror("fork");
		exit(EXIT_FAILURE);
	}

	if (pid2 == 0) {
		close(pipe_file_directory[WRITE]);
		dup2(pipe_file_directory[READ], STDIN_FILENO);
		close(pipe_file_directory[READ]);
		_exit(parse_command(cmd2, level + 1, father));
	}

	close(pipe_file_directory[READ]);
	close(pipe_file_directory[WRITE]);

	int status1, status2;

	waitpid(pid1, &status1, 0);
	waitpid(pid2, &status2, 0);

	return status1 == 0 && status2 == 0;
}

/**
 * Parse and execute a command.
 */
void comanda_invalida(void)
{
	fprintf(stderr, "Invalid command\n");
}
int parse_command(command_t *c, int level, command_t *father)
{
	/* TODO: sanity checks */
	if (c == NULL) {
		comanda_invalida();
		return SHELL_EXIT;
	}

	if (c->op == OP_NONE) {
		/* TODO: Execute a simple command. */
		return parse_simple(c->scmd, level, father); /* TODO: Replace with actual exit code of command. */
	}

	int status = 0;

	switch (c->op) {
	case OP_SEQUENTIAL:
		/* TODO: Execute the commands one after the other. */
		status = parse_command(c->cmd1, level + 1, c);
		status = parse_command(c->cmd2, level + 1, c);
		break;

	case OP_PARALLEL:
		/* TODO: Execute the commands simultaneously. */
		status = run_in_parallel(c->cmd1, c->cmd2, level + 1, c);
		break;

	case OP_CONDITIONAL_NZERO:
		/* TODO: Execute the second command only if the first one returns non-zero*/
		status = parse_command(c->cmd1, level + 1, c);
		if (status != 0)
			status = parse_command(c->cmd2, level + 1, c);
		break;

	case OP_CONDITIONAL_ZERO:
		/* TODO: Execute the second command only if the first one returns zero.*/
		status = parse_command(c->cmd1, level + 1, c);
		if (status == 0)
			status = parse_command(c->cmd2, level + 1, c);
		break;

	case OP_PIPE:
		/* TODO: Redirect the output of the first command to the input of the second.*/
		status = run_on_pipe(c->cmd1, c->cmd2, level + 1, c);
		break;

	default:
		return SHELL_EXIT;
	}

	return status; /* TODO: Replace with actual exit code of command. */
}



