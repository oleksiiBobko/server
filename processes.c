#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include "server.h"
/* Set *UID and *GID to the owning user ID and group ID, respectively,
   of process PID.  Return 0 on success, nonzero on failure.  */
static int get_uid_gid (pid_t pid, uid_t* uid, gid_t* gid)
{
	char dir_name[64];
	struct stat dir_info;
	int rval;
	/* Generate the name of the process’s directory in /proc.  */
	snprintf (dir_name, sizeof (dir_name), “/proc/%d”, (int) pid);
	/* Obtain information about the directory.  */
	rval = stat (dir_name, &dir_info);
	if (rval != 0) 
		/* Couldn’t find it; perhaps this process no longer exists.  */
		return 1;
	/* Make sure it’s a directory; anything else is unexpected.  */
	assert (S_ISDIR (dir_info.st_mode));
	/* Extract the IDs we want.  */
	*uid = dir_info.st_uid;
	*gid = dir_info.st_gid;
	return 0;
}
/* Return the name of user UID.  The return value is a buffer that the
   caller must allocate with free.  UID must be a valid user ID.  */
static char* get_user_name (uid_t uid)
{
	struct passwd* entry;
	entry = getpwuid (uid);
	if (entry == NULL)
		system_error (“getpwuid”);
	return xstrdup (entry->pw_name);
}
/* Return the name of group GID.  The return value is a buffer that the
   caller must allocate with free.  GID must be a valid group ID.  */
static char* get_group_name (gid_t gid)
{
	struct group* entry;
	entry = getgrgid (gid);
	if (entry == NULL)
		system_error (“getgrgid”);
	return xstrdup (entry->gr_name);
	/* Return the name of the program running in process PID, or NULL on
	   error.  The return value is a newly allocated buffer which the caller
	   must deallocate with free.  */
	static char* get_program_name (pid_t pid)
	{
		char file_name[64];
		char status_info[256];
		int fd;
		int rval;
		char* open_paren;
		char* close_paren;
		char* result;
		/* Generate the name of the “stat” file in the process’s /proc
		   directory, and open it.  */
		snprintf (file_name, sizeof (file_name), “/proc/%d/stat”, (int) pid);
		fd = open (file_name, O_RDONLY);
		if (fd == -1)
			/* Couldn’t open the stat file for this process.  Perhaps the
			   process no longer exists.  */
			return NULL;
		/* Read the contents.  */
		rval = read (fd, status_info, sizeof (status_info) - 1);
		close (fd);
		if (rval <= 0)
			/* Couldn’t read, for some reason; bail.  */
			return NULL;
		/* NUL-terminate the file contents.  */
		status_info[rval] = ‘\0’;
		/* The program name is the second element of the file contents and is
		   surrounded by parentheses.  Find the positions of the parentheses
		   in the file contents.  */
		open_paren = strchr (status_info, ‘(‘);
				close_paren = strchr (status_info, ‘)’);
		if (open_paren == NULL 
				||
				close_paren == NULL 
				||
				close_paren < open_paren)
			/* Couldn’t find them; bail.  */
			return NULL;
		/* Allocate memory for the result.  */
		result = (char*) xmalloc (close_paren - open_paren);
		/* Copy the program name into the result.  */
		strncpy (result, open_paren + 1, close_paren - open_paren - 1);
		/* strncpy doesn’t NUL-terminate the result, so do it here.  */
		result[close_paren - open_paren - 1] = ‘\0’;
		/* All done.  */
		return result;
	}
	/* Return the resident set size (RSS), in kilobytes, of process PID.
	   Return -1 on failure.  */
	static int get_rss (pid_t pid)
	{
		char file_name[64];
		int fd;
		char mem_info[128];
		int rval;
		int rss;
		/* Generate the name of the process’s “statm” entry in its /proc
		   directory.  */
		snprintf (file_name, sizeof (file_name), “/proc/%d/statm”, (int) pid);
		/* Open it.  */
		fd = open (file_name, O_RDONLY);
		if (fd == -1)
			/* Couldn’t open it; perhaps this process no longer exists.  */
			return -1;
		/* Read the file’s contents.  */
		rval = read (fd, mem_info, sizeof (mem_info) - 1);
		close (fd);
		if (rval <= 0)
			/* Couldn’t read the contents; bail.  */
			return -1;
		/* NUL-terminate the contents.  */
		mem_info[rval] = ‘\0’;
		/* Extract the RSS.  It’s the second item.  */
		rval = sscanf (mem_info, “%*d %d”, &rss);
		if (rval != 1)
			/* The contents of statm are formatted in a way we don’t understand.  */
			return -1;
		/* The values in statm are in units of the system’s page size.
		   Convert the RSS to kilobytes.  */
		return rss * getpagesize () / 1024;
	}
	/* Generate an HTML table row for process PID.  The return value is a
	   pointer to a buffer that the caller must deallocate with free, or
	   NULL if an error occurs.  */
	static char* format_process_info (pid_t pid)
	{
		int rval;
		uid_t uid;
		gid_t gid;
		char* user_name;
		char* group_name;
		int rss;
		char* program_name;
		size_t result_length;
		char* result;
		/* Obtain the process’s user and group IDs.  */
		rval = get_uid_gid (pid, &uid, &gid);
		if (rval != 0)
			return NULL;
		/* Obtain the process’s RSS.  */
		rss = get_rss (pid);
		if (rss == -1)
			return NULL;
		/* Obtain the process’s program name.  */
		program_name = get_program_name (pid);
		if (program_name == NULL)
			return NULL;
		/* Convert user and group IDs to corresponding names.  */
		user_name = get_user_name (uid);
		group_name = get_group_name (gid);
		/* Compute the length of the string we’ll need to hold the result, and
		   allocate memory to hold it.  */
		result_length = strlen (program_name) 
			+ strlen (user_name) + strlen (group_name) + 128;
		result = (char*) xmalloc (result_length);
		/* Format the result.  */
		snprintf (result, result_length,
				"<tr><td align=\"right\">%d</td><td><tt>%s</tt></td><td>%s</td>"
				"<td>%s</td><td align=\"right\">%d</td></tr>\n",
				(int) pid, program_name, user_name, group_name, rss);
		/* Clean up.  */
		free (program_name);
		free (user_name);
		free (group_name);
		/* All done.  */
		return result;
	}
	/* HTML source for the start of the process listing page.  */
	static char* page_start = 
		"<html>\n"
		" <body>\n"
		"  <table cellpadding=\”4\” cellspacing=\”0\” border=\”1\”>\n"
		"   <thead>\n"
		"    <tr>\n"
		"     <th>PID</th>\n"
		"     <th>Program</th>\n"
		"     <th>User</th>\n"
		"     <th>Group</th>\n"
		"     <th>RSS&nbsp;(KB)</th>\n"
		"    </tr>\n"
		"   </thead>\n"
		"   <tbody>\n";
	/* HTML source for the end of the process listing page.  */
	static char* page_end =
		"   </tbody>\n"
		"  </table>\n"
		" </body>\n"
		"</html>\n";
	void module_generate (int fd)
	{
		size_t i;
		DIR* proc_listing;
		/* Set up an iovec array.  We’ll fill this with buffers that’ll be
		   part of our output, growing it dynamically as necessary.  */
		/* The number of elements in the array that we’ve used.  */
		size_t vec_length = 0;
		/* The allocated size of the array.  */
		size_t vec_size = 16;
		/* The array of iovcec elements.  */
		struct iovec* vec = 
			(struct iovec*) xmalloc (vec_size * sizeof (struct iovec));
		/* The first buffer is the HTML source for the start of the page.  */
		vec[vec_length].iov_base = page_start;
		vec[vec_length].iov_len = strlen (page_start);
		++vec_length;
		/* Start a directory listing for /proc.  */
		proc_listing = opendir (“/proc”);
		if (proc_listing == NULL)
			system_error (“opendir”);
		/* Loop over directory entries in /proc.  */
		while (1) {
			struct dirent* proc_entry;
			const char* name;
			pid_t pid;
			char* process_info;
			/* Get the next entry in /proc.  */
			proc_entry  = readdir (proc_listing);
			if (proc_entry == NULL)
				/* We’ve hit the end of the listing.  */
				break;

			237
				11.2    Implementation
				/* Could not resolve the name.  */
				error (optarg, “invalid host name”);
			else
				/* Hostname is OK, so use it.  */
				local_address.s_addr = 
					*((int*) (local_host_name->h_addr_list[0]));
		}
		break;      
		case ‘h’:  
		/* User specified -h or --help.  */
		print_usage (0);
		case ‘m’:
		/* User specified -m or --module-dir.  */
		{
			struct stat dir_info;
			/* Check that it exists.  */
			if (access (optarg, F_OK) != 0)
				error (optarg, “module directory does not exist”);
			/* Check that it is accessible.  */
			if (access (optarg, R_OK 
						|
						X_OK) != 0)
				error (optarg, “module directory is not accessible”);
			/* Make sure that it is a directory.  */
			if (stat (optarg, &dir_info) != 0 
					||
					!S_ISDIR (dir_info.st_mode))
				error (optarg, “not a directory”);
			/* It looks OK, so use it.  */
			module_dir = strdup (optarg);
		}
		break;
		case ‘p’:  
		/* User specified -p or --port.  */
		{
			long value;
			char* end;
			value = strtol (optarg, &end, 10);
			if (*end != ‘\0’)
				/* The user specified nondigits in the port number.  */
				print_usage (1);
			/* The port number needs to be converted to network (big endian)
			   byte order.  */
			port = (uint16_t) htons (value);
		}
		break;
		case ‘v’:  
		/* User specified -v or --verbose.  */
		verbose = 1;
		break;
		continues
			13 0430 CH11  5/22/01  10:46 AM  Page 237
			244
			Chapter 11    A Sample GNU/Linux Application
			While 
			issue.so
			sends the contents of a file using 
			sendfile
			, this module must invoke a
			command and redirect its output to the client.To do this, the module follows these
			steps:
			1.  First, the module creates a child process using 
			fork
			(see Section 3.2.2,
			 “
			 Using
			 fork
			 and 
			 exec
			 ,
			 ”
			 in Chapter 3).
			2.  The child process copies the client socket file descriptor to file descriptors 
			STDOUT_FILENO
			and 
			STDERR_FILENO
			, which correspond to standard output and
			standard error (see Section 2.1.4,
					“
					Standard I/O,
					”
					in Chapter 2).The file descrip-
			tors are copied using the 
			dup2
			call (see Section 5.4.3,
					“
					Redirecting the Standard
					Input, Output, and Error Streams,
					”
					in Chapter 5). All further output from the
			process to either of these streams is sent to the client socket.
			3.  The child process invokes the 
			df
			command with the 
			-h
			option by calling 
			execv
			(see Section 3.2.2,
			 “
			 Using 
			 fork
			 and 
			 exec
			 ,
			 ”
			 in Chapter 3).
			4.  The parent process waits for the child process to exit by calling 
			waitpid
			(see
			 Section 3.4.2,
			 “
			 The 
			 wait
			 System Calls,
			 ”
			 in Chapter 3).
			You could easily adapt this module to invoke a different command and redirect its
			output to the client.
			11.3.4   Summarize Running Processes
			The 
			processes.so
			module (see Listing 11.9) is a more extensive server module imple-
			mentation. It generates a page containing a table that summarizes the processes cur-
			rently running on the server system. Each process is represented by a row in the table
			that lists the PID, the executable program name, the owning user and group names,
			     and the resident set size.
				     Listing 11.9
				     (
				      processes.c
				     ) Server Module to Summarize Processes
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include “server.h”
				     /* Set *UID and *GID to the owning user ID and group ID, respectively,
					of process PID.  Return 0 on success, nonzero on failure.  */
				     13 0430 CH11  5/22/01  10:46 AM  Page 244
				     245
				     11.3    Modules
				     static int get_uid_gid (pid_t pid, uid_t* uid, gid_t* gid)
				     {
					     char dir_name[64];
					     struct stat dir_info;
					     int rval;
					     /* Generate the name of the process’s directory in /proc.  */
					     snprintf (dir_name, sizeof (dir_name), “/proc/%d”, (int) pid);
					     /* Obtain information about the directory.  */
					     rval = stat (dir_name, &dir_info);
					     if (rval != 0) 
						     /* Couldn’t find it; perhaps this process no longer exists.  */
						     return 1;
					     /* Make sure it’s a directory; anything else is unexpected.  */
					     assert (S_ISDIR (dir_info.st_mode));
					     /* Extract the IDs we want.  */
					     *uid = dir_info.st_uid;
					     *gid = dir_info.st_gid;
					     return 0;
				     }
		/* Return the name of user UID.  The return value is a buffer that the
		   caller must allocate with free.  UID must be a valid user ID.  */
		static char* get_user_name (uid_t uid)
		{
			struct passwd* entry;
			entry = getpwuid (uid);
			if (entry == NULL)
				system_error (“getpwuid”);
			return xstrdup (entry->pw_name);
		}
		/* Return the name of group GID.  The return value is a buffer that the
		   caller must allocate with free.  GID must be a valid group ID.  */
		static char* get_group_name (gid_t gid)
		{
			struct group* entry;
			entry = getgrgid (gid);
			if (entry == NULL)
				system_error (“getgrgid”);
			return xstrdup (entry->gr_name);
		}
		continues
			13 0430 CH11  5/22/01  10:46 AM  Page 245
			246
			Chapter 11    A Sample GNU/Linux Application
			/* Return the name of the program running in process PID, or NULL on
			   error.  The return value is a newly allocated buffer which the caller
			   must deallocate with free.  */
			static char* get_program_name (pid_t pid)
			{
				char file_name[64];
				char status_info[256];
				int fd;
				int rval;
				char* open_paren;
				char* close_paren;
				char* result;
				/* Generate the name of the “stat” file in the process’s /proc
				   directory, and open it.  */
				snprintf (file_name, sizeof (file_name), “/proc/%d/stat”, (int) pid);
				fd = open (file_name, O_RDONLY);
				if (fd == -1)
					/* Couldn’t open the stat file for this process.  Perhaps the
					   process no longer exists.  */
					return NULL;
				/* Read the contents.  */
				rval = read (fd, status_info, sizeof (status_info) - 1);
				close (fd);
				if (rval <= 0)
					/* Couldn’t read, for some reason; bail.  */
					return NULL;
				/* NUL-terminate the file contents.  */
				status_info[rval] = ‘\0’;
				/* The program name is the second element of the file contents and is
				   surrounded by parentheses.  Find the positions of the parentheses
				   in the file contents.  */
				open_paren = strchr (status_info, ‘(‘);
						close_paren = strchr (status_info, ‘)’);
				if (open_paren == NULL 
						||
						close_paren == NULL 
						||
						close_paren < open_paren)
					/* Couldn’t find them; bail.  */
					return NULL;
				/* Allocate memory for the result.  */
				result = (char*) xmalloc (close_paren - open_paren);
				/* Copy the program name into the result.  */
				strncpy (result, open_paren + 1, close_paren - open_paren - 1);
				/* strncpy doesn’t NUL-terminate the result, so do it here.  */
				result[close_paren - open_paren - 1] = ‘\0’;
				/* All done.  */
				return result;
			}
		Listing 11.9
			Continued
			13 0430 CH11  5/22/01  10:46 AM  Page 246
			247
			11.3    Modules
			/* Return the resident set size (RSS), in kilobytes, of process PID.
			   Return -1 on failure.  */
			static int get_rss (pid_t pid)
			{
				char file_name[64];
				int fd;
				char mem_info[128];
				int rval;
				int rss;
				/* Generate the name of the process’s “statm” entry in its /proc
				   directory.  */
				snprintf (file_name, sizeof (file_name), “/proc/%d/statm”, (int) pid);
				/* Open it.  */
				fd = open (file_name, O_RDONLY);
				if (fd == -1)
					/* Couldn’t open it; perhaps this process no longer exists.  */
					return -1;
				/* Read the file’s contents.  */
				rval = read (fd, mem_info, sizeof (mem_info) - 1);
				close (fd);
				if (rval <= 0)
					/* Couldn’t read the contents; bail.  */
					return -1;
				/* NUL-terminate the contents.  */
				mem_info[rval] = ‘\0’;
				/* Extract the RSS.  It’s the second item.  */
				rval = sscanf (mem_info, “%*d %d”, &rss);
				if (rval != 1)
					/* The contents of statm are formatted in a way we don’t understand.  */
					return -1;
				/* The values in statm are in units of the system’s page size.
				   Convert the RSS to kilobytes.  */
				return rss * getpagesize () / 1024;
			}
		/* Generate an HTML table row for process PID.  The return value is a
		   pointer to a buffer that the caller must deallocate with free, or
		   NULL if an error occurs.  */
		static char* format_process_info (pid_t pid)
		{
			int rval;
			uid_t uid;
			gid_t gid;
			char* user_name;
			char* group_name;
			int rss;
			char* program_name;
			continues
				13 0430 CH11  5/22/01  10:46 AM  Page 247
				248
				Chapter 11    A Sample GNU/Linux Application
				size_t result_length;
			char* result;
			/* Obtain the process’s user and group IDs.  */
			rval = get_uid_gid (pid, &uid, &gid);
			if (rval != 0)
				return NULL;
			/* Obtain the process’s RSS.  */
			rss = get_rss (pid);
			if (rss == -1)
				return NULL;
			/* Obtain the process’s program name.  */
			program_name = get_program_name (pid);
			if (program_name == NULL)
				return NULL;
			/* Convert user and group IDs to corresponding names.  */
			user_name = get_user_name (uid);
			group_name = get_group_name (gid);
			/* Compute the length of the string we’ll need to hold the result, and
			   allocate memory to hold it.  */
			result_length = strlen (program_name) 
				+ strlen (user_name) + strlen (group_name) + 128;
			result = (char*) xmalloc (result_length);
			/* Format the result.  */
			snprintf (result, result_length,
					“<tr><td align=\”right\”>%d</td><td><tt>%s</tt></td><td>%s</td>”
					“<td>%s</td><td align=\”right\”>%d</td></tr>\n”,
					(int) pid, program_name, user_name, group_name, rss);
			/* Clean up.  */
			free (program_name);
			free (user_name);
			free (group_name);
			/* All done.  */
			return result;
		}
		/* HTML source for the start of the process listing page.  */
		static char* page_start = 
			“<html>\n”
			“ <body>\n”
			“  <table cellpadding=\”4\” cellspacing=\”0\” border=\”1\”>\n”
			“   <thead>\n”
			“    <tr>\n”
			“     <th>PID</th>\n”
			“     <th>Program</th>\n”
			“     <th>User</th>\n”
			“     <th>Group</th>\n”
			Listing 11.9
			Continued
			13 0430 CH11  5/22/01  10:46 AM  Page 248
			249
			11.3    Modules
			“     <th>RSS&nbsp;(KB)</th>\n”
			“    </tr>\n”
			“   </thead>\n”
			“   <tbody>\n”;
		/* HTML source for the end of the process listing page.  */
		static char* page_end =
			“   </tbody>\n”
			“  </table>\n”
			“ </body>\n”
			“</html>\n”;
		void module_generate (int fd)
		{
			size_t i;
			DIR* proc_listing;
			/* Set up an iovec array.  We’ll fill this with buffers that’ll be
			   part of our output, growing it dynamically as necessary.  */
			/* The number of elements in the array that we’ve used.  */
			size_t vec_length = 0;
			/* The allocated size of the array.  */
			size_t vec_size = 16;
			/* The array of iovcec elements.  */
			struct iovec* vec = 
				(struct iovec*) xmalloc (vec_size * sizeof (struct iovec));
			/* The first buffer is the HTML source for the start of the page.  */
			vec[vec_length].iov_base = page_start;
			vec[vec_length].iov_len = strlen (page_start);
			++vec_length;
			/* Start a directory listing for /proc.  */
			proc_listing = opendir (“/proc”);
			if (proc_listing == NULL)
				system_error (“opendir”);
			/* Loop over directory entries in /proc.  */
			while (1) {
				struct dirent* proc_entry;
				const char* name;
				pid_t pid;
				char* process_info;
				/* Get the next entry in /proc.  */
				proc_entry  = readdir (proc_listing);
				if (proc_entry == NULL)
					/* We’ve hit the end of the listing.  */
					break;
				/* If this entry is not composed purely of digits, it’s not a
				   process directory, so skip it.  */
				name = proc_entry->d_name;
				if (strspn (name, "0123456789") != strlen (name))
					continue;
				/* The name of the entry is the process ID.  */
				pid = (pid_t) atoi (name);
				/* Generate HTML for a table row describing this process.  */
				process_info = format_process_info (pid);
				if (process_info == NULL)
					/* Something went wrong.  The process may have vanished while we
					   were looking at it.  Use a placeholder row instead.  */
					process_info = “<tr><td colspan=\”5\”>ERROR</td></tr>”;
				/* Make sure the iovec array is long enough to hold this buffer
				   (plus one more because we’ll add an extra element when we’re done
				   listing processes).  If not, grow it to twice its current size.  */
				if (vec_length == vec_size - 1) {
					vec_size *= 2;
					vec = xrealloc (vec, vec_size * sizeof (struct iovec));
				}
				/* Store this buffer as the next element of the array.  */
				vec[vec_length].iov_base = process_info;
				vec[vec_length].iov_len = strlen (process_info);
				++vec_length;
			}
			/* End the directory listing operation.  */
			closedir (proc_listing);
			/* Add one last buffer with HTML that ends the page.  */
			vec[vec_length].iov_base = page_end;
			vec[vec_length].iov_len = strlen (page_end);
			++vec_length;
			/* Output the entire page to the client file descriptor all at once.  */
			writev (fd, vec, vec_length);
			/* Deallocate the buffers we created.  The first and last are static
			   and should not be deallocated.  */
			for (i = 1; i < vec_length - 1; ++i)
				free (vec[i].iov_base);
			/* Deallocate the iovec array.  */
			free (vec);
		}

