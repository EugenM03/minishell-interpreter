// SPDX-License-Identifier: BSD-3-Clause

/**
 * Copyright Munteanu Eugen 325CA 2023-2024
 * Sisteme de Operare (SO) - Tema bonus 1 - mini-shell
 *
 * Resources used: man(), SO courses and labs
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cmd.h"
#include "utils.h"

#define READ		0
#define WRITE		1

/**
 * Redirects standard input, output and/or error to specified file(s).
 * For 'cd' command, redirects both standard output and standard error.
 *
 * @param s the command to be executed
 * @param redirection_flags the flags for opening the file
 * @param redirection_type the type of redirection (in, out, err)
 * @param cd_cmd true if the command is 'cd', false otherwise
 *
 * @return 0 if the redirection was successful, a negative value otherwise
 */
int redirect_to_file(simple_command_t *s, int redirection_flags,
					 const char *redirection_type, bool cd_cmd)
{
	// Sanity checks
	if (s == NULL || redirection_type == NULL)
		return -1;

	int fd = 0;

	// Get the file name from the command, if any,
	// depending on the redirection type
	char *std_file_name = NULL;

	if (!strcmp(redirection_type, "in")) {
		std_file_name = get_word(s->in);

		if (std_file_name)
			fd = open(std_file_name, O_RDONLY);
	} else if (!strcmp(redirection_type, "out")) {
		if (cd_cmd)
			redirection_flags = O_WRONLY | O_CREAT | O_TRUNC;

		std_file_name = get_word(s->out);

		if (std_file_name)
			fd = open(std_file_name, redirection_flags, 0644);
	} else if (!strcmp(redirection_type, "err")) {
		std_file_name = get_word(s->err);

		if (std_file_name)
			fd = open(std_file_name, redirection_flags, 0644);
	} else {
		fprintf(stderr, "Invalid redirection type\n");
		return -1;
	}

	free(std_file_name);

	// Continue only if the operation on file descriptor was successful
	if (fd <= 0)
		return -1;

	// Redirect standard input/output/error to the specified file
	if (!strcmp(redirection_type, "in"))
		dup2(fd, STDIN_FILENO);
	else if (!strcmp(redirection_type, "out") && !cd_cmd)
		dup2(fd, STDOUT_FILENO);
	else if (!strcmp(redirection_type, "err"))
		dup2(fd, STDERR_FILENO);

	close(fd);

	if (cd_cmd) {
		// For 'cd' command, we also need to redirect standard error
		std_file_name = get_word(s->err);
		fd = 0;

		if (std_file_name)
			fd = open(std_file_name, redirection_flags, 0644);

		// Continue only if the operation on file descriptor was successful
		if (fd <= 0)
			return -1;

		dup2(fd, STDERR_FILENO);

		free(std_file_name);
		close(fd);
	}

	return EXIT_SUCCESS;
}

/**
 * Main function for redirection for a command.
 *
 * @param cmd the command to be executed
 *
 * @return 0 if the redirection was successful, a negative value otherwise
 */
int cmd_redirection(simple_command_t *s)
{
	// Extract file names and flags from the command
	char *output_file_name = get_word(s->out);
	char *error_file_name = get_word(s->err);
	int io_flags = s->io_flags;

	// Determine redirection flags based on APPEND or TRUNC mode
	int redirection_flags = 0;

	if (io_flags == IO_REGULAR) {
		redirection_flags = O_WRONLY | O_CREAT | O_TRUNC;
	} else if (io_flags == IO_OUT_APPEND) {
		redirection_flags = O_WRONLY | O_CREAT | O_APPEND;
	} else if (io_flags == IO_ERR_APPEND) {
		redirection_flags = O_WRONLY | O_CREAT | O_APPEND;
	} else {
		fprintf(stderr, "Invalid redirection mode\n");
		return -1;
	}

	// Perform input redirection
	redirect_to_file(s, O_RDONLY, "in", false);

	// Perform output and error redirection
	if (output_file_name && error_file_name
						 && !strcmp(output_file_name, error_file_name)) {
		// Both output and error will be redirected to same file
		int fd = open(output_file_name, redirection_flags, 0644);

		if (fd < 0) {
			free(output_file_name);
			free(error_file_name);
			return -1;
		}

		// Redirect both standard output and standard error
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);

		close(fd);
	} else {
		// Perform redirection for output or error only
		if (output_file_name)
			redirect_to_file(s, redirection_flags, "out", false);

		if (error_file_name)
			redirect_to_file(s, redirection_flags, "err", false);
	}

	free(output_file_name);
	free(error_file_name);

	return EXIT_SUCCESS;
}

/**
 * Internal change-directory command.
 */
static bool shell_cd(word_t *dir)
{
	// Sanity checks
	if (dir == NULL || dir->string == NULL)
		return false;

	// Try to change the current directory, if possible
	char *target_dir = get_word(dir);

	if (chdir(target_dir) < 0) {
		free(target_dir);
		return false;
	}

	free(target_dir);

	// 'cd' command was successful
	return true;
}

/**
 * Internal exit/quit command.
 */
static int shell_exit(void)
{
	// Execute exit/quit
	return SHELL_EXIT;
}

/**
 * Parse a simple command (internal, environment variable assignment,
 * external command).
 */
static int parse_simple(simple_command_t *s, int level, command_t *father)
{
	// Sanity checks
	if (s == NULL)
		return SHELL_EXIT;

	// If builtin command, execute the command.
	char *curr_cmd = get_word(s->verb);

	if (!strcmp(curr_cmd, "cd")) {
		// Redirect standard output and standard error and execute 'cd'
		redirect_to_file(s, O_WRONLY | O_CREAT | O_TRUNC, "out", true);
		free(curr_cmd);

		bool ret_cd = shell_cd(s->params);

		// Return the exit status (0 for success)
		return ret_cd == true ? 0 : -1;
	} else if (!strcmp(curr_cmd, "exit") || !strcmp(curr_cmd, "quit")) {
		free(curr_cmd);

		// Execute the 'exit' or 'quit' command; return the exit status
		return shell_exit();
	}

	// If variable assignment, execute the assignment and
	// return the exit status.
	char *var_assign = get_word(s->verb);

	if (strchr(var_assign, '=')) {
		// Check if there is a value to be assigned
		if (s->verb->next_part && s->verb->next_part->next_part) {
			const char *src = s->verb->string;
			char *dst = get_word(s->verb->next_part->next_part);

			// Sanity checks
			if (!src || !dst) {
				free(var_assign);
				free(curr_cmd);
				return -1;
			}

			// If there is, assign it, if possible
			int ret_assign = setenv(src, dst, 1);

			// Free resources
			free(dst);
			free(var_assign);
			free(curr_cmd);

			// Return the exit status (0 for success)
			return ret_assign != 0 ? -1 : 0;
		}
	}

	free(var_assign);

	// External command case

	// Fork new process
	pid_t curr_pid = fork();

	switch (curr_pid) {
	case -1: {
		return -1;
	}

	case 0: {
		// Perform redirections in child
		int ret_redir = cmd_redirection(s);

		if (ret_redir < 0) {
			free(curr_cmd);
			return -1;
		}

		// Load executable in child
		int argc = 0;
		int exec_ret = execvp(curr_cmd, get_argv(s, &argc));

		if (exec_ret < 0) {
			fprintf(stderr, "Execution failed for '%s'\n", curr_cmd);
			free(curr_cmd);
		}

		// Finish child process
		exit(exec_ret);
	}

	default: {
		int status = 0;

		// Wait for child
		int ret_pid = waitpid(curr_pid, &status, 0);

		if (ret_pid < 0) {
			free(curr_cmd);
			return -1;
		}

		if (WIFEXITED(status)) {
			// Return exit status
			free(curr_cmd);
			return WEXITSTATUS(status);
		}
	}
	}

	free(curr_cmd);
	curr_cmd = NULL;

	return EXIT_SUCCESS;
}

/**
 * Process two commands in parallel, by creating two children.
 */
static bool run_in_parallel(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	// Execute cmd1 and cmd2 simultaneously.

	// Create child process for cmd1
	pid_t curr_pid1 = fork();

	switch (curr_pid1) {
	case -1: {
		return false;
	}

	case 0: {
		// First child
		int ret_exec = parse_command(cmd1, level + 1, father);

		if (ret_exec < 0)
			return false;

		exit(ret_exec);
	}

	default: {
		break;
	}
	}

	// Create child process for cmd2
	pid_t curr_pid2 = fork();

	switch (curr_pid2) {
	case -1: {
		return false;
	}

	case 0: {
		// Second child
		int ret_exec = parse_command(cmd2, level + 1, father);

		if (ret_exec < 0)
			return false;

		exit(ret_exec);
	}

	default: {
		// Wait for both children
		int status_pid1 = 0, status_pid2 = 0;
		int ret_pid1 = waitpid(curr_pid1, &status_pid1, 0);

		if (ret_pid1 < 0)
			return -1;

		int ret_pid2 = waitpid(curr_pid2, &status_pid2, 0);

		if (ret_pid2 < 0)
			return -1;

		if (WIFEXITED(status_pid2))
			return WEXITSTATUS(status_pid2);
	}
	}

	// Return exit status (0 for success)
	return EXIT_SUCCESS;
}

/**
 * Run commands by creating an anonymous pipe (cmd1 | cmd2).
 */
static bool run_on_pipe(command_t *cmd1, command_t *cmd2, int level,
		command_t *father)
{
	// Redirect the output of cmd1 to the input of cmd2.
	int pipefd[2] = {0};

	// Create anonymous pipe
	if (pipe(pipefd) < 0)
		return false;

	// Create child process for cmd1
	pid_t curr_pid1 = fork();

	switch (curr_pid1) {
	case -1: {
		return false;
	}

	case 0: {
		// First child
		close(pipefd[READ]);

		// Redirect standard output of cmd1 to pipe write end
		int ret_dup2 = dup2(pipefd[WRITE], STDOUT_FILENO);

		if (ret_dup2 < 0)
			return false;

		close(pipefd[WRITE]);

		// Execute cmd1
		int ret_exec = parse_command(cmd1, level + 1, father);

		if (ret_exec < 0)
			return false;

		exit(ret_exec);
	}

	default: {
		break;
	}
	}

	// Create child process for cmd2
	pid_t curr_pid2 = fork();

	switch (curr_pid2) {
	case -1: {
		return false;
	}

	case 0: {
		// Second child
		close(pipefd[WRITE]);

		// Redirect standard input of cmd2 to pipe read end
		int ret_dup2 = dup2(pipefd[READ], STDIN_FILENO);

		if (ret_dup2 < 0)
			return false;

		close(pipefd[READ]);

		// Execute cmd2
		int ret_exec = parse_command(cmd2, level + 1, father);

		if (ret_exec < 0)
			return false;

		exit(ret_exec);
	}

	default: {
		close(pipefd[WRITE]);
		close(pipefd[READ]);

		// Wait for both children
		int status_pid1 = 0, status_pid2 = 0;
		int ret_pid1 = waitpid(curr_pid1, &status_pid1, 0);

		if (ret_pid1 < 0)
			return -1;

		int ret_pid2 = waitpid(curr_pid2, &status_pid2, 0);

		if (ret_pid2 < 0)
			return -1;

		if (WIFEXITED(status_pid2))
			return WEXITSTATUS(status_pid2);
	}
	}

	close(pipefd[READ]);
	close(pipefd[WRITE]);

	// Return exit status
	return EXIT_SUCCESS;
}

/**
 * Parse and execute a command.
 */
int parse_command(command_t *c, int level, command_t *father)
{
	// Sanity checks
	if (c == NULL)
		return SHELL_EXIT;

	if (c->op == OP_NONE) {
		// Execute a simple command (no parameters)
		return parse_simple(c->scmd, level + 1, father);
	}

	// The return value of the operation will be stored here
	int ret_op_status = 0;

	switch (c->op) {
	case OP_SEQUENTIAL:
		// Execute the commands one after the other.
		ret_op_status = parse_command(c->cmd1, level + 1, c);
		if (ret_op_status < 0)
			return ret_op_status;

		ret_op_status = parse_command(c->cmd2, level + 1, c);

		break;
	case OP_PARALLEL:
		// Execute the commands simultaneously.
		ret_op_status = run_in_parallel(c->cmd1, c->cmd2, level + 1, c);

		break;
	case OP_CONDITIONAL_NZERO:
		// Execute the second command only if the first one returns non zero.
		ret_op_status = parse_command(c->cmd1, level + 1, c);
		if (ret_op_status != 0)
			ret_op_status = parse_command(c->cmd2, level + 1, c);

		break;
	case OP_CONDITIONAL_ZERO:
		// Execute the second command only if the first one returns zero.
		ret_op_status = parse_command(c->cmd1, level + 1, c);
		if (ret_op_status == 0)
			ret_op_status = parse_command(c->cmd2, level + 1, c);

		break;
	case OP_PIPE:
		// Redirect the output of the first command to the input of the second.
		ret_op_status = run_on_pipe(c->cmd1, c->cmd2, level + 1, c);

		break;
	default:
		// Invalid operation
		return SHELL_EXIT;
	}

	// Return the exit status after the execution
	return ret_op_status;
}
